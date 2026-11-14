"""Repeatable local mixed-load probe; not a physical LAN bandwidth benchmark."""
import argparse
import concurrent.futures
import hashlib
import http.client
import json
import pathlib
import tempfile
import time
from integration import Server, check, url


def run(executable, output):
    with tempfile.TemporaryDirectory(prefix='hls-load-') as temporary:
        root = pathlib.Path(temporary)
        large = bytes(range(256)) * 32768
        expected = hashlib.sha256(large).hexdigest()
        (root / 'large.bin').write_bytes(large)
        for index in range(128):
            (root / f'small-{index:03}.txt').write_bytes((f'file-{index}:' * 1024).encode())
        server = Server(executable, root, extra=('--max-transfers', '256', '--buffer-mb', '128', '--cache-idle', '3'))
        try:
            def request(index):
                started = time.perf_counter()
                if index % 8 == 0:
                    path = '/large.bin'
                    digest = hashlib.sha256()
                    connection = http.client.HTTPConnection('127.0.0.1', server.port, timeout=30)
                    connection.request('GET', url(path))
                    response = connection.getresponse()
                    check(response.status == 200, 'Large download accepted')
                    size = 0
                    while True:
                        chunk = response.read(65536)
                        if not chunk:
                            break
                        size += len(chunk)
                        digest.update(chunk)
                    connection.close()
                    check(digest.hexdigest() == expected, 'Large download SHA-256')
                else:
                    path = f'/small-{index % 128:03}.txt'
                    status, _, data = server.request(url(path))
                    check(status == 200 and data == (root / path[1:]).read_bytes(), 'Small-file integrity')
                    size = len(data)
                return time.perf_counter() - started, size
            started = time.perf_counter()
            with concurrent.futures.ThreadPoolExecutor(max_workers=96) as pool:
                samples = list(pool.map(request, range(768)))
            elapsed = time.perf_counter() - started
            status = server.api('/api/status')
            latencies = sorted(sample[0] for sample in samples)
            report = {'executable': str(executable), 'clients': 96, 'requests': len(samples), 'largeFiles': 96, 'smallFiles': 672, 'integrityFailures': 0, 'seconds': round(elapsed, 3), 'payloadBytes': sum(sample[1] for sample in samples), 'payloadMiBPerSecond': round(sum(sample[1] for sample in samples) / elapsed / 1024**2, 2), 'p50Milliseconds': round(latencies[len(latencies)//2] * 1000, 2), 'p95Milliseconds': round(latencies[int(len(latencies)*.95)] * 1000, 2), 'statusAfterLoad': status}
            check(status['transferErrors'] == 0 and status['rejected'] == 0, 'No overload or transfer errors at configured capacity')
            time.sleep(4.5)
            report['cacheAfterCooling'] = server.api('/api/status')['cache']
            check(report['cacheAfterCooling']['allocatedBytes'] == 0, 'Loaded cache returns all content pages after idle')
            text = json.dumps(report, ensure_ascii=False, indent=2)
            print(text)
            if output:
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(text + '\n', encoding='utf-8')
        finally:
            server.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    run(args.executable.resolve(), args.output)
