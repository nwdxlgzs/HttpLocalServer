"""Inference, private state, lazy config, workspace modules and compression."""
import concurrent.futures
import gzip
import http.client
import json
import pathlib
import subprocess
import sys
import tempfile
import time
import zipfile
import zlib
from integration import Server, check


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-v330-') as directory:
        base=pathlib.Path(directory);root=base/'root';root.mkdir()
        (root/'large.js').write_text('const example = "省流压缩";\n'*300,encoding='utf-8')
        (root/'photo').write_bytes(b'\xff\xd8\xff\xe0'+b'\0'*100)
        with zipfile.ZipFile(root/'archive.bbb','w') as z:z.writestr('index.txt','test')
        state=root/'.hls-state-existing';state.mkdir();(state/'secret.txt').write_text('private')
        (root/'.hls-state-file').write_text('private')
        server=Server(exe,root,extra=('--no-magika','--write-mode','shared','--zip-preview','host','--state-dir',str(state)))
        def request(target,method='GET',data=None):
            c=http.client.HTTPConnection('127.0.0.1',server.port,timeout=20)
            try:
                body=None if data is None else json.dumps(data).encode()
                c.request(method,target,body,dict({'Content-Type':'application/json','X-HLS-App':'1'},**({'X-HLS-Token':server.api('/api/session')['token']} if target.startswith('/api/') else {})))
                r=c.getresponse();return r.status,r.read()
            finally:c.close()
        try:
            check(server.api('/api/type?path=/photo')['category']=='image','Stripped image inference')
            check(server.request('/view/photo')[1]['content-type']=='image/jpeg','Stripped image MIME')
            check(server.api('/api/type?path=/archive.bbb')['extension']=='zip','Misnamed archive inference')
            status,body=request('/api/zip/open','POST',{'path':'/archive.bbb'});check(status==200,'Misnamed ZIP feature access '+repr(body))
            entries=server.api('/api/list?showHidden=1')['entries']
            for name in ['.hls-state-existing']:
                e=next(e for e in entries if e['name']==name);check(e.get('frozen') and e['size']==0 and not e['writable'],'Private state visibly frozen')
            check(not next(e for e in entries if e['name']=='.hls-state-file').get('frozen') and server.request('/download?path=/.hls-state-file')[0]==200,'Ordinary same-prefix file remains accessible')
            for path in ['/.hls-state-existing/secret.txt']:
                for route in ['/download?path=','/api/type?path=','/api/preview-text?path=']:
                    check(server.request(route+path)[0] in (400,403),'Private state read refused')
            for coding,decode in [('gzip',gzip.decompress),('deflate',zlib.decompress),('gzip;q=0.3, deflate;q=0.8',zlib.decompress)]:
                status,h,b=server.request('/view/large.js',headers={'Accept-Encoding':coding})
                check(status==200 and h.get('content-encoding')==('gzip' if coding=='gzip' else 'deflate'),'Negotiated coding '+coding)
                check(decode(b)==(root/'large.js').read_bytes(),'Lossless compression')
            status,h,b=server.request('/view/large.js',headers={'Accept-Encoding':'deflate','Range':'bytes=0-30'})
            check(status==206 and 'content-encoding' not in h and b==(root/'large.js').read_bytes()[:31],'Ranges stay byte-addressable')
            check(server.request('/api/preview-text?path=/large.js')[0]==200,'Code is textual preview')
        finally:server.close()
        # Default CWD config remains absent until a host changes settings.
        old=pathlib.Path.cwd()
        try:
            import os
            os.chdir(root)
            server=Server(exe,root,config=True,extra=('--no-magika',))
            try:
                check(not (root/'config.json').exists() and not (root/'.hls-config-lock-config.json').exists(),'Default config and lock are lazy')
                (root/'config.json').write_text('untrusted uploaded placeholder')
                c=http.client.HTTPConnection('127.0.0.1',server.port);c.request('POST','/api/admin/settings',json.dumps({'title':'Saved lazily'}),{'X-HLS-Token':server.api('/api/session')['token'],'Content-Type':'application/json'});r=c.getresponse();check(r.status==200,r.read());c.close()
                check(json.loads((root/'config.json').read_text(encoding='utf-8'))['host']['title']=='Saved lazily','Host save replaces placeholder atomically')
                entry=next(e for e in server.api('/api/list?showHidden=1&refresh=1')['entries'] if e['name']=='.hls-config-lock-config.json')
                check(entry.get('frozen') and entry['size']==0 and entry['modified']==0 and not entry['writable'],'Lazily created real config lock visibly frozen')
                check(server.request('/download?path=/config.json')[0]==403,'Saved default config frozen')
            finally:server.close()
        finally:os.chdir(old)
        workspace=base/'app.zip'
        with zipfile.ZipFile(workspace,'w',zipfile.ZIP_DEFLATED) as z:
            z.writestr('server.qjs',"import { reply } from './lib/reply.js'; import {fs} from 'hls'; let count=0; export function onStart(){fs.mkdir('/data');fs.writeText('/data/start.txt','started');} export function onRequest(r){if(r.url==='/count')return {body:String(++count)};if(r.url==='/save'&&r.method==='POST'){fs.writeText('/data/result.txt',r.body);return reply(fs.readText('/data/result.txt'));}if(r.url==='/escape'){try{fs.writeText('/../escape.txt','bad')}catch(e){return {status:403,body:'refused'}}}if(r.url==='/config')return {body:fs.readText('/config.json')};return {};}")
            z.writestr('lib/reply.js',"export function reply(body){return {body:body,contentType:'text/plain; charset=utf-8'}}")
            z.writestr('index.html','<h1>Workspace application</h1>')
            z.writestr('assets/文件 name.css','body { color: red; }')
        server=Server(exe,root,'server',('--no-magika','--workspace-app',str(workspace),'--workspace-public','/','--script-fs','readwrite','--script-timeout-ms','500'))
        try:
            check((root/'data/start.txt').read_text()=='started','Workspace startup writes real filesystem')
            check(server.request('/count')[2]==b'1' and server.request('/count')[2]==b'2','Persistent exported module hook state')
            status,body=request('/save','POST',{'hello':'world'});check(status==200 and json.loads((root/'data/result.txt').read_text())=={'hello':'world'},'Imported backend handles and writes request')
            check(b'Workspace application' in server.request('/')[2],'ZIP static frontend')
            check(server.request('/assets/%E6%96%87%E4%BB%B6%20name.css')[2]==b'body { color: red; }','Workspace static URLs decode Unicode and spaces')
            check(server.request('/lib/reply.js')[0]==403 and server.request('/server.qjs')[0]==403,'Imported backend source private')
            check(server.request('/escape')[0]==403 and server.request('/config')[0]==200,'Workspace confinement and ordinary unused config remains readable')
        finally:server.close()
    print('v3.3 feature integration passed: inference, frozen state, compression/range, lazy CWD config, persistent ZIP workspace imports and real filesystem confinement')


if __name__=='__main__':run(str(pathlib.Path(sys.argv[1]).resolve()))
