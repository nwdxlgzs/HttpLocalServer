"""Embedded icon routes, legacy ICO frames, caching and static-site precedence."""
import gzip
import pathlib
import struct
import sys
import tempfile
from integration import Server, check

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-brand-') as folder:
        root = pathlib.Path(folder)
        server = Server(exe, root, extra=('--no-magika',))
        try:
            page = server.request('/')[2]
            for href in (b'/favicon.ico?v=1', b'/favicon.svg?v=1', b'/apple-touch-icon.png?v=1'):
                check(href in page, 'Page explicitly declares the embedded icon: ' + str(href))
            for route, file, mime in (('/favicon.ico', 'favicon.ico', 'image/x-icon'),
                                      ('/favicon.svg', 'logo.svg', 'image/svg+xml'),
                                      ('/apple-touch-icon.png', 'apple-touch-icon.png', 'image/png')):
                expected = (ROOT / 'resources/branding' / file).read_bytes()
                status, headers, body = server.request(route + '?v=1')
                check(status == 200 and body == expected and headers['content-type'] == mime,
                      'Embedded identity icon matches original asset')
                check(headers['x-content-type-options'] == 'nosniff' and 'max-age=86400' in headers['cache-control'], 'Icon MIME and cache headers')
                status, head, body = server.request(route, 'HEAD')
                check(status == 200 and not body and int(head['content-length']) == len(expected), 'HEAD retains original icon length')
                status, packed_headers, packed = server.request(route, headers={'Accept-Encoding': 'gzip'})
                check(status == 200 and packed_headers['content-encoding'] == 'gzip' and gzip.decompress(packed) == expected, 'Compressed icon matches original graphic')
                check(packed_headers['vary'] == 'Accept-Encoding' and packed_headers['etag'] == headers['etag'], 'Compressed representation uses the same weak validator')
                status, _, body = server.request(route, headers={'If-None-Match': headers['etag'], 'Accept-Encoding': 'gzip'})
                check(status == 304 and not body, 'Conditional icon retrieval does not transfer another body')
                check(server.request(route, headers={'Accept-Encoding': 'gzip;q=0'})[2] == expected, 'Explicit gzip refusal returns an identity icon')

            ico = (ROOT / 'resources/branding/favicon.ico').read_bytes()
            reserved, kind, count = struct.unpack_from('<HHH', ico)
            check((reserved, kind, count) == (0, 1, 7), 'ICO contains seven standard sizes')
            sizes = []
            for i in range(count):
                width, height, _, _, planes, bits, length, offset = struct.unpack_from('<BBBBHHII', ico, 6 + i * 16)
                size = width or 256
                check((height or 256) == size and planes == 1 and bits == 32 and offset + length <= len(ico), 'Valid 32-bit square ICO frame bounds')
                check(ico[offset:offset+4] == struct.pack('<I', 40) if size <= 48 else ico[offset:offset+8] == b'\x89PNG\r\n\x1a\n', 'IE11 small frames are DIB; large frames use PNG')
                sizes.append(size)
            check(sizes == [16, 24, 32, 48, 64, 128, 256], 'Expected Windows/desktop icon sizes')
        finally:
            server.close()
        own = b'custom hosted site icon'
        (root / 'favicon.ico').write_bytes(own)
        server = Server(exe, root, mode='server', extra=('--no-magika',))
        try:
            check(server.request('/favicon.ico')[2] == own, 'Server mode retains the hosted site icon')
        finally:
            server.close()
    print('Branding HTTP passed: embedded SVG/ICO/touch icons, gzip, HEAD, 304, ICO frames and hosted-site precedence')


if __name__ == '__main__':
    run(pathlib.Path(sys.argv[1]).resolve())
