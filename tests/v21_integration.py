"""Magika integration, recent identification cache and restricted previews."""
import concurrent.futures
import json
import pathlib
import sys
import tempfile
import urllib.parse
from integration import Server, check
from v2_integration import route, send

def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-v21-') as tmp:
        root=pathlib.Path(tmp)
        samples={
            'source':b'#!/usr/bin/env python3\nimport pathlib\ndef square(value):\n    return value * value\nprint(square(9))\n'*30,
            'settings':json.dumps({'name':'example','items':list(range(100)),'enabled':True}).encode(),
            'code.cpp':b'#include <iostream>\n#include <vector>\nint main(){std::vector<int> v{1,2,3};for(auto i:v)std::cout<<i<<std::endl;}\n'*30,
            'notes.txt':'文本预览\n'.encode()*100000,
            'image.svg':b'<svg xmlns="http://www.w3.org/2000/svg"><rect width="100" height="100" fill="blue"/></svg>',
            'sound.wav':b'RIFF'+(65536).to_bytes(4,'little')+b'WAVE'+bytes(range(256))*256,
        }
        for name,data in samples.items():(root/name).write_bytes(data)
        srv=Server(exe,root,extra=('--write-mode','shared','--allow-preview'))
        try:
            check(srv.api('/api/status')['magika']['inferences']==0,'Model is lazy, no scan at startup/list')
            def identify(_):return srv.api(route('type',path='/source'))
            with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:rows=list(pool.map(identify,range(20)))
            check(all(r['extension']=='py' and r['confidence']=='magika' and r['score']>.5 for r in rows),'Extensionless Python identified')
            check(srv.api('/api/status')['magika']['inferences']==1,'Simultaneous identical recognition is coalesced')
            check(rows[0]['mismatch'] and rows[0]['suggestedName']=='source.py','Inferred suffix suggestion')
            for _ in range(10):identify(0)
            check(srv.api('/api/status')['magika']['inferences']==1,'Recent version cache avoids inference')
            check(srv.api(route('type',path='/settings'))['extension']=='json','Extensionless JSON identified')
            check(srv.api(route('type',path='/code.cpp'))['category']=='code','Source file category remains code')
            (root/'source').write_bytes(samples['settings'])
            check(srv.api(route('type',path='/source'))['extension']=='json','File version invalidates recognition')
            status,h,body=srv.request(route('preview-text',path='/notes.txt'))
            check(status==200 and 256*1024-3<=len(body)<=256*1024 and h['x-hls-preview-truncated']=='1','TXT preview bounded to 256 KiB')
            check(srv.request(route('preview-text',path='/code.cpp'))[0]==415,'Other source files have no text preview')
            check(srv.request(route('preview-text',path='/settings'))[0]==415,'Extensionless text not implicitly previewed')
            status,h,body=srv.request('/view/image.svg')
            check(status==200 and h['content-type'].startswith('image/svg+xml') and h['content-security-policy'].startswith('sandbox'),'Sandboxed SVG inline preview')
            media_ticket=srv.api(route('download-ticket',path='/sound.wav'))['url']
            media_version=urllib.parse.parse_qs(urllib.parse.urlsplit(media_ticket).query)['version'][0]
            media_url='/view/sound.wav?version='+urllib.parse.quote(media_version)
            status,h,body=srv.request(media_url,headers={'Range':'bytes=2048-4095'})
            check(status==206 and body==samples['sound.wav'][2048:4096] and h['content-type'].startswith('audio/wav') and h['accept-ranges']=='bytes','Media preview streams byte ranges with the correct MIME')
            check('content-disposition' not in h,'Preview media is inline')
            status,h,body=srv.request(media_url,method='HEAD')
            check(status==200 and not body and int(h['content-length'])==len(samples['sound.wav']),'Media metadata uses HEAD without body')
            (root/'sound.wav').write_bytes(samples['sound.wav']+b'changed')
            check(srv.request(media_url,headers={'Range':'bytes=4096-'})[0]==409,'Preview cannot splice a different file version')
            token=srv.api('/api/session')['token']
            status,_,body=send(srv,'PUT',route('upload',path='/uploaded'),samples['code.cpp'],{'X-HLS-Token':token})
            check(status==201 and json.loads(body)['type']['confidence']=='magika','Upload invokes model for code')
            print('v2.1 integration passed: model, coalescing, cache, version, extension suggestions and bounded preview')
        finally:srv.close()
        disabled=Server(exe,root,extra=('--no-magika','--no-preview'))
        try:
            row=disabled.api(route('type',path='/settings'))
            check(row['confidence']!='magika' and disabled.api('/api/status')['magika']['inferences']==0,'Model can be disabled')
            check(disabled.request(route('preview-text',path='/notes.txt'))[0]==403,'TXT preview needs preview permission')
        finally:disabled.close()
if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
