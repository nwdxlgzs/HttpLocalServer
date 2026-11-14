"""Bounded ZIP preview, adversarial metadata and private related resources."""
import concurrent.futures
import http.client
import io
import json
import pathlib
import struct
import sys
import tempfile
import urllib.parse
import zipfile
from integration import Server, check


def fixture(entries):
    data = io.BytesIO()
    with zipfile.ZipFile(data, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, value in entries:
            archive.writestr(name, value)
    return data.getvalue()


def call(server, target, method='GET', value=None, token='', cookie=''):
    headers = {'X-HLS-Token': token, 'Cookie': cookie}
    body = None if value is None else json.dumps(value).encode()
    connection = http.client.HTTPConnection('127.0.0.1', server.port, timeout=15)
    try:
        connection.request(method, target, body, headers)
        response = connection.getresponse()
        return response.status, dict((k.lower(), v) for k, v in response.getheaders()), response.read()
    finally:
        connection.close()


def run(exe):
    for mode in ('filetrans', 'server'):
        with tempfile.TemporaryDirectory(prefix='hls-zip-preview-') as folder:
            root = pathlib.Path(folder)
            good = fixture([('site/index.html', b'<link href="/site/a.css"><img src="pic.png"><h1>zip</h1>'),
                            ('site/a.css', b'body{color:red}'), ('site/pic.png', bytes(range(251))*10),
                            ('empty.txt', b''), ('cmap.bin',(pathlib.Path(__file__).resolve().parents[1]/'web/preview/vendor/pdf/cmaps/Adobe-Japan1-2.bcmap').read_bytes()), ('中文.txt', '内容'.encode())])
            (root/'good.zip').write_bytes(good)
            for i in range(5):
                (root/('other%d.zip'%i)).write_bytes(good)
            (root/'bomb.zip').write_bytes(fixture([('inflate.txt', b'A'*(2*1024*1024))]))
            (root/'traversal.zip').write_bytes(fixture([('../escape.txt', b'no')]))
            (root/'absolute.zip').write_bytes(fixture([('/escape.txt', b'no')]))
            (root/'collision.zip').write_bytes(fixture([('A.txt', b'one'), ('a.txt', b'two')]))
            (root/'folder-collision.zip').write_bytes(fixture([('a', b'one'), ('a/b.txt', b'two')]))
            # Claim an oversized ZIP64-style value without allocating its output.
            bad = bytearray(good); central = bad.index(b'PK\x01\x02')
            struct.pack_into('<I', bad, central+24, 0x7fffffff)
            (root/'size.zip').write_bytes(bad)
            bad = bytearray(good); second = bad.index(b'PK\x01\x02', central+4)
            struct.pack_into('<I', bad, second+42, 0)
            (root/'overlap.zip').write_bytes(bad)
            bad = bytearray(good); struct.pack_into('<H', bad, central+8, 1)
            (root/'encrypted.zip').write_bytes(bad)
            # Deflate claims a small output and its own CRC; extraction must
            # enforce actual output, rather than trusting its central directory.
            forged = bytearray(fixture([('forged.txt', b'Z'*500000)])); c=forged.index(b'PK\x01\x02')
            struct.pack_into('<I', forged, c+24, 10); struct.pack_into('<I', forged, 22, 10)
            (root/'forged.zip').write_bytes(forged)
            server = Server(exe, root, mode, ('--zip-preview', 'host', '--no-magika', '--state-dir', str(root/'private-state')))
            try:
                token = server.api('/api/session')['token']
                check(server.api('/api/session')['zipPreview']=='host', 'ZIP mode published')
                status, headers, body = call(server, '/api/zip/open', 'POST', {'path':'/good.zip'}, token)
                check(status==200, 'Open ordinary ZIP: '+repr(body)); grant=json.loads(body); key=grant['id']; base=grant['base']
                cookie=headers.get('set-cookie','').split(';')[0]
                status,_,other=call(server,'/api/zip/open','POST',{'path':'/good.zip'},token,cookie);check(status==200,'Independent second preview');other=json.loads(other);check(other['id']!=key,'Each ZIP tab owns a distinct grant');check(call(server,'/api/zip/close?id='+other['id'],'POST',{},token,cookie)[0]==200,'Close second tab independently')
                listing=server.api('/api/zip/list?id='+key+'&path=/site')
                check(len(listing['entries'])==3, 'ZIP virtual directory')
                status, headers, body=server.request(base+'site/index.html')
                check(status==200 and (base+'site/a.css').encode() in body, 'ZIP root URLs remain inside capability')
                check("object-src 'none'" in headers['content-security-policy'] and base in headers['content-security-policy'], 'Scoped sandbox policy')
                status, _, body=server.request(base+'site/a.css')
                check(status==200 and body==b'body{color:red}', 'Related ZIP resource')
                status,_,body=server.request(base+'cmap.bin');check(status==200 and body==(pathlib.Path(__file__).resolve().parents[1]/'web/preview/vendor/pdf/cmaps/Adobe-Japan1-2.bcmap').read_bytes(),'Final DEFLATE code lookahead does not reject valid file')
                status, headers, body=server.request(base+'site/pic.png', headers={'Range':'bytes=9-21'})
                check(status==206 and body==(bytes(range(251))*10)[9:22], 'ZIP resource Range')
                check(server.request(base+'../config.json')[0]>=400, 'Traversal rejected')
                for name in ('bomb','traversal','absolute','collision','folder-collision','size','overlap','encrypted'):
                    status, _, body=call(server, '/api/zip/open', 'POST', {'path':'/'+name+'.zip'}, token, cookie)
                    check(status==422, name+' rejected: '+repr((status,body)))
                status, _, body=call(server, '/api/zip/open', 'POST', {'path':'/forged.zip'}, token, cookie)
                check(status==200, 'Forged stream metadata inspected without expansion')
                forged_grant=json.loads(body)
                check(server.request(forged_grant['base']+'forged.txt')[0]==422, 'Actual inflated output bound')
                check(not list((root/'private-state').glob('.hls-upload-*')), 'Failed extraction never published')
                status, _, _=call(server, '/api/admin/settings', 'POST', {'zipPreview':'client'}, token, cookie)
                check(status==200 and server.request(base+'site/a.css')[0]==403, 'Host preview revoked immediately')
                check(server.api('/api/session')['zipPreview']=='client', 'Client mode configured')
                check(call(server, '/api/zip/open','POST',{'path':'/good.zip'},token,cookie)[0]==403, 'Client mode never inflates on host')
                status, _, _=call(server, '/api/admin/settings','POST',{'zipPreview':'off'},token,cookie)
                check(status==200, 'ZIP disabled dynamically')
            finally:
                server.close()
    print('ZIP preview passed: both modes, bounds, traversal, overlap, forged output, related resources, Range and revocation')


if __name__=='__main__':
    run(str(pathlib.Path(sys.argv[1]).resolve()))
