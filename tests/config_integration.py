"""Configuration precedence, persistence and frozen filesystem namespace tests."""
import concurrent.futures
import http.client
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import urllib.parse
from integration import Server, check


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-config-') as temporary:
        base=pathlib.Path(temporary);root=base/'share';root.mkdir()
        private=root/'private';private.mkdir();config=private/'settings.json'
        config.write_text(json.dumps({'version':1,'options':{'root':'..','write-mode':'readonly','preview':False,'p2p':True,'ip-limit':17},'host':{'title':'From JSON','announcement':'Persisted'}}),encoding='utf-8')
        aliases=root/'aliases';aliases.mkdir();alias=aliases/'unrelated.dat';os.link(config,alias)
        (root/'nested').mkdir();(root/'nested/settings.json').write_text('also frozen',encoding='utf-8')
        (aliases/'ordinary.txt').write_text('ordinary',encoding='utf-8')
        (root/'public.txt').write_text('public',encoding='utf-8')
        extra=('--config',str(config),'--no-magika','--write-mode','shared','--allow-preview','--mcp','--mount-rw','/Other='+str(private))
        server=Server(exe,root,extra=extra,config=True)
        def request(target,method='GET',data=None,body=None,custom=None):
            connection=http.client.HTTPConnection('127.0.0.1',server.port,timeout=20)
            headers={}
            if method not in ('GET','HEAD'):
                headers={'X-HLS-Token':token,'Origin':'http://127.0.0.1:'+str(server.port),'Content-Type':'application/json'}
            if custom:headers.update(custom)
            try:
                connection.request(method,target,body=json.dumps(data) if data is not None else body,headers=headers)
                r=connection.getresponse();return r.status,r.read()
            finally:connection.close()
        token=server.api('/api/session')['token']
        try:
            initial=server.api('/api/session');check(initial['writeMode']=='shared' and initial['previewAllowed'] and initial['p2pEnabled'],'CLI booleans and enum override JSON, unspecified P2P retained')
            state=server.api('/api/admin/settings');check(state['ipLimit']==17 and state['title']=='From JSON' and state['persistence']=='config','JSON settings loaded and persistence announced')
            frozen_targets=[('/private','settings.json'),('/Other','settings.json'),('/aliases','unrelated.dat')]
            ordinary_target=server.api('/api/list?'+urllib.parse.urlencode({'path':'/nested'}))['entries']
            ordinary=next(e for e in ordinary_target if e['name']=='settings.json')
            check(not ordinary.get('frozen') and not ordinary.get('hidden') and ordinary['size']>0,'Same-name ordinary file is not frozen')
            for directory,name in frozen_targets:
                visible=server.api('/api/list?'+urllib.parse.urlencode({'path':directory}))
                check(not any(e['name']==name for e in visible['entries']),'Frozen dependencies are hidden by default '+directory)
                listing=server.api('/api/list?'+urllib.parse.urlencode({'path':directory,'showHidden':1}))
                entry=next(e for e in listing['entries'] if e['name']==name)
                check(entry['frozen'] and entry['hidden'] and entry['size']==0 and entry['modified']==0 and not entry['writable'],'Dependency metadata and hardlink identity '+directory)
            for name in ['/private/settings.json','/Other/settings.json','/aliases/unrelated.dat']:
                encoded=urllib.parse.quote(name,safe='')
                for route in ['/download?path=','/api/download-ticket?path=','/api/type?path=','/api/preview-text?path=','/api/upload-target?path=']:
                    check(request(route+encoded)[0]==403,'Frozen read '+route+name)
                check(request('/view'+name)[0]==403,'Frozen preview resource')
                check(request('/api/entry?path='+encoded+'&recursive=1','DELETE')[0]==403,'Frozen delete')
                check(request('/api/rename?path='+encoded+'&to=/replacement.txt','POST')[0]==403,'Frozen rename')
                check(request('/api/rename?path=/public.txt&to='+encoded,'POST')[0]==403,'Frozen overwrite by move')
                check(request('/api/copy?path='+encoded+'&to=/copy.txt','POST')[0]==403,'Frozen copy source')
                check(request('/api/copy?path=/public.txt&to='+encoded,'POST')[0]==403,'Frozen copy destination')
                check(request('/api/upload?path='+encoded,'PUT',body=b'overwrite')[0]==403,'Frozen upload')
                check(request('/api/p2p/join','POST',{'path':name,'version':'fake'})[0]==403,'Frozen P2P join')
            for path in ['/private','/aliases','/Other']:
                encoded=urllib.parse.quote(path,safe='')
                reply=request('/api/entry?path='+encoded+'&recursive=1','DELETE');check(reply[0]==403,'Ancestor delete '+path+': '+repr(reply))
                check(request('/api/rename?path='+encoded+'&to=/renamed','POST')[0]==403,'Ancestor rename preserves configuration and aliases')
            for target in ['/download?path=/private/../settings.json','/view/private/%2e%2e/settings.json','/download?path=%2Fprivate%5Csettings.json','/download?path=%2Fprivate%2Fsettings.json%3Ax']:
                check(request(target)[0] in (400,403),'Traversal or ADS denied')
            frozen_search=server.api('/api/search?path=/&q=settings')
            check(frozen_search['total']==1 and all(e['name']=='settings.json' and not e.get('frozen') for e in frozen_search['entries']),'Default search keeps ordinary same-name file')
            import time
            deadline=time.monotonic()+8
            while True:
                frozen_search=server.api('/api/search?path=/&q=settings&showHidden=1')
                if frozen_search['total']>=4:break
                check(time.monotonic()<deadline,'Index publishes frozen config entries');time.sleep(.1)
            check(sum(1 for e in frozen_search['entries'] if e.get('frozen'))>=3 and any(not e.get('frozen') for e in frozen_search['entries']),'Show hidden search separates dependencies from ordinary same-name files')
            check(request('/api/admin/settings','POST',{'title':'Saved title','announcement':'Saved announcement','ipLimit':33,'previewAllowed':False,'p2pCacheMiB':8})[0]==200,'Host settings atomically persisted')
            check(request('/api/admin/attention','POST',{'path':'/public.txt','marked':True})[0]==200,'Attention files persisted')
            saved=json.loads(config.read_text(encoding='utf-8'))
            check(saved['options']['ip-limit']==33 and saved['host']['title']=='Saved title' and saved['host']['attentionPaths']==['/public.txt'],'Saved config contains runtime settings and attention')
            check(saved['protectedFileIds'],'Hardlinked previous generation identity retained')
            busy=subprocess.run([str(exe),'--config',str(config),'--root',str(root),'--port','19996'],capture_output=True,timeout=20)
            check(busy.returncode!=0,'Two processes cannot write the same config concurrently')
            rpc={'jsonrpc':'2.0','id':1,'method':'tools/call','params':{'name':'read_text','arguments':{'path':'/private/settings.json'}}}
            code,reply=request('/mcp','POST',rpc,custom={'Authorization':'Bearer '+state['mcpToken'],'MCP-Protocol-Version':'2025-11-25'})
            check(code==200 and json.loads(reply)['result']['isError'],'MCP cannot bypass frozen reads')
            if os.name=='nt':
                import ctypes
                kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p];kernel.CreateFileW.restype=ctypes.c_void_p
                kernel.CloseHandle.argtypes=[ctypes.c_void_p]
                handle=kernel.CreateFileW(str(config),0x80,3,None,3,0x80,None)
                check(handle not in (None,ctypes.c_void_p(-1).value),'Create no-delete config guard')
                before=config.read_bytes()
                try:
                    check(request('/api/admin/settings','POST',{'title':'must not publish'})[0]==500,'Failed atomic commit is reported')
                    check(server.api('/api/admin/settings')['title']=='Saved title' and config.read_bytes()==before,'Failed save retains previous live and disk settings')
                finally:kernel.CloseHandle(handle)

            check(request('/download?path=/aliases/unrelated.dat')[0]==403,'Retired hardlink remains frozen after atomic replacement')
            check((aliases/'ordinary.txt').exists() and alias.exists() and config.exists(),'Rejected ancestor operations have no partial effects')
            # Concurrent reads must never catch the JSON between guard and commit.
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                checks=[pool.submit(request,'/download?path=/aliases/unrelated.dat') for _ in range(32)]
                check(all(f.result()[0]==403 for f in checks),'Concurrent protected requests remain denied')
        finally:server.close()
        server=Server(exe,root,extra=('--config',str(config),'--no-magika','--allow-preview'),config=True)
        token=server.api('/api/session')['token']
        try:
            state=server.api('/api/admin/settings')
            check(state['title']=='Saved title' and state['ipLimit']==33 and state['previewAllowed'] and state['p2pCacheMiB']==8,'Restart restores saved settings; explicit CLI still wins')
            check(state['attentionPaths']==['/public.txt'],'Attention survives restart')
            check(request('/download?path=/aliases/unrelated.dat')[0]==403,'Retired hardlink remains frozen after restart')
        finally:server.close()
        # Invalid files fail closed and are never silently regenerated.
        for document in ['{broken',json.dumps({'version':1,'options':{'strict-host':False}}),json.dumps({'version':2}),json.dumps({'version':1,'options':{'root':'bad\x00path'}})]:
            invalid=base/'invalid.json';invalid.write_text(document,encoding='utf-8')
            result=subprocess.run([str(exe),'--config',str(invalid),'--root',str(root),'--port','19997'],capture_output=True,timeout=20)
            check(result.returncode!=0 and invalid.read_text(encoding='utf-8')==document,'Invalid config rejected without modification')
        web=root/'website';web.mkdir();(web/'index.html').write_text('Server mode',encoding='utf-8')
        webconfig=base/'server-options.json';webconfig.write_text('{"version":1,"options":{}}',encoding='utf-8');os.link(webconfig,web/'secret-alias.dat')
        server=Server(exe,web,mode='server',extra=('--config',str(webconfig),'--no-magika'),config=True)
        try:check(server.request('/secret-alias.dat')[0]==403,'Server mode protects physical config identity through an alias')
        finally:server.close()
        fresh=base/'new config.json';server=Server(exe,root,extra=('--config',str(fresh),'--no-magika'),config=True)
        try:check(fresh.exists() and json.loads(fresh.read_text(encoding='utf-8'))['options']['port']==server.port,'Missing selected config created from effective startup values')
        finally:server.close()
    print('Configuration tests passed: JSON/CLI precedence, atomic persistence/restart, hardlink history, native mounts, frozen listing/search, reads/writes/copy/P2P/ancestors/traversal and malformed JSON')


if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
