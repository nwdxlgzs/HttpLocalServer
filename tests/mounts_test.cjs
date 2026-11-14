// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const root=path.resolve(__dirname,'..');
function load(ctx,name){vm.runInContext(fs.readFileSync(path.join(root,'web/scripts',name),'utf8'),ctx,{filename:name});}
function failure(name){const e=Error(name);e.name=name;return e;}
class FileHandle{
    constructor(name,bytes){this.kind='file';this.name=name;this.data=Buffer.from(bytes||'');this.time=1000;}
    async move(destination,name){if(destination.children.has(name))throw failure('InvalidModificationError');this.owner.children.delete(this.name);this.name=name;this.owner=destination;destination.children.set(name,this);}
    async getFile(){const data=Buffer.from(this.data);return {name:this.name,size:data.length,lastModified:this.time,slice:(a,b)=>data.subarray(a,b)};}
    async createWritable(){const chunks=[];return {write:async bytes=>chunks.push(Buffer.from(bytes)),close:async()=>{this.data=Buffer.concat(chunks);this.time++;},abort:async()=>{chunks.length=0;}};}
}
class DirectoryHandle{
    constructor(name){this.kind='directory';this.name=name;this.children=new Map();}
    async getDirectoryHandle(name,options){let h=this.children.get(name);if(!h&&options&&options.create){h=new DirectoryHandle(name);this.children.set(name,h);}if(!h)throw failure('NotFoundError');if(h.kind!=='directory')throw failure('TypeMismatchError');h.owner=this;return h;}
    async getFileHandle(name,options){let h=this.children.get(name);if(!h&&options&&options.create){h=new FileHandle(name);this.children.set(name,h);}if(!h)throw failure('NotFoundError');if(h.kind!=='file')throw failure('TypeMismatchError');h.owner=this;return h;}
    entries(){const iterator=this.children.entries();return {next:()=>Promise.resolve(iterator.next())};}
    async removeEntry(name){if(!this.children.has(name))throw failure('NotFoundError');this.children.delete(name);}
}
async function run(){
    const elements={},calls=[],window={Promise:Promise,isSecureContext:true,showDirectoryPicker:()=>Promise.resolve(),FileSystemDirectoryHandle:DirectoryHandle,FileSystemFileHandle:FileHandle};
    function el(id){return elements[id]||(elements[id]={className:'',style:{},textContent:'',innerHTML:'',setAttribute(){}});}
    const ctx=vm.createContext({Promise,console,Math,Date,JSON,Object,Uint8Array,ArrayBuffer,setTimeout,clearTimeout,window,
        $:el,session:{writable:true,writeMode:'shared',token:'token'},browserID:'page-test-123456789',withinPath:(r,p)=>p===r||p.startsWith(r+'/'),join:(p,n)=>(p==='/'?'':p)+'/'+n,parent:p=>p.slice(0,p.lastIndexOf('/'))||'/',base:p=>p.slice(p.lastIndexOf('/')+1),show(){},on(){},uiIcon(){return '';},esc:s=>s,query:o=>new URLSearchParams(o).toString(),enc:encodeURIComponent,normalized:p=>p,
        current:()=>({path:'/'}),uploadTarget:p=>p,toast:s=>calls.push(s),refresh(){},text:(e,s)=>e.textContent=s,request(){},modal(){},cancel:{label:'Cancel',run(){}},doc:{},siteTitle:'Share'});
    load(ctx,'resume.js');load(ctx,'frozen.js');load(ctx,'mounts.js');assert(ctx.canDirectoryPick()&&ctx.canDirectorySave(),'Capabilities depend on real picker and handle methods');window.isSecureContext=false;assert(!ctx.canDirectoryPick(),'Insecure LAN HTTP never advertises unavailable file APIs');window.isSecureContext=true;window.Promise=undefined;assert(!ctx.canDirectoryPick(),'Missing Promise hides modern directory APIs');window.Promise=Promise;
    ctx.applyMounts([{path:'/ro',writable:false,browser:true},{path:'/rw',writable:true,browser:true,movable:false}]);assert(ctx.isMountRoot('/ro')&&ctx.isMountRoot('/')&&!ctx.isMountRoot('/ro/file'),'Mount roots and ancestors are protected');assert(ctx.canMoveEntry({path:'/rw/file'}),'Writable mounts support copy-and-delete move fallback');
    const directory=new DirectoryHandle('Picked');directory.children.set('a.txt',new FileHandle('a.txt','first'));directory.children.set('.hidden',new FileHandle('.hidden','secret'));directory.children.set('nested',new DirectoryHandle('nested'));directory.children.get('nested').children.set('deep.txt',new FileHandle('deep.txt','deep')); 
    const provider={id:'mount-id',key:'secret',path:'/Mounted',handle:directory,writable:false,cache:{},writes:{},copies:{}};
    const [one,two]=await Promise.all([ctx.providerListing(provider,'/',false),ctx.providerListing(provider,'/',false)]);assert.strictEqual(one,two,'Concurrent metadata scans share the same in-flight Promise');assert.equal(one.items.length,2);assert.equal((await ctx.providerListing(provider,'/',true)).items.length,3,'Provider hidden preference is isolated');
    directory.children.set('config.json',new FileHandle('config.json','private'));directory.children.set('.HLS-CONFIG-LOCK-test',new FileHandle('.HLS-CONFIG-LOCK-test','private'));provider.cache={};
    assert.equal((await ctx.providerListing(provider,'/',false)).items.length,3,'Ordinary names remain visible when they are not host dependencies');
    const revealed=(await ctx.providerListing(provider,'/',true)).items.filter(e=>e.frozen);assert.equal(revealed.length,0,'Browser mounts do not invent host dependency freezes');
    await assert.rejects(()=>ctx.providerHandle(provider,'/../escape',false,true),/无效/,'Provider never traverses parents');
    const search=await ctx.providerSearch(provider,'/','deep',false);assert.equal(search.items[0].relative,'/nested/deep.txt','Mounted search includes descendants within its scope');
    const stat=await ctx.providerOperation(provider,{op:'stat',path:'/a.txt'});assert.equal(stat.size,5);assert.equal(stat.version,'mount-id:5:1000');
    await assert.rejects(()=>ctx.providerOperation(provider,{op:'mkdir',path:'/forbidden'}),/只读/,'Read-only providers reject writes regardless of UI');
    provider.writable=true;await ctx.providerOperation(provider,{op:'mkdir',path:'/created'});assert(directory.children.has('created'));
    let uploaded=0;ctx.XMLHttpRequest=function(){this.open=(method,url)=>{this.url=url;};this.setRequestHeader=()=>{};this.send=file=>{uploaded+=file.size;this.status=201;setTimeout(()=>this.onload(),0);};};
    await Promise.all([ctx.replicateFile(provider,'/a.txt',true,stat.version),ctx.replicateFile(provider,'/a.txt',false)]);assert.equal(uploaded,5,'Background and demanded materialization share one upload');
    await ctx.replicateFile(provider,'/a.txt',true);assert.equal(uploaded,5,'Already mirrored versions do not consume upload allowance again');
    directory.children.get('a.txt').time++;await ctx.replicateFile(provider,'/a.txt',true);assert.equal(uploaded,10,'Changed versions are copied into the host again');
    await assert.rejects(()=>ctx.providerOperation(provider,{op:'write-begin',path:'/a.txt',upload:'stale',version:stat.version}),/已存在|改变/,'Stale read/write mount versions cannot replace content');
    const fresh=await ctx.providerOperation(provider,{op:'stat',path:'/a.txt'});await ctx.providerOperation(provider,{op:'write-begin',path:'/a.txt',upload:'write1',version:fresh.version});
    ctx.rawMountRequest=()=>Promise.resolve(Buffer.from('replacement'));
    await ctx.providerOperation(provider,{op:'write-chunk',upload:'write1',offset:0,bytes:11});assert.equal(directory.children.get('a.txt').data.toString(),'first','Browser write stays private until publication');
    await ctx.providerOperation(provider,{op:'write-end',upload:'write1'});assert.equal(directory.children.get('a.txt').data.toString(),'replacement','Completed browser write publishes before remirroring');

    // Durable private payload publication remains COW, handles a lost ACK,
    // and never replaces an independently changed provider file.
    const cachedBytes=Buffer.alloc(800001,82),cacheReads=[];
    ctx.rawMountRequest=(method,url,body,type,range)=>{cacheReads.push(range);const chunk=cachedBytes.subarray(range.first,range.last+1);return Promise.resolve(chunk.buffer.slice(chunk.byteOffset,chunk.byteOffset+chunk.length));};
    ctx.p2pReadBlob=blob=>Promise.resolve(Uint8Array.from(blob));
    const oldReplicate=ctx.replicateFile;ctx.replicateFile=()=>Promise.resolve();
    const cachedJob={op:'write-cache',path:'/cached.bin',size:cachedBytes.length,version:'',id:'cached-job'};
    const cacheReceipt=await ctx.providerOperation(provider,cachedJob);assert.equal(directory.children.get('cached.bin').data.compare(cachedBytes),0);assert(cacheReads.every(r=>r.last-r.first+1<=262144));
    assert.equal((await ctx.providerOperation(provider,cachedJob)).version,cacheReceipt.version,'A repeated publication is reconciled by exact bytes');
    directory.children.get('cached.bin').data=Buffer.alloc(cachedBytes.length,83);directory.children.get('cached.bin').time++;
    await assert.rejects(()=>ctx.providerOperation(provider,cachedJob),e=>e.status===409);assert.equal(directory.children.get('cached.bin').data[0],83);
    await assert.rejects(()=>ctx.providerOperation(provider,{op:'delete',path:'/cached.bin',version:cacheReceipt.version}),e=>e.status===409,'Conditional move cleanup never deletes changed provider content');
    ctx.replicateFile=oldReplicate;
    const moving=await ctx.providerOperation(provider,{op:'stat',path:'/a.txt'});await ctx.providerOperation(provider,{op:'move',path:'/a.txt',to:'/created/moved.txt',version:moving.version,noReplace:true});assert(!directory.children.has('a.txt'));assert.equal(directory.children.get('created').children.get('moved.txt').data.toString(),'replacement');
    directory.children.set('other.txt',new FileHandle('other.txt','keep me'));await assert.rejects(()=>ctx.providerOperation(provider,{op:'move',path:'/other.txt',to:'/created/moved.txt',noReplace:true}),e=>e.status===409,'Mounted moves reject occupied destinations');assert(directory.children.has('other.txt'));
    // Source handles can supply a bounded P2P block before any host copy.
    const crypto=require('crypto'),sourceBytes=Buffer.alloc(2*1048576+9,73),sourceHandle=new FileHandle('source.bin',sourceBytes);
    directory.children.set('source.bin',sourceHandle);ctx.providerMounts.push(provider);
    ctx.p2pError=(message,status)=>Object.assign(Error(message),{status:status||0});ctx.p2pReadBlob=bytes=>Promise.resolve(Uint8Array.from(bytes));ctx.p2pDigest=bytes=>Promise.resolve(crypto.createHash('sha256').update(bytes).digest('hex'));
    const sourceVersion=ctx.fileVersion(provider,await sourceHandle.getFile()),meta={source:true,sourceMount:provider.id,sourceVersion,sourcePath:'/source.bin',path:'/Mounted/source.bin',size:sourceBytes.length,pieces:3};
    assert.strictEqual(ctx.p2pMountedSource(meta),provider);const sourceBlock=await ctx.p2pSourceRead(meta,1);assert.equal(sourceBlock.length,1048576);assert.equal(Buffer.compare(Buffer.from(sourceBlock),sourceBytes.subarray(1048576,2097152)),0);
    const hashes=await ctx.providerHashes(provider,{path:'/source.bin',version:sourceVersion,start:0});assert.equal(hashes.hashes.length,3);assert.equal(hashes.hashes[2],crypto.createHash('sha256').update(sourceBytes.subarray(2097152)).digest('hex'));
    sourceHandle.time++;await assert.rejects(()=>ctx.p2pSourceRead(meta,0),e=>e.status===409,'Changed source cannot enter the old swarm');assert.equal(ctx.p2pMountedSource(Object.assign({},meta,{path:'/Other/source.bin'})),null,'Source proof cannot address another logical mount');
    assert.equal(ctx.p2pMountedSource(Object.assign({},meta,{source:false})),null,'Generic relay cannot silently read local handles');
    directory.children.set('settings.json',new FileHandle('settings.json','ordinary'));provider.cache={};
    const ordinaryConfig=(await ctx.providerListing(provider,'/',true)).items.find(e=>e.name==='settings.json');assert(!ordinaryConfig.frozen&&ordinaryConfig.size>0,'Browser mount does not invent dependency freezes');
    await ctx.providerFile(provider,'/settings.json');
    assert(ctx.frozenEntry({path:'/arbitrary',frozen:true}));

    // Run the directory saver against a filesystem handle double and a versioned
    // HTTP source. No ZIP and no whole-file Blob are used.
    load(ctx,'tasks.js');load(ctx,'jobs.js');ctx.localStorage={getItem(){return null;},setItem(){},removeItem(){}};ctx.drawerMode='';const target=new DirectoryHandle('Local'),payload=Buffer.alloc(700001,42),requests=[];let doneResolve;const completed=new Promise(r=>doneResolve=r);
    ctx.zipBusy=true;ctx.records=[];ctx.drawerMode='';ctx.saveRecords=()=>doneResolve();ctx.size=n=>String(n);ctx.selected=null;ctx.uiIcon=()=>'';ctx.request=(method,url,body,done)=>{
        if(url.startsWith('/api/list'))done(null,{total:3,entries:[{name:'config.json',path:'/Source/config.json',directory:false,frozen:true},{name:'empty',path:'/Source/empty',directory:true},{name:'big.bin',path:'/Source/big.bin',directory:false,size:payload.length}],truncated:false});else done(null,{url:'/download?version=pinned',size:payload.length});
    };
    // Distinguish the empty child listing from its parent.
    const request=ctx.request;ctx.request=(m,u,b,d)=>u.startsWith('/api/list')&&decodeURIComponent(u).includes('path=/Source/empty')?d(null,{total:0,entries:[],truncated:false}):request(m,u,b,d);
    ctx.XMLHttpRequest=function(){let range='';this.open=(method,url)=>{this.url=url;};this.setRequestHeader=(k,v)=>{if(k==='Range')range=v;};this.send=()=>{const match=/bytes=(\d+)-(\d+)/.exec(range),a=Number(match[1]),b=Number(match[2]);requests.push([a,b]);const chunk=payload.subarray(a,b+1);this.response=chunk.buffer.slice(chunk.byteOffset,chunk.byteOffset+chunk.length);this.status=206;this.getResponseHeader=()=>`bytes ${a}-${b}/${payload.length}`;setTimeout(()=>this.onload(),0);};};
    load(ctx,'directory_save.js');const rootEntry={path:'/Source',name:'Source',directory:true},task=ctx.taskCreate('目录保存','Source','/Source',0),state={type:'directory',root:rootEntry,handle:target,plan:null};ctx.jobAttach(task,'directory',state);ctx.runDirectoryJob(task,state);await Promise.race([completed,new Promise((_,r)=>setTimeout(()=>r(Error('Direct directory save timed out')),3000))]);
    assert.equal(target.children.get('big.bin').data.compare(payload),0,'Direct folder download writes exact streamed bytes');assert(target.children.get('empty') instanceof DirectoryHandle,'Empty directories survive direct saves');assert.equal(requests.length,3);assert(requests.every(([a,b])=>b-a+1<=262144),'Direct folder download retains bounded 256 KiB reads');assert(!target.children.has('config.json'),'Direct folder downloads skip frozen contents');assert(ctx.records[0].direct,'Saved folder history is distinguished from ZIP generation');
    console.log('Mount/browser tests passed: capability gating, hidden filters, coalesced scans and copies, read-only enforcement, version checks, COW publication and bounded direct-folder download');
}
run().catch(error=>{console.error(error);process.exitCode=1;});
