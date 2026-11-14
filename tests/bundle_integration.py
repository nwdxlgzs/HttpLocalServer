"""Verify the distributed executable contains the exact current web bundle."""
import gzip
import pathlib
import re
import subprocess
import sys
import tempfile
from integration import Server, check


def run(executable):
    repository = pathlib.Path(__file__).resolve().parents[1]
    expected_version = re.search(r'project\(HttpLocalServer VERSION ([0-9.]+)', (repository/'CMakeLists.txt').read_text()).group(1)
    for option in ('--version', '--help'):
        text = subprocess.check_output([executable, option]).decode('utf-8')
        check(text.startswith('HttpLocalServer '+expected_version), 'Consistent CLI version '+option)
    packed = (repository / 'resources/web.gz').read_bytes()
    raw = gzip.decompress(packed)
    for mode in ('filetrans', 'server'):
        with tempfile.TemporaryDirectory(prefix='hls-bundle-') as root:
            server = Server(executable, pathlib.Path(root), mode, ('--no-magika',))
            try:
                check(server.api('/api/status')['version'] == expected_version, 'Consistent status version')
                target = '/' if mode == 'filetrans' else '/_hls/'
                status, headers, body = server.request(target, headers={'Accept-Encoding': 'gzip'})
                check(headers.get('server') == 'HttpLocalServer/'+expected_version, 'Consistent HTTP version header')
                check(status == 200 and headers.get('content-encoding') == 'gzip' and body == packed,
                      'Exact compressed embedded assets: ' + mode)
                status, _, body = server.request(target)
                check(status == 200 and body == raw, 'Exact identity assets: ' + mode)
            finally:
                server.close()
    print('bundle integration passed: both modes serve exact current gzip and identity web assets')


if __name__ == '__main__':
    run(str(pathlib.Path(sys.argv[1]).resolve()))
