// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const root=path.resolve(__dirname,'..');
function load(ctx,name){vm.runInContext(fs.readFileSync(path.join(root,'web/scripts',name),'utf8'),ctx,{filename:name});}
async function run(){
    const ctx=vm.createContext({setTimeout,clearTimeout,console});load(ctx,'dragdrop.js');
    function file(name){return {name,isFile:true,file(done){setTimeout(()=>done({name,size:1}),0);}};}
    function folder(name,batches){return {name,isDirectory:true,createReader(){let i=0;return {readEntries(done){done(batches[i++]||[]);}};}};}
    const child=folder('nested',[[file('中文.txt')],[]]),first=Array.from({length:105},(_,i)=>file('file-'+i+'.txt'));
    const directory=folder('Folder',[first,[child,file('tail.txt')],[]]);
    const collect=data=>new Promise((resolve,reject)=>ctx.collectDroppedFiles(data,(error,files)=>error?reject(Error(error)):resolve(files)));
    const collected=await collect({items:[{webkitGetAsEntry:()=>directory}],files:[]});
    assert.equal(collected.length,107,'readEntries is called until every batch has been read');
    assert(collected.some(x=>x.relativePath==='Folder/nested/中文.txt'),'Nested folder names and Unicode survive traversal');
    assert(collected.some(x=>x.relativePath==='Folder/tail.txt'),'Items beyond the first browser batch are uploaded');
    assert.equal((await collect({items:[{webkitGetAsEntry:()=>folder('Empty',[[]])}],files:[]})).length,0,'Empty directories create no phantom files');
    let depth=file('last.txt');for(let i=0;i<66;i++)depth=folder('d',[[depth],[]]);
    await assert.rejects(()=>collect({items:[{webkitGetAsEntry:()=>depth}],files:[]}),/层级/,'Deep trees fail within the traversal budget');
    const plain={name:'plain.txt'};assert.strictEqual((await collect({items:[],files:[plain]}))[0],plain,'Old browsers retain plain FileList uploads');

    const elements={},timers=new Map();let timerID=0,requests=[],refreshes=0;
    function el(id){return elements[id]||(elements[id]={className:'hidden',innerHTML:'',textContent:'',value:'',style:{}});}
    const t={path:'/folder',generation:3,stamp:'old',aliasRevision:1,loading:false,needsRefresh:false};
    const watch=vm.createContext({console,Math,Date,window:{},doc:{hidden:false,activeElement:null},standalonePath:'',session:{localAdmin:false,token:'token'},tabs:[t,{path:'/folder'},{path:'/other'}],
        $:el,current:()=>t,text:(e,s)=>e.textContent=s,show:(e,yes)=>e.className=yes?'':'hidden',applySite(){},schedule(){},nodes:[],uploadBusy:false,fileDrag:null,dropNode:null,tabDrag:null,
        refresh(preserve){assert.strictEqual(preserve,true);refreshes++;t.needsRefresh=false;},
        request(method,url,body,done){requests.push({method,url,body,done});},
        setTimeout(fn,delay){timers.set(++timerID,{fn,delay});return timerID;},clearTimeout(id){timers.delete(id);},localStorage:{setItem(){}}});
    load(watch,'collaboration.js');watch.showHidden=false;watch.pollBrowse();watch.pollBrowse();assert.equal(requests.length,1,'Only one heartbeat may be in flight');
    assert.deepEqual(Array.from(requests[0].body.paths),['/folder','/other'],'All tabs share one deduplicated lease request');
    requests[0].done(null,{stamp:'new',aliasRevision:1});assert(t.needsRefresh&&refreshes===0,'Default directory changes only show a refresh notice');
    assert.equal(el('directory-update').className,'');assert([...timers.values()].some(x=>x.delay>=12000&&x.delay<15000),'Visible polling has randomized bounded spacing');
    watch.setAutoRefresh(true);assert.equal(refreshes,1,'Visitor can opt into automatic refresh');
    t.needsRefresh=true;watch.uploadBusy=true;watch.maybeAutoRefresh();assert.equal(refreshes,1,'Auto refresh does not interrupt a file upload');
    watch.uploadBusy=false;watch.doc.hidden=true;watch.maybeAutoRefresh();assert.equal(refreshes,1,'Hidden documents do not render automatic refreshes');
    watch.doc.hidden=false;watch.pollBrowse();const late=requests[1];t.generation++;t.needsRefresh=false;late.done(null,{stamp:'stale',aliasRevision:2});assert.equal(t.needsRefresh,false,'Late heartbeat cannot mark a new navigation stale');
    t.path='/_clients/192.0.2.10/folder';t.pages={0:{items:[{path:t.path+'/file.txt'}]}};watch.pollBrowse();
    assert.equal(requests[2].body.owners[0],'/_clients/192.0.2.10','Only visible owner names are requested');
    requests[2].done(null,{stamp:'old',aliasRevision:3,names:{'/_clients/192.0.2.10':'Reviewed person'}});
    assert.equal(t.pages[0].items[0].ownerName,'Reviewed person','Approved name updates cached metadata directly');assert.equal(t.needsRefresh,false,'Name approval does not reload the directory or require manual refresh');
    console.log('Folder/updates tests passed: batched recursive traversal, empty/deep folders, Unicode, legacy upload, one request in flight, jitter, opt-in refresh and stale-response guard');
}
run().catch(error=>{console.error(error);process.exitCode=1;});
