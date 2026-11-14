"""Streaming writes, resource limits and legacy preview acceptance."""
import concurrent.futures
import hashlib
import http.client
import json
import pathlib
import socket
import sys
import tempfile
import time
import urllib.parse
from integration import Server, check, url

def route(name, **values):
    return '/api/' + name + '?' + urllib.parse.urlencode(values)

def send(server, method, target, body=None, headers=None, source=None):
    conn=http.client.HTTPConnection('127.0.0.1',server.port,timeout=30,source_address=source)
    try:
        conn.request(method,target,body,headers or {})
        r=conn.getresponse()
        return r.status,dict(r.getheaders()),r.read()
    finally:
        conn.close()

def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-v2-') as tmp:
        root=pathlib.Path(tmp)
        (root/'site').mkdir()
        (root/'site/index.html').write_text('<!doctype html><html><body>preview<script>document.body.dataset.script="ran"</script></body></html>')
        for n in range(207): (root/f'file-{n:03}.txt').write_text(str(n))
        srv=Server(exe,root,extra=('--write-mode','shared','--allow-preview','--max-upload-mb','64','--thread-idle','1'))
        try:
            session=srv.api('/api/session')
            check(session['clientIP']=='127.0.0.1' and session['localAdmin'],'Actual TCP client identity')
            headers={'X-HLS-Token':session['token']}
            check(len(srv.api('/api/list')['entries'])==100,'Default page is exactly 100 entries')
            check(len(srv.api('/api/list?offset=200')['entries'])==8,'Last lazy page')
            check(send(srv,'POST',route('mkdir',path='/created'),headers=headers)[0]==201,'HTTP mkdir')
            # Iterable body makes http.client use chunked encoding; contents exceed socket and body buffers.
            chunk=bytes(range(256))*1024
            expected=hashlib.sha256(chunk*32).hexdigest()
            status,_,body=send(srv,'PUT',route('upload',path='/created/streamed'),(chunk for _ in range(32)),headers)
            check(status==201,f'Chunked streaming upload: {status}: {body!r}')
            check(hashlib.sha256((root/'created/streamed').read_bytes()).hexdigest()==expected,'Upload integrity')
            check(send(srv,'PUT',route('upload',path='/created/streamed'),b'overwrite',headers)[0]==409,'No overwrite')
            png=b'\x89PNG\r\n\x1a\n'+b'\x00'*100
            status,_,body=send(srv,'PUT',route('upload',path='/created/photo.bad'),png,headers)
            info=json.loads(body)
            check(status==201 and info['type']['mismatch'] and info['type']['suggestedName']=='photo.png','Upload type mismatch')
            check(send(srv,'POST',route('rename',path='/created/photo.bad',to='/created/photo.png'),headers=headers)[0]==200,'Rename')
            check(any(e['name']=='photo.png' for e in srv.api(route('list',path='/created'))['entries']),'Listing invalidation after rename')
            check(send(srv,'DELETE',route('entry',path='/created/photo.png'),headers=headers)[0]==200,'Delete')
            check(send(srv,'POST',route('mkdir',path='/csrf'))[0]==403,'Token required')
            check(send(srv,'POST',route('mkdir',path='/origin'),headers=dict(headers,Origin='null'))[0]==403,'Opaque preview origin denied')
            check(send(srv,'POST',route('mkdir',path='/origin'),headers=dict(headers,Origin='http://evil.test'))[0]==403,'Origin denied')
            check(send(srv,'GET','/api/session',headers={'Host':'evil.test'})[0]==403,'Rebinding host denied')
            for bad in ['/../escape','/created/../../escape','/con.txt','/NUL','/.hls-upload-secret','/foo:bar','/'+'a'*256]:
                check(send(srv,'PUT',route('upload',path=bad),b'x',headers)[0] in (400,403),'Mutation traversal: '+bad)
            # A rejected upload cannot extend its close deadline by drip-feeding the body.
            drip=socket.create_connection(('127.0.0.1',srv.port));drip.settimeout(3)
            drip.sendall(('PUT '+route('upload',path='/../denied')+' HTTP/1.1\r\nHost: 127.0.0.1:'+str(srv.port)+'\r\nX-HLS-Token: '+session['token']+'\r\nContent-Length: 999999\r\n\r\n').encode())
            rejected_response=http.client.HTTPResponse(drip);rejected_response.begin();check(rejected_response.status==400,'Rejected upload error delivered');rejected_response.read();rejected_response.close()
            for _ in range(7):
                try:drip.sendall(b'x')
                except OSError:break
                time.sleep(.2)
            try:ended=drip.recv(8192)==b''
            except OSError:ended=True
            drip.close();check(ended,'Rejected upload has an absolute drain deadline')
            # Abort a declared large request after some chunks; no destination or hidden temp may remain.
            sock=socket.create_connection(('127.0.0.1',srv.port))
            sock.sendall(('PUT '+route('upload',path='/created/abort')+' HTTP/1.1\r\nHost: 127.0.0.1:'+str(srv.port)+'\r\nX-HLS-Token: '+session['token']+'\r\nContent-Length: 10000000\r\n\r\n').encode()+b'A'*8192)
            time.sleep(.1);sock.close()
            for _ in range(80):
                if not list((root/'created').glob('.hls-*')):break
                time.sleep(.05)
            check(not (root/'created/abort').exists() and not list((root/'created').glob('.hls-*')),'Disconnect cleanup')
            sock=socket.create_connection(('127.0.0.1',srv.port))
            sock.sendall(('PUT '+route('upload',path='/created/expect')+' HTTP/1.1\r\nHost: 127.0.0.1:'+str(srv.port)+'\r\nX-HLS-Token: '+session['token']+'\r\nContent-Length: 3\r\nExpect: 100-continue\r\n\r\n').encode())
            check(b'100 Continue' in sock.recv(4096),'100-continue authorization before body')
            sock.sendall(b'abc');check(b'201 Created' in sock.recv(4096),'100-continue commit');sock.close()
            check(send(srv,'PUT',route('upload',path='/created/too-big'),headers=dict(headers,**{'Content-Length':str(65*1024**2)}))[0]==413,'Upload limit checked at headers')
            def small(n):
                data=(str(n)+'#').encode()*4096
                status,_,body=send(srv,'PUT',route('upload',path=f'/created/concurrent-{n}'),data,headers)
                check(status==201,f'Concurrent upload {n}: {status} {body}')
                check(srv.request(url('/created/concurrent-'+str(n)))[2]==data,'Concurrent read/write integrity')
            with concurrent.futures.ThreadPoolExecutor(max_workers=24) as pool:list(pool.map(small,range(64)))
            status,h,body=srv.request('/view/site/')
            check(status==200 and b'preview' in body and h['content-security-policy'].startswith('sandbox '),'Direct website preview sandbox')
            status,h,body=srv.request('/view/site/',headers={'User-Agent':'Mozilla/5.0 Trident/7.0; rv:11.0'})
            check(status==200 and 'attachment' in h.get('content-disposition',''),'IE active document download degradation')
            print('v2 HTTP passed: 8 MiB chunked upload, 64 parallel files, abort, limits, writes, type, preview')
        finally:srv.close()
        ro=Server(exe,root,extra=('--no-preview',))
        try:
            token=ro.api('/api/session')['token']
            check(send(ro,'PUT',route('upload',path='/readonly'),b'x',{'X-HLS-Token':token})[0]==403,'Read-only stream rejection')
            check(ro.request('/view/site/')[0]==403,'Preview can be disabled')
        finally:ro.close()
        with (root/'large-stream.bin').open('wb') as output:output.truncate(128*1024**2)
        auto=Server(exe,root,fixed_io=False,extra=('--io-max','4','--file-max','4','--metadata-max','3','--thread-idle','1'))
        held=[]
        try:
            for _ in range(24):
                conn=http.client.HTTPConnection('127.0.0.1',auto.port,timeout=10)
                conn.request('GET',url('/large-stream.bin'));check(conn.getresponse().status==200,'Adaptive load begins');held.append(conn)
            for _ in range(50):
                counts=auto.api('/api/status')['threads']
                if counts['io']==4:break
                time.sleep(.1)
            check(counts['io']==4,'Network workers grow to configured cap')
            for conn in held:conn.close()
            held=[]
            for _ in range(80):
                counts=auto.api('/api/status')['threads'];check(counts['io']>=2,'Network minimum maintained')
                if counts=={'io':2,'file':2,'metadata':1}:break
                time.sleep(.1)
            check(counts=={'io':2,'file':2,'metadata':1},'Network/storage/metadata workers retire: '+str(counts))
            print('Adaptive network growth, cap and idle retirement passed')
        finally:
            for conn in held:conn.close()
            auto.close()
        cap=Server(exe,root,extra=('--ip-limit','1'))
        try:
            sock=socket.create_connection(('127.0.0.1',cap.port));time.sleep(.1)
            check(cap.request('/api/status')[0]==200,'Same IP does not consume extra distinct slot')
            rejected=False
            try:send(cap,'GET','/api/status',source=('127.0.0.2',0))
            except (OSError,http.client.HTTPException):rejected=True
            check(rejected,'Distinct IP cap enforced')
            sock.close();time.sleep(.3)
            check(send(cap,'GET','/api/status',source=('127.0.0.2',0))[0]==200,'Distinct slot released on disconnect')
            print('Distinct IP cap/release passed')
        finally:cap.close()
if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
