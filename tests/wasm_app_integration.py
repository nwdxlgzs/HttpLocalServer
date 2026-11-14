"""Real WAMR, component bridges, application boundaries and config precedence."""
import concurrent.futures,gzip,hashlib,io,http.client,json,pathlib,socket,subprocess,sys,tempfile,time,zipfile
from integration import Server,check
from types import SimpleNamespace
from wasm_fixtures import arithmetic,imported,start_loop,wasi_args,enormous_memory,invalid_ref_null

SCRIPT=r'''
import hls,{fs,wasm,sqlite,crypto,compression,types,zip,args,capabilities} from 'hls';
let calc,db;
export function onStart(){
  calc=wasm.instantiate('/calc.wasm');db=sqlite.open();
  sqlite.execute(db,'CREATE TABLE notes (id INTEGER PRIMARY KEY, text TEXT)');
  sqlite.execute(db,'INSERT INTO notes(text) VALUES (?)',['你好']);
  if(capabilities.fs==='readwrite'){fs.mkdir('/data');fs.writeText('/data/start.txt','started');}
}
function respond(value){return {body:JSON.stringify(value),contentType:'application/json'};}
export function onRequest(r){
 if(r.url==='/api/app/info')return respond({args,capabilities,sum:calc.exports.add(19,23)});
 if(r.url==='/allow-only')return {allow:true};
 if(r.url==='/api/app/test'){
  const results={};
  results.i64=String(calc.exports.increment64(9007199254740993n));
  results.f64=calc.exports.add64(1.5,2.75);
  const bytes=calc.exports.memory.buffer;new Uint8Array(bytes)[0]=42;results.shared=calc.exports.load(0);
  const old=calc.exports.memory.buffer;results.grow=calc.exports.grow(1);results.detached=old.byteLength;results.memory=calc.exports.memory.buffer.byteLength;
  const local=wasm.instantiate('/import.wasm',{env:{twice:x=>x*2}}),other=wasm.instantiate('/import.wasm',{env:{twice:x=>x*3}});
  results.imports=[local.exports.call(7),other.exports.call(7),local.exports.call(8)];local.close();other.close();
  try{local.exports.call(1);}catch(e){results.closed=true;}
  return respond(results);
 }
 if(r.url==='/api/app/memory'){
  const c=wasm.instantiate('/calc.wasm'),old=c.exports.memory.buffer;new Uint8Array(old)[0]=42;
  const result={shared:c.exports.load(0),grow:c.exports.grow(1),detached:old.byteLength,length:c.exports.memory.buffer.byteLength};
  c.close();result.closedBuffer=true;return respond(result);
 }
 if(r.url==='/api/app/numeric'){
  const local=wasm.instantiate('/import.wasm',{env:{twice:x=>x*2}}),other=wasm.instantiate('/import.wasm',{env:{twice:x=>x*3}});
  const result={sum:calc.exports.add(19,23),i64:String(calc.exports.increment64(9007199254740993n)),float:calc.exports.add64(1.5,2.75),imports:[local.exports.call(7),other.exports.call(7),local.exports.call(8)]};
  local.close();other.close();try{local.exports.call(1);}catch(e){result.closed=true;}return respond(result);
 }
 if(r.url==='/api/app/wasm-api'){
  const module=new WebAssembly.Module(wasmBytes()),inst=new WebAssembly.Instance(module);
  const result={valid:WebAssembly.validate(wasmBytes()),bad:WebAssembly.validate(new Uint8Array([1,2])),sum:inst.exports.add(1,2),exports:WebAssembly.Module.exports(module)};inst.close();return respond(result);
 }
 if(r.url==='/api/app/wasi'){
  const wasi=wasm.wasi({args:['app','你好']});const instance=wasm.instantiate('/wasi.wasm',{wasi_snapshot_preview1:wasi.wasiImport});wasi.initialize(instance);let rc=instance.exports.args();const view=new DataView(instance.exports.memory.buffer);const result={rc,count:view.getUint32(0,true),bytes:view.getUint32(4,true)};instance.close();return respond(result);
 }
 if(r.url==='/api/app/components'){
  const result={hash:crypto.sha256('abc'),gzip:hls.text.decode(compression.gunzip(compression.gzip('hello world'))),deflate:hls.text.decode(compression.inflate(compression.deflate('hello world'))),rows:sqlite.query(db,'SELECT text FROM notes').rows,type:types.detect('/file-no-extension').mime,entries:zip.list('/sample.zip'),entry:hls.text.decode(zip.read('/sample.zip','/hello.txt')),qr:hls.qr.svg('http://127.0.0.1'),random:crypto.randomToken().length};
  const restored=sqlite.open(sqlite.snapshot(db));result.restored=sqlite.query(restored,'SELECT text FROM notes').rows;sqlite.close(restored);result.empty=crypto.sha256(new Uint8Array(0));result.digest=crypto.digest(capabilities.mbedTLS?'SHA512':'SHA256','abc');
  const tricky=[];Object.defineProperty(tricky,0,{get(){try{sqlite.close(db);}catch(e){result.reentryBlocked=true;}return '你好';}});tricky.length=1;result.stillOpen=sqlite.query(db,'SELECT ? AS text',tricky).rows;
  result.namedGzip=hls.text.decode(compression.gunzip(fs.readBytes('/named.gz')));
  try{sqlite.execute(db,"VACUUM INTO ':memory:'");}catch(e){result.vacuumDenied=true;}
  try{sqlite.execute(db,"ATTACH DATABASE '/escape.sqlite' AS other");}catch(e){result.attachDenied=true;}
  try{compression.gunzip(new Uint8Array([0,0,0]));}catch(e){result.badCompression=true;}
  return respond(result);
 }
 if(r.url==='/api/app/transfer'){const inst=wasm.instantiate('/calc.wasm'),buffer=inst.exports.memory.buffer;let denied=0;for(const name of ['transfer','transferToFixedLength','transferToImmutable']){try{ArrayBuffer.prototype[name].call(buffer);}catch(e){denied++;}}const size=buffer.byteLength;inst.close();const normal=new Uint8Array([1,2]).buffer.transfer();return respond({denied,size,closed:buffer.byteLength,normal:new Uint8Array(normal)[1]});}
 if(r.url==='/api/app/many-calls'){let count=0;try{for(;count<20000;count++)calc.exports.add(1,2);}catch(e){return respond({stopped:true,count});}return respond({stopped:false,count});}
 if(r.url==='/api/app/fuel'){try{calc.exports.spin();}catch(e){return respond({stopped:true});}return respond({stopped:false});}
 if(r.url==='/api/app/start-loop'){try{wasm.instantiate('/loop.wasm');}catch(e){return respond({stopped:true});}}
 if(r.url==='/api/app/huge'){try{wasm.instantiate('/huge.wasm');}catch(e){return respond({stopped:true});}}
 if(r.url==='/api/app/badref'){try{wasm.instantiate('/badref.wasm');}catch(e){return respond({stopped:true});}}
 if(r.url==='/api/app/missing'){try{wasm.instantiate('/wasi.wasm');}catch(e){return respond({stopped:true});}}
 if(r.url==='/api/app/instances'){const list=[];let limited=false;try{for(let n=0;n<20;n++)list.push(wasm.instantiate('/import.wasm',{env:{twice:x=>x}}));}catch(e){limited=true;}for(const item of list)item.close();return respond({limited,count:list.length});}
 if(r.url==='/api/app/reentry'){const nested=wasm.instantiate('/import.wasm',{env:{twice:x=>calc.exports.add(x,x)}});let denied=false;try{nested.exports.call(1);}catch(e){denied=true;}nested.close();return respond({denied});}
 if(r.url==='/api/app/frozenzip'){try{fs.readBytes('/application.zip');}catch(e){return respond({denied:true});}return respond({denied:false});}
 if(r.url==='/api/app/frozen'){try{fs.readText('/config.json');}catch(e){return respond({denied:true});}return respond({denied:false});}
 if(r.url==='/api/app/write'){try{fs.writeText('/result.txt','changed');return respond({wrote:true});}catch(e){return respond({wrote:false});}}
 if(r.url==='/api/app/private-state'){try{fs.readBytes('/.hls-state-test/state.sqlite');}catch(e){return respond({denied:true});}return respond({denied:false});}
 if(r.url==='/api/app/escape'){try{fs.readText('/../outside.txt');}catch(e){return respond({denied:true});}}
 return {};
}
function wasmBytes(){return new Uint8Array(JSON.parse(fs.readText('/calc.json')));}
'''

def package(path,script=SCRIPT):
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in {'server.qjs':script,'calc.wasm':arithmetic(),'import.wasm':imported(),'loop.wasm':start_loop(),'wasi.wasm':wasi_args(),'huge.wasm':enormous_memory(),'badref.wasm':invalid_ref_null(),'lib/unloaded.js':'private backend source','public/index.html':'<h1>ZIP app</h1>','public/client.js':'window.app=true;','public/config.json':'{"public":true}'}.items():z.writestr(name,data)

def request(server,target,method='GET',body=None,headers=None,remote=False,https=False):
    import ssl
    kwargs={'timeout':20,'source_address':('127.0.0.2',0)} if remote else {'timeout':20}
    hostname=getattr(server,'hostname','127.0.0.1')
    conn=(http.client.HTTPSConnection(hostname,server.port,context=ssl._create_unverified_context(),**kwargs) if https else http.client.HTTPConnection(hostname,server.port,**kwargs))
    try:
        conn.request(method,target,body=body,headers=headers or {});r=conn.getresponse();return r.status,dict(r.getheaders()),r.read()
    finally:conn.close()

def run(exe,https=False):
    with tempfile.TemporaryDirectory(prefix='hls-wamr-') as folder:
        root=pathlib.Path(folder);app=root/'application.zip';package(app)
        (root/'private.txt').write_text('never publish implicitly');(root/'calc.json').write_text(json.dumps(list(arithmetic())));(root/'file-no-extension').write_bytes(b'\x89PNG\r\n\x1a\n'+b'\0'*100)
        with zipfile.ZipFile(root/'sample.zip','w') as z:z.writestr('hello.txt','archive text')
        with gzip.GzipFile(filename='named.txt',fileobj=(root/'named.gz').open('wb'),mode='wb',mtime=0) as stream:stream.write(b'named gzip')
        config=root/'config.json';config.write_text(json.dumps({'version':1,'options':{'mode':'server','workspace-app':'application.zip','workspace-arg':['config value'],'script-fs':'readwrite'}}))
        with socket.socket() as port_probe:port_probe.bind(('127.0.0.1',0));control_port=port_probe.getsockname()[1]
        flags=('--mcp','--workspace-control-port',str(control_port),'--workspace-app',str(app),'--script-fs','readwrite','--strict-host','--state-dir',str(root/'.hls-state-test'),'--wasm-instructions','10000','--script-timeout-ms','500','--no-magika',*(['--https','--tls-self-signed'] if https else []))
        server=Server(exe,root,'server',flags)
        try:
            def get(path,*args,**kwargs):return request(server,path,*args,https=https,**kwargs)
            def data(path):
                status,_,body=get(path);check(status==200,f'{path}: {status} {body}');return json.loads(body)
            check(get('/')[2]==b'<h1>ZIP app</h1>' and get('/client.js')[2]==b'window.app=true;','public frontend served')
            for path in ['/private.txt','/allow-only','/server.qjs','/lib/unloaded.js','/calc.wasm']:
                check(get(path)[0] in [403,404],f'No backend/fallback disclosure: {path}')
            n=data('/api/app/numeric');check(n['sum']==42 and n['i64']=='9007199254740994' and n['float']==4.25 and n['imports']==[14,21,16] and n['closed'],'numeric types, import isolation, close')
            mem=data('/api/app/memory');check(mem=={'shared':42,'grow':1,'detached':0,'length':131072,'closedBuffer':True},'live memory and growth detachment')
            api=data('/api/app/wasm-api');check(api['valid'] and not api['bad'] and api['sum']==3,'synchronous WebAssembly API')
            wasi=data('/api/app/wasi');check(wasi=={'rc':0,'count':2,'bytes':11},'explicit sandbox WASI arguments')
            result=data('/api/app/components');check(result['hash']==hashlib.sha256(b'abc').hexdigest() and result['gzip']==result['deflate']=='hello world' and result['rows']==[['你好']] and result['entry']=='archive text' and result['attachDenied'] and result['badCompression'] and result['random']>=32,'native component bridges and SQL confinement')
            check(result['reentryBlocked'] and result['stillOpen']==[['你好']] and result['vacuumDenied'] and result['namedGzip']=='named gzip','native getter reentry, VACUUM isolation and standard gzip header')
            check(data('/api/app/transfer')=={'denied':3,'size':65536,'closed':0,'normal':2},'host-owned memory cannot escape detachment via transfer')
            many=data('/api/app/many-calls');check(many['stopped'] and 0<many['count']<20000,'fuel shared across calls')
            check(result['restored']==[['你好']] and result['empty']==hashlib.sha256(b'').hexdigest() and result['digest'] in [hashlib.sha256(b'abc').hexdigest(),hashlib.sha512(b'abc').hexdigest()],'SQLite restore, empty buffers, optional mbedTLS digest')
            for operation in ['fuel','start-loop','huge','badref','missing']:
                before=time.monotonic();check(data('/api/app/'+operation)['stopped'],operation+' stopped');check(time.monotonic()-before<3,'bounded '+operation)
            check(data('/api/app/info')['sum']==42,'runtime recovery after traps')
            check(data('/api/app/instances')=={'limited':True,'count':7} and data('/api/app/reentry')['denied'],'instance ceiling and import reentry')
            check(data('/api/app/private-state')['denied'] and data('/api/app/frozenzip')['denied'],'real state and application ZIP private to backend')
            check(not data('/api/app/write')['wrote'],'GET cannot mutate filesystem')
            check(get('/api/app/write','POST',body='')[0]==403,'cross-site simple mutation denied')
            check(get('/api/app/write','POST',body='',headers={'X-HLS-App':'1','Origin':'http://attacker.invalid'})[0]==403,'foreign Origin denied')
            check(get('/api/app/write','POST',body='',headers={'X-HLS-App':'1'})[0]==200 and (root/'result.txt').read_text()=='changed','explicit native API mutation permitted')
            check(data('/api/app/escape')['denied'],'script path traversal denied')
            for path in ['/_hls/','/api/list?path=/','/download?path=/private.txt','/api/status','/api/session']:
                check(get(path,remote=True)[0]==403,'remote application cannot reach file control '+path)
            check(get('/api/app/info',headers={'Host':'localhost:'+str(server.port)})[0]==403,'localhost cannot become an application origin')
            check(get('/api/app/info',remote=True)[0]==200,'LAN app available independent of private share')
            for path in ['/api/admin/settings','/api/session','/_hls/','/mcp']:
                check(get(path)[0]==403,'Application origin has no host privileges '+path)
            control=SimpleNamespace(port=control_port,hostname="localhost")
            check(request(control,'/_hls/',https=https)[0]==200 and request(control,'/api/admin/settings',https=https)[0]==200,'Separate loopback control origin')
            check(request(control,'/_hls/',headers={'Host':'127.0.0.1:'+str(control_port)},https=https)[0]==403,'numeric control host rejected for legacy-browser origin safety')
            admin=json.loads(request(control,'/api/admin/settings',https=https)[2]);check(admin['wasmEngine']=='WAMR 2.4.5','host discovers WAMR')
            rpc={'jsonrpc':'2.0','id':1,'method':'tools/call','params':{'name':'runtime_components'}}
            check(request(control,'/mcp','POST',json.dumps(rpc),{'Content-Type':'application/json','Authorization':'Bearer '+admin['mcpToken'],'MCP-Protocol-Version':'2025-11-25'},https=https)[0]==200,'MCP reachable on separate control origin')
            for path in ['/client.js','/api/app/info']:
                check(request(control,path,https=https)[0]==404,'Application code excluded from control origin')
            check(request(control,'/api/admin/settings',headers={'Origin':('https' if https else 'http')+'://127.0.0.1:'+str(server.port)},https=https)[0]==403,'Cross-origin app cannot read host settings')
            check(get('/api/upload?path=/remote.txt','PUT',body='bad',headers={'Content-Length':'3'},remote=True)[0]==403 and not (root/'remote.txt').exists(),'streaming upload boundary')
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:check(all(pool.map(lambda _:get('/api/app/info')[0]==200,range(32))),'concurrent workspace requests')
        finally:server.close()
        # CLI wins over config arrays. The config paths remain relative to config.
        server=Server(exe,root,None,('--config',str(config),'--workspace-arg','CLI value','--no-magika','--script-timeout-ms','500'),config=True)
        try:
            check(request(server,'/')[2]==b'<h1>ZIP app</h1>','Server mode selected by config without positional mode')
            check(json.loads(request(server,'/api/app/info')[2])['args']==['CLI value'],'CLI application arguments replace config array')
            check(json.loads(request(server,'/api/app/frozen')[2])['denied'],'active configuration remains frozen to backend')
            check(request(server,'/config.json')[2]==b'{"public":true}','ZIP public asset does not inherit unrelated physical config freeze')
        finally:server.close()
        for flags in [('--workspace-access','host'),('--workspace-allow-ip','192.0.2.0/24')]:
            server=Server(exe,root,'server',('--workspace-app',str(app),'--script-fs','readonly','--strict-host','--no-magika','--script-timeout-ms','500',*flags))
            try:
                check(request(server,'/api/app/info',remote=True)[0]==403 and request(server,'/api/app/info')[0]==200,'host-only / CIDR policy')
                check(not json.loads(request(server,'/api/app/write','POST','',{'X-HLS-App':'1'})[2])['wrote'],'readonly backend filesystem remains readonly for POST')
            finally:server.close()
        server=Server(exe,root,'server',('--workspace-app',str(app),'--password','app secret','--no-magika','--script-timeout-ms','500'))
        try:
            check(request(server,'/api/app/info')[0]==401,'Application APIs require global login')
            code,headers,_=request(server,'/api/access/login','POST',json.dumps({'password':'app secret'}),{'Content-Type':'application/json'})
            cookie=next(value for key,value in headers.items() if key.lower()=='set-cookie').split(';',1)[0]
            check(code==200 and request(server,'/api/app/info',headers={'Cookie':cookie})[0]==200,'Authenticated application access')
            check(request(server,'/api/admin/settings',headers={'Cookie':cookie})[0]==403,'Authenticated app session has no manager privilege')
        finally:server.close()
        probe="export function onRequest(){let denied=false;try{hls.wasm.instantiate(new Uint8Array([0,97,115,109,1,0,0,0]));}catch(e){denied=true;}return {body:JSON.stringify({denied,wasm:hls.capabilities.wasm})};}"
        disabled=root/'disabled.zip';package(disabled,probe)
        server=Server(exe,root,'server',('--workspace-app',str(disabled),'--no-wasm','--no-magika'))
        try:check(json.loads(request(server,'/')[2])=={'denied':True,'wasm':False},'Wasm disabling enforced')
        finally:server.close()
        server=Server(exe,root,'server',('--workspace-app',str(app),'--workspace-share','--workspace-allow-ip','127.0.0.2/32','--strict-host','--no-magika','--script-timeout-ms','500'))
        try:check(request(server,'/download?path=/private.txt',remote=True)[2]==b'never publish implicitly' and request(server,'/api/list?path=/',remote=True)[0]==200,'explicit shared data access and positive CIDR rule')
        finally:server.close()
        config.write_text(json.dumps({'version':1,'options':{'mode':'filetrans'}}))
        server=Server(exe,root,None,('--config',str(config),'--no-magika'),config=True)
        try:check(server.api('/api/status')['mode']=='filetrans','FileTrans mode selected by config')
        finally:server.close()
        hook=root/'isolated.js';hook.write_text('function onRequest(r){var i=hls.wasm.instantiate(new Uint8Array('+json.dumps(list(imported()))+'),{env:{twice:function(x){return x*2;}}});try{return {body:String(i.exports.call(21))};}finally{i.close();}}',encoding='utf-8')
        server=Server(exe,root,'filetrans',('--script',str(hook),'--script-timeout-ms','500','--no-magika'))
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:check(all(pool.map(lambda _:request(server,'/probe')[2]==b'42',range(24))),'isolated concurrent QuickJS contexts use independent WAMR imports')
        finally:server.close()
        print('WAMR/app integration passed: numeric/imports/memory, fuel/start/limits/recovery, components, concurrency, application access and config')

if __name__=='__main__':run(str(pathlib.Path(sys.argv[1]).resolve()),'--https' in sys.argv[2:])
