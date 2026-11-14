"""Private browser-generated streams, checkpoints, resumable copies and mount delivery."""
import concurrent.futures
import hashlib
import pathlib
import sys
import subprocess
import tempfile
import threading
import time
import urllib.parse
from integration import Server, check
from durable_integration import send


def run(executable):
    for mode in ('filetrans', 'server'):
        with tempfile.TemporaryDirectory(prefix='hls-private-transfer-') as folder:
            root = pathlib.Path(folder)
            payload = bytes(range(251)) * 19000
            (root / 'source.bin').write_bytes(payload)
            (root / 'config.json').write_text('{"version":1,"host":{"announcement":"secret"}}')
            flags = ('--no-magika','--config',str(root/'config.json'))
            server = Server(executable, root, mode, flags,config=True)
            try:
                token = server.api('/api/session')['token']
                def post(route, body):
                    return send(server, route, 'POST', body, token)
                status, cache, _ = post('/api/transfer-cache/create', dict(path='/source.bin', name='private.zip'))
                check(status == 201, 'Read-only clients may create private output')
                query = urllib.parse.urlencode(dict(id=cache['id'], key=cache['key']))
                route = '/api/transfer-cache/'
                check(post(route+'chunk?'+query+'&offset=0', b'first')[1]['offset'] == 5, 'Append stream')
                check(post(route+'chunk?'+query+'&offset=0', b'first')[1]['offset'] == 5, 'Retry has no duplicate bytes')
                check(post(route+'chunk?'+query+'&offset=0', b'other')[0] == 409, 'Mismatching retry rejected')
                check(send(server, route+'status?'+query, source='127.0.0.2')[0] == 403, 'Capabilities are bound to IP')
                check(post(route+'checkpoint?'+query, dict(offset=5, journal={'plan':{'test':'plan'}, 'state':{'test':'one'}}))[0] == 200, 'Atomic journal and byte checkpoint')
                check(post(route+'chunk?'+query+'&offset=5', b'uncommitted')[0] == 200, 'Append beyond checkpoint')
                server.close()
                server = Server(executable, root, mode, flags,config=True)
                token = server.api('/api/session')['token']
                state = server.api(route+'status?'+query)
                check(state['offset'] == 16 and state['checkpoint'] == 5 and state['journal']['state']['test'] == 'one', 'Restart preserves bytes and exact journal')
                check(post(route+'trim?'+query, dict(offset=6))[0] == 409, 'Cannot retain uncommitted bytes')
                check(post(route+'trim?'+query, dict(offset=5))[0] == 200, 'Rollback discards uncommitted bytes')
                check(post(route+'checkpoint?'+query, dict(offset=5, journal={'state':{'test':'two'}}))[0] == 200, 'Cursor can replace journal without resending plan')
                check(server.api(route+'status?'+query)['journal']['plan']['test'] == 'plan', 'Immutable plan retained')
                check(post(route+'complete?'+query, {})[0] == 200, 'Complete output')
                code, data, headers = send(server, route+'read?'+query, headers={'Range':'bytes=1-3'})
                check(code == 206 and data == b'irs' and headers.get('Content-Range') == 'bytes 1-3/5', 'Native Range download of generated output')
                check(post(route+'chunk?'+query+'&offset=5', b'x')[0] == 409, 'Completed output immutable')
                check(post(route+'create', dict(path='/config.json', name='x'))[0] == 403, 'Frozen config cannot enter private operations')
                check(post(route+'create', dict(path='/../outside', name='x'))[0] >= 400, 'Traversal refused')
                check(post(route+'cancel?'+query, {})[0] == 200 and send(server, route+'status?'+query)[0] == 404, 'Explicit cancellation removes private output')
                check(post('/api/resume/create', dict(path='/copy.bin', size=len(payload), fingerprint='copy'))[0] == 403, 'Read-only cannot publish copies')
            finally:
                server.close()
            flags = ('--config',str(root/'config.json'),'--no-magika', '--write-mode', 'shared', '--max-transfers', '1')
            server = Server(executable, root, mode, flags,config=True)
            try:
                token = server.api('/api/session')['token']
                ticket = server.api('/api/download-ticket?path=/source.bin')
                code, upload, _ = post('/api/resume/create', dict(path='/copy.bin', size=len(payload), fingerprint='copy-test'))
                check(code == 201, 'Create durable native copy')
                query = urllib.parse.urlencode(dict(id=upload['id'], key=upload['key']))
                values = dict(source='/source.bin', version=ticket['version'], offset=0, id=upload['id'], key=upload['key'])
                check(post('/api/resume/copy-chunk', values)[1]['offset'] == 1048576, 'Pinned native chunk copied')
                check(post('/api/resume/copy-chunk', values)[1]['offset'] == 1048576, 'Copy retry idempotent')
                server.close()
                server = Server(executable, root, mode, flags,config=True)
                token = server.api('/api/session')['token']
                offset = server.api('/api/resume/status?'+query)['offset']
                check(offset == 1048576, 'Mid-file copy survives server restart')
                while offset < len(payload):
                    values['offset'] = offset
                    code, result, _ = post('/api/resume/copy-chunk', values)
                    check(code == 200, 'Copy next chunk '+str(result))
                    offset = result['offset']
                check(post('/api/resume/commit?'+query, {})[0] in (200,201), 'Publish copied bytes')
                check((root/'copy.bin').read_bytes() == payload, 'Exact recovered copy')
                check(post('/api/resume/commit?'+query, {})[0] == 200, 'Publication receipt idempotent')
                (root/'source.bin').write_bytes(b'changed')
                check(post('/api/resume/copy-chunk', values)[0] == 409, 'Changed source rejected')
                (root/'source.bin').write_bytes(payload)
                # Provider authentication is distinct from the uploader capability.
                code, mount, _ = post('/api/mount/register', dict(path='/Mounted', browser='private-provider-page-123456', writable=True, movable=False))
                check(code == 200, 'Writable provider registers')
                quota_before = sum(item['used'] for item in server.api('/api/admin/state')['quotas'])
                code, cache, _ = post('/api/transfer-cache/create', dict(path='/Mounted/file.bin', name='file.bin', purpose='upload', size=len(payload)))
                check(code == 201, 'Durable mounted upload stage')
                query = urllib.parse.urlencode(dict(id=cache['id'], key=cache['key']))
                offset = 0
                while offset < len(payload):
                    code, result, _ = post('/api/transfer-cache/chunk?'+query+'&offset='+str(offset), payload[offset:offset+1048576])
                    check(code == 200, 'Bounded mounted stage '+str(result))
                    offset = result['offset']
                stop = threading.Event()
                errors = []
                received = bytearray()
                def provider():
                    try:
                        while not stop.is_set():
                            code, data, _ = post('/api/mount/poll', {'providers':[{'id':mount['id'],'key':mount['key']}]})
                            for job in data['jobs']:
                                check(job['op'] == 'write-cache', 'Durable mount publication uses streamed private payload')
                                q = urllib.parse.urlencode(dict(mount=mount['id'], key=mount['key'], id=job['id']))
                                at = 0
                                while at < job['size']:
                                    last = min(job['size'], at+262144)
                                    status, block, _ = send(server, '/api/mount/payload?'+q, headers={'Range':f'bytes={at}-{last-1}'})
                                    check(status == 206 and block == payload[at:last], 'Provider receives exact bounded private ranges')
                                    received.extend(block)
                                    at = last
                                check(post('/api/mount/reply?'+q, {'size':job['size'],'version':'provider-new-version'})[0] == 200, 'Provider commits versioned receipt')
                            time.sleep(.02)
                    except Exception as error:
                        errors.append(error)
                thread = threading.Thread(target=provider, daemon=True)
                thread.start()
                try:
                    code, receipt, _ = post('/api/transfer-cache/publish-mount?'+query, {'version':''})
                    check(code == 201 and receipt['version'] == 'provider-new-version', 'Uploader receives durable provider receipt '+str(receipt))
                    check('type' in receipt and 'mime' in receipt['type'], 'Mounted upload retains file identification and suffix advice')
                    check(bytes(received) == payload, 'No whole-file proxy buffer')
                    check((root/'Mounted/file.bin').read_bytes() == payload, 'Host publishes existing staged bytes without provider reupload')
                    check(sum(item['used'] for item in server.api('/api/admin/state')['quotas']) - quota_before == len(payload), 'Staged bytes are charged only once')
                    check(post('/api/transfer-cache/publish-mount?'+query, {'version':''})[1] == receipt, 'Lost publication ACK does not republish')
                finally:
                    stop.set()
                    thread.join(10)
                if errors:
                    raise errors[0]
                check(post('/api/transfer-cache/cancel?'+query, {})[0] == 200, 'Finished mount stage releasable')
                subprocess.run(['node', str(pathlib.Path(__file__).with_name('copy_jobs_test.cjs')), 'http://127.0.0.1:'+str(server.port), str(root)], check=True, timeout=40)
            finally:
                server.close()
    print('Private transfer integration passed: both modes, read-only ZIP spool, IP binding, restart, atomic journals, Range, frozen/traversal protection, chunked native copy and provider delivery')


if __name__ == '__main__':
    run(str(pathlib.Path(sys.argv[1]).resolve()))
