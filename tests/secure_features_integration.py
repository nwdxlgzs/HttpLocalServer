"""New control/transfer features use the same verified mbedTLS transport."""
import json
import pathlib
import tempfile
import sys
import time
import urllib.parse
from integration import check
from tls_integration import SecureServer


def run(executable):
    for mode in ('filetrans','server'):
        with tempfile.TemporaryDirectory(prefix='hls-secure-features-') as folder:
            root=pathlib.Path(folder);script=root/'hooks.js'
            script.write_text("function onRequest(r){if(r.url==='/secure-hook')return {body:r.secure?'tls':'plain'};return {};}",encoding='utf-8')
            server=SecureServer(executable,root,('--tls-self-signed','--write-mode','shared','--recycle-bin','--script',str(script),'--p2p','--no-compression'),mode)
            def call(route,method='GET',data=None):
                raw=json.dumps(data).encode() if isinstance(data,dict) else data
                code,headers,body=server.request(route,method,headers={'X-HLS-Token':token},body=raw)
                try:body=json.loads(body)
                except (ValueError,UnicodeDecodeError):pass
                return code,body,headers
            try:
                token=server.api('/api/session')['token']
                check(call('/secure-hook')[1]==b'tls','Script sees secure request context')
                check(call('/_hls/')[0]==200,'Secure manager in both modes')
                code,upload,_=call('/api/resume/create','POST',{'path':'/secure.bin','size':700000,'fingerprint':'secure-fixture'})
                check(code==201,'Secure resumable admission')
                query=urllib.parse.urlencode({'id':upload['id'],'key':upload['key']});payload=b'fixture'*100000
                check(call('/api/resume/chunk?'+query+'&offset=0','POST',payload)[0]==200,'Secure durable chunk')
                check(call('/api/resume/commit?'+query,'POST',{})[0]==201,'Secure COW publication')
                check(call('/download?path=/secure.bin')[1]==payload,'Secure delivered bytes')
                code,peer,_=call('/api/p2p/relay-join','POST',{})
                check(code==200,'P2P enabled on secure Server and FileTrans')
                check(call('/api/p2p/relay-leave','POST',peer)[0]==200,'Secure peer teardown')
                check(call('/api/admin/settings','POST',{'downloadKiB':256})[0]==200,'Secure live rate policy')
                started=time.monotonic();check(call('/download?path=/secure.bin')[1]==payload,'Throttled HTTPS integrity')
                check(2.3<=time.monotonic()-started<8,'HTTPS asynchronous pacing')
                check(call('/api/entry?path=/secure.bin&recursive=1','DELETE')[0]==200,'Secure recycle delete')
                trash=call('/api/recycle')[1]['entries'][0]
                check(call('/api/recycle/restore?id='+trash['id'],'POST',{})[0]==200,'Secure restore')
            finally:server.close()
    print('secure features passed: both modes, QuickJS context, resumable uploads, P2P, live HTTPS pacing and recycle/restore')


if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
