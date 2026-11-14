"""Real mbedTLS transport tests. cryptography is a test-only certificate generator."""
import concurrent.futures
import datetime
import gzip
import http.client
import ipaddress
import json
import os
import pathlib
import socket
import ssl
import subprocess
import sys
import tempfile
import time
from integration import Server, check, make_sparse, url


class SecureServer(Server):
    def __init__(self, exe, root, extra=(), mode='filetrans'):
        super().__init__(exe, root, mode=mode, extra=('--https', '--no-magika', *extra))
        self.context = ssl._create_unverified_context()  # Only our disposable fixture.

    def connection(self, context=None):
        return http.client.HTTPSConnection('127.0.0.1', self.port, context=context or self.context, timeout=20)

    def request(self, target, method='GET', headers=None, body=None, context=None):
        connection = self.connection(context)
        try:
            connection.request(method, target, body=body, headers=headers or {})
            response = connection.getresponse()
            return response.status, {k.lower(): v for k, v in response.getheaders()}, response.read()
        finally:
            connection.close()

    def json_request(self, target, data, headers=None):
        return self.request(target, 'POST', dict({'Content-Type': 'application/json'}, **(headers or {})), json.dumps(data).encode())


def credentials(folder, ec=False):
    from cryptography import x509
    from cryptography.hazmat.backends import default_backend
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec as curve, rsa
    from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
    backend=default_backend()
    key = curve.generate_private_key(curve.SECP256R1(),backend) if ec else rsa.generate_private_key(public_exponent=65537, key_size=2048,backend=backend)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'HttpLocalServer test only')])
    now = datetime.datetime.utcnow()
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now-datetime.timedelta(days=1))
            .not_valid_after(now+datetime.timedelta(days=3))
            .add_extension(x509.SubjectAlternativeName([x509.DNSName('localhost'), x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]), False)
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), False).sign(key, hashes.SHA256(),backend))
    cert_path, key_path = folder/'server.pem', folder/'private.pem'
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    return cert_path, key_path


def fragment_handshake(server):
    incoming, outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
    context = ssl._create_unverified_context()
    context.maximum_version = ssl.TLSVersion.TLSv1_2
    connection = context.wrap_bio(incoming, outgoing, server_side=False, server_hostname='localhost')
    with socket.create_connection(('127.0.0.1', server.port), timeout=10) as wire:
        def flush():
            data = outgoing.read()
            for at in range(0, len(data), 13):
                wire.sendall(data[at:at+13])
        while True:
            try:
                connection.do_handshake()
                flush()
                break
            except ssl.SSLWantReadError:
                flush()
                incoming.write(wire.recv(79))
        connection.write(('GET /api/session HTTP/1.1\r\nHost: 127.0.0.1:'+str(server.port)+'\r\nConnection: close\r\n\r\n').encode())
        flush()
        result = b''
        while b'"clientIP"' not in result:
            try:
                result += connection.read(4096)
            except ssl.SSLWantReadError:
                flush()
                data = wire.recv(97)
                check(data, 'Fragmented TLS connection stays open until the response')
                incoming.write(data)
        check(result.startswith(b'HTTP/1.1 200'), 'Fragmented ClientHello, encrypted records and HTTP headers')


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-tls-') as directory:
        root = pathlib.Path(directory)
        payload = bytes(range(256))*16384+b'end'
        (root/'payload.bin').write_bytes(payload)
        text = b'HTTPS streaming compression\n'*100000
        (root/'large.txt').write_bytes(text)
        (root/'index.html').write_text('<h1>HTTPS site</h1>', encoding='utf-8')
        (root/'site').mkdir()
        (root/'site/index.html').write_text('<link rel="stylesheet" href="/site/main.css"><h1>TLS preview</h1>', encoding='utf-8')
        (root/'site/main.css').write_text('h1{color:blue}', encoding='utf-8')
        make_sparse(root/'huge.bin')
        server = SecureServer(exe, root, ('--tls-self-signed', '--write-mode', 'shared', '--mcp'))
        try:
            for version in [ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3]:
                context = ssl._create_unverified_context()
                context.minimum_version = context.maximum_version = version
                context.set_alpn_protocols(['h2', 'http/1.1'])
                connection = server.connection(context)
                try:
                    connection.connect()
                    check(connection.sock.version()==version.name.replace('TLSv1_', 'TLSv1.'), 'Negotiated exact TLS version')
                    check(connection.sock.selected_alpn_protocol()=='http/1.1', 'ALPN never advertises unimplemented HTTP/2')
                    cert = connection.sock.getpeercert(binary_form=True)
                    connection.request('GET', '/api/session')
                    check(connection.getresponse().status==200, 'HTTP over '+str(version))
                finally:
                    connection.close()
            trusted = ssl.create_default_context(cadata=ssl.DER_cert_to_PEM_cert(cert))
            check(server.request('/api/session', context=trusted)[0]==200, 'Self-signed certificate validates when explicitly trusted, including IP SAN')
            windows7=ssl._create_unverified_context()
            windows7.minimum_version=windows7.maximum_version=ssl.TLSVersion.TLSv1_2
            windows7.set_ciphers('ECDHE-RSA-AES128-SHA256')
            windows7.set_ecdh_curve('prime256v1')
            check(server.request('/api/session',context=windows7)[0]==200,'Win7 Schannel RSA/P256/SHA256 cipher subset can negotiate TLS1.2')
            obsolete=ssl._create_unverified_context()
            obsolete.minimum_version=obsolete.maximum_version=ssl.TLSVersion.TLSv1_1
            obsolete.set_ciphers('ALL:@SECLEVEL=0')
            try:
                server.request('/',context=obsolete)
                raise AssertionError('TLS 1.1 must not negotiate')
            except (OSError,ssl.SSLError,http.client.HTTPException):
                pass
            resumed_context=ssl._create_unverified_context()
            resumed_context.maximum_version=ssl.TLSVersion.TLSv1_2
            saved=None
            for attempt in range(2):
                with socket.create_connection(('127.0.0.1',server.port),timeout=10) as raw:
                    with resumed_context.wrap_socket(raw,server_hostname='localhost',session=saved,suppress_ragged_eofs=False) as encrypted:
                        if attempt:check(encrypted.session_reused,'TLS tickets resume subsequent connections')
                        encrypted.sendall(('GET /api/session HTTP/1.1\r\nHost: 127.0.0.1:'+str(server.port)+'\r\nConnection: close\r\n\r\n').encode())
                        while encrypted.recv(65536):pass
                        saved=encrypted.session
                        check(saved.has_ticket,'Server issues a bounded-lifetime encrypted session ticket')
            try:
                server.request('/', context=ssl.create_default_context())
                raise AssertionError('Untrusted temporary certificate was accepted')
            except ssl.SSLCertVerificationError:
                pass
            session = json.loads(server.request('/api/session')[2])
            auth = {'X-HLS-Token': session['token'], 'Origin': 'https://127.0.0.1:'+str(server.port)}
            check(server.request('/api/mkdir?path=/created', 'POST', auth)[0]==201, 'HTTPS Origin accepted for writes')
            bad = dict(auth, Origin='http://127.0.0.1:'+str(server.port))
            check(server.request('/api/mkdir?path=/bad', 'POST', bad)[0]==403, 'HTTP Origin rejected by HTTPS listener')
            upload = os.urandom(1100003)
            check(server.request('/api/upload?path=/uploaded.bin', 'PUT', auth, upload)[0]==201, 'Encrypted streamed upload')
            check(server.request(url('/uploaded.bin'))[2]==upload, 'Encrypted upload is byte exact')
            connection=server.connection()
            try:
                connection.request('PUT','/api/upload?path=/chunked.bin',body=[upload[:17],upload[17:65555],upload[65555:]],headers=auth,encode_chunked=True)
                response=connection.getresponse()
                check(response.status==201,'Chunked uploads work across TLS record boundaries')
                response.read()
            finally:
                connection.close()
            check(server.request(url('/chunked.bin'))[2]==upload,'Chunked TLS upload bytes are exact')
            check(server.request(url('/payload.bin'), headers={'Range':'bytes=16380-65540'})[2]==payload[16380:65541], 'Range across multiple TLS records')
            check(server.request(url('/huge.bin'), headers={'Range':'bytes='+str(5*1024**3+123)+'-'})[2]==b'RANGE-OVER-4G', 'HTTPS Range beyond 4 GiB')
            status, headers, packed = server.request(url('/large.txt'), headers={'Accept-Encoding':'gzip'})
            check(status==200 and headers['transfer-encoding']=='chunked' and gzip.decompress(packed)==text, 'HTTPS chunked gzip stream')
            connection=server.connection()
            try:
                for _ in range(12):
                    connection.request('GET', url('/payload.bin'), headers={'Range':'bytes=2-20'})
                    response=connection.getresponse()
                    check(response.status==206 and response.read()==payload[2:21], 'Repeated TLS Keep-Alive requests')
            finally:
                connection.close()
            def ranged(index):
                at=index*2311
                return server.request(url('/payload.bin'), headers={'Range':f'bytes={at}-{at+65535}'})[2]==payload[at:at+65536]
            with concurrent.futures.ThreadPoolExecutor(max_workers=24) as pool:
                check(all(pool.map(ranged, range(96))), '96 concurrent/overlapping HTTPS ranges preserve independent TLS state')
            urls=json.loads(server.request('/api/urls')[2])['urls']
            check(urls and all(u['url'].startswith('https://') for u in urls), 'All advertised URLs use HTTPS')
            check(server.request(urls[0]['qr'])[0]==200, 'HTTPS QR route works')
            fragment_handshake(server)
            code, _, _ = server.json_request('/api/admin/settings', {'hotlinkSeconds':30, 'loginPassword':'tls-test-pass'}, auth)
            check(code==200, 'Host settings accept HTTPS origin')
            code, headers, _ = server.json_request('/api/access/login', {'password':'tls-test-pass'}, {'Origin':auth['Origin']})
            check(code==200 and '; Secure' in headers['set-cookie'] and '; HttpOnly' in headers['set-cookie'], 'HTTPS login issues a Secure HttpOnly cookie')
            auth['Cookie']=headers['set-cookie'].split(';')[0]
            auth['Referer']=auth['Origin']+'/'
            check(server.request(url('/payload.bin'), headers=dict(auth, Range='bytes=0-99'))[0]==206, 'HTTPS referer accepted by hotlink guard')
            check(server.request(url('/payload.bin'), headers=dict(auth, Referer='http://127.0.0.1:'+str(server.port)+'/'))[0]==403, 'Mixed-scheme referer is rejected')
            code, headers, _=server.request('/view/site/index.html', headers=auth)
            check(code==302, 'Authenticated preview grant issued over TLS')
            code, preview_headers, body=server.request(headers['location'])
            check(code==200 and b'TLS preview' in body and 'https://' in preview_headers['content-security-policy'], 'Preview CSP uses HTTPS')
            check(server.request(headers['location'].rsplit('/',1)[0]+'/main.css')[0]==200, 'Related sandbox resources traverse TLS')
            code, _, admin=server.request('/api/admin/settings', headers=auth)
            mcp_token=json.loads(admin)['mcpToken']
            rpc={'jsonrpc':'2.0','id':1,'method':'tools/call','params':{'name':'download_url','arguments':{'path':'/payload.bin'}}}
            code, _, body=server.json_request('/mcp',rpc,{'Authorization':'Bearer '+mcp_token,'MCP-Protocol-Version':'2025-11-25'})
            result=json.loads(json.loads(body)['result']['content'][0]['text'])
            check(code==200 and result['url'].startswith('https://'), 'MCP returns HTTPS download URL')
        finally:
            server.close()
        # A real supplied RSA key is deliberately inside the share, with a hard
        # link alias and a native mount alias: no route may expose or replace it.
        private=root/'credentials';private.mkdir()
        cert_path,key_path=credentials(private)
        os.link(key_path, root/'alias.key')
        server=SecureServer(exe,root,('--tls-cert',str(cert_path),'--tls-key',str(key_path),'--write-mode','shared','--mount','/mapped='+str(private)))
        try:
            context=ssl.create_default_context(cafile=str(cert_path))
            check(server.request('/',context=context)[0]==200,'Configured RSA certificate is verified by a real TLS client')
            token=json.loads(server.request('/api/session')[2])['token'];auth={'X-HLS-Token':token}
            for name in ['/credentials/private.pem','/mapped/private.pem','/alias.key']:
                check(server.request(url(name))[0]==403,'Private key cannot be downloaded through '+name)
                check(server.request('/api/upload?path='+name,'PUT',auth,b'replace')[0]==403,'Private key cannot be replaced')
            check(server.request('/api/entry?path=/credentials&recursive=1','DELETE',auth)[0]==403,'Private-key parent cannot be recursively deleted')
            check(server.request('/api/rename?path=/credentials&to=/moved','POST',auth)[0]==403,'Private-key parent cannot be renamed')
        finally:
            server.close()
        ec_folder=root/'ec';ec_folder.mkdir();ec_cert,ec_key=credentials(ec_folder,True)
        server=SecureServer(exe,root,('--tls-cert',str(ec_cert),'--tls-key',str(ec_key)),mode='server')
        try:
            check(b'HTTPS site' in server.request('/',context=ssl.create_default_context(cafile=str(ec_cert)))[2], 'ECDSA certificate and Server mode')
        finally:
            server.close()
        # The same supplied credentials can be loaded in DER without PEM NUL
        # termination, and private keys never need an external crypto process.
        from cryptography import x509
        from cryptography.hazmat.backends import default_backend
        from cryptography.hazmat.primitives import serialization
        der_cert,der_key=ec_folder/'server.der',ec_folder/'private.der'
        der_cert.write_bytes(x509.load_pem_x509_certificate(ec_cert.read_bytes(),default_backend()).public_bytes(serialization.Encoding.DER))
        loaded=serialization.load_pem_private_key(ec_key.read_bytes(),None,default_backend())
        der_key.write_bytes(loaded.private_bytes(serialization.Encoding.DER,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
        server=SecureServer(exe,root,('--tls-cert',str(der_cert),'--tls-key',str(der_key)))
        try:
            check(server.request('/',context=ssl.create_default_context(cafile=str(ec_cert)))[0]==200,'DER certificate and private key are accepted')
        finally:
            server.close()

        # Slow or malformed peers consume only the separately bounded handshake
        # budget, then release it at the original absolute deadline.
        server=SecureServer(exe,root,('--tls-self-signed','--tls-handshakes','2','--tls-handshake-timeout','1'))
        sockets=[]
        try:
            time.sleep(.1)
            sockets=[socket.create_connection(('127.0.0.1',server.port),timeout=2) for _ in range(2)]
            for peer in sockets:peer.sendall(b'\x16')
            time.sleep(.1)
            try:
                server.request('/api/session')
                raise AssertionError('Handshake capacity was not enforced')
            except (OSError,ssl.SSLError,http.client.HTTPException):
                pass
            time.sleep(1.1)
            check(server.request('/api/session')[0]==200,'Handshake deadline releases stalled clients and capacity')
        finally:
            for peer in sockets:peer.close()
            server.close()
        wrong=subprocess.run([str(exe),'--https','--tls-cert',str(cert_path),'--tls-key',str(ec_key),'--root',str(root)],capture_output=True,timeout=20)
        check(wrong.returncode!=0 and b'mismatch' in wrong.stderr,'Mismatched credentials fail before accepting connections')
    print('HTTPS passed: TLS1.2/1.3, TLS1.1 rejection, ALPN/tickets/close-notify, verified RSA/ECDSA/SAN/DER, origins/cookies, streaming/chunked upload/gzip, 5 GiB Range, 96 ranges, Keep-Alive, preview/CSP/MCP/QR, fragmented TLS, protected key aliases, handshake bounds and credential validation')


if __name__=='__main__':
    run(pathlib.Path(sys.argv[1]).resolve())
