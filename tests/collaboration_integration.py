"""Folder streaming, browser leases, moderated names and directory polling over HTTP."""
import concurrent.futures
import json
import pathlib
import random
import sys
import tempfile
import time
import socket
from integration import Server, check
from v2_integration import route, send


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-collaboration-') as temporary:
        root=pathlib.Path(temporary)
        server=Server(exe,root,extra=('--no-magika','--write-mode','isolated','--strict-host'))
        try:
            peer=('127.0.0.2',0)
            def api(endpoint):
                status,_,body=send(server,'GET',endpoint,source=None if endpoint.startswith('/api/admin/') else peer)
                check(status==200,'API '+endpoint+': '+str(status));return json.loads(body)
            session=api('/api/session'); own=session['uploadRoot']
            headers={'X-HLS-Token':session['token']}
            def post(endpoint,data):
                status,_,body=send(server,'POST',endpoint,json.dumps(data).encode(),dict(headers,**{'Content-Type':'application/json'}),source=None if endpoint.startswith('/api/admin/') else peer)
                return status,json.loads(body)
            def browse(identifier,paths,current=None):
                return post('/api/browse',{'id':identifier,'paths':paths,'current':current or own,'showHidden':False,'owners':[own]})
            a='page-http-test-browser-a';b='page-http-test-browser-b'
            for _ in range(4):check(api('/api/session')['uploadRoot']==own,'Repeated sessions keep peer-derived owner')
            listing=api(route('list',path=own,browser=a))
            check(listing['total']==0 and listing['virtual'] and not (root/'_clients').exists(),'Virtual browse never creates a physical IP folder')
            check(post('/api/alias',{'name':'premature'})[0]==403,'Upload is required before a name can be submitted')
            target=own+'/Folder/nested/hello.txt'
            status,_,body=send(server,'PUT',route('upload',path=target,parents=1),b'first',headers,source=peer)
            check(status==201 and json.loads(body)['path']==target,'A real streamed file creates safe nested parents')
            check((root/target.lstrip('/')).read_bytes()==b'first','Nested uploaded bytes survive publication')
            check(post('/api/alias',{'name':'Test <person>'})[0]==200,'JSON name endpoint accepts a pending submission')
            check('Test <person>' not in json.dumps(api(route('list',path='/_clients',browser=a))),'Pending names are absent from public listing responses')
            check(browse(a,[own])[1]['names'][own]=='','An explicitly requested owner name remains blank before approval')
            queue=api('/api/admin/aliases')['entries'];check(len(queue)==1,'Only the host review endpoint returns the pending name')
            review=queue[0]
            check(post('/api/admin/aliases',dict(path=review['path'],revision='stale',action='approve'))[0]==409,'Host cannot approve a stale name revision')
            check(post('/api/admin/aliases',dict(path=review['path'],revision=review['revision'],action='approve'))[0]==200,'Host approval endpoint publishes the exact reviewed name')
            check(api(route('list',path='/_clients',browser=a))['entries'][0]['ownerName']=='Test <person>','Approved names appear separately from physical directory paths')
            check(browse(a,[own])[1]['names'][own]=='Test <person>','Approved capsule metadata is delivered without reloading the directory')
            check(browse(a,[own+'/Folder/nested'],own+'/Folder/nested')[0]==200,'Browser can lease an open descendant')
            check(browse(b,[own])[0]==200,'Another browser can independently lease the same owner')
            baseline=api(route('list',path=own+'/Folder/nested',browser=a))['stamp']
            (root/target.lstrip('/')).write_bytes(b'other')
            time.sleep(5.2)
            state=browse(a,[own+'/Folder/nested'],own+'/Folder/nested')[1]
            check(state['stamp']!=baseline,'Polling detects a same-size external content update')
            def visitor(_):
                for attempt in range(12):
                    status,reply=browse(a,[own+'/Folder/nested'],own+'/Folder/nested')
                    if status!=503:return status,reply
                    check(reply.get('error')=='Active request limit reached; retry later','Explicit control backpressure')
                    time.sleep(random.uniform(.02,.05)*min(attempt+1,5))
                return status,reply
            with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                replies=list(pool.map(visitor,range(48)))
            check(all(status==200 and reply['stamp']==state['stamp'] for status,reply in replies),'Concurrent visitors receive one consistent cached directory fingerprint: '+repr([(code,reply.get('error'),reply.get('stamp')) for code,reply in replies if code!=200 or reply.get('stamp')!=state['stamp']]))
            check(send(server,'DELETE',route('entry',path=target),headers=headers,source=peer)[0]==200,'Final file can be deleted by its uploader')
            time.sleep(1.5);physical=root/own.lstrip('/')
            check(physical.exists(),'An empty IP folder survives while any browser is inside it')
            browse(a,[]);time.sleep(1.5);check(physical.exists(),'One browser leaving does not release another browser lease')
            browse(b,[])
            for _ in range(80):
                if not physical.exists():break
                time.sleep(.1)
            check(not physical.exists(),'The final browser leaving permits safe empty-tree reclamation')
            check(api(route('list',path=own,browser=a))['virtual'],'The reclaimed IP folder remains navigable without recreating it')
            check(not physical.exists(),'Revisiting the virtual folder still has no filesystem side effect')
            for unsafe in (own+'/../escape.txt',own+'/folder/'+('/deep'*66)+'/file.txt'):
                check(send(server,'PUT',route('upload',path=unsafe,parents=1),b'x',headers,source=peer)[0]==400,'Recursive parent creation rejects traversal and excessive depth')
            # Simulate a browser disappearing without pagehide and a streamed
            # upload breaking midway. A lease must eventually expire itself.
            sock=socket.create_connection(('127.0.0.1',server.port),timeout=5,source_address=peer)
            target=route('upload',path=own+'/aborted/nested/broken.txt',parents=1)
            sock.sendall(('PUT '+target+' HTTP/1.1\r\nHost: 127.0.0.1:'+str(server.port)+'\r\nX-HLS-Token: '+session['token']+'\r\nContent-Length: 65536\r\n\r\n').encode()+b'x'*8192)
            for _ in range(100):
                if physical.exists():break
                time.sleep(.02)
            sock.close();check(physical.exists(),'Upload can create parents while a browser lease is still active')
            deadline=time.monotonic()+49
            while physical.exists() and time.monotonic()<deadline:time.sleep(.2)
            check(not physical.exists(),'An expired crashed-browser lease permits aborted-upload directory cleanup')
            print('Collaboration HTTP passed: lazy/virtual owners, streamed folders, name review, stale approvals, external changes, 48 concurrent polls and multi-browser empty-tree cleanup')
        finally:
            server.close()


if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
