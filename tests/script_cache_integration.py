"""Real HTTP regression for trusted bytecode/VM reuse and bounded ZIP assets."""
import concurrent.futures
import hashlib
import http.client
import json
import os
import pathlib
import socket
import sys
import tempfile
import threading
import time
import zipfile

from integration import Server, check


HOOK = r'''
var calls = 0;
function onRequest(r) {
    if (r.url === '/loop') { for (;;) {} }
    if (r.url === '/promise') return Promise.resolve({body:'no'});
    if (r.url === '/jobs') {
        Promise.resolve().then(function () { hls.crypto.sha256('never run after return'); });
        return {body:'queued'};
    }
    if (r.url === '/cycle') {
        var cycle = {}; cycle.self = cycle; globalThis.cycle = cycle;
        return {body:'cycle'};
    }
    var before = Object.prototype.previous;
    Object.prototype.previous = r.url;
    var db = hls.sqlite.open();
    hls.sqlite.execute(db, 'CREATE TABLE t (value TEXT)');
    hls.sqlite.execute(db, 'INSERT INTO t VALUES (?)', ['fresh']);
    return {body:JSON.stringify({count:++calls,old:before || '',
        rows:hls.sqlite.query(db,'SELECT value FROM t').rows,
        hash:hls.crypto.sha256('abc')})};
}
'''

APP = r'''
import './public/private.js';
var requests = 0;
export function onRequest(r) {
    ++requests;
    if(r.headers['x-deny']==='1')return {allow:false};
    if(r.url==='/api/app/count')return {body:String(requests)};
    return {};
}
'''


def wait_for(test, message, seconds=8):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if test():
            return
        time.sleep(.1)
    check(False, message)


def control_status(port):
    conn = http.client.HTTPConnection('localhost', port, timeout=10)
    try:
        conn.request('GET', '/api/status')
        result = conn.getresponse()
        body = result.read()
        check(result.status == 200, 'Control status available')
        return json.loads(body)['script']
    finally:
        conn.close()


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-script-cache-') as folder:
        root = pathlib.Path(folder)
        hook = root / 'hook.js'
        hook.write_text(HOOK, encoding='utf-8')
        expected_hash = hashlib.sha256(b'abc').hexdigest()
        for mode in ('filetrans', 'server'):
            server = Server(exe, root, mode, ('--script', str(hook), '--no-magika',
                '--script-timeout-ms', '100', '--thread-idle', '2'))
            try:
                def probe(_=0):
                    status, _, body = server.request('/probe')
                    check(status == 200, 'Isolated request succeeds')
                    value = json.loads(body)
                    check(value == {'count':1, 'old':'', 'rows':[['fresh']], 'hash':expected_hash},
                          'Globals, prototypes, SQLite and native bindings isolated')
                for _ in range(270):
                    probe()
                with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                    list(pool.map(probe, range(32)))
                stats = server.api('/api/status')['script']
                check(stats['compilations'] == 1 and stats['runtimeReused'] >= 270 and
                      stats['runtimeCreated'] >= 2 and stats['active'] == 0 and stats['idleRuntimes'] <= 4,
                      'Source compiled once, runtime reused and periodically recycled')
                retired = stats['runtimeRetired']
                check(server.request('/jobs')[2] == b'queued', 'Synchronous response with pending job')
                check(server.api('/api/status')['script']['runtimeRetired'] > retired,
                      'Runtime containing pending jobs is discarded')
                for target in ('/promise', '/loop'):
                    check(server.request(target)[0] == 500, 'Promise and CPU limit rejected: ' + target)
                    probe()
                for _ in range(8):
                    check(server.request('/cycle')[2] == b'cycle', 'Cyclic context collects')
                probe()
                wait_for(lambda: server.api('/api/status')['script']['idleRuntimes'] == 0,
                         'Idle VMs release memory')
            finally:
                server.close()

        app = root / 'application.zip'
        payloads = {name:os.urandom(450000) for name in ('a.bin', 'b.bin', 'c.bin')}
        with zipfile.ZipFile(app, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr('server.qjs', APP)
            z.writestr('public/private.js', 'export const secret=42;')
            z.writestr('public/index.html', '<h1>cached app</h1>')
            z.writestr('public/too-big.bin', os.urandom((2 << 20) + 1))
            for name, data in payloads.items():
                z.writestr('public/' + name, data)
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        flags = ('--workspace-app', str(app), '--workspace-control-port', str(port),
                 '--workspace-cache-mb', '1', '--no-magika', '--script-timeout-ms', '500')
        server = Server(exe, root, 'server', flags)
        try:
            check(server.request('/')[2] == b'<h1>cached app</h1>', 'Initial ZIP page')
            check(server.request('/index.html?query=1')[2] == b'<h1>cached app</h1>', 'Normalized cache key')
            initial = control_status(port)['assets']
            check(initial['extractions'] == 1 and initial['hits'] == 1, 'Repeated asset extracted once')
            check(server.request('/', headers={'X-Deny':'1'})[0] == 403, 'Cache cannot bypass request policy')
            check(server.request('/private.js')[0] == 403, 'Imported backend module remains private')
            check(server.request('/too-big.bin')[0] == 413, 'Per-resource extraction limit preserved')
            check(server.request('/%2e%2e/server.qjs')[0] in (400, 403), 'Traversal denied')
            barrier = threading.Barrier(12)
            def cold(_):
                barrier.wait()
                status, _, body = server.request('/a.bin')
                check(status == 200 and body == payloads['a.bin'], 'Cold parallel asset integrity')
            with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                list(pool.map(cold, range(12)))
            after = control_status(port)['assets']
            check(after['extractions'] == initial['extractions'] + 1 and
                  after['hits'] + after['coalesced'] >= initial['hits'] + 11,
                  'Same-file cold requests merge or hit retained data')
            for _ in range(8):
                check(server.request('/a.bin')[2] == payloads['a.bin'], 'Hot asset stable')
            for name in ('b.bin', 'c.bin'):
                check(server.request('/' + name)[2] == payloads[name], 'Cold asset stable')
            before = control_status(port)['assets']
            check(server.request('/a.bin')[2] == payloads['a.bin'], 'Hot asset survives scan')
            after = control_status(port)['assets']
            check(after['hits'] == before['hits'] + 1 and 0 < after['residentBytes'] <= (1 << 20),
                  'Frequency admission and total cache ceiling')
            wait_for(lambda: control_status(port)['assets']['residentBytes'] == 0, 'ZIP cache cools to zero')
            check(server.request('/a.bin')[2] == payloads['a.bin'], 'Cooled asset reloads')
            # External host writes are outside the HTTP freeze policy.
            with app.open('ab') as output:
                output.write(b'changed')
            check(server.request('/a.bin')[0] == 409, 'Cache validates changed package before responding')
            check(control_status(port)['assets']['residentBytes'] == 0, 'Changed package invalidates cache')
        finally:
            server.close()

        # A malformed stored payload has valid matching directory/local CRC
        # fields, so validation fails during extraction rather than startup.
        bad = root / 'bad.zip'
        with zipfile.ZipFile(bad, 'w', zipfile.ZIP_STORED) as z:
            z.writestr('server.qjs', 'export function onRequest(){return {};}')
            z.writestr('public/bad.bin', b'EXPECTED-CRC')
        blob = bad.read_bytes().replace(b'EXPECTED-CRC', b'CORRUPT!-CRC')
        bad.write_bytes(blob)
        server = Server(exe, root, 'server', ('--workspace-app', str(bad),
            '--workspace-control-port', str(port), '--no-magika'))
        try:
            check(server.request('/bad.bin')[0] == 422 and server.request('/bad.bin')[0] == 422,
                  'Failed CRC is not admitted or served on retry')
            stats = control_status(port)['assets']
            check(stats['entries'] == 0 and stats['loading'] == 0 and stats['extractions'] == 2,
                  'Extraction errors clear pending/cache state')
        finally:
            server.close()
        disabled = root / 'uncached.zip'
        with zipfile.ZipFile(disabled, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr('server.qjs', 'export function onRequest(){return {};}')
            z.writestr('public/index.html', 'uncached')
        server = Server(exe, root, 'server', ('--workspace-app', str(disabled),
            '--workspace-control-port', str(port), '--workspace-cache-mb', '0', '--no-magika'))
        try:
            check(server.request('/')[2] == b'uncached' and server.request('/')[2] == b'uncached', 'Cache off serves assets')
            stats = control_status(port)['assets']
            check(stats['limitBytes'] == 0 and stats['residentBytes'] == 0 and stats['extractions'] == 2,
                  'Zero budget disables retained data')
        finally:
            server.close()
    print('Script cache integration passed: bytecode/VM reuse, realm isolation, jobs/limits/recovery, idle release, ZIP coalescing/admission/policy/CRC/invalidation')


if __name__ == '__main__':
    run(str(pathlib.Path(sys.argv[1]).resolve()))
