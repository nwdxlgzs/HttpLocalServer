"""Crash/restart, upload idempotency, recycle, scripts and both management modes."""
import http.client
import json
import pathlib
import sys
import sqlite3
import tempfile
import time
import urllib.parse
from integration import Server, check


def send(server, route, method='GET', body=None, token=None, headers=None, source=None):
    connection = http.client.HTTPConnection('127.0.0.1', server.port, timeout=30, source_address=(source,0) if source else None)
    fields = dict(headers or {})
    if token:
        fields['X-HLS-Token'] = token
    if isinstance(body, (dict, list)):
        body = json.dumps(body).encode()
        fields['Content-Type'] = 'application/json'
    try:
        connection.request(method, route, body, fields)
        result = connection.getresponse()
        data = result.read()
        try:
            parsed = json.loads(data)
        except (ValueError, UnicodeDecodeError):
            parsed = data
        return result.status, parsed, dict(result.getheaders())
    finally:
        connection.close()


def run(executable):
    for mode in ('filetrans', 'server'):
        with tempfile.TemporaryDirectory(prefix='hls-durable-') as folder:
            root = pathlib.Path(folder)
            script = root / 'hooks.js'
            script.write_text('''function onRequest(r) {
 if (r.url === '/hello') return {body:'script alive',headers:{'X-Hook':'yes'}};
 if (r.url === '/write') return {write:{path:'/scripted.txt',text:r.body}};
 if (r.url === '/forbidden') return {file:'/config.json'};
 if (r.url === '/loop') {for(;;){}}
 if (r.url === '/stack') {function f(){return 1+f();}return {body:String(f())};}
 if (r.url === '/memory') {var a=[];for(;;)a.push(new Array(100000).fill('x'));}
 return {};
}
function onFileOperation(e) {if (e.path.indexOf('blocked') >= 0) return {allow:false,message:'blocked by test'};}
''', encoding='utf-8')
            (root / 'home.htm').write_text('HOME', encoding='utf-8')
            (root / '404.html').write_text('CUSTOM NOT FOUND', encoding='utf-8')
            (root / 'config.json').write_text('{"version":1,"host":{"announcement":"HOST SECRET"}}', encoding='utf-8')
            flags = ('--config',str(root/'config.json'),'--write-mode', 'shared', '--recycle-bin', '--script', str(script),
                     '--default-documents', 'home.htm', '--spa-fallback', '/home.htm',
                     '--error-page', '404=/404.html', '--response-header', 'X-Site=test', '--no-magika')
            server = Server(executable, root, mode, flags,config=True)
            try:
                token = server.api('/api/session')['token']
                check(send(server, '/_hls/')[0] == 200, 'Management shell is available in both modes')
                check(send(server, '/hello')[1] == b'script alive', 'Request hook works')
                check(send(server, '/write', 'POST', b'controlled write', token)[0] == 201, 'Script COW write')
                check((root / 'scripted.txt').read_bytes() == b'controlled write', 'Script published controlled body')
                check(send(server, '/write', 'POST', b'without token')[0] == 403, 'Scripts cannot bypass write tokens')
                check(send(server, '/forbidden')[0] == 403, 'Scripts cannot bypass frozen config')
                start = time.monotonic()
                check(send(server, '/loop')[0] == 500 and time.monotonic()-start < 3, 'Script CPU deadline')
                check(send(server, '/memory')[0] == 500, 'Script memory limit')
                check(send(server, '/stack')[0] == 500, 'Script stack limit')
                check(send(server, '/hello')[0] == 200, 'Runtime recovers after failed scripts')
                if mode == 'server':
                    check(send(server, '/')[1] == b'HOME', 'Configured default document')
                    check(send(server, '/app/route', headers={'Accept': 'text/html'})[1] == b'HOME', 'SPA extensionless fallback')
                    status, data, headers = send(server, '/missing.bin')
                    check(status == 404 and data == b'CUSTOM NOT FOUND' and headers.get('X-Site') == 'test', 'Custom error/header')
                payload = b'abcd' * (700000)
                status, upload, _ = send(server, '/api/resume/create', 'POST', {'path': '/large.bin', 'size': len(payload), 'fingerprint': 'fixture-v1'}, token)
                check(status == 201, 'Create resumable upload: '+str(upload))
                query = urllib.parse.urlencode({'id': upload['id'], 'key': upload['key']})
                first = payload[:1048576]
                check(send(server, '/api/resume/chunk?'+query+'&offset=0', 'POST', first, token)[1]['offset'] == len(first), 'First chunk')
                before = server.api('/api/admin/state')['quotas'][0]['used']
                check(send(server, '/api/resume/chunk?'+query+'&offset=0', 'POST', first, token)[0] == 200, 'Idempotent duplicate chunk')
                check(server.api('/api/admin/state')['quotas'][0]['used'] == before, 'Duplicate chunk is not charged twice')
                check(send(server, '/api/resume/chunk?'+query+'&offset=1', 'POST', b'bad', token)[0] == 409, 'Retry content checked')
                server.close()
                server = Server(executable, root, mode, flags,config=True)
                token = server.api('/api/session')['token']
                check(server.api('/api/resume/status?'+query)['offset'] == len(first), 'Offset survives process restart')
                check(server.api('/api/admin/state')['quotas'][0]['used'] == before, 'Quota survives restart')
                offset = len(first)
                while offset < len(payload):
                    chunk = payload[offset:offset+1048576]
                    check(send(server, '/api/resume/chunk?'+query+'&offset='+str(offset), 'POST', chunk, token)[0] == 200, 'Resume chunk')
                    offset += len(chunk)
                status, result, _ = send(server, '/api/resume/commit?'+query, 'POST', {}, token)
                check(status == 201, 'Commit resumable upload: '+str(result))
                check((root / 'large.bin').read_bytes() == payload, 'Published bytes match')
                check(send(server, '/api/resume/commit?'+query, 'POST', {}, token)[0] == 200, 'Commit replay idempotent')
                check(send(server, '/api/entry?path=/large.bin&recursive=1', 'DELETE', token=token)[0] == 200, 'Recycle delete')
                check(not (root/'large.bin').exists(), 'Recycled path removed')
                trash = server.api('/api/recycle')['entries']
                check(len(trash) == 1, 'Recycle metadata exists')
                check(send(server, '/api/recycle/restore?id='+trash[0]['id'], 'POST', {}, token)[0] == 200, 'Restore')
                check((root / 'large.bin').read_bytes() == payload, 'Restored bytes')
                (root/'source.txt').write_text('source')
                (root/'destination.txt').write_text('old')
                status, rejected, _ = send(server, '/api/batch', 'POST', {'action':'move','conflict':'replace','items':[{'path':'/source.txt','to':'/destination.txt'}]}, token)
                check(status == 200 and rejected['results'][0]['status'] == 409 and (root/'destination.txt').read_text() == 'old', 'Unconfirmed batch replacement is rejected')
                stale = server.api('/api/download-ticket?path=/destination.txt')['version']
                (root/'destination.txt').write_text('changed-after-confirmation')
                status, rejected, _ = send(server, '/api/batch', 'POST', {'action':'move','conflict':'replace','items':[{'path':'/source.txt','to':'/destination.txt','replaceVersion':stale}]}, token)
                check(status == 200 and rejected['results'][0]['status'] == 409 and (root/'source.txt').exists(), 'Stale destination confirmation cannot move over a newer version')
                status, batch, _ = send(server, '/api/batch', 'POST', {'action':'move','conflict':'replace','items':[{'path':'/source.txt','to':'/destination.txt','replaceVersion':send(server, '/api/download-ticket?path=/destination.txt')[1]['version']}]}, token)
                check(status == 200 and batch['results'][0]['status'] == 200 and (root/'destination.txt').read_text() == 'source' and not (root/'source.txt').exists(), 'Batch versioned replace move')
                (root/'source.txt').write_text('second')
                status, batch, _ = send(server, '/api/batch', 'POST', {'action':'move','conflict':'keep-both','items':[{'path':'/source.txt','to':'/destination.txt'}]}, token)
                check(status == 200 and batch['results'][0]['to'] == '/destination (1).txt', 'Batch keep both')
                (root/'safe.txt').write_text('safe')
                status, _, _ = send(server, '/api/batch', 'POST', {'action':'delete','items':[{'path':'/safe.txt'},{'path':'/config.json'}]}, token)
                check(status == 403 and (root/'safe.txt').exists(), 'Whole batch frozen-path preflight')
                check(send(server, '/api/resume/create', 'POST', {'path':'/blocked.bin','size':1,'fingerprint':'x'}, token)[0] == 403, 'File hook policy')
                check(send(server, '/api/admin/state', 'POST', {'action':'reset-quota','ip':'127.0.0.1'}, token)[0] == 200, 'Host quota reset')
                check(send(server,'/api/list?path=/.hls-trash')[0]==403,'Actual recycle storage cannot be browsed')
                frozen_trash=next(e for e in server.api('/api/list?path=/&showHidden=1')['entries'] if e['name']=='.hls-trash');check(frozen_trash.get('frozen') and not frozen_trash['writable'],'Actual recycle storage visibly frozen only on request')
                entries = server.api('/api/list?path=/')['entries']
                check(all(e['name'].startswith('.hls-state-') and e.get('frozen') and e['size']==0 and not e['writable'] for e in entries if e['name'].startswith('.hls-')), 'State placeholders are frozen; private trash stays absent '+repr([e for e in entries if e['name'].startswith('.hls-')]))
                check(send(server,'/api/entry?path=/large.bin&recursive=1','DELETE',token=token)[0]==200,'Recycle survives a later restart')
                retained=server.api('/api/recycle')['entries'][0]
                server.close()
                database=next(root.glob('.hls-state-*/state.sqlite'))
                with sqlite3.connect(database) as db:
                    for i in range(1024):
                        record={'ip':'192.0.2.1','size':0,'phase':'done','expires':int(time.time())+600}
                        db.execute("INSERT OR REPLACE INTO kv(scope,k,v) VALUES('resume',?,?)",(format(i,'048x'),json.dumps(record)))
                db.close()
                server=Server(executable,root,mode,flags,config=True)
                token=server.api('/api/session')['token']
                check(server.api('/api/recycle')['entries'][0]['id']==retained['id'],'Recycle metadata survives restart')
                check(send(server,'/api/recycle/restore?id='+retained['id'],'POST',{},token)[0]==200,'Restore after restart')
                code,receipt,_=send(server,'/api/resume/create','POST',{'path':'/after-many-uploads.txt','size':0,'fingerprint':'capacity-fixture'},token)
                check(code==201,'Completed receipts never become a lifetime upload limit')
                check(server.api('/api/admin/state')['uploads']<=1024,'Receipt metadata remains bounded')
                query=urllib.parse.urlencode({'id':receipt['id'],'key':receipt['key']})
                check(send(server,'/api/resume/commit?'+query,'POST',{},token)[0]==201,'New upload publishes after receipt compaction')
            finally:
                server.close()
    print('durable integration passed: both modes, crash/restart, idempotency, quota, recycle, bounded scripts, frozen preflight')


if __name__ == '__main__':
    run(pathlib.Path(sys.argv[1]).resolve())
