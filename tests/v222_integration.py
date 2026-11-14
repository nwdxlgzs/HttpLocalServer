"""Attention click policy and direct paging to a file in its parent directory."""
import concurrent.futures
import json
import os
import pathlib
import subprocess
import sys
import tempfile
from integration import Server, check
from v2_integration import route, send


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-reveal-') as directory:
        root = pathlib.Path(directory)
        folder = root / '中文目录'
        folder.mkdir()
        (folder / 'subfolder').mkdir()
        (root / 'root.txt').write_text('root', encoding='utf-8')
        extensions = ['txt', 'json', 'bin', 'md']
        for index in range(307):
            file = folder / ('file-%03d.%s' % (index, extensions[index % 4]))
            file.write_bytes(b'x' * (1 + (index * 37) % 701))
            os.utime(file, (1700000000 + index * 7, 1700000000 + index * 7))
        server = Server(exe, root, extra=('--no-magika',))
        try:
            token = server.api('/api/session')['token']
            headers = {'X-HLS-Token': token, 'Content-Type': 'application/json'}

            def update(values, custom=None):
                return send(server, 'POST', '/api/admin/settings', json.dumps(values).encode(), custom or headers)

            def focused(path, **values):
                values = dict({'path': '/中文目录', 'focus': path, 'limit': 100}, **values)
                return server.api(route('list', **values))

            check(server.api('/api/site')['attentionClick'] == 'locate', 'Attention click defaults to locate')
            check(server.api('/api/admin/settings')['attentionClick'] == 'locate', 'Host sees current click policy')
            body = server.request('/')[2]
            check(b'setting-attention-click' in body and b'loadFocused' in body, 'Embedded page exposes the host action setting and targeted paging')
            check(update({'attentionClick': 'download'}, {'Content-Type': 'application/json'})[0] == 403, 'Click policy mutation requires the host CSRF token')
            check(update({'attentionClick': 'download'}, dict(headers, Origin='http://evil.test'))[0] == 403, 'Click policy rejects foreign Origin')
            check(update({'attentionClick': 'download'})[0] == 200, 'Host can switch to direct download')
            check(server.api('/api/site')['attentionClick'] == 'download', 'Public poll publishes download policy')
            before = server.api('/api/site')
            for invalid in ['open', '', 1, None, True, ['locate']]:
                check(update({'title': 'must not commit', 'attentionClick': invalid})[0] == 400, 'Invalid click policy rejected')
                check(server.api('/api/site') == before, 'Policy validation commits no partial settings')
            check(update({'attentionClick': 'locate'})[0] == 200 and server.api('/api/site')['attentionClick'] == 'locate', 'Host can restore default locate action')

            for sort in ['name', 'size', 'modified', 'type']:
                for order in ['asc', 'desc']:
                    all_entries = server.api(route('list', path='/中文目录', limit=500, sort=sort, order=order))['entries']
                    for index in [1, 99, 100, 101, 200, 299, 307]:
                        target = all_entries[index]['path']
                        result = focused(target, sort=sort, order=order, offset=0)
                        offset = index // 100 * 100
                        check(result['focusPath'] == target and result['focusIndex'] == index and result['offset'] == offset, 'Focus resolves correct sorted page: %s/%s/%d' % (sort, order, index))
                        check(result['entries'] == all_entries[offset:offset + 100], 'Target page matches ordinary pagination')
                        check(len(result['entries']) <= 100 and result['total'] == len(all_entries), 'Focus returns one bounded page')
            check(server.api(route('list', path='/', focus='/root.txt'))['focusPath'] == '/root.txt', 'Root file can be located')
            (folder / '带空格 文件.txt').write_text('unicode', encoding='utf-8')
            check(focused('/中文目录/带空格 文件.txt', refresh=1)['focusPath'] == '/中文目录/带空格 文件.txt', 'UTF-8 and spaces survive focused paging')
            for values in [
                {'focus': '/root.txt'}, {'focus': '/../escape'}, {'focus': '/中文目录/file.txt:stream'},
                {'focus': '/中文目录/subfolder'}, {'focus': '/中文目录/missing'}, {'focus': '/'},
                {'focus': '/root.txt', 'path': '/', 'limit': 0},
                {'focus': '/root.txt', 'path': '/', 'limit': 501},
            ]:
                target = dict({'path': '/中文目录'}, **values)
                check(server.request(route('list', **target))[0] >= 400, 'Invalid focus, directory or limit rejected: ' + str(values))
            # A cached directory must not hide a newly marked file from refresh=1.
            server.api(route('list', path='/', limit=100))
            (root / 'new.txt').write_text('external addition', encoding='utf-8')
            fresh = server.api(route('list', path='/', focus='/new.txt', refresh=1))
            check(fresh['focusPath'] == '/new.txt' and any(e['path'] == '/new.txt' for e in fresh['entries']), 'Refresh bypasses both directory cache checks')
            targets = ['/中文目录/file-%03d.%s' % (index, extensions[index % 4]) for index in range(205, 225)]
            with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
                results = list(pool.map(focused, targets))
            check(all(result['focusPath'] == target for result, target in zip(results, targets)), 'Concurrent focused requests retain independent targets')
        finally:
            server.close()

        configured = Server(exe, root, extra=('--no-magika', '--attention-click', 'download'))
        try:
            check(configured.api('/api/site')['attentionClick'] == 'download', 'CLI initializes global download policy')
        finally:
            configured.close()
        invalid = subprocess.run([str(exe), '--attention-click', 'unknown'], capture_output=True, timeout=10)
        check(invalid.returncode != 0 and b'locate or download' in invalid.stderr, 'CLI rejects unknown attention click action')

        with tempfile.TemporaryDirectory(prefix='hls-reveal-cap-') as limited_directory:
            limited = pathlib.Path(limited_directory)
            for name in ['a.txt', 'b.txt']:
                (limited / name).write_text('x')
            capped = Server(exe, limited, extra=('--no-magika', '--directory-limit', '1'))
            try:
                data = capped.api(route('list', path='/'))
                visible = data['entries'][0]['name']
                omitted = 'b.txt' if visible == 'a.txt' else 'a.txt'
                check(data['truncated'] and capped.request(route('list', path='/', focus='/' + omitted))[0] == 409, 'A real file outside the directory budget reports a bounded conflict')
            finally:
                capped.close()
        print('v2.2.2 passed: global click policy, host guards, targeted pages, sorting, paths, refresh and concurrency')


if __name__ == '__main__':
    run(pathlib.Path(sys.argv[1]).resolve())
