"""Real HTTP control-plane/hash tests; browser tests cover actual WebRTC bytes."""
import concurrent.futures
import hashlib
import json
import pathlib
import sys
import tempfile
import time
from integration import Server,check,url
from v2_integration import send,route


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-p2p-') as temporary:
        root=pathlib.Path(temporary)
        payload=bytes(range(256))*9000+b'peer end marker'
        (root/'data.bin').write_bytes(payload);(root/'other.bin').write_bytes(b'another file')
        (root/'empty').write_bytes(b'')
        disabled=Server(exe,root,extra=('--no-magika',))
        try:
            check(not disabled.api('/api/session')['p2pEnabled'],'P2P default is off')
            check(send(disabled,'POST','/api/p2p/join',b'{}',{'X-HLS-Token':disabled.api('/api/session')['token']})[0]==404,'Disabled P2P has no usable control endpoint')
        finally:disabled.close()
        server=Server(exe,root,extra=('--p2p','--no-magika','--write-mode','shared','--strict-host'))
        token=server.api('/api/session')['token'];headers={'X-HLS-Token':token,'Content-Type':'application/json'}
        def post(name,data,source=None,custom=None):
            status,head,body=send(server,'POST','/api/p2p/'+name,json.dumps(data).encode(),custom or headers,source=source)
            return status,json.loads(body)
        def member(name,peer,values=None,**kwargs):
            return post(name,dict(id=peer['id'],key=peer['key'],**(values or {})),**kwargs)
        def join(path):
            ticket=server.api(route('download-ticket',path=path));code,peer=post('join',dict(path=path,version=ticket['version']));check(code==200,'Join a pinned regular file');return peer
        try:
            check(server.api('/api/session')['p2pEnabled'],'CLI enables P2P')
            check(post('join',dict(path='/data.bin',version='wrong'))[0]==409,'Old file versions cannot join')
            check(post('join',dict(path='/../escape',version='x'))[0]==400,'Traversal rejected')
            empty=server.api(route('download-ticket',path='/empty'))
            check(post('join',dict(path='/empty',version=empty['version']))[0]==413,'Empty file has ordinary download fallback')
            check(post('join',dict(path='/data.bin',version='x'),custom=dict(headers,Origin='http://evil.example'))[0]==403,'Cross-origin signaling rejected')
            check(post('join',dict(path='/data.bin',version='x'),custom={'Content-Type':'application/json'})[0]==403,'Write token required even for read-only shares')
            a,b=join('/data.bin'),join('/data.bin');c=join('/other.bin')
            check(a['pieceBytes']==1048576 and a['pieces']==3 and a['size']==len(payload),'Bounded chunk plan without whole-file hashing')
            poll={'ack':0,'have':[[0,2]],'received':0,'sent':0}
            code,data=member('poll',a,poll);check(code==200 and any(p['id']==b['id'] for p in data['peers']),'Discover participants of exact file version')
            check(not any(p['id']==c['id'] for p in data['peers']),'Other files never mix into swarm')
            check('key' not in json.dumps(data),'Discovery never exposes another member key')
            check(member('poll',a,poll,source=('127.0.0.2',0))[0]==403,'Participant key is bound to TCP IP')
            check(post('poll',dict(id=a['id'],key='x',**poll))[0]==403,'Forged member key rejected')
            check(member('poll',a,dict(poll,have=[[2,4]]))[0]==400,'Availability cannot exceed file')
            check(member('poll',a,dict(poll,have=[[1,2],[0,1]]))[0]==400,'Availability sorted/disjoint bound')
            check(member('signal',a,dict(to=c['id'],type='offer',sdp='v=0\r\n'))[0]==403,'SDP cannot reach another file swarm')
            check(member('signal',a,dict(to=b['id'],type='offer',sdp='v=0\r\n'))[0]==200,'Offer delivered only to an authorized member')
            code,one=member('poll',b,dict(poll,have=[]));check(code==200 and len(one['messages'])==1,'Recipient polls a bounded signal queue')
            code,again=member('poll',b,dict(poll,have=[]));check(again['messages']==one['messages'],'Unacknowledged signal survives retry')
            last=one['messages'][0]['sequence'];code,cleared=member('poll',b,dict(poll,have=[],ack=last));check(not cleared['messages'],'Acknowledgement removes delivered signal')
            check(member('signal',a,dict(to=b['id'],type='offer',sdp='x'*32769))[0]==400,'SDP size bound')
            def hash_page(_):
                for retry in range(20):
                    result=member('hashes',a,{'start':0})
                    if result[0]!=503:return result
                    time.sleep(.025)
                return result
            with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                pages=list(pool.map(hash_page,range(24)))
            wanted=[hashlib.sha256(payload[i:i+1048576]).hexdigest() for i in range(0,len(payload),1048576)]
            check(all(code==200 and page['hashes']==wanted for code,page in pages),'Concurrent digest requests coalesce and match independent SHA-256')
            check(member('hashes',a,{'start':1})[0]==400,'Hash pages are aligned')
            # SHA padding boundaries and empty-message vector via actual data.
            for length in [1,55,56,63,64,65,127,128,129,1048576]:
                data=bytes(i%251 for i in range(length));(root/'vectors').write_bytes(data);peer=join('/vectors')
                check(member('hashes',peer,{'start':0})[1]['hashes']==[hashlib.sha256(data).hexdigest()],'SHA-256 padding/length vector '+str(length))
                member('leave',peer)
            (root/'data.bin').write_bytes(b'changed content')
            check(member('poll',a,poll)[0]==409,'External file change invalidates peer membership')
            code,_,_=send(server,'POST','/api/admin/settings',json.dumps({'p2pEnabled':False}).encode(),headers);check(code==200,'Host can disable P2P live')
            check(member('hashes',a,{'start':0})[0]==404,'Disabling revokes hash/control endpoints')
            check(send(server,'POST','/api/admin/settings',json.dumps({'p2pEnabled':True}).encode(),headers,source=('127.0.0.2',0))[0]==403,'Remote peers cannot enable host P2P')
        finally:server.close()
        # Login revision and hotlink idle rules apply to control-only traffic.
        secured=Server(exe,root,extra=('--p2p','--password','peer-pass','--hotlink-seconds','2','--no-magika'))
        try:
            code,head,body=send(secured,'POST','/api/access/login',b'{"password":"peer-pass"}',{'Content-Type':'application/json'})
            cookie=dict(Cookie=head.get('Set-Cookie',head.get('set-cookie')).split(';')[0]);session=json.loads(secured.request('/api/session',headers=cookie)[2]);auth=dict(cookie,**{'X-HLS-Token':session['token']})
            ticket=json.loads(secured.request(route('download-ticket',path='/other.bin'),headers=cookie)[2])
            code,_,body=send(secured,'POST','/api/p2p/join',json.dumps(dict(path='/other.bin',version=ticket['version'])).encode(),auth);peer=json.loads(body);check(code==200,'Authenticated browser joins')
            values=dict(id=peer['id'],key=peer['key'],have=[],ack=0,received=0,sent=0)
            time.sleep(1.1);check(send(secured,'POST','/api/p2p/poll',json.dumps(values).encode(),auth)[0]==200,'Unexpired idle poll')
            time.sleep(1.1);check(send(secured,'POST','/api/p2p/poll',json.dumps(values).encode(),auth)[0]==403,'Polling alone never extends hotlink session')
        finally:secured.close()
    print('P2P HTTP passed: default off, explicit host control, bounded peer discovery/SDP/acks, IP/token/origin/file-version isolation, independently verified/coalesced SHA-256, dynamic disable and idle-session expiry')


if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
