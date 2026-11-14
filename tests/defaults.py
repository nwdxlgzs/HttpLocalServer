"""Verify the zero-argument CLI defaults; requires localhost:8000 to be unused."""
import http.client
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time

executable = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='hls-defaults-') as root:
    (pathlib.Path(root) / 'default.txt').write_bytes(b'default-root')
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen([str(executable)], cwd=root, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        try:
            for _ in range(100):
                try:
                    connection = http.client.HTTPConnection('127.0.0.1', 8000, timeout=1)
                    connection.request('GET', '/api/status')
                    response = connection.getresponse()
                    status = json.loads(response.read())
                    connection.close()
                    assert response.status == 200 and status['mode'] == 'filetrans'
                    break
                except OSError:
                    time.sleep(.05)
            else:
                raise RuntimeError('Default port did not become ready')
            connection = http.client.HTTPConnection('127.0.0.1', 8000, timeout=2)
            connection.request('GET', '/download?path=%2Fdefault.txt')
            response = connection.getresponse()
            assert response.status == 200 and response.read() == b'default-root'
            connection.close()
            print('Zero-argument defaults passed: filetrans, 0.0.0.0:8000, current directory')
        finally:
            process.terminate()
            process.wait(timeout=10)
            if os.name != 'nt':
                assert process.returncode == 0
        log.seek(0)
        output = log.read().decode('utf-8', errors='replace')
        assert 'Listen: 0.0.0.0:8000' in output, output
