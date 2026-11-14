"""Real-process acceptance tests. Python is a development dependency only."""
import concurrent.futures
import ctypes
import http.client
import json
import os
import pathlib
import socket
import subprocess
import tempfile
import time
import urllib.parse


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def url(path):
    return '/download?' + urllib.parse.urlencode({'path': path})


class Server:
    def __init__(self, exe, root, mode='filetrans', extra=(), fixed_io=True, config=False):
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            self.port = sock.getsockname()[1]
        self.log = tempfile.TemporaryFile()
        flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
        self.process = subprocess.Popen([str(exe), *([] if config else ['--no-config']), *([] if mode is None else [mode]), '--root', str(root), '--bind', '127.0.0.1', '--port', str(self.port), *(['--io-threads', '4'] if fixed_io else []), '--cache-mb', '32', '--cache-idle', '3', '--heat-half-life', '2', '--index-interval', '2', *extra], stdout=self.log, stderr=self.log, creationflags=flags)
        for _ in range(400):
            if self.process.poll() is not None:
                self.log.seek(0)
                raise RuntimeError(self.log.read().decode('utf-8', errors='replace'))
            try:
                with socket.create_connection(('127.0.0.1', self.port), timeout=.1):
                    return
            except OSError:
                time.sleep(.05)
        self.process.terminate()
        self.process.wait(timeout=10)
        self.log.seek(0)
        output = self.log.read().decode('utf-8', errors='replace')
        self.log.close()
        raise RuntimeError('Server did not start: ' + output[-4000:])

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.log.seek(0)
        output = self.log.read().decode('utf-8', errors='replace')
        self.log.close()
        if os.name != 'nt':
            check(self.process.returncode == 0, f'Graceful shutdown failed ({self.process.returncode}): {output[-4000:]}')
        check('ERROR: AddressSanitizer' not in output and 'runtime error:' not in output and 'LeakSanitizer' not in output, 'Runtime sanitizer report: ' + output[-4000:])

    def request(self, target, method='GET', headers=None):
        connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=20)
        try:
            connection.request(method, target, headers=headers or {})
            response = connection.getresponse()
            return response.status, {k.lower(): v for k, v in response.getheaders()}, response.read()
        finally:
            connection.close()

    def api(self, target):
        status, _, body = self.request(target)
        check(status == 200, f'{target}: {status}, {body[:100]!r}')
        return json.loads(body)


def make_sparse(path):
    with path.open('wb') as output:
        if os.name == 'nt':
            import msvcrt
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.DeviceIoControl.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
            returned = ctypes.c_uint32()
            check(kernel.DeviceIoControl(msvcrt.get_osfhandle(output.fileno()), 0x900c4, None, 0, None, 0, ctypes.byref(returned), None), 'Sparse fixture support')
        output.seek(5 * 1024**3 + 123)
        output.write(b'RANGE-OVER-4G')


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-integration-') as temporary:
        root = pathlib.Path(temporary) / '共享目录'
        root.mkdir()
        (root / 'Folder').mkdir()
        payload = bytes(range(256)) * 4096 + b'end-marker'
        (root / 'payload.bin').write_bytes(payload)
        (root / 'empty.bin').write_bytes(b'')
        (root / '中文测试.txt').write_bytes('中文下载\n'.encode('utf-8'))
        (root / 'Folder/nested.txt').write_text('nested', encoding='utf-8')
        (root / 'index.html').write_text('<h1>static works</h1>', encoding='utf-8')
        make_sparse(root / 'large.bin')
        outside = pathlib.Path(temporary) / 'outside.txt'
        outside.write_text('must not leak', encoding='utf-8')
        linked = False
        try:
            (root / 'escape.txt').symlink_to(outside)
            linked = True
        except OSError:
            pass
        server = Server(exe, root)
        try:
            status, _, body = server.request('/')
            check(status == 200 and b'<html' in body and b'<script>' in body, 'Embedded offline UI')
            target = url('/payload.bin')
            status, headers, body = server.request(target)
            check(status == 200 and body == payload, 'Full download')
            etag = headers['etag']
            pinned = server.api('/api/download-ticket?' + urllib.parse.urlencode({'path': '/payload.bin'}))['url']
            status_pinned, pinned_headers, _ = server.request(pinned, 'HEAD')
            check(status_pinned == 200, 'Version-pinned download URL')
            status_pinned, _, pinned_body = server.request(pinned, headers={'Range': 'bytes=17-1000', 'If-Range': pinned_headers['last-modified']})
            check(status_pinned == 206 and pinned_body == payload[17:1001], 'Browser-style If-Range resume on pinned URL')
            status, headers, body = server.request(target, 'HEAD')
            check(status == 200 and not body and int(headers['content-length']) == len(payload), 'HEAD')
            for value, expected in [('bytes=0-0', payload[:1]), ('bytes=17-1000', payload[17:1001]), ('bytes=1048500-', payload[1048500:]), ('bytes=-10', payload[-10:]), ('bytes=-99999999', payload)]:
                status, headers, body = server.request(target, headers={'Range': value})
                check(status == 206 and body == expected and int(headers['content-length']) == len(expected), value)
            status, headers, _ = server.request(target, headers={'Range': 'bytes=99999999-'})
            check(status == 416 and headers['content-range'] == 'bytes */' + str(len(payload)), '416')
            status, _, body = server.request(target, headers={'Range': 'bytes=0-4,8-9'})
            check(status == 200 and body == payload, 'Multirange conservative fallback')
            status, _, body = server.request(target, headers={'Range': 'bytes=5-', 'If-Range': etag})
            check(status == 200 and body == payload, 'Weak If-Range cannot splice versions')
            status, _, body = server.request(target, headers={'If-None-Match': etag})
            check(status == 304 and not body, '304')
            status, _, body = server.request(url('/large.bin'), headers={'Range': f'bytes={5 * 1024**3 + 123}-'})
            check(status == 206 and body == b'RANGE-OVER-4G', 'Range beyond 4 GiB')
            check(server.request(url('/empty.bin'))[2] == b'', 'Empty file')
            check(server.request(url('/empty.bin'), headers={'Range': 'bytes=0-'})[0] == 416, 'Empty-file range')
            status, headers, body = server.request(url('/中文测试.txt'))
            check(status == 200 and body.decode('utf-8') == '中文下载\n' and '%E4%B8%AD' in headers.get('content-disposition',''), f'Unicode download: {status}, {headers!r}, {body!r}')
            for path in ['/../outside.txt', '/Folder/../../outside.txt', '/payload.bin:stream', '/Folder\\nested.txt']:
                check(server.request(url(path))[0] in (400, 403), 'Path confinement: ' + path)
            for target_invalid in ['/download?path=%00', '/download?path=%c0%af', '/download?path=%ZZ']:
                check(server.request(target_invalid)[0] == 400, 'Malformed URL')
            if linked:
                check(server.request(url('/escape.txt'))[0] == 403, 'Symlink confinement')
            listing = server.api('/api/list?limit=2')
            check(len(listing['entries']) == 2 and listing['entries'][0]['directory'], 'Folders-first pagination')
            second = server.api('/api/list?offset=2&limit=2')
            check(not {e['path'] for e in listing['entries']} & {e['path'] for e in second['entries']}, 'Stable pagination')
            for _ in range(150):
                result = server.api('/api/search?' + urllib.parse.urlencode({'q': '中文'}))
                if result['entries']:
                    break
                time.sleep(.1)
            check(any(e['name'] == '中文测试.txt' for e in result['entries']), 'Two-character Chinese search')
            for query in ['测试', '中文测', 'payload', '" OR 1=1 --']:
                result = server.api('/api/search?' + urllib.parse.urlencode({'q': query}))
                check(bool(result['entries']) == (query != '" OR 1=1 --'), 'Indexed search: ' + query)
            connection = http.client.HTTPConnection('127.0.0.1', server.port, timeout=10)
            for _ in range(8):
                connection.request('GET', target, headers={'Range': 'bytes=0-1023'})
                response = connection.getresponse()
                check(response.status == 206 and response.read() == payload[:1024], 'Keep-Alive')
            connection.close()
            def concurrent_get(index):
                start = (index * 997) % (len(payload) - 65536)
                status, _, body = server.request(target, headers={'Range': f'bytes={start}-{start + 65535}'})
                check(status == 206 and body == payload[start:start + 65536], 'Concurrent integrity')
            started = time.monotonic()
            with concurrent.futures.ThreadPoolExecutor(max_workers=24) as pool:
                list(pool.map(concurrent_get, range(144)))
            elapsed = time.monotonic() - started
            stats = server.api('/api/status')['cache']
            check(stats['hits'] > 0 and stats['allocatedBytes'] <= stats['limitBytes'] + stats['bufferBytes'], 'Cache hits and memory ceiling')
            (root / 'payload.bin').write_bytes(b'new file version')
            check(server.request(pinned, headers={'Range': 'bytes=1-', 'If-Range': pinned_headers['last-modified']})[0] == 409, 'Pinned version cannot splice modified content')
            check(server.request(target)[2] == b'new file version', 'Version invalidation')
            time.sleep(4.5)
            stats = server.api('/api/status')['cache']
            check(stats['residentBytes'] == 0 and stats['allocatedBytes'] == 0 and stats['releasedBytes'] > 0, 'Cooling returns OS pages')
            print(f'FileTrans passed: protocol, Unicode, search, 5 GiB range, cache cooling; 144 range requests in {elapsed:.2f}s')
        finally:
            server.close()
        static = Server(exe, root, 'server')
        try:
            check(static.request('/')[2] == b'<h1>static works</h1>', 'Static index')
            check(static.request('/Folder')[0] == 301, 'Directory redirect')
            check(static.request('/Folder/nested.txt')[2] == b'nested', 'Static resource')
            check(static.api('/api/status')['mode'] == 'server' and static.request('/_hls/')[0] == 200, 'Server shares management APIs and has a separate UI entry')
            check(static.request('/%2e%2e/outside.txt')[0] == 400, 'Static confinement')
            print('Server mode passed')
        finally:
            static.close()
        overloaded = Server(exe, root, extra=('--max-transfers', '1'))
        try:
            slow = http.client.HTTPConnection('127.0.0.1', overloaded.port, timeout=10)
            slow.request('GET', url('/large.bin'))
            check(slow.getresponse().status == 200, 'Slow transfer starts')
            check(overloaded.request(url('/payload.bin'))[0] == 503, 'Bounded overload rejection')
            check(overloaded.request('/api/list')[0] == 200, 'Reserved control slots remain responsive')
            slow.close()
            print('Backpressure passed')
        finally:
            overloaded.close()


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=pathlib.Path)
    run(parser.parse_args().executable.resolve())
