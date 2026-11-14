// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// Execute the shipped ZIP writer against a live server, without a browser or UI.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const http = require('node:http');
const assert = require('node:assert/strict');
const origin = process.argv[2], output = process.argv[3], managed=process.argv[4]==='managed';
if (!origin || !output) throw Error('Usage: node tests/archive_test.cjs http://127.0.0.1:8000 output.zip');
const elements = {}, chunks = [];
let completed, rejected, requests = 0;
const done = new Promise((resolve, reject) => { completed = resolve; rejected = reject; });
class XHR {
    open(method, url) { this.method = method; this.url = url; this.headers = {}; }
    setRequestHeader(name, value) { this.headers[name] = value; }
    getResponseHeader(name) { return this.responseHeaders[name.toLowerCase()] || null; }
    abort() { if (this.task) this.task.destroy(); }
    send() {
        requests++;
        this.task = http.request(new URL(this.url, origin), {method:this.method, headers:this.headers}, response => {
            this.status = response.statusCode; this.responseHeaders = response.headers;
            const buffers = []; let length = 0;
            response.on('data', bytes => { buffers.push(bytes); length += bytes.length; if(this.onprogress)this.onprogress({loaded:length}); });
            response.on('end', () => {
                const bytes = Buffer.concat(buffers);
                this.response = bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength);
                if(this.onload)this.onload();
            });
        });
        this.task.on('error', error => { if(this.onerror)this.onerror(error); else rejected(error); });
        this.task.end();
    }
}
const context = {
    console, window:{},session:{namespaceId:'archive-http-fixture'},localStorage:{setItem(){},getItem(){return null;},removeItem(){}},browserID:'archive-http-fixture',clearTimeout,Math,JSON,Object,Date,
    join:(p,n)=>(p==='/'?'':p)+'/'+n,parent:p=>p.slice(0,p.lastIndexOf('/'))||'/',current:()=>null,
    console, Uint8Array, Uint32Array, DataView, ArrayBuffer, setTimeout,
    fflate: require('../web/vendor/fflate.js'), XMLHttpRequest: XHR,
    $: id => elements[id] || (elements[id] = {style:{}}), show:()=>{}, text:()=>{}, on:()=>{},
    enc:encodeURIComponent, unescape, encodeURIComponent,
    query: values => Object.keys(values).map(key => encodeURIComponent(key)+'='+encodeURIComponent(values[key])).join('&'),
    base: value => value.split('/').pop(), size:String, siteTitle:'Fixture', records:[], saveRecords:()=>{}, drawerMode:'',
    toast: message => { if(!message.startsWith('ZIP 已生成'))rejected(Error(message)); },
    request: (method,url,body,callback) => {
        const xhr = new XHR(); xhr.open(method,url); xhr.onload=()=>{ try { callback(xhr.status===200?null:'HTTP '+xhr.status,JSON.parse(Buffer.from(xhr.response).toString())); } catch(error){rejected(error);} };
        xhr.onerror=rejected; xhr.send(); return xhr;
    }
};
vm.createContext(context);if(managed)for(const name of ['tasks.js','jobs.js'])vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/scripts',name),'utf8'),context);
vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/scripts/frozen.js'),'utf8'),context);
vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/scripts/archive.js'),'utf8'),context);
const entry={path:'/archive-fixture',name:'archive-fixture',directory:true};const task=managed?context.taskCreate('ZIP','fixture.zip',entry.path,0):null;context.runArchive(entry,'fixture.zip',{
    write:(bytes,next)=>{ chunks.push(Buffer.from(bytes)); next(); },
    close:next=>{ fs.writeFileSync(output,Buffer.concat(chunks)); next(); completed(); },
    abort:()=>rejected(Error('Archive aborted'))
},task,managed?{root:entry,filename:'fixture.zip',plan:null}:null);
const deadline=setTimeout(()=>rejected(Error('Archive timed out')),30000);
done.then(()=>{clearTimeout(deadline);if(managed){assert.equal(task.state,'done');assert.equal(task.done,task.total);assert.equal(context.transferSlots,0);}assert.ok(requests>=8,'Paged directories and multiple file ranges were used');console.log('ZIP64 writer passed:',requests,'requests,',fs.statSync(output).size,'bytes');},error=>{clearTimeout(deadline);console.error(error);process.exitCode=1;});
