// SPDX-License-Identifier: Apache-2.0
// Run shipped ES5 upload transport against the real C++ server. Bounded FileReader/XHR doubles.
const fs=require('fs'),vm=require('vm'),http=require('http'),path=require('path'),os=require('os'),assert=require('assert'),cp=require('child_process'),net=require('net');
const executable=path.resolve(process.argv[2]),root=fs.mkdtempSync(path.join(os.tmpdir(),'hls-resume-js-'));
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function port(){return new Promise(resolve=>{const s=net.createServer();s.listen(0,'127.0.0.1',()=>{const p=s.address().port;s.close(()=>resolve(p));});});}
async function main(){
    const listen=await port(),server=cp.spawn(executable,['--no-config','--root',root,'--bind','127.0.0.1','--port',String(listen),'--write-mode','shared','--no-magika','--max-transfers','1'],{stdio:['ignore','pipe','pipe'],windowsHide:true});let log='';server.stderr.on('data',x=>log+=x);server.stdout.on('data',x=>log+=x);
    function api(route,method='GET',body=null,token=''){return new Promise((resolve,reject)=>{const bytes=body===null?null:Buffer.from(typeof body==='string'?body:JSON.stringify(body));const req=http.request({host:'127.0.0.1',port:listen,path:route,method,headers:{...(token?{'X-HLS-Token':token}:{}),...(bytes?{'Content-Length':bytes.length}:{})}},res=>{const chunks=[];res.on('data',x=>chunks.push(x));res.on('end',()=>resolve({status:res.statusCode,data:JSON.parse(Buffer.concat(chunks).toString())}));});req.on('error',reject);req.end(bytes);});}
    try{
        let session;for(let i=0;i<100;i++){try{session=(await api('/api/session')).data;break;}catch{if(server.exitCode!==null)throw Error(log);await sleep(30);}}assert(session,'Server ready');
        const storage=new Map();let lost=false,simulateLostAck=false;
        class XHR{
            constructor(){this.upload={};this.headers={};this.aborted=false;}
            open(method,url){this.method=method;this.url=url;}
            setRequestHeader(k,v){this.headers[k]=v;}
            async send(body){const bytes=body instanceof Blob?Buffer.from(await body.arrayBuffer()):body===null?null:Buffer.from(body||'');if(this.aborted)return;if(bytes)this.headers['Content-Length']=bytes.length;
                this.req=http.request({host:'127.0.0.1',port:listen,path:this.url,method:this.method,headers:this.headers},res=>{const chunks=[];res.on('data',x=>chunks.push(x));res.on('end',()=>{if(this.aborted)return;this.status=res.statusCode;this.responseText=Buffer.concat(chunks).toString();if(simulateLostAck&&!lost&&this.url.includes('/api/resume/chunk')){lost=true;if(this.onerror)this.onerror();return;}if(this.onload)this.onload();});});this.req.on('error',()=>{if(!this.aborted&&this.onerror)this.onerror();});this.req.end(bytes);}
            abort(){this.aborted=true;if(this.req)this.req.destroy();}
        }
        class Reader{async readAsArrayBuffer(blob){try{this.result=await blob.arrayBuffer();if(this.onload)this.onload();}catch(e){if(this.onerror)this.onerror(e);}}}
        const ctx=vm.createContext({console,Date,Math,JSON,Object,Array,Uint8Array,Uint32Array,DataView,ArrayBuffer,Blob,FileReader:Reader,XMLHttpRequest:XHR,setTimeout,clearTimeout,setInterval,clearInterval,session,window:{FileReader:Reader,Uint8Array},localStorage:{getItem:k=>storage.get(k)||null,setItem:(k,v)=>storage.set(k,v),removeItem:k=>storage.delete(k)},drawerMode:'',enc:encodeURIComponent,query:o=>new URLSearchParams(o).toString(),current:()=>null,toast(){},size:n=>String(n),esc:String,request(method,route,body,done){api(route,method,body,session.token).then(r=>done(r.status<300?null:r.data.error,r.data),e=>done(e.message));}});
        for(const name of ['tasks.js','p2p_hash.js','resume.js'])vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/scripts',name),'utf8'),ctx,{filename:name});
        const data=Buffer.alloc(3*1048576+101,0x61);data[1048576+500]=0x62;
        function file(bytes){const blob=new Blob([bytes]);return {name:'source.bin',size:blob.size,lastModified:123456,slice:(a,b)=>blob.slice(a,b)};}
        const source=file(data),first=ctx.resumableXHR('/recovered.bin','');first.open('PUT','/api/upload?path=%2Frecovered.bin&parents=1');
        const paused=new Promise(resolve=>{first.upload.onprogress=e=>{if(e.loaded>=2*1048576){first.task.pause();resolve();}};});first.send(source);await paused;
        assert.equal(first.task.state,'paused');assert.equal(first.task.done,2*1048576);assert(!fs.existsSync(path.join(root,'recovered.bin')),'No partial publication');
        const changed=Buffer.from(data);changed[1048576+500]^=0xff;
        const mismatch=ctx.resumableXHR('/recovered.bin','',first.task);mismatch.open('PUT',first.task.resume.target);
        const rejected=new Promise(resolve=>mismatch.onload=()=>resolve(mismatch.status));mismatch.send(file(changed));assert.equal(await rejected,409,'A middle-only source change is detected despite equal metadata and edge samples');
        const restored=ctx.resumableXHR('/recovered.bin','',first.task);restored.open('PUT',first.task.resume.target);const completed=new Promise(resolve=>restored.onload=()=>resolve(restored.status));restored.send(source);assert.equal(await completed,201);assert(fs.readFileSync(path.join(root,'recovered.bin')).equals(data));
        simulateLostAck=true;const retry=ctx.resumableXHR('/lost-ack.bin','');retry.open('PUT','/api/upload?path=%2Flost-ack.bin');const retried=new Promise(resolve=>retry.onload=()=>resolve(retry.status));retry.send(source);assert.equal(await retried,201);assert(lost,'Lost acknowledgement was exercised');assert(fs.readFileSync(path.join(root,'lost-ack.bin')).equals(data));
        const state=(await api('/api/admin/state')).data;assert.equal(state.quotas[0].used,data.length*2,'Lost acknowledgement does not double-charge');
        for(const name of ['transfer_cache.js','mounts.js'])vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/scripts',name),'utf8'),ctx,{filename:name});
        ctx.base=value=>value.split('/').pop();
        const mounted=(await api('/api/mount/register','POST',{path:'/Mounted',browser:'resume-mounted-page-123456',writable:true,movable:false},session.token)).data;
        const stage=ctx.mountedXHR('/Mounted/upload.bin','');const staged=new Promise(resolve=>{stage.upload.onprogress=e=>{if(e.loaded>=2*1048576){stage.task.pause();resolve();}};});stage.send(source);await staged;const saved=JSON.parse(storage.get('hls-tasks-v1')).find(t=>t.id===stage.task.id);assert.equal(saved.done,2*1048576);assert(saved.job.cache);
        const mountMismatch=ctx.mountedXHR('/Mounted/upload.bin','',saved);const mountRejected=new Promise(resolve=>mountMismatch.onload=()=>resolve(mountMismatch.status));mountMismatch.send(file(changed));assert.equal(await mountRejected,409,'Mounted source prefix is fully checked after reselection');
        let polling=true,providerError=null,delivered=Buffer.alloc(0),sequence=0;const providerFiles=new Map();
        const provider=(async()=>{while(polling){const polled=await api('/api/mount/poll','POST',{providers:[{id:mounted.id,key:mounted.key}]},session.token);for(const job of polled.data.jobs){let q=new URLSearchParams({mount:mounted.id,key:mounted.key,id:job.id}).toString(),reply={},status=200;
            if(job.op==='stat'){const value=providerFiles.get(job.path);if(value)reply={directory:false,size:value.bytes.length,modified:0,version:value.version};else{status=404;reply={error:'Not found'};}}
            else if(job.op==='delete'){const value=providerFiles.get(job.path);if(!value||value.version!==job.version){status=409;reply={error:'Source changed'};}else providerFiles.delete(job.path);}
            else if(job.op==='write-cache'){const chunks=[];for(let at=0;at<job.size;at+=262144){const last=Math.min(job.size,at+262144);chunks.push(await new Promise((resolve,reject)=>{const request=http.get({host:'127.0.0.1',port:listen,path:'/api/mount/payload?'+q,headers:{Range:'bytes='+at+'-'+(last-1)}},r=>{assert.equal(r.statusCode,206);const b=[];r.on('data',v=>b.push(v));r.on('end',()=>resolve(Buffer.concat(b)));});request.on('error',reject);}));}delivered=Buffer.concat(chunks);const version='mounted-source-v'+(++sequence);providerFiles.set(job.path,{bytes:delivered,version});reply={size:job.size,version};}
            else throw Error('Unexpected provider op '+job.op);
            await api('/api/mount/reply?'+q+'&status='+status,'POST',reply,session.token);
        }await sleep(20);}})().catch(e=>{providerError=e;});
        try{
            const mountRestored=ctx.mountedXHR('/Mounted/upload.bin','',saved),mountDone=new Promise(resolve=>mountRestored.onload=()=>resolve(mountRestored.status));mountRestored.send(source);assert.equal(await mountDone,201);assert(delivered.equals(data),'Exact mounted upload after prefix recovery');assert.equal((await api('/api/transfer-cache/status?'+new URLSearchParams(saved.job.cache))).data.offset,data.length);
            ctx.withinPath=(r,p)=>p===r||p.startsWith(r+'/');ctx.join=(p,n)=>(p==='/'?'':p)+'/'+n;ctx.parent=p=>p.slice(0,p.lastIndexOf('/'))||'/';ctx.findMount=p=>p.startsWith('/Mounted/')?{path:'/Mounted',browser:true,writable:true,movable:false}:null;ctx.frozenEntry=()=>false;ctx.refresh=()=>{};
            for(const name of ['jobs.js','batch.js','clipboard.js'])vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/scripts',name),'utf8'),ctx,{filename:name});ctx.taskConflict='replace';
            async function finished(task){for(let i=0;i<1000;i++){if(/^(done|failed)$/.test(task.state)){assert.equal(task.state,'done',task.error);return;}await sleep(5);}throw Error('Cross-mount transfer timed out');}
            const copy=ctx.startCopyJob({name:'recovered.bin',path:'/recovered.bin',size:data.length,directory:false},'/Mounted/copy.bin');await finished(copy);assert(providerFiles.get('/copy.bin').bytes.equals(data));assert(fs.readFileSync(path.join(root,'Mounted/copy.bin')).equals(data),'Copied provider bytes are immediately retained on host');
            await new Promise((resolve,reject)=>ctx.renameVersioned('/Mounted/upload.bin','/Mounted/renamed.bin',providerFiles.get('/upload.bin').version,error=>error?reject(Error(error)):resolve()));assert(!providerFiles.has('/upload.bin'));assert(providerFiles.get('/renamed.bin').bytes.equals(data),'Rename works without native handle.move');
            const move=ctx.startCopyJob({name:'copy.bin',path:'/Mounted/copy.bin',size:data.length,directory:false,version:providerFiles.get('/copy.bin').version},'/moved-from-mount.bin',null,true);await finished(move);assert(!providerFiles.has('/copy.bin'));assert(!fs.existsSync(path.join(root,'Mounted/copy.bin')));assert(fs.readFileSync(path.join(root,'moved-from-mount.bin')).equals(data));
        }finally{polling=false;await provider;}if(providerError)throw providerError;
        console.log('Resumable browser transport passed: bounded slices, pause/reselect, middle-byte rejection, prefix chain, lost-ack recovery, durable quota, staged mounted upload and cross-mount copy/move');
    }finally{if(server.exitCode===null){server.kill();await new Promise(r=>server.once('exit',r));}fs.rmSync(root,{recursive:true,force:true});}
}
const timeout=setTimeout(()=>{console.error('Upload transport test timed out');process.exitCode=1;},30000);main().then(()=>clearTimeout(timeout),e=>{clearTimeout(timeout);console.error(e);process.exitCode=1;});
