"""MCP 2026-07-28 wire contract and legacy interoperability over HTTP/TLS."""
import base64,json,pathlib,sys,tempfile
from integration import Server,check
from wasm_app_integration import request

def run(exe,https=False):
    for mode in ['filetrans','server']:
        with tempfile.TemporaryDirectory(prefix='hls-mcp-current-') as temp:
            root=pathlib.Path(temp);(root/'hello.txt').write_text('你好',encoding='utf-8')
            server=Server(exe,root,mode,('--mcp','--no-magika','--strict-host',*(['--https','--tls-self-signed'] if https else [])))
            try:
                status,_,body=request(server,'/api/admin/settings',https=https);check(status==200,'host settings');token=json.loads(body)['mcpToken']
                def call(method,params=None,modern=True,override=None,remote=False):
                    headers={'Content-Type':'application/json','Authorization':'Bearer '+token,'MCP-Protocol-Version':'2026-07-28' if modern else '2025-11-25'}
                    payload=dict(params or {})
                    if modern:
                        payload['_meta']={'io.modelcontextprotocol/protocolVersion':'2026-07-28','io.modelcontextprotocol/clientCapabilities':{}}
                        headers['Mcp-Method']=method
                        if method=='tools/call':headers['Mcp-Name']=payload.get('name','')
                    if override:headers.update(override)
                    data=json.dumps({'jsonrpc':'2.0','id':1,'method':method,'params':payload}).encode()
                    code,_,raw=request(server,'/mcp','POST',data,headers,remote,https)
                    return code,json.loads(raw)
                code,reply=call('server/discover');check(code==200 and reply['result']['supportedVersions'][0]=='2026-07-28' and reply['result']['resultType']=='complete' and reply['result']['ttlMs']>=0 and reply['result']['cacheScope']=='private','current discovery')
                check(reply['result']['_meta']['io.modelcontextprotocol/serverInfo']['name']=='HttpLocalServer','per-response identity')
                code,reply=call('tools/list');check(code==200 and all(x['annotations']['readOnlyHint'] for x in reply['result']['tools']) and 'runtime_components' in [x['name'] for x in reply['result']['tools']],'read-only schemas and components tool')
                code,reply=call('tools/call',{'name':'read_text','arguments':{'path':'/hello.txt'}});check(code==200 and reply['result']['structuredContent']['text']=='你好','structured tool result')
                encoded='=?base64?'+base64.b64encode(b'read_text').decode()+'?='
                check(call('tools/call',{'name':'read_text','arguments':{'path':'/hello.txt'}},override={'Mcp-Name':encoded})[0]==200,'encoded Mcp-Name')
                for override in [{'Mcp-Method':'wrong'},{'MCP-Protocol-Version':'2025-11-25'},{'Mcp-Method':''}]:
                    code,reply=call('tools/list',override=override);check(code==400 and reply['error']['code']==-32020,'metadata/header consistency')
                code,reply=call('tools/call',{'name':'read_text'},override={'Mcp-Name':'server_status'});check(code==400 and reply['error']['code']==-32020,'tool header/body consistency')
                code,reply=call('no-such-method');check(code==404 and reply['error']['code']==-32601,'modern unknown RPC status')
                code,reply=call('tools/list',modern=False,override={'MCP-Protocol-Version':'2099-01-01'});check(code==400 and reply['error']['code']==-32022 and '2026-07-28' in reply['error']['data']['supported'],'unsupported version response')
                code,reply=call('initialize',{'protocolVersion':'2026-07-28'},modern=False);check(code==200 and reply['result']['protocolVersion']=='2025-11-25','legacy handshake counteroffer')
                code,reply=call('tools/list',modern=False);check(code==200 and 'tools' in reply['result'],'legacy tools')
                check(call('tools/list',remote=True)[0]==403,'local-only MCP even with valid token')
                check(call('tools/list',override={'Authorization':'Bearer wrong'})[0]==401,'Bearer authentication')
            finally:server.close()
    print('MCP current integration passed: discovery, metadata, HTTP headers/status, structured results, auth and legacy; '+('HTTPS' if https else 'HTTP'))

if __name__=='__main__':run(str(pathlib.Path(sys.argv[1]).resolve()),'--https' in sys.argv[2:])
