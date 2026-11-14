"""Wire timing and concurrency ceilings, with live policy changes in both modes."""
import concurrent.futures
import http.client
import pathlib
import tempfile
import time
import sys
from integration import Server, check
from durable_integration import send


def run(executable):
    for mode in ('filetrans','server'):
        with tempfile.TemporaryDirectory(prefix='hls-traffic-') as folder:
            root=pathlib.Path(folder);data=bytes(range(256))*1024
            (root/'bytes.bin').write_bytes(data)
            server=Server(executable,root,mode,('--no-magika','--no-compression','--write-mode','shared','--download-kbps','256','--ip-download-kbps','128'))
            try:
                token=server.api('/api/session')['token']
                start=time.monotonic();check(server.request('/download?path=/bytes.bin')[2]==data,'Throttled bytes intact')
                elapsed=time.monotonic()-start;check(elapsed>=1.75 and elapsed<8,'Per-IP send rate: '+str(elapsed))
                start=time.monotonic()
                with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                    results=list(pool.map(lambda _:server.request('/download?path=/bytes.bin'),range(2)))
                elapsed=time.monotonic()-start
                check(all(r[0]==200 and r[2]==data for r in results),'Concurrent downloads intact')
                check(elapsed>=3.5 and elapsed<12,'Multiple sockets share one per-IP allowance: '+str(elapsed))
                check(send(server,'/api/admin/settings','POST',{'ipTransfers':1,'ipDownloadKiB':64},token)[0]==200,'Live concurrency update')
                connection=http.client.HTTPConnection('127.0.0.1',server.port,timeout=15)
                connection.request('GET','/download?path=/bytes.bin');response=connection.getresponse();check(response.status==200,'First transfer admitted')
                check(server.request('/download?path=/bytes.bin')[0]==429,'Second transfer gets explicit per-IP concurrency response')
                check(send(server,'/api/admin/settings','POST',{'ipDownloadKiB':0,'downloadKiB':0,'ipTransfers':0,'uploadKiB':256,'ipUploadKiB':128},token)[0]==200,'Disable send limits without stale scheduling debt')
                check(response.read()==data,'Existing transfer finishes after live change');connection.close()
                start=time.monotonic();status,_,_=send(server,'/api/upload?path=/received.bin','PUT',data,token)
                check(status==201 and (root/'received.bin').read_bytes()==data,'Throttled upload intact')
                check(1.75<=time.monotonic()-start<5,'Receive limit applies to native upload without tiny-read timer penalties')
                code,up,_=send(server,'/api/resume/create','POST',{'path':'/chunked.bin','size':1048576,'fingerprint':'throttle-fixture'},token)
                check(code==201,'Admit throttled resumable upload')
                start=time.monotonic()
                code,_,_=send(server,'/api/resume/chunk?id='+up['id']+'&key='+up['key']+'&offset=0','POST',b'x'*1048576,token)
                check(code==200 and 7.5<=time.monotonic()-start<12,'Control-body upload aggregates tiny reads before async pacing')
                status=server.api('/api/status');check(status['receivedBytes']>=len(data),'Receive metrics')
                check(send(server,'/api/admin/settings','POST',{'uploadKiB':0,'ipUploadKiB':0,'downloadKiB':256},token)[0]==200,'Global fairness policy')
                def remote(ip):
                    c=http.client.HTTPConnection('127.0.0.1',server.port,timeout=15,source_address=(ip,0))
                    try:
                        c.request('GET','/download?path=/bytes.bin');r=c.getresponse();return r.status,r.read()
                    finally:c.close()
                started=time.monotonic()
                with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                    outputs=list(pool.map(remote,['127.0.0.2','127.0.0.3']))
                check(all(code==200 and body==data for code,body in outputs),'Distinct IPs both progress')
                check(1.75<=time.monotonic()-started<10,'Two addresses fairly share the global ceiling')
                check(send(server,'/api/admin/settings','POST',{'downloadKiB':1},token)[0]==200,'Very slow limit accepted')
                c=http.client.HTTPConnection('127.0.0.1',server.port,timeout=15)
                c.request('GET','/download?path=/bytes.bin');r=c.getresponse();c.close()
                check(send(server,'/api/admin/settings','POST',{'downloadKiB':256},token)[0]==200,'Change limit after cancellation')
                started=time.monotonic();check(server.request('/download?path=/bytes.bin')[2]==data,'Cancelled slow transfer does not poison a later transfer')
                check(time.monotonic()-started<5,'Cancellation leaves no future token debt')
            finally:
                server.close()
    print('traffic tests passed: both modes, aggregate per-IP schedules, live limits, transfer cap and upload backpressure')


if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
