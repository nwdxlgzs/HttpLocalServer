"""Compare two binaries with the same short loopback script/ZIP workload."""
import argparse
import concurrent.futures
import datetime
import hashlib
import http.client
import json
import os
import pathlib
import platform
import threading
import tempfile
import time
import zipfile

from integration import Server, check


def measure(server, target, expected, clients, seconds):
    for _ in range(8):
        status, _, body = server.request(target)
        check(status == 200 and body == expected, 'Warm-up integrity')
    barrier, deadline = threading.Barrier(clients + 1), {}
    def worker():
        conn = http.client.HTTPConnection('127.0.0.1', server.port, timeout=20)
        rows = []
        barrier.wait()
        try:
            while time.perf_counter() < deadline['end']:
                started = time.perf_counter()
                try:
                    conn.request('GET', target)
                    response = conn.getresponse()
                    body = response.read()
                    status = response.status
                    if status == 200 and body != expected:
                        status = 'integrity-error'
                except Exception as error:
                    status = type(error).__name__
                    conn.close()
                rows.append((str(status), (time.perf_counter() - started) * 1000))
        finally:
            conn.close()
        return rows
    with concurrent.futures.ThreadPoolExecutor(max_workers=clients) as pool:
        futures = [pool.submit(worker) for _ in range(clients)]
        started = time.perf_counter()
        deadline['end'] = started + seconds
        barrier.wait()
        rows = [row for future in futures for row in future.result()]
        elapsed = time.perf_counter() - started
    counts = {}
    for status, _ in rows:
        counts[status] = counts.get(status, 0) + 1
    latencies = sorted(ms for status, ms in rows if status == '200')
    return {'clients':clients, 'seconds':round(elapsed, 3), 'requests':len(rows), 'statuses':counts,
            'successfulRPS':round(counts.get('200', 0) / elapsed, 2),
            'payloadMiBPerSecond':round(counts.get('200', 0) * len(expected) / elapsed / (1 << 20), 2),
            'successP95ms':round(latencies[int((len(latencies)-1)*.95)], 2) if latencies else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', required=True, type=pathlib.Path)
    parser.add_argument('--after', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--seconds', type=float, default=3)
    options = parser.parse_args()
    check(1 <= options.seconds <= 30, 'Duration must be 1-30 seconds per case')
    small = 'function onRequest(){return {body:"ok"};}'
    many = '\n'.join('function unused%d(){return %d;}' % (i, i) for i in range(2048)) + '\n' + small
    payload = os.urandom(450000)
    report = {'measuredAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'environment':{'platform':platform.platform(), 'logicalCPUs':os.cpu_count(),
                             'network':'local IPv4 loopback; HTTP; Python threaded keep-alive client'},
              'notes':['Short before/after diagnostic; not a capacity or sustained-load guarantee',
                       'Fixed 4 IO/8 metadata workers, isolated concurrency 8, 500ms script deadline, no Magika',
                       'Per-worker continuous requests, no failed-request retry, successful RPS excludes all errors',
                       'ZIP resource is warmed before measurements; ordinary script creates a fresh realm every time',
                       'Each case measured once; Python/network/OS overhead contributes to observed performance'],
              'workloads':{'isolated-small':{'sourceBytes':len(small)},
                           'isolated-2048-functions':{'sourceBytes':len(many)},
                           'workspace-static':{'bodyBytes':len(payload)}}, 'binaries':[], 'results':[]}
    with tempfile.TemporaryDirectory(prefix='hls-script-performance-') as folder:
        root = pathlib.Path(folder)
        app = root / 'app.zip'
        with zipfile.ZipFile(app, 'w', zipfile.ZIP_DEFLATED) as archive:
            archive.writestr('server.qjs', 'export function onRequest(){return {};}')
            archive.writestr('public/data.bin', payload)
        for label, exe in [('before', options.before.resolve(strict=True)), ('after', options.after.resolve(strict=True))]:
            report['binaries'].append({'label':label, 'path':str(exe), 'sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
            for name, source in [('isolated-small',small), ('isolated-2048-functions',many), ('workspace-static',None)]:
                flags = ['--no-magika', '--script-timeout-ms','500', '--script-concurrency','8', '--metadata-threads','8']
                if source is None:
                    flags += ['--workspace-app',str(app)]
                else:
                    script = root / 'hook.js'
                    script.write_text(source, encoding='utf-8')
                    flags += ['--script',str(script)]
                server = Server(exe, root, 'server', flags)
                try:
                    for clients in (1, 8):
                        value = measure(server, '/data.bin' if source is None else '/probe',
                                        payload if source is None else b'ok', clients, options.seconds)
                        value.update({'binary':label,'scenario':name})
                        report['results'].append(value)
                        print(json.dumps(value), flush=True)
                finally:
                    server.close()
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
