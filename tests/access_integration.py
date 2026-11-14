"""Authentication, host identity, hotlink sessions, copy and gzip wire tests."""
import concurrent.futures
import gzip
import zlib
import http.client
import hashlib
import json
import pathlib
import sys
import tempfile
import time
import re
import xml.etree.ElementTree as ET
try:
    import cv2
    import numpy as np
except ImportError:
    cv2=None
from integration import Server,check,url
from v2_integration import route,send


def call(server,method,target,data=None,headers=None,source=None):
    raw=data if isinstance(data,bytes) else json.dumps(data).encode() if data is not None else b''
    status,head,body=send(server,method,target,raw,headers or {},source=source) if source else send(server,method,target,raw,headers or {})
    return status,{k.lower():v for k,v in head.items()},body


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-access-') as temporary:
        root=pathlib.Path(temporary);payload=b'hello compression and copying\n'*300000
        (root/'data.txt').write_bytes(payload);(root/'index.html').write_text('<h1>server website</h1>')
        (root/'site').mkdir();(root/'site/style.css').write_text('body{background:url(/site/picture.svg)}');(root/'site/picture.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg"/>');(root/'site/index.html').write_text('<link rel="stylesheet" href="/site/style.css"><img src="picture.svg">')
        (root/'noise.bin').write_bytes(bytes(range(256))*1000)
        secured=Server(exe,root,extra=('--no-magika','--write-mode','isolated','--password','test-passphrase','--strict-host','--ip-upload-mb','1'))
        try:
            check(secured.request('/')[0]==200 and b'/api/access/login' in secured.request('/')[2],'Unauthenticated root shows login without embedded application')
            for target in ['/api/session','/api/list',url('/data.txt'),'/view/data.txt','/api/preview-text?path=/data.txt','/api/qr']:
                check(secured.request(target)[0]==401,'Login gates '+target)
            check(call(secured,'PUT',route('upload',path='/unauthed.txt'),b'x')[0]==401,'Streaming upload cannot bypass login')
            login_headers={'Content-Type':'application/json'}
            check(call(secured,'POST','/api/access/login',{'password':'test-passphrase'},dict(login_headers,Origin='http://evil.example'))[0]==403,'Cross-origin login is refused')
            code,headers,_=call(secured,'POST','/api/access/login',{'password':'test-passphrase'},login_headers)
            check(code==200 and 'HttpOnly' in headers['set-cookie'] and 'SameSite=Strict' in headers['set-cookie'],'Login issues an HttpOnly same-site session')
            cookie=headers['set-cookie'].split(';')[0];auth={'Cookie':cookie}
            session=json.loads(secured.request('/api/session',headers=auth)[2]);auth['X-HLS-Token']=session['token']
            check(session['localAdmin'] and session['uploadRoot']=='/','Host isolated upload root remains unrestricted')
            check(call(secured,'PUT',route('upload',path='/host-upload.txt',drop=1),b'host',auth)[0]==201 and (root/'host-upload.txt').exists(),'Host drag upload remains in selected shared directory')
            check(call(secured,'PUT',route('upload',path='/host-over-ip-quota.bin'),b'x'*(2<<20),auth)[0]==201,'Isolated host administrator is exempt from the per-IP upload allowance')
            remote=('127.0.0.2',0)
            check(call(secured,'GET','/api/session',headers=auth,source=remote)[0]==401,'Session cookie is bound to real TCP peer IP')
            for attempt in range(5):
                code,head,body=call(secured,'POST','/api/access/login',{'password':'wrong'},login_headers,remote);data=json.loads(body)
                check(code==(429 if attempt==4 else 401),'Five chances before temporary block')
                if attempt<4:check(data['remaining']==4-attempt,'Remaining login chances decrement exactly once')
            wait=int(head['retry-after']);check(119<=wait<=120,'First ban starts at two minutes')
            time.sleep(.2)
            for _ in range(3):
                code,head,_=call(secured,'POST','/api/access/login',{'password':'test-passphrase'},login_headers,remote)
                check(code==429 and int(head['retry-after'])<=wait,'Blocked retries cannot extend ban or bypass it using a correct password')
            blocked=json.loads(secured.request('/api/admin/access',headers=auth)[2])['blocked'];check(blocked[0]['ip']=='127.0.0.2','Host sees blocked source IP')
            check(call(secured,'POST','/api/admin/access',{'ip':'127.0.0.2'},auth)[0]==200,'Host manually unlocks an IP')
            code,headers,_=call(secured,'POST','/api/access/login',{'password':'test-passphrase'},login_headers,remote);check(code==200,'Unblocked client can log in')
            peer={'Cookie':headers['set-cookie'].split(';')[0]}
            state=json.loads(call(secured,'GET','/api/session',headers=peer,source=remote)[2]);peer['X-HLS-Token']=state['token']
            check(not state['localAdmin'],'Strict host rejects 127.0.0.2 even though it is loopback')
            check(call(secured,'POST','/api/admin/settings',{'title':'hijack'},peer,remote)[0]==403,'Strict remote peer cannot change host settings')
            check(call(secured,'PUT',route('upload',path='/remote-upload.txt',drop=1),b'peer',peer,remote)[0]==201 and (root/'_clients/127.0.0.2/remote-upload.txt').exists(),'Strict remote isolated upload routes into real peer folder')
            # Host copy is streamed on disk, pinned and no-clobber.
            ticket=json.loads(secured.request(route('download-ticket',path='/data.txt'),headers=auth)[2])
            code,_,body=call(secured,'POST','/api/copy?'+__import__('urllib.parse',fromlist=['urlencode']).urlencode(dict(path='/data.txt',to='/copy.txt',version=ticket['version'],copy='copy-http-success-12345')),headers=auth)
            check(code==201 and (root/'copy.txt').read_bytes()==payload,'Server-side copy preserves all source bytes')
            check(call(secured,'POST',route('copy',path='/data.txt',to='/copy.txt'),headers=auth)[0]==409,'Copy never clobbers an existing destination')
            check(call(secured,'POST',route('copy',path='/data.txt',to='/../escape'),headers=auth)[0]==400,'Copy rejects destination traversal')
            check(call(secured,'POST',route('copy',path='/data.txt',to='/forbidden.txt'),headers=peer,source=remote)[0]==403,'Copy destination obeys isolated IP authorization')
            code,preview_headers,_=secured.request('/view/site/index.html',headers=auth)
            check(code==302 and preview_headers['location'].startswith('/preview-assets/'),'Authenticated preview receives a dedicated resource grant')
            grant=preview_headers['location'];code,_,body=secured.request(grant)
            check(code==200 and b'/preview-assets/' in body,'Opaque preview can read HTML without inheriting the login cookie')
            style=grant.rsplit('/',1)[0]+'/style.css';check(b'/preview-assets/' in secured.request(style)[2],'Root-relative CSS URLs keep the preview grant')
            image=secured.request(grant.rsplit('/',1)[0]+'/picture.svg');check(image[0]==200,'Related preview images load without cookies');ET.fromstring(image[2])
            check(call(secured,'GET',grant,source=remote)[0]==403,'Preview grant is bound to provider IP')
            prefix=grant.split('/site/')[0];check(secured.request(prefix+'/api/admin/settings')[0]==403,'Preview grant cannot enter administration APIs')
            update={'loginPassword':'next-passphrase','hotlinkSeconds':2}
            code,headers,_=call(secured,'POST','/api/admin/settings',update,auth);check(code==200,'Host can change passphrase and hotlink timeout live')
            check(secured.request('/api/session',headers=auth)[0]==401,'Changing the passphrase revokes previous sessions')
            auth['Cookie']=headers['set-cookie'].split(';')[0]
            check(secured.request(url('/data.txt'),headers=auth)[2]==payload,'The host applying settings retains authenticated access')
            check(secured.request(url('/data.txt'),headers=dict(auth,Referer='http://evil.example/',**{'Sec-Fetch-Site':'cross-site'}))[0]==403,'Hotlink defense rejects external document requests')
            time.sleep(1.1)
            check(call(secured,'POST','/api/browse',{'id':'browser-access-test-123456','paths':['/'],'current':'/','owners':[]},auth)[0]==200,'Background browse polls remain available')
            time.sleep(1.1)
            check(secured.request(url('/data.txt'),headers=auth)[0]==403,'Idle download session expires')
            check(call(secured,'POST','/api/access/activate',{},auth)[0]==200,'Explicit browser action renews expired download access')
            check(secured.request(url('/data.txt'),headers=dict(auth,Range='bytes=10-19'))[2]==payload[10:20],'Range resumption works after session renewal')
            check(secured.request(grant)[0] in (401,403),'Passphrase rotation revokes old preview grants')
            # Positive traffic renews the shared media/download idle timer.
            for _ in range(3):
                time.sleep(1.1);check(secured.request(url('/data.txt'),headers=dict(auth,Range='bytes=0-1023'))[0]==206,'Ongoing Range traffic renews inactivity countdown')
            urls=json.loads(secured.request('/api/urls',headers=auth)[2])['urls'];check(urls and urls[0]['recommended'],'URL list marks a recommended address')
            if cv2 is not None:
                svg=secured.request(urls[0]['qr'],headers=auth)[2].decode();size=int(re.search(r'viewBox="0 0 (\d+)',svg)[1]);image=np.full((size,size),255,np.uint8)
                for x,y in re.findall(r'M(\d+),(\d+)h1v1h-1z',svg):image[int(y),int(x)]=0
                image=cv2.resize(image,None,fx=8,fy=8,interpolation=cv2.INTER_NEAREST);decoded,_,_=cv2.QRCodeDetector().detectAndDecode(image)
                check(decoded==urls[0]['url'],'Independent QR decoder recovers exact server URL')
        finally:secured.close()
        normal=Server(exe,root,extra=('--no-magika',))
        try:
            status,headers,body=normal.request(url('/data.txt'),headers={'Accept-Encoding':'gzip'})
            check(status==200 and headers.get('content-encoding')=='gzip' and headers.get('transfer-encoding')=='chunked' and gzip.decompress(body)==payload,'Large text files stream as valid chunked gzip')
            for encoding,want in [('gzip;q=0',False),('gzip;q=0.5',True),('*;q=1, gzip;q=0',False),('br, gzip; q=1',True),('GZIP;Q=0.8',True),('deflate',False),('gzip;q=0.1,deflate;q=0.9',False)]:
                status,headers,body=normal.request('/',headers={'Accept-Encoding':encoding});check((headers.get('content-encoding')=='gzip')==want,'Accept-Encoding q-values: '+encoding)
                coding=headers.get('content-encoding','');decoded=gzip.decompress(body) if coding=='gzip' else zlib.decompress(body) if coding=='deflate' else body
                check(b'HttpLocalServer' in decoded,'Negotiated embedded page decodes')
                if encoding in ('deflate','gzip;q=0.1,deflate;q=0.9','*;q=1, gzip;q=0'):check(coding=='deflate','Deflate negotiation honors explicit exclusions and quality')
            status,headers,body=normal.request(url('/data.txt'),headers={'Accept-Encoding':'gzip','Range':'bytes=100-999'})
            check(status==206 and 'content-encoding' not in headers and body==payload[100:1000],'Byte ranges retain identity coding and byte-accurate offsets')
            check('content-encoding' not in normal.request(url('/noise.bin'),headers={'Accept-Encoding':'gzip'})[1],'Binary payload is not wastefully recompressed')
            connection=http.client.HTTPConnection('127.0.0.1',normal.port,timeout=20);connection._http_vsn=10;connection._http_vsn_str='HTTP/1.0'
            try:
                connection.request('GET',url('/data.txt'),headers={'Host':'127.0.0.1:'+str(normal.port),'Accept-Encoding':'gzip'})
                response=connection.getresponse();check(response.status==200 and response.getheader('Transfer-Encoding') is None and gzip.decompress(response.read())==payload,'HTTP/1.0 receives a valid close-delimited gzip stream')
            finally:connection.close()

            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                results=list(pool.map(lambda _:normal.request(url('/data.txt'),headers={'Accept-Encoding':'gzip'}),range(8)))
            check(all(gzip.decompress(r[2])==payload for r in results),'Concurrent compressors retain independent stream state')
        finally:normal.close()
        disabled=Server(exe,root,extra=('--no-magika','--no-compression'))
        try:
            for target in ['/',url('/data.txt')]:check('content-encoding' not in disabled.request(target,headers={'Accept-Encoding':'gzip'})[1],'CLI compression switch disables embedded and file gzip')
        finally:disabled.close()
        cli=Server(exe,root,extra=('--no-magika','--show-cli-qr'))
        try:
            cli.log.seek(0);log=cli.log.read().decode('utf-8');check('Recommended:' in log and '##' in log and '\x1b[' not in log,'CLI QR is opt-in and redirected output contains plain ASCII rather than terminal escapes')
        finally:cli.close()
        hosting=Server(exe,root,mode='server',extra=('--no-magika','--password','site-pass','--hotlink-seconds','2'))
        try:
            check(b'/api/access/login' in hosting.request('/')[2],'Server mode presents the login gate')
            code,headers,_=call(hosting,'POST','/api/access/login',{'password':'site-pass'},{'Content-Type':'application/json'});check(code==200,'Server login endpoint works')
            check(b'server website' in hosting.request('/',headers={'Cookie':headers['set-cookie'].split(';')[0]})[2],'Server index opens with login and anti-hotlink together')
        finally:hosting.close()
        print('Access HTTP passed: login coverage, five attempts, stable two-minute ban, manual unblock, strict host, administrator upload, isolated copy, session revocation, idle/active hotlink, QR decoding, q-values, streaming gzip and identity Range')


if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
