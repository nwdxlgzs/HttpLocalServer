"""Authenticated mounted sources, manifests, priority and host fallback (HTTP/TLS)."""
import concurrent.futures
import hashlib
import http.client
import json
import pathlib
import ssl
import sys
import tempfile
import threading
import time
import urllib.parse
from integration import Server, check


def run(exe,secure=False):
    with tempfile.TemporaryDirectory(prefix='hls-source-pcdn-') as temporary:
        root=pathlib.Path(temporary);flags=('--https','--tls-self-signed') if secure else ()
        server=Server(exe,root,extra=('--p2p','--strict-host','--no-magika','--write-mode','shared','--max-transfers','1',*flags))
        scheme='https' if secure else 'http'
        def raw(target,method='GET',data=None,source='127.0.0.2',binary=False):
            cls=http.client.HTTPSConnection if secure else http.client.HTTPConnection
            options={'source_address':(source,0),'timeout':30}
            if secure:options['context']=ssl._create_unverified_context()
            connection=cls('127.0.0.1',server.port,**options)
            headers={'Origin':scheme+'://127.0.0.1:'+str(server.port)}
            if method!='GET':headers['X-HLS-Token']=token;headers['Content-Type']='application/octet-stream' if binary else 'application/json'
            try:
                connection.request(method,target,body=data if binary else json.dumps(data) if data is not None else None,headers=headers)
                response=connection.getresponse();return response.status,{k.lower():v for k,v in response.getheaders()},response.read()
            finally:connection.close()
        token='';token=json.loads(raw('/api/session')[2])['token']
        def post(route,data,source='127.0.0.2'):
            code,_,body=raw(route,'POST',data,source);return code,json.loads(body)
        def api(target,source='127.0.0.3'):
            code,_,body=raw(target,source=source);check(code==200,'Read '+target+': '+str(code)+' '+repr(body[:120]));return json.loads(body)
        def member(route,meta,data=None,source='127.0.0.3'):
            return post('/api/p2p/'+route,dict(id=meta['id'],key=meta['key'],**(data or {})),source)
        stop=threading.Event();errors=[];operations=[];counts={}
        payloads={f'/file{i}.bin':bytes([i+31])*1200000 for i in range(5)}
        version='same-size-and-time-for-different-paths'
        try:
            code,mount=post('/api/mount/register',dict(path='/Source',browser='source-page-12345678901234',writable=False,movable=False));check(code==200,'Source mount registered')
            def provider():
                try:
                    while not stop.is_set():
                        code,data=post('/api/mount/poll',{'providers':[dict(id=mount['id'],key=mount['key'])]});check(code==200,'Provider heartbeat')
                        for job in data['jobs']:
                            operations.append(job['op']);payload=payloads.get(job.get('path'),b'');response={};status=200
                            if job['op']=='stat':response=dict(directory=False,size=len(payload),modified=1700000000,version=version)
                            elif job['op']=='p2p-hashes':
                                counts[job['path']]=counts.get(job['path'],0)+1
                                if job['version']!=version:status=409;response={'error':'source changed'}
                                else:response=dict(start=job['start'],hashes=[hashlib.sha256(payload[i*1048576:(i+1)*1048576]).hexdigest() for i in range(job['start'],min(job['start']+16,(len(payload)+1048575)//1048576))])
                            elif job['op']=='materialize':
                                target='/api/upload?'+urllib.parse.urlencode(dict(path='/Source'+job['path'],mirror=mount['key'],sourceVersion=job['version']))
                                code,_,body=raw(target,'PUT',payload,binary=True);check(code in (200,201),'Materialize fallback: '+repr(body[:200]))
                            else:raise AssertionError('Unexpected job '+job['op'])
                            target='/api/mount/reply?'+urllib.parse.urlencode(dict(mount=mount['id'],key=mount['key'],id=job['id'],status=status))
                            check(post(target,response)[0]==200,'Provider completes authenticated job')
                        time.sleep(.015)
                except Exception as error:errors.append(error)
            thread=threading.Thread(target=provider,daemon=True);thread.start()
            code,source=post('/api/p2p/relay-join',{});check(code==200,'Mounted source page joins silent sending')
            proof=[dict(id=mount['id'],key=mount['key'])]
            check(member('relay-poll',source,dict(sources=[dict(id=mount['id'],key='wrong')]),'127.0.0.2')[0]==403,'Same-IP page cannot impersonate directory owner')
            check(member('relay-poll',source,dict(sources=proof),'127.0.0.3')[0]==403,'Source proof is bound to actual owner IP')
            check(member('relay-poll',source,dict(sources=proof),'127.0.0.2')[0]==200,'Owner proves directory handle lease')
            downloads=[];tickets=[]
            for i in range(5):
                ticket=api('/api/download-ticket?p2p=1&path=/Source/file'+str(i)+'.bin');tickets.append(ticket)
                code,peer=post('/api/p2p/join',dict(path='/Source/file'+str(i)+'.bin',version=ticket['version']),'127.0.0.3');check(code==200,'Join unmaterialized source '+str(i));downloads.append(peer)
            check(not (root/'Source').exists() and 'materialize' not in operations,'P2P tickets and source admission do not wait for whole-file upload')
            code,data=member('relay-poll',source,dict(sources=proof),'127.0.0.2');assigned=data['assignments']
            check(code==200 and len(assigned)==4 and all(p['source'] and p['sourceMount']==mount['id'] for p in assigned),'Source page handles at most four files with authoritative source metadata')
            poll=dict(ack=0,have=[],wanted=0,received=0,sent=0)
            swarm=member('poll',downloads[0],poll)[1];check(swarm['peers'][0]['source'],'Original mounted source is ranked first')
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                replies=list(pool.map(lambda _:member('hashes',downloads[0],dict(start=0)),range(8)))
            expected=[hashlib.sha256(payloads['/file0.bin'][i:i+1048576]).hexdigest() for i in range(0,1200000,1048576)]
            check(all(code==200 and value['hashes']==expected for code,value in replies) and counts['/file0.bin']==1,'Concurrent source manifest requests: '+repr(replies)+' counts '+repr(counts)+' provider errors '+repr(errors))
            second=member('hashes',downloads[1],dict(start=0))[1]
            check(second['hashes']!=expected and counts['/file1.bin']==1,'Hash cache is separated by logical path even when source version/size match')
            check('materialize' not in operations,'Manifest traffic never proxies file bodies through the host')
            code,_=member('signal',downloads[0],dict(to=assigned[0]['id'],type='offer',sdp='v=0\r\n'));check(code==200,'Downloader can establish source WebRTC')
            code,data=member('poll',assigned[0],dict(poll,wanted=None),'127.0.0.2');check(code==200 and data['messages'],'Source receives host-authorized connection signal')
            # If WebRTC cannot supply a piece, the ordinary HTTP fallback first
            # creates one durable host replica and serves the requested Range.
            cls=http.client.HTTPSConnection if secure else http.client.HTTPConnection;opts={'timeout':30}
            if secure:opts['context']=ssl._create_unverified_context()
            connection=cls('127.0.0.1',server.port,**opts)
            try:
                connection.request('GET',tickets[0]['url'],headers={'Range':'bytes=0-1048575'});r=connection.getresponse();received=r.read();check(r.status==206 and received==payloads['/file0.bin'][:1048576],'Origin fallback: '+str(r.status)+' '+repr(received[:200])+' provider errors '+repr(errors))
            finally:connection.close()
            check(operations.count('materialize')==1 and (root/'Source/file0.bin').read_bytes()==payloads['/file0.bin'],'Completed host copy is durable')
            check(member('relay-leave',source,source='127.0.0.2')[0]==200,'Page opt-out revokes every source assignment')
            check(all(member('poll',p,dict(poll,wanted=None),'127.0.0.2')[0]==403 for p in assigned),'All four direct source leases removed together')
            check(post('/api/mount/remove',dict(path='/Source',key=mount['key']))[0]==200,'Unmount leaves completed replicas')
            check(api('/api/download-ticket?path=/Source/file0.bin')['size']==1200000,'Ordinary HTTP remains available after unmount')
            check(member('poll',downloads[0],poll)[0]==409,'Old swarm cannot cross an unmount/replacement boundary')
            stop.set();thread.join(5);check(not errors,'Provider failures: '+repr(errors))
        finally:stop.set();server.close()
    print(scheme.upper()+' mounted PCDN passed: source proofs, four-file bound, priority, no pre-materialization, coalesced/path-isolated manifests, HTTP fallback, opt-out and durable unmount')


if __name__=='__main__':
    exe=pathlib.Path(sys.argv[1]).resolve();run(exe);run(exe,True)
