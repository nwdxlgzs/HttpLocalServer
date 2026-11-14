"""Durable name moderation, IP quota and login lockout across process restart."""
import pathlib
import tempfile
import sys
from integration import Server,check
from durable_integration import send


def run(executable):
    for mode in ('filetrans','server'):
        with tempfile.TemporaryDirectory(prefix='hls-state-') as folder:
            root=pathlib.Path(folder)
            flags=('--no-magika','--strict-host','--write-mode','isolated','--password','fixture-pass','--ip-upload-mb','1')
            server=Server(executable,root,mode,flags)
            def login(ip='127.0.0.1',password='fixture-pass'):
                code,data,headers=send(server,'/api/access/login','POST',{'password':password},source=ip)
                return code,data,headers.get('Set-Cookie','').split(';')[0]
            def signed(route,method='GET',body=None,ip='127.0.0.1',cookie='',token=''):
                return send(server,route,method,body,token,{'Cookie':cookie},source=ip)
            try:
                code,_,host_cookie=login();check(code==200,'Host login')
                token=signed('/api/session',cookie=host_cookie)[1]['token']
                code,_,remote_cookie=login('127.0.0.2');check(code==200,'Remote login')
                remote='127.0.0.2'
                check(signed('/api/upload?path=/_clients/127.0.0.2/a.txt','PUT',b'abc',remote,remote_cookie,token)[0]==201,'Remote isolated upload')
                check(signed('/api/alias','POST',{'name':'Fixture Alice'},remote,remote_cookie,token)[0]==200,'Submit name')
                pending=signed('/api/admin/aliases',cookie=host_cookie)[1]['entries'][0]
                check(signed('/api/admin/aliases','POST',{'path':pending['path'],'revision':pending['revision'],'action':'approve'},cookie=host_cookie,token=token)[0]==200,'Host approves')
                for i in range(5):
                    code,_,_=login('127.0.0.3','wrong')
                    check(code==(429 if i==4 else 401),'Exactly five login attempts')
                server.close();server=Server(executable,root,mode,flags)
                code,blocked,_=login('127.0.0.3')
                check(code==429 and 90<=blocked['retryAfter']<=120,'Ban deadline survives restart')
                code,_,host_cookie=login();check(code==200,'Host logs in after restart')
                token=signed('/api/session',cookie=host_cookie)[1]['token']
                entry=signed('/api/list?path=/_clients',cookie=host_cookie)[1]['entries'][0]
                check(entry.get('ownerName')=='Fixture Alice','Approved alias survives restart')
                quotas=signed('/api/admin/state',cookie=host_cookie)[1]['quotas']
                check(next(q for q in quotas if q['ip']==remote)['used']==3,'Consumed quota survives restart')
                check(signed('/api/admin/access','POST',{'ip':'127.0.0.3'},cookie=host_cookie,token=token)[0]==200,'Host unblocks durable ban')
                check(login('127.0.0.3')[0]==200,'Unblocked address can log in')
            finally:
                server.close()
    print('state tests passed: both modes, isolated ownership, approved aliases, durable quota and idempotent restart lockout/unblock')


if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
