"""Host context-menu attention marks: permissions, bounds and concurrent mutations."""
import concurrent.futures
import json
import pathlib
import sys
import tempfile
from integration import Server, check
from v2_integration import send, route


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-attention-') as directory:
        root = pathlib.Path(directory)
        for folder in ['folder', 'folder2']:
            (root / folder).mkdir()
            (root / folder / 'file.txt').write_text(folder, encoding='utf-8')
        for name in ['first.txt', 'second.txt', '中文.txt']:
            (root / name).write_text(name, encoding='utf-8')
        for index in range(129):
            (root / ('cap-%03d.txt' % index)).write_text('fixture', encoding='utf-8')
        server = Server(exe, root, extra=('--no-magika',))
        try:
            token = server.api('/api/session')['token']
            headers = {'X-HLS-Token': token, 'Content-Type': 'application/json'}

            def mark(path, marked=True, custom=None):
                return send(server, 'POST', '/api/admin/attention', json.dumps({'path': path, 'marked': marked}).encode(), custom or headers)

            def settings(values):
                return send(server, 'POST', '/api/admin/settings', json.dumps(values).encode(), headers)

            def site():
                return server.api('/api/site')

            check(site()['attentionPaths'] == [], 'Attention starts empty')
            body = server.request('/')[2]
            check(b'setting-featured' not in body and b'setting-note' not in body and b'admin/attention' in body, 'Context-menu marks replace single-file settings fields')
            check(mark('/first.txt', custom={'Content-Type': 'application/json'})[0] == 403, 'Attention requires CSRF token')
            check(mark('/first.txt', custom=dict(headers, Origin='http://evil.test'))[0] == 403, 'Attention rejects foreign Origin')
            for payload in [b'{', b'[]', b'{"path":"/first.txt","marked":1}', b'{"path":"/first.txt","marked":true,"extra":0}']:
                check(send(server, 'POST', '/api/admin/attention', payload, headers)[0] == 400, 'Invalid attention schema is rejected')
            for path in ['/../escape', '/file.txt:stream', '/folder', '/missing']:
                check(mark(path)[0] >= 400, 'Unsafe or non-file attention target rejected: ' + path)
            check(site()['attentionPaths'] == [], 'Failed requests cannot change marks')
            for path in ['/first.txt', '/second.txt', '/中文.txt', '/folder/file.txt', '/folder2/file.txt']:
                check(mark(path)[0] == 200, 'Host may mark multiple files even in readonly mode')
            expected = site()['attentionPaths']
            revision = site()['revision']
            check(mark('/first.txt')[0] == 200 and site()['attentionPaths'] == expected and site()['revision'] == revision, 'Duplicate mark is idempotent')
            check(settings({'announcement': 'Multiple files', 'title': 'Attention fixture'})[0] == 200 and site()['attentionPaths'] == expected, 'Other settings preserve attention state')
            check(mark('/second.txt', False)[0] == 200 and '/first.txt' in site()['attentionPaths'] and '/second.txt' not in site()['attentionPaths'], 'Unmark removes only the chosen file')
            (root / '中文.txt').unlink()
            check(mark('/中文.txt', False)[0] == 200, 'Host can clear a mark after external removal')
            check(settings({'writeMode': 'shared'})[0] == 200, 'Enable fixture mutations')
            check(send(server, 'POST', route('rename', path='/folder', to='/renamed'), headers=headers)[0] == 200, 'Rename marked parent directory')
            check('/renamed/file.txt' in site()['attentionPaths'] and '/folder2/file.txt' in site()['attentionPaths'] and '/folder/file.txt' not in site()['attentionPaths'], 'Marks follow directory rename without prefix collision')
            check(send(server, 'DELETE', route('entry', path='/renamed', recursive='1'), headers=headers)[0] == 200, 'Delete marked subtree')
            check('/renamed/file.txt' not in site()['attentionPaths'] and '/folder2/file.txt' in site()['attentionPaths'], 'Deleting a subtree removes only its marks')
            check(send(server, 'POST', route('rename', path='/first.txt', to='/first-renamed.txt'), headers=headers)[0] == 200, 'Rename a marked file')
            check('/first-renamed.txt' in site()['attentionPaths'], 'File mark follows rename')
            for path in site()['attentionPaths']:
                check(mark(path, False)[0] == 200, 'Clear fixture marks')
            paths = ['/cap-%03d.txt' % index for index in range(128)]
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                results = list(pool.map(mark, paths))
            check(all(result[0] == 200 for result in results) and set(site()['attentionPaths']) == set(paths), 'Concurrent per-file changes do not lose unrelated marks')
            revision = site()['revision']
            check(mark('/cap-128.txt')[0] == 409 and len(site()['attentionPaths']) == 128 and site()['revision'] == revision, 'Bounded mark list rejects overflow atomically')
            check(mark(paths[0])[0] == 200, 'Duplicate remains valid at capacity')
            check(mark(paths[0], False)[0] == 200 and mark('/cap-128.txt')[0] == 200, 'Removing a mark releases capacity')
            print('v2.2.1 attention passed: host guards, multiple files, rename/delete, concurrency and bounds')
        finally:
            server.close()


if __name__ == '__main__':
    run(pathlib.Path(sys.argv[1]).resolve())
