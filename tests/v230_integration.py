"""Versioned COW uploads, concurrent writers and protected drag-move targets."""
import concurrent.futures
import http.client
import json
import pathlib
import socket
import sys
import tempfile
import time
from integration import Server, check, url
from v2_integration import route, send


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-cow-') as directory:
        root = pathlib.Path(directory)
        (root / 'from').mkdir()
        (root / 'to').mkdir()
        original = b'old content\n' * 800000
        replacement = b'new content\n' * 800000
        (root / 'from/data.bin').write_bytes(original)
        (root / 'to/collision.bin').write_bytes(b'keep destination')
        server = Server(exe, root, extra=('--no-magika', '--write-mode', 'shared'))
        try:
            token = server.api('/api/session')['token']
            headers = {'X-HLS-Token': token}

            def ticket(path):
                return server.api(route('download-ticket', path=path))

            def put(path, data, version='', drop=False):
                values = {'path': path}
                if version:
                    values['replaceVersion'] = version
                if drop:
                    values['drop'] = 1
                return send(server, 'PUT', route('upload', **values), data, headers)

            session = server.api('/api/session')
            check(session['localAdmin'] and session['uploadRoot'] == '/', 'Host session provides shared drop root')
            body = server.request('/')[2]
            check('解锁主机设置'.encode() not in body and b'initFileDrag' in body and b'player-surface' in body, 'Final UI includes settings, drag handling and rebuilt player')
            check(server.api(route('upload-target', path='/missing.txt')) == {'exists': False}, 'Missing target probe succeeds without a failed HTTP request')
            check(server.api(route('upload-target', path='/to'))['directory'], 'Directory collision has no replaceable file version')
            check(server.api(route('upload-target', path='/from/data.bin'))['version'] == ticket('/from/data.bin')['version'], 'Preflight observes the exact replaceable generation')
            first = ticket('/from/data.bin')
            old_reader = http.client.HTTPConnection('127.0.0.1', server.port, timeout=30)
            old_reader.request('GET', first['url'])
            old_response = old_reader.getresponse()
            check(old_response.status == 200, 'Old generation download starts')
            prefix = old_response.read(32768)
            check(put('/from/data.bin', b'unconditional')[0] == 409, 'Unconditional overwrite remains forbidden')
            result = put('/from/data.bin', replacement, first['version'])
            check(result[0] == 200 and json.loads(result[2])['version'] != first['version'], 'Explicit versioned replacement atomically publishes new file')
            check(prefix + old_response.read() == original, 'An active download finishes entirely from the original generation')
            old_reader.close()
            check(server.request(url('/from/data.bin'))[2] == replacement, 'New download sees new generation')
            check(server.request(first['url'])[0] == 409, 'Old pinned URL cannot resume into a replacement')
            check(put('/from/data.bin', b'stale', first['version'])[0] == 409, 'Stale writer is rejected without changes')
            current = ticket('/from/data.bin')

            def writer(index):
                return put('/from/data.bin', ('writer-%d' % index).encode() * 4096, current['version'])[0]

            with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                results = list(pool.map(writer, range(20)))
            check(results.count(200) == 1 and all(status in (200, 409) for status in results), 'Exactly one writer can replace a given version')
            with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                results = list(pool.map(lambda index: put('/same-name.bin', bytes([index]) * 32768)[0], range(20)))
            check(results.count(201) == 1 and all(status in (201, 409) for status in results), 'Same-name new uploads cannot overwrite one another')

            def partial(path, expected):
                sock = socket.create_connection(('127.0.0.1', server.port), timeout=15)
                sock.settimeout(15)
                target = route('upload', path=path, replaceVersion=expected)
                head = 'PUT ' + target + ' HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nX-HLS-Token: %s\r\nContent-Length: 65536\r\n\r\n' % (server.port, token)
                sock.sendall(head.encode() + b'A' * 8192)
                for _ in range(100):
                    if list((root / 'from').glob('.hls-upload-*')):
                        break
                    time.sleep(.02)
                check(list((root / 'from').glob('.hls-upload-*')), 'Partial write uses a hidden temporary file')
                return sock

            current = ticket('/from/data.bin')
            sock = partial('/from/data.bin', current['version'])
            check(send(server, 'POST', route('rename', path='/from', to='/renamed'), headers=headers)[0] == 409, 'Parent move is blocked while a replacement is streaming')
            check(send(server, 'DELETE', route('entry', path='/from/data.bin'), headers=headers)[0] == 409, 'Deletion cannot race a replacement')
            check(send(server, 'POST', route('mkdir', path='/unrelated'), headers=headers)[0] == 201, 'Unrelated mutations remain available while upload streams')
            before_abort = server.request(url('/from/data.bin'))[2]
            sock.close()
            for _ in range(100):
                if not list((root / 'from').glob('.hls-upload-*')):
                    break
                time.sleep(.02)
            check(not list((root / 'from').glob('.hls-upload-*')) and server.request(url('/from/data.bin'))[2] == before_abort, 'Aborted replacement cleans its temp file and preserves published content')

            current = ticket('/from/data.bin')
            sock = partial('/from/data.bin', current['version'])
            external = b'external edit'
            (root / 'from/data.bin').write_bytes(external)
            sock.sendall(b'A' * (65536 - 8192))
            response = http.client.HTTPResponse(sock)
            response.begin()
            check(response.status == 409, 'An externally changed version is rechecked before commit')
            response.read();response.close();sock.close()
            check(server.request(url('/from/data.bin'))[2] == external, 'Failed version check leaves external edit intact')

            current = ticket('/from/data.bin')
            check(send(server, 'POST', route('rename', path='/from/data.bin', to='/to/moved.bin', version=first['version']), headers=headers)[0] == 409, 'Move with a stale version fails')
            check(send(server, 'POST', route('rename', path='/from/data.bin', to='/to/collision.bin', version=current['version']), headers=headers)[0] == 409, 'Move cannot replace a destination file')
            check((root / 'to/collision.bin').read_bytes() == b'keep destination', 'Move conflict preserves destination bytes')
            check(send(server, 'POST', route('rename', path='/from/data.bin', to='/to/moved.bin', version=current['version']), headers=headers)[0] == 200, 'Writable file moves to a target directory')
            check(not (root / 'from/data.bin').exists() and (root / 'to/moved.bin').read_bytes() == external, 'Move publishes correct source bytes at destination')
            check(send(server, 'POST', route('rename', path='/to', to='/to/child'), headers=headers)[0] >= 400, 'A folder cannot move into itself')
            check(put('/to/drop.txt', b'dropped', drop=True)[0] == 201 and (root / 'to/drop.txt').read_bytes() == b'dropped', 'Shared external drop retains target directory')
            check(put('/../escape', b'x', drop=True)[0] == 400, 'Drop path traversal is rejected')
            update = json.dumps({'writeMode': 'readonly'}).encode()
            check(send(server, 'POST', '/api/admin/settings', update, dict(headers, **{'Content-Type': 'application/json'}))[0] == 200, 'Switch to readonly')
            check(put('/to/denied', b'x', drop=True)[0] == 403 and send(server, 'POST', route('rename', path='/to/moved.bin', to='/denied'), headers=headers)[0] == 403, 'Readonly mode blocks both file drops and moves')
        finally:
            server.close()

        isolated = Server(exe, root, extra=('--no-magika', '--write-mode', 'isolated'))
        try:
            session = isolated.api('/api/session')
            own = session['uploadRoot']
            check(own == '/' and session['writableRoot'] == '/', 'Host isolated uploads retain administrator root rights')
            headers = {'X-HLS-Token': session['token']}
            status, _, body = send(isolated, 'PUT', route('upload', path='/to/host-drop.txt', drop=1), b'own root', headers)
            check(status == 201 and json.loads(body)['path'] == '/to/host-drop.txt', 'Isolated host drop retains the selected destination')
            (root / own.lstrip('/') / 'inside').mkdir()
            status, _, body = send(isolated, 'PUT', route('upload', path='/inside/deep.txt', drop=1), b'inner', headers)
            check(status == 201 and json.loads(body)['path'] == '/inside/deep.txt', 'Drop within own root keeps selected subfolder')
        finally:
            isolated.close()
        print('v2.3 passed: COW reader continuity, version conflicts, concurrent writers, abort cleanup, safe moves and drop routing')


if __name__ == '__main__':
    run(pathlib.Path(sys.argv[1]).resolve())
