"""Dependency-aware frozen visibility and identity checks."""
import os,pathlib,sys,tempfile,time,urllib.parse
from integration import Server,check

def run(executable):
    with tempfile.TemporaryDirectory(prefix="hls-frozen-deps-") as directory:
        base=pathlib.Path(directory);root=base/"share";root.mkdir();private=root/"private";private.mkdir()
        config=private/"settings.json";config.write_text('{"version":1,"options":{}}',encoding="utf-8")
        aliases=root/"aliases";aliases.mkdir();os.link(config,aliases/"config-alias.dat")
        (root/"nested").mkdir();(root/"nested/settings.json").write_text("ordinary",encoding="utf-8")
        (root/"config.json").write_text("ordinary root",encoding="utf-8")
        (root/".hls-config-lock-unrelated.json").write_text("ordinary lock-looking name",encoding="utf-8")
        flags=("--config",str(config),"--no-magika","--write-mode","shared","--mount-rw","/Mapped="+str(private))
        server=Server(executable,root,extra=flags,config=True)
        try:
            deadline=time.monotonic()+3
            while not (private/".hls-config-lock-settings.json").exists():
                check(time.monotonic()<deadline,"Configuration lock was created");time.sleep(.02)
            def listing(path,hidden=False):
                return server.api('/api/list?'+urllib.parse.urlencode({'path':path,'showHidden':1 if hidden else 0}))['entries']
            ordinary=next(e for e in listing('/nested') if e['name']=='settings.json')
            check(not ordinary.get('frozen') and not ordinary.get('hidden') and ordinary['size']>0,'Same-name ordinary file stays shareable')
            root_ordinary=next(e for e in listing('/') if e['name']=='config.json')
            check(not root_ordinary.get('frozen'),'Unrelated config basename is not frozen')
            lock_looking=next(e for e in listing('/',True) if e['name']=='.hls-config-lock-unrelated.json')
            check(not lock_looking.get('frozen'),'Unrelated lock-looking name is not frozen')
            frozen_paths=['/private/settings.json','/private/.hls-config-lock-settings.json','/aliases/config-alias.dat','/Mapped/settings.json']
            for path in frozen_paths:
                shown=next(e for e in listing(path.rsplit('/',1)[0] or '/',True) if e['path']==path)
                check(shown.get('frozen') and shown.get('hidden') and not shown.get('writable') and shown.get('size')==0 and shown.get('modified')==0,'Dependency metadata: '+path)
                check(not any(e['path']==path for e in listing(path.rsplit('/',1)[0] or '/')),'Dependency hidden by default: '+path)
                encoded=urllib.parse.quote(path,safe='')
                check(server.request('/download?path='+encoded)[0] in (400,403),'Frozen download denied: '+path)
            search=server.api('/api/search?path=/&q=settings')
            check(any(e['path']=='/nested/settings.json' and not e.get('frozen') for e in search['entries']),'Search keeps ordinary same-name file')
            revealed=server.api('/api/search?path=/&q=config-alias&showHidden=1')
            check(revealed['total']==1 and revealed['entries'][0].get('frozen'),'Search reveals dependency hardlink only with showHidden')
        finally:server.close()
        server=Server(executable,root,mode='server',extra=("--no-magika",))
        try:
            check(server.request('/download?path=/config.json')[0]==200,'Server serves unrelated config name')
        finally:server.close()
    print('Dependency-aware frozen tests passed: actual config/lock identities, state/mount aliases, ordinary same-name files, hidden default, show-hidden search, FileTrans and Server')

if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
