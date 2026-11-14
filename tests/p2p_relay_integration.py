"""Default passive PCDN control-plane tests over both HTTP and HTTPS."""
import hashlib
import http.client
import json
import pathlib
import ssl
import sys
import tempfile
import time
from integration import Server, check, url


def run(exe, secure=False):
    with tempfile.TemporaryDirectory(prefix='hls-pcdn-') as temporary:
        root=pathlib.Path(temporary);payload=b'pcdn lazy cache'*90000
        (root/'file.bin').write_bytes(payload)
        server=Server(exe,root,extra=('--p2p','--strict-host','--no-magika',*(('--tls-self-signed',) if secure else ())))
        scheme='https' if secure else 'http'
        def request(target,method='GET',data=None,source='127.0.0.1',headers=None):
            options=dict(timeout=15,source_address=(source,0))
            connection=http.client.HTTPSConnection('127.0.0.1',server.port,context=ssl._create_unverified_context(),**options) if secure else http.client.HTTPConnection('127.0.0.1',server.port,**options)
            try:
                connection.request(method,target,json.dumps(data).encode() if data is not None else None,headers or {})
                response=connection.getresponse();return response.status,response.read()
            finally:connection.close()
        def get(target):
            code,body=request(target);check(code==200,'Read '+target);return json.loads(body)
        token=get('/api/session')['token'];headers={'Content-Type':'application/json','X-HLS-Token':token,'Origin':scheme+'://127.0.0.1:'+str(server.port)}
        def post(route,data=None,source='127.0.0.1',custom=None):
            code,body=request('/api/p2p/'+route,'POST',data or {},source,custom or headers);return code,json.loads(body)
        def member(route,meta,data=None,source='127.0.0.2'):
            return post(route,dict(id=meta['id'],key=meta['key'],**(data or {})),source)
        try:
            initial=get('/api/session');check(initial['p2pCacheMiB']==64 and initial['p2pCacheSeconds']==3600,'Host default cache64MiB and one hour')
            code,body=request('/api/admin/settings','POST',{'p2pCacheMiB':2,'p2pCacheSeconds':4},headers=headers);check(code==200,'Host dynamically sets page budget and TTL')
            check(get('/api/session')['p2pCacheMiB']==2 and get('/api/session')['p2pCacheSeconds']==4,'Policy published to visitors')
            code,relay=post('relay-join',source='127.0.0.2');check(code==200,'Idle visitor registers without downloading')
            check(member('relay-poll',relay)[1]['assignment'] is None,'No demand means no file assignment or prefetch')
            check(member('relay-poll',relay,source='127.0.0.3')[0]==403,'Passive page bound to actual IP')
            check(post('relay-poll',dict(id=relay['id'],key='bad'),source='127.0.0.2')[0]==403,'Passive key protected')
            check(post('relay-join',custom=dict(headers,Origin=scheme+'://evil.example'))[0]==403,'Same-origin guard applies to idle registration')
            ticket=get('/api/download-ticket?path=/file.bin');code,download=post('join',dict(path='/file.bin',version=ticket['version']));check(code==200,'Explicit downloader joins')
            code,data=member('relay-poll',relay);assignment=data['assignment'];check(code==200 and assignment['path']=='/file.bin','Idle page assigned lazily on demand')
            check(assignment['cacheMiB']==2 and assignment['cacheSeconds']==4,'Assignment uses live host policy')
            poll=dict(ack=0,have=[],received=0,sent=0,wanted=0)
            code,peers=member('poll',download,poll,source='127.0.0.1');check(any(p['id']==assignment['id'] and p['relay'] and not p['have'] for p in peers['peers']),'Pull-through relay advertised separately from verified cached pieces')
            check(member('hashes',assignment,{'start':0})[1]['hashes']==[hashlib.sha256(payload[i:i+1048576]).hexdigest() for i in range(0,len(payload),1048576)],'Passive relay uses authoritative hashes')
            check(member('signal',download,dict(to=assignment['id'],type='offer',sdp='v=0\r\n'),source='127.0.0.1')[0]==200,'Downloader signals passive relay')
            check(len(member('poll',assignment,dict(poll,wanted=None))[1]['messages'])==1,'Relay receives bounded offer')
            age1=member('poll',assignment,dict(poll,wanted=None))[1]['activityAgeMs'];time.sleep(.12)
            age2=member('poll',assignment,dict(poll,wanted=None))[1]['activityAgeMs'];check(age2>=age1+80,'Idle polling does not renew cache activity')
            member('signal',download,dict(to=assignment['id'],type='offer',sdp='v=0\r\n'),source='127.0.0.1')
            check(member('poll',assignment,dict(poll,wanted=None))[1]['activityAgeMs']<age2,'Host verified coordination renews activity')
            code,second=post('relay-join',source='127.0.0.3');check(code==200 and member('relay-poll',second,source='127.0.0.3')[1]['assignment'] is None,'Only one passive origin copy per active file')
            check(member('relay-leave',relay)[0]==200,'Page-wide disable removes passive node')
            check(member('poll',assignment,poll)[0]==403,'Opt-out revokes assigned file lease')
            replacement=member('relay-poll',second,source='127.0.0.3')[1]['assignment'];check(replacement is not None,'Other eligible visitor takes over')
            code,_=request('/api/admin/settings','POST',{'p2pCacheMiB':0,'p2pCacheSeconds':3600},headers=headers);check(code==200,'Zero cache allowed')
            policy=member('poll',replacement,dict(poll,wanted=None),source='127.0.0.3')[1];check(policy['cacheMiB']==0 and policy['cacheSeconds']==3600,'New budget and TTL pushed to active nodes')
            code,body=request('/api/admin/settings','POST',{'p2pEnabled':False},headers=headers);check(code==200,'Host total switch disables both roles')
            check(member('relay-poll',second,source='127.0.0.3')[0]==404 and post('join',dict(path='/file.bin',version=ticket['version']))[0]==404,'Total off rejects both automatic senders and downloaders')
        finally:server.close()
    print(scheme.upper()+' passive PCDN passed: idle opt-in, lazy assignment, IP/key/Origin isolation, authoritative hashes, one relay per file, opt-out revocation/takeover and host total switch')


if __name__=='__main__':
    exe=pathlib.Path(sys.argv[1]).resolve();run(exe);run(exe,True)
