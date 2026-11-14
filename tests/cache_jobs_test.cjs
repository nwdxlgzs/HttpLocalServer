// SPDX-License-Identifier: Apache-2.0
// Real HTTP + shipped ES5 logic, separate page realms and durable private journals.
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),http=require('http'),path=require('path');
const origin=process.argv[2],output=process.argv[3],root=path.resolve(__dirname,'..'),local=new Map();
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(predicate){for(let i=0;i<2000;i++){if(predicate())return;await wait(5);}throw Error('Managed cache job timed out');}
function get(url){return new Promise((resolve,reject)=>http.get(new URL(url,origin),r=>{const chunks=[];r.on('data',b=>chunks.push(b));r.on('end',()=>resolve(Buffer.concat(chunks)));}).on('error',reject));}
async function make(fail){
 const session=JSON.parse((await get('/api/session')).toString()),calls=[],nodes={};let ctx;
 class XHR{
  constructor(){this.upload={};this.status=0;this.readyState=0;}
  open(method,url){this.method=method;this.url=url;this.headers={};}
  setRequestHeader(k,v){this.headers[k]=v;}
  getResponseHeader(k){return this.responseHeaders[k.toLowerCase()]||null;}
  abort(){this.aborted=true;if(this.req)this.req.destroy();}
  send(body){const range=this.headers.Range||'',url=new URL(this.url,origin);calls.push({url:this.url,range,body});if(fail&&url.pathname==='/download'&&range&&Number(range.match(/\d+/)[0])>=524288){setTimeout(()=>this.onerror&&this.onerror(),0);return;}
   this.req=http.request(url,{method:this.method,headers:this.headers},r=>{this.status=r.statusCode;this.responseHeaders=r.headers;const chunks=[];r.on('data',b=>chunks.push(b));r.on('end',()=>{if(this.aborted)return;const b=Buffer.concat(chunks);this.response=b.buffer.slice(b.byteOffset,b.byteOffset+b.length);this.responseText=b.toString();this.readyState=4;if(this.onreadystatechange)this.onreadystatechange();if(this.onload)this.onload();});});this.req.on('error',()=>{if(!this.aborted&&this.onerror)this.onerror();});this.req.end(body===null||body===undefined?undefined:typeof body==='string'?body:Buffer.from(body.buffer,body.byteOffset,body.byteLength));
  }
 }
 const doc={activeElement:null,body:{appendChild(){},removeChild(){}},createElement(){return {style:{},click(){},setAttribute(){}};}};
 ctx=vm.createContext({console,Promise,JSON,Object,Date,Math,Number,isFinite,Error,ArrayBuffer,Uint8Array,Uint16Array,Uint32Array,DataView,btoa:s=>Buffer.from(s,'binary').toString('base64'),atob:s=>Buffer.from(s,'base64').toString('binary'),setTimeout,clearTimeout,setInterval,clearInterval,window:{},session,XMLHttpRequest:XHR,doc,document:doc,localStorage:{getItem:k=>local.get(k)||null,setItem:(k,v)=>local.set(k,v),removeItem:k=>local.delete(k)},fflate:require('../web/vendor/fflate.js'),browserID:'cache-job-test-123456789',drawerMode:'',siteTitle:'Fixture',records:[],current:()=>null,refresh(){},on(){},show(){},text(){},toast(){},saveRecords(){},taskControls(){},$ :id=>nodes[id]||(nodes[id]={style:{},className:'hidden',setAttribute(){}}),enc:encodeURIComponent,encodeURIComponent,decodeURIComponent,unescape,escape,base:p=>p.split('/').pop(),parent:p=>p.slice(0,p.lastIndexOf('/'))||'/',join:(p,n)=>(p==='/'?'':p)+'/'+n,query:o=>new URLSearchParams(o).toString(),frozenEntry:e=>!!e.frozen,withinPath:(r,p)=>p===r||p.startsWith(r+'/'),findMount:()=>null,size:String,taskConflict:'replace'});
 ctx.request=(method,url,body,done)=>{const x=new XHR();x.open(method,url);if(method!=='GET')x.setRequestHeader('X-HLS-Token',session.token);x.onload=()=>{let data;try{data=JSON.parse(x.responseText);}catch(e){done('Invalid JSON');return;}done(x.status>=200&&x.status<300?null:data.error||'HTTP '+x.status,data);};x.onerror=()=>done('Injected connection loss');x.send(body===null||body===undefined?null:JSON.stringify(body));return x;};
 for(const name of ['tasks.js','transfer_cache.js','jobs.js','batch.js','archive.js'])vm.runInContext(fs.readFileSync(path.join(root,'web/scripts',name),'utf8'),ctx,{filename:name});ctx.taskConflict='replace';ctx.calls=calls;return ctx;
}
async function run(){
 let c=await make(true),entry={path:'/archive-fixture',name:'archive-fixture',directory:true};
 let cache=await new Promise((resolve,reject)=>c.transferCreate(entry.path,'resumed.zip',(e,v)=>e?reject(e):resolve(v)));cache.offset=0;cache.serverJournal=true;
 let t=c.taskCreate('ZIP','resumed.zip',entry.path,0),state={type:'zip',root:entry,filename:'resumed.zip',plan:null,checkpoint:null,cache};c.jobAttach(t,'zip',state);c.runArchive(entry,'resumed.zip',c.zipCacheSink(cache,'resumed.zip'),t,state);
 await until(()=>t.state==='failed');assert(state.checkpoint.entry.uncompressed>=262144,'Mid-file compression state persisted');const sourcePath=state.checkpoint.entry.path,offset=state.checkpoint.entry.uncompressed,stored=JSON.parse(local.get('hls-tasks-v1')).find(x=>x.id===t.id);
 c=await make(false);c.transferTasks.push(stored);c.jobRestore(stored);await until(()=>!!stored.recovery);assert(stored.recovery.checkpoint.entry.engine.s,'Compressor restored from server journal without IndexedDB');c.jobResume(stored);await until(()=>stored.state==='done');const zip=await get(c.transferURL(cache,'read'));fs.writeFileSync(output,zip);const ranges=c.calls.filter(x=>x.url.startsWith('/download')&&new URL(x.url,origin).searchParams.get('path')===sourcePath&&x.range).map(x=>Number(x.range.match(/\d+/)[0]));assert(ranges.some(x=>x===offset),'First resumed entry starts at committed input offset');assert(!ranges.some(x=>x<offset),'No committed source prefix is fetched again');assert.equal(c.transferSlots,0);
 // Queue immutable checkpoints; the first acknowledgement is deliberately delayed.
 const task={checkpointQueue:[]},pending=[],done=[];c.request=(method,url,body,callback)=>pending.push({body,callback});const cacheState={cache:{id:'x',key:'y',serverJournal:true,offset:1}};c.transferCheckpoint(task,cacheState,e=>done.push(e));cacheState.cache.offset=2;c.transferCheckpoint(task,cacheState,e=>done.push(e));assert.equal(pending.length,1);assert.equal(pending[0].body.offset,1);pending[0].callback();assert.equal(pending.length,2);assert.equal(pending[1].body.offset,2);pending[1].callback();assert.equal(done.length,2);
 console.log('Private ZIP recovery passed: actual HTTP, separate page realms, no IndexedDB, mid-file compressor snapshot, exact continued ranges, serialized journals and bounded native download');
}
module.exports={make,local,until,get};if(require.main===module)run().catch(e=>{console.error(e);process.exitCode=1;});
