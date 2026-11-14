// SPDX-License-Identifier: Apache-2.0
const fs=require('fs'),vm=require('vm'),path=require('path'),assert=require('assert'),crypto=require('crypto');
const root=path.resolve(__dirname,'..'),elements={},events=[];
function element(id){return elements[id]||(elements[id]={className:'',style:{},textContent:'',setAttribute(){}});}
const context=vm.createContext({Promise,Uint8Array,ArrayBuffer,DataView,Date,Math,JSON,Object,Error,setTimeout,clearTimeout,console,
    window:{Promise,ArrayBuffer,DataView,FileReader:function(){},isSecureContext:true,RTCPeerConnection:function(){},crypto:{subtle:{digest:(name,bytes)=>Promise.resolve(Uint8Array.from(crypto.createHash('sha256').update(bytes).digest()).buffer)}}},
    navigator:{},session:{token:'token'},$:element,show:(el,yes)=>el.hidden=!yes,text:(el,value)=>el.textContent=value,size:n=>String(n),records:[],saveRecords(){},drawerMode:'',toast:s=>events.push(s)});
function load(name){vm.runInContext(fs.readFileSync(path.join(root,'web/scripts',name),'utf8'),context,{filename:name});}
load('tasks.js');load('jobs.js');load('frozen.js');load('p2p_cache.js');load('p2p_hash.js');load('p2p.js');load('p2p_download.js');load('p2p_relay.js');
const realRelayUpdate=context.p2pRelayUpdate;context.p2pRelayUpdate=()=>{};
async function waitFor(test){for(let i=0;i<1000;i++){if(test())return;await new Promise(r=>setTimeout(r,5));}throw Error('P2P test timeout');}
async function run(){
    assert(context.canP2P());context.window.isSecureContext=false;assert(context.canP2P(),'Ordinary LAN HTTP supports WebRTC with portable SHA-256');context.window.isSecureContext=true;context.p2pEnabled=true;
    const secureCrypto=context.window.crypto;context.window.crypto=null;
    for(const length of [0,1,55,56,63,64,65,127,128,129,1048576]){const bytes=Uint8Array.from({length},(_,i)=>i%251);assert.equal(await context.p2pDigest(bytes),crypto.createHash('sha256').update(bytes).digest('hex'),'Portable SHA-256 vector '+length);}
    context.window.crypto=secureCrypto;
    assert.deepEqual(Array.from(context.p2pRanges([[0,2],[2,3]],3),x=>Array.from(x)),[[0,2],[2,3]]);assert.equal(context.p2pRanges([[1,2],[0,1]],3).length,0);assert.equal(context.p2pRanges([[0,4]],3).length,0);
    const originalNetwork=context.PeerNetwork,payload=Buffer.alloc(6*1048576+17);for(let i=0;i<payload.length;i++)payload[i]=i%251;
    const digest=index=>crypto.createHash('sha256').update(payload.subarray(index*1048576,(index+1)*1048576)).digest('hex');
    let jobs=0,hashCalls=0,networkClosed=0;const requests=[];
    context.p2pAPI=(meta,route,data)=>{
        if(route==='join')return Promise.resolve({id:'a'.repeat(48),key:'secret',version:'v1',size:payload.length,pieceBytes:1048576,pieces:7,maxPeers:4,cacheMiB:4,uploadKiB:0,seedSeconds:1,url:'/pinned'});
        if(route==='hashes'){hashCalls++;return Promise.resolve({start:data.start,hashes:Array.from({length:7},(_,i)=>digest(i))});}
        return Promise.resolve({});
    };
    let corruptDropped=0;
    context.PeerNetwork=function(job){this.job=job;this.announce=()=>{};this.count=()=>1;this.close=()=>{networkClosed++;};this.drop=()=>{corruptDropped++;};this.fetch=index=>{
        if(index===2||index===4)return Promise.resolve(null);
        const bytes=Uint8Array.from(payload.subarray(index*1048576,(index+1)*1048576));if(index===1)bytes[3]^=255;
        return Promise.resolve({bytes,peer:{id:'bad',badUntil:0}});
    };};
    context.XMLHttpRequest=function(){this.open=(method,url)=>{};this.setRequestHeader=(name,value)=>{if(name==='Range')this.range=value;};this.getResponseHeader=()=>this.contentRange;this.abort=()=>this.onabort();this.send=()=>{
        const match=/bytes=(\d+)-(\d+)/.exec(this.range),a=+match[1],b=+match[2];requests.push([a,b]);const chunk=payload.subarray(a,b+1);this.response=Uint8Array.from(chunk).buffer;this.status=206;this.contentRange=`bytes ${a}-${b}/${payload.length}`;setTimeout(()=>this.onload(),1);
    };};
    const written=[],sink={disk:true,persisted:0,existing:null,write:(bytes,index)=>{assert(bytes.length<=1048576);written[index]=Buffer.from(bytes);return Promise.resolve();},read:index=>Promise.resolve(written[index]),checkpoint:(n)=>{sink.persisted=n;return Promise.resolve();},pause:n=>{sink.persisted=n;return Promise.resolve();},resume:()=>Promise.resolve(),close:total=>{assert.equal(total,payload.length);return Promise.resolve();},release(){}};
    const job=new context.PeerDownload({name:'large.bin',path:'/large.bin'},{version:'v1',size:payload.length},sink);context.p2pJob=job;job.start();await waitFor(()=>job.complete);
    assert.equal(Buffer.concat(written).compare(payload),0,'Only independently hash-verified chunks reach the output');assert.equal(corruptDropped,1,'A corrupt peer is removed');
    assert.equal(requests.length,3,'Missing and corrupt peer chunks fall back to bounded host Range requests');assert(requests.every(([a,b])=>b-a+1<=1048576));assert(job.peerBytes>0&&job.originBytes===3*1048576);assert.equal(hashCalls,1,'Digest pages are reused');assert(context.p2pCacheBytes<=4*1048576,'Whole-page cache respects the host budget');assert(job.stopped,'Completion ends the output job without any share prompt');job.stop();
    // Resume from a selected partial file verifies its bytes rather than
    // trusting the file length or an old progress record.
    let localReads=0;context.p2pReadBlob=blob=>{localReads++;return Promise.resolve(Uint8Array.from(blob.bytes));};
    const resumeSink=Object.assign({},sink,{existing:{size:2*1048576,slice:(a,b)=>({bytes:payload.subarray(a,b)})}});requests.length=0;written.length=0;
    const resumed=new context.PeerDownload({name:'large.bin',path:'/large.bin'},{version:'v1',size:payload.length},resumeSink);context.p2pJob=resumed;resumed.start();await waitFor(()=>resumed.complete);assert.equal(resumed.localBytes,2*1048576);assert.equal(localReads,2);assert.equal(Buffer.concat(written).compare(payload),0);resumed.stop();
    context.PeerNetwork=originalNetwork;
    const frames=[],sample=Uint8Array.from(payload.subarray(0,60000)),net=Object.create(originalNetwork.prototype);
    net.job={meta:{uploadKiB:0,pieces:7},availability:()=>[[0,1]],readPiece:()=>Promise.resolve(sample),sentBytes:0,render(){}};net.peers={};net.closed=false;net.lastPoll=Date.now();net.sendActive=0;net.tokenTime=Date.now();net.tokens=0;
    const peer={id:'p',closed:false,pc:{sctp:{maxMessageSize:12000}},channel:{readyState:'open',bufferedAmount:0,send:value=>frames.push(value)}};
    net.serve(peer,{req:5,index:0});await waitFor(()=>!peer.serving);assert.equal(net.job.sentBytes,sample.length);assert(frames.filter(x=>typeof x!=='string').every(x=>x.byteLength<=12000),'Packets respect negotiated maxMessageSize');
    let offset=0;for(const frame of frames.filter(x=>typeof x!=='string')){const view=new DataView(frame);assert.equal(view.getUint32(0),5);assert.equal(view.getUint32(4),offset);offset+=frame.byteLength-8;}assert.equal(offset,sample.length);
    const receiving=Object.create(originalNetwork.prototype);receiving.closed=false;receiving.lastPoll=Date.now();receiving.job={meta:{pieces:7,pieceBytes:1048576,size:payload.length},availability:()=>[],render(){}};let dropped=0;receiving.drop=()=>dropped++;
    const channel={readyState:'open'},remote={pc:{},pending:{req:5,index:0,bytes:null},channel:null};receiving.channel(remote,channel);channel.onmessage({data:'null'});assert.equal(dropped,1,'Malformed JSON envelope is rejected');
    channel.onmessage({data:JSON.stringify({t:'data',req:5,index:0,size:2*1048576})});assert.equal(dropped,2,'Unsolicited oversized chunk cannot allocate memory');
    let reconstructed=null;remote.pending={req:6,index:6,bytes:null,resolve:value=>reconstructed=value};
    const tail=payload.subarray(6*1048576),frame=new ArrayBuffer(tail.length+8),view=new DataView(frame);view.setUint32(0,6);view.setUint32(4,0);new Uint8Array(frame,8).set(tail);
    channel.onmessage({data:JSON.stringify({t:'data',req:6,index:6,size:tail.length})});channel.onmessage({data:frame});assert.equal(Buffer.from(reconstructed.bytes).compare(tail),0,'Valid framed tail is reconstructed exactly');
    await lifecycle();await diskSink();await relaySender();await cachePolicy();downloadPolicy();context.p2pPageClosed=true;context.p2pCacheClear();
    assert(networkClosed>=2);console.log('P2P browser logic passed: capability gates, verified chunks and fallback, bounded resources/framing, local resume, pause/cancel races and COW sink checkpoints');
}
function downloadPolicy(){
    context.p2pSendEnabled=true;context.p2pEnabled=true;context.window.innerWidth=1280;context.window.isSecureContext=false;
    assert(context.canP2PDownload({size:4*1048576}));assert(!context.canP2PDownload({size:10*1024*1048576}),'Huge files never offered in a memory-only browser');
    context.navigator.deviceMemory=.5;assert.equal(context.p2pMemoryLimit(),4*1048576,'Low-memory hints lower the Blob ceiling');assert(!context.canP2PDownload({size:5*1048576}));delete context.navigator.deviceMemory;
    context.window.showSaveFilePicker=function(){};assert(!context.canP2PDownload({size:10*1024*1048576}),'Insecure context cannot use a nominal file-picker API');context.window.isSecureContext=true;assert(context.canP2PDownload({size:10*1024*1048576}),'Large P2P is offered with trusted native disk writing');
    delete context.window.showSaveFilePicker;context.window.innerWidth=390;assert.equal(context.p2pMemoryLimit(),16*1048576);context.window.innerWidth=1280;
    const transfers=fs.readFileSync(path.join(root,'web/scripts/transfers.js'),'utf8').split('function saveRecords')[0];assert(!/p2pDownload\(/.test(transfers),'Default download never enters P2P');
    const source=fs.readFileSync(path.join(root,'web/scripts/p2p_download.js'),'utf8'),functionBody=source.slice(source.indexOf('function p2pDownload('),source.indexOf('function PeerDownload('));assert(!/modal\(|confirm\(/.test(functionBody),'Explicit P2P does not ask whether to share');
}
async function relaySender(){
    const payload=Uint8Array.from([10,20,30,40]),meta={id:'b'.repeat(48),key:'secret',path:'/relay.bin',version:'v1',size:4,pieces:1,pieceBytes:1048576,cacheMiB:4,maxPeers:4,uploadKiB:0};
    const Network=context.PeerNetwork;context.PeerNetwork=function(){this.announce=()=>{};this.close=()=>{};};
    const relay=new context.PeerRelay(meta);let reads=0;relay.hash=()=>Promise.resolve(crypto.createHash('sha256').update(payload).digest('hex'));const gate=deferred();relay.origin=()=>{reads++;return gate.promise;};
    const a=relay.readPiece(0),b=relay.readPiece(0);gate.resolve(payload);assert.equal(Buffer.from(await a).compare(Buffer.from(await b)),0);assert.equal(reads,1,'Concurrent relay pulls coalesce');await relay.readPiece(0);assert.equal(reads,1,'Relay reuses verified cache without host reads');
    context.p2pCacheClear();context.p2pSourceRead=()=>Promise.resolve(payload);let forbiddenOrigin=0;relay.origin=()=>{forbiddenOrigin++;return Promise.reject(Error('source must bypass host'));};
    assert.equal((await relay.readPiece(0)).length,4);assert.equal(forbiddenOrigin,0,'Mounted source provides verified bytes without reading host origin');delete context.p2pSourceRead;context.p2pCacheClear();relay.origin=()=>{reads++;return Promise.resolve(payload);};
    context.p2pSetSending(false);for(let i=0;i<10;i++){assert.equal(await relay.readPiece(0),null,'Page-wide opt-out applies to every subsequent event');assert.equal(relay.availability().length,0);}assert.equal(reads,1);assert.equal(context.p2pSendEnabled,false);context.p2pPolicy(true);assert.equal(context.p2pSendEnabled,false,'Host policy updates do not reset page opt-out');
    context.p2pSetSending(true);assert.equal((await relay.readPiece(0)).length,4);assert.equal(reads,2,'Re-enable refetches because opt-out erased the page cache');relay.stop();context.PeerNetwork=Network;
    const fresh=vm.createContext({Object,window:{},console});vm.runInContext(fs.readFileSync(path.join(root,'web/scripts/p2p.js'),'utf8'),fresh);assert.equal(fresh.p2pSendEnabled,true,'New document starts enabled without stored preference');
    // Turning sending off does not close the active receiver or stop its sink.
    let closed=0;const active={stopped:false,network:{announce(){},close(){closed++;}}};context.p2pJob=active;context.p2pSetSending(false);assert.equal(active.stopped,false);assert.equal(closed,0,'Sender opt-out keeps explicit P2P receiving active');context.p2pJob=null;context.p2pSendEnabled=true;
    // An opted-in idle page registers and receives one lazy assignment; disabling
    // removes both registration and worker, even across a late async response.
    let registrations=0,leaves=0;context.p2pAPI=(m,route)=>{if(route==='relay-join'){registrations++;return Promise.resolve({id:'r',key:'k'});}if(route==='relay-leave')leaves++;return Promise.resolve({assignment:null});};context.p2pRelayUpdate=realRelayUpdate;context.p2pRelayUpdate();await waitFor(()=>context.p2pRelayTimer);assert.equal(registrations,1);context.p2pSetSending(false);assert(!context.p2pRelayPage&&!context.p2pRelayTimer);await waitFor(()=>leaves===1);context.p2pRelayUpdate=()=>{};
}
async function cachePolicy(){
    context.p2pSendEnabled=true;context.p2pEnabled=true;context.p2pCacheClear();context.p2pCacheConfigure(2,3600);
    const first={path:'/a',version:'1'},second={path:'/b',version:'1'},bytes=new Uint8Array(1048576);
    context.p2pCachePut(first,0,bytes);context.p2pCachePut(second,0,bytes);context.p2pCachePut(first,1,bytes);assert.equal(context.p2pCacheBytes,2*1048576,'Two files share one page budget');assert.equal(context.p2pCacheGet(first,0),null);assert(context.p2pCacheGet(second,0));
    assert.equal(context.p2pCacheGet({path:'/b',version:'2'},0),null,'Cache is version isolated');
    const now=Date.now();context.p2pCachedFiles[context.p2pCacheName(second)]=now-3601000;context.p2pCacheTrim();assert.equal(context.p2pCacheGet(second,0),null,'One-hour idle data expires despite polls');
    context.p2pCachedFiles[context.p2pCacheName(first)]=now-3599000;context.p2pCacheTouch(first,100);assert(context.p2pCachedFiles[context.p2pCacheName(first)]>=now-110,'Host-confirmed new activity renews validity');
    const confirmed=context.p2pCachedFiles[context.p2pCacheName(first)];context.p2pCacheTouch(first,200000);assert.equal(context.p2pCachedFiles[context.p2pCacheName(first)],confirmed,'Old poll activity cannot falsely renew');
    context.p2pCacheConfigure(0,3600);assert.equal(context.p2pCacheBytes,0,'Host zero immediately frees cache');assert.equal(context.p2pSending(),false);context.p2pCacheConfigure(64,1);context.p2pCachePut(first,0,bytes);context.p2pCachedFiles[context.p2pCacheName(first)]=Date.now()-1001;context.p2pCacheTrim();assert.equal(context.p2pCacheBytes,0,'Shortened TTL evicts immediately');context.p2pCacheConfigure(64,3600);
}
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};}
async function lifecycle(){
    function fixture(){const calls={pause:0,release:0,start:0},sink={disk:true,persisted:0,existing:null,pause:()=>{calls.pause++;return Promise.resolve();},release:()=>calls.release++,resume:()=>Promise.resolve(),close:()=>Promise.resolve(),write:()=>Promise.resolve()};const job=new context.PeerDownload({name:'x',path:'/x'},{size:1048576,version:'v1'},sink);job.meta={pieces:1,pieceBytes:1048576,cacheMiB:4};return {job,sink,calls};}
    // A stopped job must release buffers even when a pending hash fails.
    for(const reject of [false,true]){const {job,calls}=fixture(),gate=deferred();job.hash=()=>gate.promise;job.step();job.stop();if(reject)gate.reject(Error('offline'));else gate.resolve('hash');await waitFor(()=>calls.release===1);assert.equal(calls.pause,1);assert(!job.running);}
    // Pausing waits for an active operation, then reauthorizes before rejoin.
    {const {job,calls}=fixture(),gate=deferred();job.hash=()=>gate.promise;job.start=()=>calls.start++;job.step();job.pause();assert.equal(calls.pause,0);gate.resolve('hash');await waitFor(()=>calls.pause===1&&!job.running);let activated=false;context.request=(method,url,body,done)=>{assert.equal(method,'POST');assert.equal(url,'/api/access/activate');activated=true;done(null,{});};job.resume();await waitFor(()=>calls.start===1);assert(activated&&!job.paused);job.stop();await waitFor(()=>calls.release===1);}
    // Cancellation during a writer reopen or final commit cannot leak it.
    for(const phase of ['resume','pause','finish']){const {job,sink,calls}=fixture(),gate=deferred();if(phase==='resume'){job.paused=true;sink.resume=()=>gate.promise;job.resume();}else if(phase==='pause'){sink.pause=()=>{calls.pause++;return gate.promise;};job.paused=true;job.settlePause();}else{sink.close=()=>gate.promise;job.finish();}job.stop();gate.resolve();await waitFor(()=>calls.release===1);assert(!job.running);assert(!job.complete);}
}
async function diskSink(){
    let stored=Buffer.alloc(1048576+17,3),opened=0,commits=0;
    const handle={getFile:async()=>{const bytes=Buffer.from(stored);return {size:bytes.length,slice:(a,b)=>({bytes:bytes.subarray(a,b)})};},createWritable:async options=>{assert(options.keepExistingData);assert.equal(opened++,0,'Only one COW writer is open');let draft=Buffer.from(stored);return {write:async value=>{const end=value.position+value.data.length;if(end>draft.length)draft=Buffer.concat([draft,Buffer.alloc(end-draft.length)]);Buffer.from(value.data).copy(draft,value.position);},truncate:async n=>{draft=draft.subarray(0,n);},close:async()=>{stored=draft;opened--;commits++;}};}};
    const sink=await context.p2pDiskSink(handle),piece=Uint8Array.from([4,5,6]);await sink.write(piece,1,1048576);assert.equal(stored[1048576],3,'Private writes are not visible before commit');await sink.checkpoint(2,true);assert.equal(opened,1);assert.equal(stored[1048576],4);assert.equal(Buffer.from(await sink.read(1,1048576,stored.length))[0],4);await sink.pause(2);assert.equal(opened,0);await sink.resume();await sink.close(1048576+3);assert.equal(opened,0);assert.equal(stored.length,1048576+3);assert.equal(commits,3);sink.release();assert.equal(sink.existing,null);
}
run().catch(e=>{console.error(e);process.exitCode=1;});
