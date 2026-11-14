"""Live settings, MCP transport, compressed resources and document previews."""
import concurrent.futures
import gzip
import json
import pathlib
import sys
import tempfile
from integration import Server,check
from v2_integration import send

def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-v22-') as directory:
        root=pathlib.Path(directory)
        (root/'readme.md').write_text('# Shared notes\n\n**Markdown** [link](https://example.com)\n',encoding='utf-8')
        (root/'data.json').write_text(json.dumps({'tree':{'items':list(range(250))}}),encoding='utf-8')
        (root/'file.txt').write_text('plain text',encoding='utf-8')
        (root/'assets').mkdir();(root/'site').mkdir()
        (root/'site/index.html').write_text('<!doctype html><link rel="stylesheet" href="/assets/style.css"><script src="/assets/app.js"></script>',encoding='utf-8')
        (root/'assets/style.css').write_text('body{color:rgb(12,34,56)}',encoding='utf-8')
        (root/'assets/app.js').write_text('fetch("/assets/data.json").then(function(r){return r.json()}).then(function(d){document.body.textContent=d.message})',encoding='utf-8')
        (root/'assets/data.json').write_text('{"message":"loaded related resource"}',encoding='utf-8')
        server=Server(exe,root,extra=('--mcp',))
        try:
            token=server.api('/api/session')['token']
            status,h,body=server.request('/')
            check(status==200 and b'zipRecord' in body and b'markdownDocument' in body,'Uncompressed fallback expands embedded source')
            status,h,packed=server.request('/',headers={'Accept-Encoding':'gzip'})
            check(status==200 and h['content-encoding']=='gzip' and gzip.decompress(packed)==body,'Embedded page is gzip and both responses agree')
            check(len(packed)<len(body)//2 and h['vary']=='Accept-Encoding','Compressed resource and cache negotiation')
            check(server.request('/api/preview-text?path=/readme.md')[0]==200,'Preview enabled by default')
            status,_,_=send(server,'POST','/api/admin/settings',b'{}',{'Content-Type':'application/json'})
            check(status==403,'Admin mutations require CSRF token')
            status,_,_=send(server,'GET','/api/admin/settings',headers={'Origin':'http://evil.test'})
            check(status==403,'Admin panel rejects foreign Origin')
            admin=server.api('/api/admin/settings')
            check(admin['ipLimit']==256 and admin['writeMode']=='readonly','CLI state initializes live settings')
            check(admin['previewAllowed'] and not admin['thumbnailsEnabled'],'Preview defaults on and thumbnails default off')
            for route in ['/view/site/index.html','/view/assets/style.css','/assets/style.css','/assets/app.js','/assets/data.json']:
                status,headers,resource=server.request(route,headers={'Origin':'null'})
                check(status==200 and resource and headers.get('access-control-allow-origin')=='null','Sandbox related assets, including root-relative paths: '+route)
                check(headers['content-security-policy'].startswith('sandbox allow-scripts;'),'Preview documents keep opaque sandbox origin')
            check(server.request('/api/session',headers={'Origin':'null'})[0]==403,'Preview scripts cannot retrieve management token')
            check('access-control-allow-origin' not in server.request('/api/status')[1],'CORS grant never extends to management APIs')
            for route in ['/view/%2e%2e/outside','/assets/%2e%2e/%2e%2e/outside','/view/file.txt%3astream']:
                check(server.request(route)[0]>=400,'Static preview path attack rejected: '+route)
            status,headers,viewer=server.request('/preview?path=/file.txt')
            check(status==200 and b'startStandalone' in viewer and "media-src 'self'" in headers['content-security-policy'],'Dedicated new-window viewer supports native media')
            updates={'title':'测试共享','previewAllowed':True,'writeMode':'shared','ipLimit':0,'ownerLimit':300,'maxUploadMiB':2,'announcement':'Download the marked files.','magikaEnabled':False}
            def update(values):return send(server,'POST','/api/admin/settings',json.dumps(values).encode(),{'X-HLS-Token':token,'Content-Type':'application/json'})
            check(update(updates)[0]==200,'Apply supported settings atomically')
            check(server.api('/api/site')['announcement']=='Download the marked files.','Public announcement is available')
            check('mcpToken' not in server.api('/api/site'),'Public data never includes MCP token')
            check(server.api('/api/session')['writeMode']=='shared' and server.api('/api/session')['previewAllowed'],'Browser permission state updates without restart')
            check(server.request('/api/preview-text?path=/readme.md')[0]==200 and server.request('/api/preview-text?path=/data.json')[0]==200,'Markdown and JSON bounded text endpoints')
            check(update({'ipLimit':-1,'title':'should not commit'})[0]==400,'Invalid settings rejected')
            check(server.api('/api/site')['title']=='测试共享','Validation has no partial updates')
            status,_,_=send(server,'POST','/api/mkdir?path=/live',headers={'X-HLS-Token':token})
            check(status==201,'Dynamic shared write permission enforced')
            check(update({'writeMode':'readonly'})[0]==200,'Switch back to readonly')
            check(update({'thumbnailsEnabled':True})[0]==200 and server.api('/api/site')['thumbnailsEnabled'] and server.api('/api/session')['thumbnailsEnabled'],'Host thumbnail policy is published to clients')
            check(update({'thumbnailsEnabled':False,'previewAllowed':False})[0]==200,'Disable previews and thumbnails dynamically')
            for route in ['/preview?path=/file.txt','/view/file.txt','/assets/data.json','/api/preview-text?path=/file.txt']:
                check(server.request(route)[0]==403,'Disabled preview policy blocks all preview routes')
            check(update({'previewAllowed':True})[0]==200,'Preview policy can be restored')
            check(send(server,'POST','/api/mkdir?path=/denied',headers={'X-HLS-Token':token})[0]==403,'Dynamic readonly permission enforced')
            headers={'Content-Type':'application/json','Authorization':'Bearer '+admin['mcpToken'],'MCP-Protocol-Version':'2025-11-25'}
            def rpc(method,params=None,identity=1,custom=None):
                data={'jsonrpc':'2.0','method':method,'id':identity}
                if params is not None:data['params']=params
                result=send(server,'POST','/mcp',json.dumps(data).encode(),custom or headers)
                return result[0],json.loads(result[2])
            check(rpc('initialize',{'protocolVersion':'2025-11-25'})[1]['result']['protocolVersion']=='2025-11-25','MCP initialize')
            check(send(server,'POST','/mcp',b'{}',{'Content-Type':'application/json'})[0]==401,'MCP requires its distinct Bearer token')
            check(rpc('ping',custom=dict(headers,Origin='http://evil.test'))[0]==403,'MCP rejects foreign Origin')
            tools=rpc('tools/list')[1]['result']['tools']
            check(len(tools)==8 and all(tool['annotations']['readOnlyHint'] for tool in tools),'MCP exposes read-only tools and schemas')
            listing=rpc('tools/call',{'name':'list_files','arguments':{'path':'/','limit':2}})[1]['result']
            check(len(json.loads(listing['content'][0]['text'])['entries'])==2,'MCP directory pagination')
            text=rpc('tools/call',{'name':'read_text','arguments':{'path':'/readme.md'}})[1]['result']
            check('# Shared notes' in text['content'][0]['text'],'MCP bounded text reading')
            traversal=rpc('tools/call',{'name':'read_text','arguments':{'path':'/../escape'}})[1]
            check('error' in traversal or traversal.get('result',{}).get('isError'),'MCP path traversal rejected')
            check(rpc('unknown')[1]['error']['code']==-32601,'MCP unknown method reports JSON-RPC error')
            check(send(server,'GET','/mcp',headers=headers)[0]==405,'Stateless endpoint has no SSE GET stream')
            check(send(server,'POST','/mcp',b'{',headers)[0]==400,'MCP parse error')
            check(send(server,'POST','/api/admin/settings',b' '*65537,{'X-HLS-Token':token})[0]==413,'Control body is bounded to 64 KiB')
            with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
                futures=[pool.submit(update,{'announcement':str(i)}) for i in range(20)]
                check(all(job.result()[0]==200 for job in futures),'Concurrent settings snapshots publish safely')
            check(update({'mcpEnabled':False})[0]==200 and send(server,'POST','/mcp',b'{}',headers)[0]==404,'MCP can be disabled live')
            print('v2.2 HTTP passed: compressed assets, live settings, announcements, documents and MCP')
        finally:server.close()
        disabled=Server(exe,root,extra=('--no-preview','--lazy-thumbnails'))
        try:
            check(not disabled.api('/api/session')['previewAllowed'] and disabled.api('/api/site')['thumbnailsEnabled'],'CLI accepts preview-disable and thumbnail policy')
            check(disabled.request('/view/file.txt')[0]==403,'CLI preview disable is enforced')
        finally:disabled.close()
        no_thumbnails=Server(exe,root,extra=('--lazy-thumbnails','--no-lazy-thumbnails'))
        try:
            check(not no_thumbnails.api('/api/site')['thumbnailsEnabled'],'Explicit no-lazy-thumbnails overrides earlier enable flag')
        finally:no_thumbnails.close()

if __name__=='__main__':run(pathlib.Path(sys.argv[1]).resolve())
