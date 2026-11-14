"""Every packed preview asset must survive the actual native HTTP path."""
import concurrent.futures
import gzip
import hashlib
import http.client
import json
import pathlib
import sys
import tempfile
import threading
from integration import Server,check

ROOT=pathlib.Path(__file__).resolve().parents[1]


def run(executable):
    manifest=json.loads((ROOT/'resources/previews-manifest.json').read_text(encoding='utf-8'))
    packed=(ROOT/'resources/previews.bin').read_bytes()
    for mode in ('filetrans','server'):
        with tempfile.TemporaryDirectory(prefix='hls-packed-previews-') as directory:
            server=Server(executable,pathlib.Path(directory),mode,('--no-magika',))
            clients=[];client_lock=threading.Lock();local=threading.local()
            try:
                def request(url,headers=None):
                    if not hasattr(local,'client'):
                        local.client=http.client.HTTPConnection('127.0.0.1',server.port,timeout=20)
                        with client_lock:clients.append(local.client)
                    local.client.request('GET',url,headers=headers or {})
                    response=local.client.getresponse()
                    return response.status,{key.lower():value for key,value in response.getheaders()},response.read()
                def verify(asset):
                    url='/_hls/preview-assets/'+asset['path']
                    status,headers,body=request(url,headers={'Accept-Encoding':'gzip'})
                    expected=packed[asset['offset']:asset['offset']+asset['bytes']]
                    check(status==200 and body==expected,'Exact compressed asset '+asset['path'])
                    check(headers.get('content-encoding')=='gzip','Negotiated preview gzip')
                    raw=gzip.decompress(body)
                    check(hashlib.sha256(raw).hexdigest()==asset['sha256'],'Asset digest '+asset['path'])
                    status,plain_headers,plain=request(url)
                    check(status==200 and plain==raw and plain_headers['content-type']==asset['mime'],'Exact identity asset '+asset['path'])
                    status,_,_=request(url,headers={'If-None-Match':headers['etag']})
                    check(status==304,'Preview validator '+asset['path'])
                with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                    list(pool.map(verify,manifest['assets']))
                status,headers,body=server.request('/_hls/viewer')
                check(status==200 and b'id="stage"' in body,'Viewer document in '+mode)
                for shell,frame,ancestors in [('/_hls/','DENY',"frame-ancestors 'none'"),('/preview?path=/example.png','SAMEORIGIN',"frame-ancestors 'self'")]:
                    status,headers,_=server.request(shell)
                    check(status==200 and headers['x-frame-options']==frame and ancestors in headers['content-security-policy'],'Only preview shell permits same-origin embedding')
                check(server.api('/api/session')['zipPreview']=='off','ZIP defaults disabled')
                status,headers,_=server.request('/_hls/preview-assets/zip-sandbox.html',headers={'Host':'localhost:'+str(server.port)})
                check(status==200 and 'localhost:'+str(server.port) in headers['content-security-policy'],'Sandbox CSP uses allowed localhost authority')
            finally:
                for client in clients:client.close()
                server.close()
    print('Preview HTTP assets passed:',len(manifest['assets']),'assets in both modes, gzip/identity/ETag, default-off and localhost CSP')


if __name__=='__main__':run(str(pathlib.Path(sys.argv[1]).resolve()))
