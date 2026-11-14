"""Native mounts, provider materialization, resumable host delivery and IP quotas."""
import concurrent.futures
import json
import pathlib
import sys
import tempfile
import threading
import time
import socket
import http.client
from integration import Server,check,url
from v2_integration import route,send


def expect_upload(server,path,payload,headers):
    sock=socket.create_connection(('127.0.0.1',server.port),timeout=10)
    try:
        target=route('upload',path=path)
        raw='PUT '+target+' HTTP/1.1\r\nHost: 127.0.0.1:'+str(server.port)+'\r\nContent-Length: '+str(len(payload))+'\r\nExpect: 100-continue\r\nX-HLS-Token: '+headers['X-HLS-Token']+'\r\n\r\n'
        sock.sendall(raw.encode());first=b''
        while not first.endswith(b'\r\n\r\n'):first+=sock.recv(1)
        status=int(first.split(b' ')[1])
        if status!=100:return status
        sock.sendall(payload);response=http.client.HTTPResponse(sock);response.begin();response.read();return response.status
    finally:sock.close()


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-mounts-') as temporary:
        base=pathlib.Path(temporary);root=base/'share';outside=base/'partition';root.mkdir();outside.mkdir()
        (outside/'nested').mkdir();(outside/'nested/test.txt').write_bytes(b'native-mounted')
        server=Server(exe,root,extra=('--no-magika','--write-mode','shared','--mount','/Volumes/Data='+str(outside),'--mount-rw','/Write='+str(outside),'--mcp'))
        try:
            session=server.api('/api/session');headers={'X-HLS-Token':session['token']}
            def post(target,data):
                status,_,body=send(server,'POST',target,json.dumps(data).encode(),headers);return status,json.loads(body)
            check(server.api(route('list',path='/'))['entries'][0]['directory'],'Synthetic mount parent appears at root')
            check(server.api(route('list',path='/Volumes'))['entries'][0]['name']=='Data','Virtual parents contain host mount roots')
            check(server.request(url('/Volumes/Data/nested/test.txt'))[2]==b'native-mounted','Mounted partition uses secure ordinary downloads')
            check(server.api(route('search',path='/Volumes/Data',q='test'))['total']==1,'Scoped native mount search reads mounted content')
            check(send(server,'PUT',route('upload',path='/Volumes/Data/denied.txt'),b'no',headers)[0]==403,'Read-only native mount rejects upload even in shared mode')
            check(send(server,'PUT',route('upload',path='/Write/allowed.txt'),b'yes',headers)[0]==201 and (outside/'allowed.txt').read_bytes()==b'yes','Writable native mount stores into selected partition')
            check(send(server,'DELETE',route('entry',path='/Volumes',recursive=1),headers=headers)[0]==403,'Virtual mount ancestors cannot be deleted')
            check(send(server,'POST',route('rename',path='/Write/allowed.txt',to='/copy.txt'),headers=headers)[0]==409,'Cross-mount rename cannot silently copy or lose data')
            status,mount=post('/api/mount/register',dict(path='/Browser',browser='page-provider-test-123456',writable=False,movable=False));check(status==200,'Browser provider registers a read-only virtual directory')
            check(not (root/'Browser').exists(),'Registration does not copy a whole folder or create a physical alias')
            check(post('/api/mount/poll',{'providers':[{'id':mount['id'],'key':'wrong'}]})[0]==403,'Provider poll key cannot be forged')
            native_request=dict(path='/remote-admin',source=str(outside),writable=True)
            check(send(server,'POST','/api/mount/native',json.dumps(native_request).encode(),headers,source=('127.0.0.2',0))[0]==200,'Loopback aliases are local host devices by the established permission model')
            payload=b'provider-data-'*70000;version='provider-version-one';stopping=threading.Event();operations=[];copied=threading.Event();errors=[]
            def provider():
                try:
                    while not stopping.is_set():
                        code,data=post('/api/mount/poll',{'providers':[dict(id=mount['id'],key=mount['key'])]})
                        check(code==200,'Provider poll remains available during deferred downloads')
                        for job in data['jobs']:
                            operations.append(job['op']);response={};status=200
                            if job['op']=='stat':
                                response={'directory':True} if job['path']=='/' else dict(directory=False,size=len(payload),modified=1700000000,version=version)
                            elif job['op']=='list':response=dict(entries=[dict(name='remote.bin',directory=False,size=len(payload),modified=1700000000,hidden=False)],total=1,offset=0,truncated=False,stamp='listing-one')
                            elif job['op']=='stamp':response={'stamp':'listing-one'}
                            elif job['op']=='materialize':
                                code,_,body=send(server,'PUT',route('upload',path='/Browser/remote.bin',mirror=mount['key'],sourceVersion=version),payload,headers)
                                check(code in (200,201),'Demanded file is first streamed into host replica storage');copied.set()
                            else:raise AssertionError('Unexpected operation '+job['op'])
                            target='/api/mount/reply?'+__import__('urllib.parse',fromlist=['urlencode']).urlencode(dict(id=job['id'],mount=mount['id'],key=mount['key'],status=status))
                            check(post(target,response)[0]==200,'Provider reply resolves the deferred request')
                        time.sleep(.03)
                except Exception as error:errors.append(error)
            thread=threading.Thread(target=provider,daemon=True);thread.start()
            try:
                listing=server.api(route('list',path='/Browser'));check(listing['total']==1 and listing['entries'][0]['path']=='/Browser/remote.bin','Provider listing has a server-validated virtual path')
                admin=server.api('/api/admin/settings')
                rpc={'jsonrpc':'2.0','id':4,'method':'tools/call','params':{'name':'list_files','arguments':{'path':'/Browser'}}}
                status,_,rpc_body=send(server,'POST','/mcp',json.dumps(rpc).encode(),dict(headers,Authorization='Bearer '+admin['mcpToken'],**{'MCP-Protocol-Version':'2025-11-25','Content-Type':'application/json'}))
                check(status==200 and not json.loads(rpc_body)['result']['isError'],'MCP file listing waits for browser providers asynchronously: '+str(status)+' '+repr(rpc_body[:300]))
                ticket=server.api(route('download-ticket',path='/Browser/remote.bin'))
                check(copied.is_set() and operations.count('materialize')==1,'A ticket waits for host materialization before being issued')
                result=server.request(ticket['url'],headers={'Range':'bytes=200000-599999'})
                check(result[0]==206 and result[2]==payload[200000:600000],'The host distributes version-pinned byte ranges from its stored copy')
                with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                    replies=list(pool.map(lambda _:server.request(ticket['url'],headers={'Range':'bytes=0-65535'}),range(8)))
                check(all(reply[0]==206 and reply[2]==payload[:65536] for reply in replies),'Concurrent host distribution shares a copied file without reuploading')
                check(operations.count('materialize')==1 and 'read' not in operations,'File bodies are distributed by the host rather than browser relay reads')
                check(send(server,'PUT',route('upload',path='/Browser/hijack.bin',mirror='wrong'),b'x',headers)[0]==403,'A shared client cannot forge replica writes')
                check(send(server,'DELETE',route('entry',path='/Browser/remote.bin'),headers=headers)[0]==403,'Read-only browser mounts stay read-only in shared mode')
            finally:stopping.set();thread.join(timeout=10)
            if errors:raise errors[0]
            check(server.request(ticket['url'],headers={'Range':'bytes=0-65535'})[2]==payload[:65536],'Already pinned downloads keep working immediately after provider polling stops')
            deadline=time.monotonic()+48
            while time.monotonic()<deadline:
                state=next(item for item in server.api('/api/mounts')['mounts'] if item['id']==mount['id'])
                if not state['online']:break
                time.sleep(.3)
            check(not state['online'],'Provider lease expiry marks the browser directory offline')
            offline=server.api(route('list',path='/Browser'))
            check(offline['entries'][0]['name']=='remote.bin','Offline mounts can browse the complete host copies')
            check(server.request(server.api(route('download-ticket',path='/Browser/remote.bin'))['url'])[2]==payload,'An offline mount can issue fresh host download tickets')
            check((root/'Browser/remote.bin').read_bytes()==payload,'Complete materialization is retained on host disk')
            check(server.request('/download?path=/.hls-mounts/'+mount['id']+'/remote.bin')[0]==404,'Unused legacy replica path is absent; no name-only reservation')
            check(post('/api/admin/settings',dict(ipUploadMiB=1))[0]==200,'Host can update per-IP upload allowance dynamically')
            check(expect_upload(server,'/over-quota.bin',b'x'*200000,headers)==413,'Previously received replica bytes count toward the source IP allowance')
            check(post('/api/admin/settings',dict(ipUploadMiB=0))[0]==200,'Zero lifts the upload allowance')
            check(send(server,'PUT',route('upload',path='/unlimited.bin'),b'x'*200000,headers)[0]==201,'Uploads resume after the host removes the limit')
            check(post('/api/mount/remove',dict(path='/Browser',key=mount['key']))[0]==200,'Unmount removes only the virtual mapping')
            check((root/'Browser/remote.bin').read_bytes()==payload,'Unmount preserves completed files at their selected shared path')
            check(server.request(url('/Browser/remote.bin'))[2]==payload,'Unmounted copies remain directly downloadable')
            status,stream=post('/api/mount/register',dict(path='/InFlight',browser='page-stream-test-123456',writable=False,movable=False));check(status==200,'Second mount registers for live-unmount verification')
            complete=route('upload',path='/InFlight/complete.txt',mirror=stream['key'],sourceVersion='one')
            check(send(server,'PUT',complete,b'complete',headers)[0]==201,'Completed replica exists before unmount')
            sock=socket.create_connection(('127.0.0.1',server.port),timeout=10)
            try:
                uploading=route('upload',path='/InFlight/partial.bin',mirror=stream['key'],sourceVersion='partial')
                raw='PUT '+uploading+' HTTP/1.1\r\nHost: 127.0.0.1:'+str(server.port)+'\r\nContent-Length: 800000\r\nExpect: 100-continue\r\nX-HLS-Token: '+headers['X-HLS-Token']+'\r\n\r\n'
                sock.sendall(raw.encode());response=b''
                while not response.endswith(b'\r\n\r\n'):response+=sock.recv(1)
                check(b'100 Continue' in response,'Replica writer starts with bounded streaming body')
                sock.sendall(b'p'*262144);time.sleep(.1)
                check(post('/api/mount/remove',dict(path='/InFlight',key=stream['key']))[0]==200,'Unmount succeeds during an unfinished replica upload')
                try:sock.sendall(b'p'*(800000-262144))
                except (BrokenPipeError,ConnectionResetError):pass
                reply=http.client.HTTPResponse(sock);reply.begin();reply.read();check(reply.status==409,'An upload cannot publish after its provider mapping is removed')
            finally:sock.close()
            check((root/'InFlight/complete.txt').read_bytes()==b'complete' and not (root/'InFlight/partial.bin').exists(),'Unmount retains complete files and never publishes partial files')

        finally:server.close()
        # Limits are checked for the entire accumulated transfer, including
        # unknown-length HTTP bodies, rather than Content-Length alone.
        limited=Server(exe,root,extra=('--no-magika','--write-mode','shared','--ip-upload-mb','1'))
        try:
            token=limited.api('/api/session')['token'];headers={'X-HLS-Token':token}
            check(send(limited,'POST','/api/admin/state',json.dumps({'action':'reset-quota','ip':'127.0.0.1'}).encode(),headers)[0]==200,'Reset the persisted fixture allowance before the concurrent reservation test')
            for attempt in range(5):
                if attempt:check(send(limited,'POST','/api/admin/state',json.dumps({'action':'reset-quota','ip':'127.0.0.1'}).encode(),headers)[0]==200,'Release all reservations between repeated races')
                def writer(i):return expect_upload(limited,'/quota-'+str(attempt)+'-'+str(i),b'q'*600000,headers)
                with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:statuses=list(pool.map(writer,range(2)))
                check(statuses.count(201)==1 and statuses.count(413)==1,'Concurrent upload chunks cannot overdraw one IP allowance')
            check(send(limited,'POST','/api/admin/state',json.dumps({'action':'reset-quota','ip':'127.0.0.1'}).encode(),headers)[0]==200,'Clear fixture before interrupted short-body accounting')
            sock=socket.create_connection(('127.0.0.1',limited.port),timeout=10)
            raw='PUT /api/upload?path=/interrupted-quota.bin HTTP/1.1\r\nHost: 127.0.0.1:'+str(limited.port)+'\r\nContent-Length: 600000\r\nX-HLS-Token: '+token+'\r\n\r\n'
            sock.sendall(raw.encode()+b'p'*60000);sock.shutdown(socket.SHUT_WR)
            while sock.recv(8192):pass
            sock.close()
            deadline=time.monotonic()+5
            while time.monotonic()<deadline:
                quota=limited.api('/api/admin/state')['quotas']
                if quota and quota[0]['used']==60000:break
                time.sleep(.02)
            check(quota[0]['used']==60000 and not (root/'interrupted-quota.bin').exists(),'Short incomplete body remains charged and never publishes')
        finally:limited.close()
        system=Server(exe,root,extra=('--no-magika','--system-root','--no-index'))
        try:
            result=system.api(route('list',path='/'));check(result['total']>0,'System root exposes drives on Windows and filesystem root on Unix')
        finally:system.close()
        print('Mounts HTTP passed: native partitions, virtual roots, read-only/write mounts, scoped search, provider identities, host-first demand materialization, concurrent Range distribution and cumulative IP quotas')


if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
