// SPDX-License-Identifier: Apache-2.0
// File-manager clipboard replay; the real copy transport is tested over HTTP.
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const elements={},events=new Map(),calls=[],toasts=[],files=new Set(),folders=new Set(['/Dest']),stored={};
function el(id){return elements[id]||(elements[id]={id,tagName:'DIV',style:{},className:id==='modal-shade'?'hidden':'',children:[],appendChild(n){this.children.push(n);},setAttribute(){},getAttribute(){return null;},getBoundingClientRect(){return {left:0,top:0};}});}
const doc=el('document'),win=el('window');win.innerWidth=1000;win.innerHeight=700;doc.createElement=()=>el('created'+Math.random());let dialog,active=0,held=null,hold=false;
const tree={'/Source':[{path:'/Source/config.json',name:'config.json',frozen:true,directory:false},{path:'/Source/one.txt',name:'one.txt',directory:false,size:5},{path:'/Source/Sub',name:'Sub',directory:true},{path:'/Source/.hidden',name:'.hidden',directory:false,size:5}],'/Source/Sub':[{path:'/Source/Sub/two.txt',name:'two.txt',directory:false,size:5}]};
const ctx=vm.createContext({console,doc,window:win,session:{writable:true},tabs:[{path:'/Source'},{path:'/Dest'}],zipBusy:false,zipCancel:null,selected:null,browserID:'clipboard-test-page',JSON,Date,Math,Number,encodeURIComponent,Object,Array,isFinite,drawerMode:'',
    localStorage:{getItem:k=>stored[k],setItem(k,v){stored[k]=v;},removeItem(k){delete stored[k];}},$(id){return el(id);},on(target,name,fn){if(!events.has(target))events.set(target,{});events.get(target)[name]=fn;},text(target,value){target.textContent=value;},show(){},toast(s){toasts.push(s);},setTimeout(fn,ms){if(!ms)fn();},clearTimeout(){},current(){return ctx.tabs[active];},canManage(e){return !!e&&!e.frozen;},canMoveEntry(){return true;},canWrite(p){return ctx.session.writable&&p.startsWith('/Dest');},withinPath(a,b){return a===b||b.startsWith(a+'/');},base(p){return p.slice(p.lastIndexOf('/')+1);},parent(p){return p.slice(0,p.lastIndexOf('/'))||'/';},join(p,n){return (p==='/'?'':p)+'/'+n;},enc:encodeURIComponent,query(v){return new URLSearchParams(v).toString();},modal(title,message,value,actions){dialog={title,value,actions};},cancel:{label:'取消',run(){}},refresh(){},reset(){},selectedFrom(){return null;},uploadTarget(p){return p;},upload(files,path){ctx.lastUpload={files,path};},findMount(){return null;},
    request(method,target,body,done){calls.push({method,target});const url=new URL(target,'http://localhost'),p=url.searchParams.get('path'),to=url.searchParams.get('to');let data={},error=null;
        if(url.pathname==='/api/mkdir'){if(folders.has(p))error='Destination already exists';else folders.add(p);}
        else if(url.pathname==='/api/upload-target')data={exists:files.has(p)||folders.has(p),directory:folders.has(p),version:'confirmed'};
        else if(url.pathname==='/api/list'){assert.equal(url.searchParams.get('showHidden'),'1');data={entries:tree[p]||[],total:(tree[p]||[]).length,stamp:'stable'};}
        else if(url.pathname==='/api/download-ticket')data={url:'/download?path='+encodeURIComponent(p),version:'v1',size:5};
        else if(url.pathname==='/api/copy'){assert.equal(url.searchParams.get('version'),'v1');if(files.has(to))error='Destination already exists';else if(hold){held=()=>{files.add(to);done(null,{});};return {abort(){held=null;}};}else files.add(to);}
        done(error,data);return {abort(){}};
    }
});
vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/scripts/frozen.js'),'utf8'),ctx);
for(const script of ['tasks.js','jobs.js','batch.js','clipboard.js'])vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/scripts',script),'utf8'),ctx);ctx.initClipboard();
ctx.copyEntry({path:'/config.json',name:'config.json',frozen:true});assert.equal(ctx.fileClipboard,null,'Frozen configuration cannot enter the clipboard');
ctx.copyEntry({path:'/Source',name:'Source',directory:true});active=1;ctx.pasteEntry('/Dest');assert.equal(dialog.value,'Source');dialog.actions[1].run('Copied');
assert(files.has('/Dest/Copied/one.txt')&&files.has('/Dest/Copied/Sub/two.txt')&&files.has('/Dest/Copied/.hidden'),'Folder copy preserves hierarchy and hidden contents across application tabs');assert(!ctx.copyBusy&&!ctx.zipBusy);
ctx.copyEntry({path:'/Dest/report.txt',name:'report.txt',directory:false});ctx.pasteEntry('/Dest');assert.equal(dialog.value,'report - 副本.txt');dialog.actions[1].run('report - 副本.txt');assert(files.has('/Dest/report - 副本.txt'));
ctx.pasteEntry('/Dest');dialog.actions[1].run('report - 副本.txt');assert.equal(dialog.title,'存在同名项目','Conflicting copies ask instead of silently skipping');dialog.actions[0].run();
const before=calls.length;ctx.copyEntry({path:'/Source',name:'Source',directory:true});ctx.pasteEntry('/Source/Sub');assert.equal(calls.length,before,'A folder cannot be copied into itself');ctx.pasteEntry('/Dest');dialog.actions[1].run('../escape');assert.equal(calls.length,before,'Invalid destination names never reach the API');
ctx.copyEntry({path:'/Source/one.txt',name:'one.txt'});hold=true;ctx.pasteEntry('/Dest');dialog.actions[1].run('Cancelled.txt');assert.equal(ctx.transferTasks[0].state,'running');ctx.transferTasks[0].cancel();assert(ctx.transferTasks[0].state==='cancelled'&&!files.has('/Dest/Cancelled.txt'),'Cancellation releases the transfer UI and aborts the active request');hold=false;
events.get(doc).paste({target:{tagName:'DIV'},clipboardData:{files:[{name:'pasted.png'}]},preventDefault(){}});assert.equal(ctx.lastUpload.path,'/Dest','Native clipboard files take precedence over internal file references');
ctx.session.writable=false;const count=calls.length;ctx.pasteEntry('/Dest');assert.equal(calls.length,count,'Read-only destinations reject paste');
console.log('Clipboard tests passed: cross-tab hierarchy, hidden entries, pinned versions, no-clobber conflicts, self/traversal protection, cancellation and native clipboard files');

// Cuts retain failed, skipped and in-flight items; successful moves are consumed once.
ctx.session.writable=true;const cutA={path:'/Source/Sub',name:'Sub',directory:true},cutB={path:'/Source/one.txt',name:'one.txt'};
ctx.clipboardSet([cutA,cutB],true);let moveJob=null;ctx.batchAction=(action,items,destination,done)=>{moveJob={action,items,destination,done};};
ctx.pasteEntry('/Dest');assert.equal(moveJob.action,'move');assert.equal(moveJob.items.length,2);assert.equal(ctx.clipboardItems().length,2,'An in-flight cut remains available after submission');
moveJob.done(cutA);assert.deepEqual(Array.from(ctx.clipboardItems(),e=>e.path),[cutB.path],'Only successful items leave a partial cut');moveJob.done(cutB);assert.equal(ctx.fileClipboard,null);assert.equal(stored['hls-file-clipboard'],undefined);
ctx.clipboardSet([cutA],true);ctx.canWrite=()=>true;moveJob=null;ctx.pasteEntry('/Source/Sub/deeper');assert.equal(moveJob,null,'Cut cannot paste a folder into its descendant');assert(ctx.fileClipboard,'Rejected cuts remain recoverable');
const oldID=ctx.fileClipboard.id;ctx.copyEntry(cutB);ctx.clipboardMoved(oldID,cutB.path);assert.equal(ctx.fileClipboard.mode,'copy','An older move never consumes a newly copied clipboard');
console.log('Cut tests passed: multiple entries, cross-tab routing, partial-success consumption, rejected descendant moves and clipboard replacement races');
