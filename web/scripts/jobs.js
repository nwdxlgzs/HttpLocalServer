// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// Persist metadata and browser-granted handles, never downloaded file bodies.
var jobDatabase=null,jobMemory={},jobOpenWait=[],jobOpening=false,jobDialogQueue=[],jobDialogBusy=false;
function jobDB(done){
    if(jobDatabase){done(jobDatabase);return;}if(!window.indexedDB){done(null);return;}
    jobOpenWait.push(done);if(jobOpening)return;jobOpening=true;
    function finish(db){jobDatabase=db;jobOpening=false;var pending=jobOpenWait;jobOpenWait=[];for(var i=0;i<pending.length;i++)pending[i](db);}
    try{var opening=window.indexedDB.open('hls-transfer-jobs',1);opening.onupgradeneeded=function(){opening.result.createObjectStore('jobs');};opening.onsuccess=function(){var db=opening.result;db.onversionchange=function(){db.close();jobDatabase=null;};finish(db);};opening.onerror=opening.onblocked=function(){finish(null);};}catch(ignore){finish(null);}
}
function jobSnapshot(value){var snapshot={},key;for(key in value)if(value.hasOwnProperty(key)&&key!=='plan')snapshot[key]=value[key];if(value.plan){snapshot.plan={index:value.plan.index,bytes:value.plan.bytes,total:value.plan.total};snapshot.planRef=true;snapshot.current=value.plan.items[value.plan.index]||null;}return snapshot;}
function jobSave(t,value,done){
    if(t.state==='cancelled'||t.state==='done'){if(done)done();return;}if(!value.namespace&&session)value.namespace=session.namespaceId||'';value.savedAt=new Date().getTime();jobMemory[t.id]=value;t.recovery=value;var finished=false;
    function complete(saved){if(finished)return;finished=true;function finish(recoverable){var changed=false;if(t.job){var warning=recoverable?'':'此浏览器无法保存恢复信息；请保持本页打开';changed=t.job.recoverable!==recoverable||t.job.warning!==warning;t.job.recoverable=recoverable;t.job.warning=warning;}if(changed)taskSave();taskRenderSoon();if(done)done();}if(typeof transferCheckpoint==='function'&&value.cache&&value.cache.serverJournal)transferCheckpoint(t,value,function(error){if(error){if(t.checkpointError)t.checkpointError(new Error(error));else jobFail(t,error);return;}finish(true);});else finish(saved);}
    jobDB(function(db){var snapshot=jobSnapshot(value);if(!db){try{if(value.handle){complete(false);return;}if(value.plan&&!t.planStoredLocal){var items=JSON.stringify(value.plan.items);if(items.length>1500000){complete(false);return;}localStorage.setItem('hls-job:'+t.id+':plan',items);t.planStoredLocal=true;}var json=JSON.stringify(snapshot);if(json.length>1500000){complete(false);return;}localStorage.setItem('hls-job:'+t.id,json);complete(true);}catch(ignore){complete(false);}return;}
        try{var tx=db.transaction('jobs','readwrite'),store=tx.objectStore('jobs');if(value.plan&&!t.planStored)store.put(value.plan.items,t.id+':plan');store.put(snapshot,t.id);tx.oncomplete=function(){if(value.plan)t.planStored=true;complete(true);};tx.onerror=tx.onabort=function(){complete(false);};}catch(ignore){complete(false);}
    });
}
function jobLoad(t,done,localOnly){
    if(t.recovery||jobMemory[t.id]){done(t.recovery||jobMemory[t.id]);return;}
    if(!localOnly&&t.job&&t.job.cache&&typeof transferRestore==='function'){transferRestore(t,function(value){if(value)done(value);else jobLoad(t,done,true);});return;}
    function restore(value,items){if(!value&&typeof transferRestore==='function'){transferRestore(t,done);return;}if(value&&value.planRef){if(!items){done(null);return;}value.plan.items=items;if(value.current&&value.plan.index<items.length)items[value.plan.index]=value.current;delete value.current;delete value.planRef;}t.recovery=value;done(value);}
    jobDB(function(db){if(!db){try{var value=JSON.parse(localStorage.getItem('hls-job:'+t.id)||'null');restore(value,value&&value.planRef?JSON.parse(localStorage.getItem('hls-job:'+t.id+':plan')||'null'):null);t.planStoredLocal=!!value;}catch(ignore){done(null);}return;}
        try{var r=db.transaction('jobs','readonly').objectStore('jobs').get(t.id);r.onsuccess=function(){var value=r.result;if(value&&value.planRef){var plan=db.transaction('jobs','readonly').objectStore('jobs').get(t.id+':plan');plan.onsuccess=function(){t.planStored=true;restore(value,plan.result);};plan.onerror=function(){done(null);};}else restore(value,null);};r.onerror=function(){done(null);};}catch(ignore){done(null);}
    });
}
function jobForget(t){delete jobMemory[t.id];t.recovery=null;try{localStorage.removeItem('hls-job:'+t.id);localStorage.removeItem('hls-job:'+t.id+':plan');}catch(ignore){}jobDB(function(db){if(db)try{var store=db.transaction('jobs','readwrite').objectStore('jobs');store['delete'](t.id);store['delete'](t.id+':plan');}catch(ignore){}});}
function jobFail(t,error){if(t.state==='cancelled'||t.state==='done')return;taskState(t,'failed',error&&error.message||String(error));}
function jobResume(t){
    if(t.slot||t.restoring)return;var value=t.recovery;if(!value){jobLoad(t,function(v){if(v){t.recovery=v;taskState(t,'needs-output','检查点已读取，请点击继续任务');}else jobFail(t,'找不到任务检查点，请重新发起');});return;}
    if(value.namespace&&(!session||value.namespace!==session.namespaceId)){jobFail(t,'服务器共享目录已改变，拒绝恢复旧任务');return;}function run(){t.restoring=false;if(value.type==='zip')resumeArchive(t,value);else if(value.type==='directory')runDirectoryJob(t,value);else if(value.type==='copy')runCopyJob(t,value);else if(value.type==='p2p')resumePeerJob(t,value);}
    if(value.handle&&value.handle.requestPermission){t.restoring=true;var permission;try{permission=value.handle.requestPermission({mode:'readwrite'});}catch(error){t.restoring=false;jobFail(t,error);return;}permission.then(function(result){if(result==='granted')run();else{t.restoring=false;jobFail(t,'需要原输出位置的写入授权，请点击继续任务重试');}},function(error){t.restoring=false;jobFail(t,error);});}else run();
}
function jobAttach(t,type,value){t.job={type:type};if(value.cache)t.job.cache={id:value.cache.id,key:value.cache.key};t.retry=function(){jobResume(t);};t.continueJob=t.retry;jobSave(t,value);taskControls(t);}
function jobRestore(t){if(!t.job)return;if(t.job.type==='mount-upload'){if(typeof mountUploadRestore==='function')mountUploadRestore(t);return;}t.cancel=function(){jobCancel(t);};t.continueJob=function(){jobResume(t);};t.retry=t.continueJob;jobLoad(t,function(v){if(v)t.recovery=v;});}
function jobStart(t,run,lane){taskState(t,'queued');taskQueue(t,run,lane);}
function jobDialog(title,message,actions,escape){jobDialogQueue.push({title:title,message:message,actions:actions,escape:escape});jobDialogPump();}
function jobDialogPump(){
    if(jobDialogBusy||!jobDialogQueue.length)return;if($('modal-shade').className.indexOf('hidden')<0){setTimeout(jobDialogPump,150);return;}
    var item=jobDialogQueue.shift(),closed=false;jobDialogBusy=true;
    function close(run){return function(){if(closed)return;closed=true;jobDialogBusy=false;run();setTimeout(jobDialogPump,0);};}
    var actions=[];for(var i=0;i<item.actions.length;i++)actions.push({label:item.actions[i].label,primary:item.actions[i].primary,run:close(item.actions[i].run)});
    modal(item.title,item.message,null,actions);modalEscape=close(item.escape||function(){});
}
// Both local and server destinations use exactly the same policy and dialogs.
function jobConflict(path,directory,lookup,done){
    lookup(path,function(error,info){if(error){done(error);return;}if(!info.exists){done(null,path,'');return;}
        function keep(){var name=base(path),dot=directory?-1:name.lastIndexOf('.'),index=0;function next(){if(++index>1000){done('同名项目过多');return;}var suffix=' ('+index+')',chosen=dot>0?name.substring(0,dot)+suffix+name.substring(dot):name+suffix,candidate=join(parent(path),chosen);lookup(candidate,function(problem,fresh){if(problem)done(problem);else if(fresh.exists)setTimeout(next,0);else done(null,candidate,'');});}next();}
        function replace(){if(directory||info.directory){done('不能以文件覆盖文件夹');return;}done(null,path,info.version);}
        if(taskConflict==='skip'){done(null,null);return;}if(taskConflict==='keep-both'){keep();return;}if(taskConflict==='replace'){replace();return;}
        jobDialog('存在同名项目',base(path)+' 已存在。请选择此次操作；也可将策略应用到后续项目。',[
            {label:'跳过',run:function(){done(null,null);}},
            {label:'保留两份',run:keep},
            {label:'替换此版本',primary:true,run:replace},
            {label:'后续全部保留两份',run:function(){taskConflict='keep-both';keep();}},
            {label:'后续全部跳过',run:function(){taskConflict='skip';done(null,null);}}
        ],function(){done(null,null);});
    });
}
function jobPlan(root,t,done){
    var items=[],stack=[],seen=Object.create(null),budget=0,total=0,ended=false;
    function fail(error){if(ended)return;ended=true;done(error);}
    function add(e,relative,depth){if(!e||!e.path||seen[e.path]||depth>128||relative.length>4096){fail('目录清单存在重复或路径过长');return false;}seen[e.path]=true;if(frozenEntry(e))return true;budget+=e.path.length*2+relative.length*2+200;if(items.length>=100000||budget>16*1048576){fail('目录清单超过 100000 项或 16 MiB 上限');return false;}
        var item={path:e.path,name:e.name,relative:relative,directory:!!e.directory,size:e.directory?0:Number(e.size||0),modified:e.modified||0,version:e.version||''};if(!isFinite(item.size)||item.size<0||total+item.size>9007199254740991){fail('文件总大小无法安全表示');return false;}total+=item.size;items.push(item);return true;}
    if(!root.directory&&typeof root.size!=='number'){request('GET','/api/download-ticket?path='+enc(root.path),null,function(error,ticket){if(error){done(error);return;}if(root.version&&root.version!==(ticket.sourceVersion||ticket.version)){done('源文件已改变');return;}var copy={path:root.path,name:root.name,directory:false,size:ticket.size,modified:ticket.modified,version:ticket.sourceVersion||ticket.version};jobPlan(copy,t,done);});return;}if(!add(root,'',0))return;if(root.directory)stack.push({path:root.path,relative:'',offset:0,stamp:null,depth:0});
    function step(){if(ended||!taskGate(t,step))return;if(!stack.length){ended=true;t.total=total;taskSave();done(null,{items:items,total:total,index:0,bytes:0});return;}var frame=stack[stack.length-1];
        t.inflight=request('GET','/api/list?'+query({path:frame.path,offset:frame.offset,limit:100,sort:'name',order:'asc',showHidden:1,browser:browserID}),null,function(error,data){if(ended||t.state==='cancelled')return;if(error){fail(error);return;}if(!data.entries||data.truncated||frame.stamp!==null&&(frame.stamp!==data.stamp||frame.total!==data.total)){fail('扫描期间目录发生变化，请重新发起');return;}frame.stamp=data.stamp;frame.total=data.total;
            var children=[];for(var i=0;i<data.entries.length;i++){var e=data.entries[i];if(!e.name||e.name==='.'||e.name==='..'||/[\\/:]/.test(e.name)||e.path!==join(frame.path,e.name)){fail('目录包含无效路径');return;}if(frozenEntry(e))continue;var relative=frame.relative?frame.relative+'/'+e.name:e.name;if(!add(e,relative,frame.depth+1))return;if(e.directory)children.push({path:e.path,relative:relative,offset:0,stamp:null,depth:frame.depth+1});}
            frame.offset+=data.entries.length;if(frame.offset>=data.total)stack.pop();else if(!data.entries.length){fail('目录分页不完整');return;}for(i=children.length-1;i>=0;i--)stack.push(children[i]);if(t.state!=='paused')taskState(t,'running','正在扫描目录 · '+items.length+' 项');setTimeout(step,0);
        });
    }step();
}
function jobWait(t,action,done){if(!t){action(done);return;}taskRelease(t);if(t.state!=='paused')taskState(t,'queued','等待挂载提供方');action(function(){var args=arguments;if(t.state==='cancelled')return;function resume(){done.apply(null,args);}if(t.state==='paused')t.waiting=resume;else{taskState(t,'queued');taskQueue(t,resume);}});}
function jobTicket(item,done,t){var mount=typeof findMount==='function'?findMount(item.path):null;function check(error,ticket){if(error){done(error);return;}var sourceVersion=ticket.sourceVersion||ticket.version;if(item.ticketVersion&&ticket.version!==item.ticketVersion||item.sourceVersion&&item.sourceVersion!==sourceVersion||item.version&&item.version!==sourceVersion||ticket.size!==item.size||item.modified&&ticket.modified!==item.modified){done('源文件已改变，请重新发起任务');return;}item.ticketVersion=ticket.version;item.sourceVersion=sourceVersion;done(null,ticket);}function ask(next){request('GET','/api/download-ticket?path='+enc(item.path),null,next);}if(mount&&mount.browser)jobWait(t,ask,check);else ask(check);}
function jobCancel(t){if(t.inflight)t.inflight.abort();var state=t.recovery,item=state&&state.plan&&state.plan.items[state.plan.index];if(state&&state.cache)transferCancel(state.cache);if(item&&item.mountCache)transferCancel(item.mountCache);if(item&&item.resume){function discard(attempt){request('POST','/api/resume/cancel?'+query({id:item.resume.id,key:item.resume.key}),{},function(error){if(error&&/active/i.test(error)&&attempt<5)setTimeout(function(){discard(attempt+1);},500);});}discard(0);}taskState(t,'cancelled');}
function startCopyJob(entry,target,existing,move){
    if(entry.directory&&withinPath(entry.path,target)||entry.path===target){toast('不能复制到自身或内部');return;}
    var t=existing||taskCreate(move?'移动':'复制',entry.name,target,0),state={type:'copy',root:entry,target:target,plan:null,move:!!move};jobAttach(t,'copy',state);t.cancel=function(){jobCancel(t);};jobStart(t,function(){runCopyPlan(t,state);});return t;
}
function runCopyJob(t,state){taskControls(t);t.cancel=function(){jobCancel(t);};jobStart(t,function(){runCopyPlan(t,state);});}
function copyNativeChunked(t,state,item,ticket,done){
    function save(next){jobSave(t,state,next);}
    function call(op,body,next){t.inflight=request(op==='status'?'GET':'POST','/api/resume/'+op+(op==='create'||op==='copy-chunk'?'':'?'+query({id:item.resume.id,key:item.resume.key})),body,next);}
    function finish(){call('commit',{},function(error,result){if(error){done(error);return;}done(null,result);});}
    function chunk(){if(!taskGate(t,chunk))return;var at=item.resume.offset||0;if(at===ticket.size){finish();return;}call('copy-chunk',{source:item.path,version:ticket.version,offset:at,id:item.resume.id,key:item.resume.key},function(error,result){if(error){done(error);return;}if(result.offset!==Math.min(ticket.size,at+1048576)){done('复制检查点偏移无效');return;}item.resume.offset=result.offset;taskProgress(t,state.plan.bytes+result.offset,state.plan.total);save(function(){setTimeout(chunk,0);});});}
    if(item.resume){call('status',null,function(error,status){if(error){done(error);return;}if(status.size!==ticket.size||status.path!==item.to||status.offset<0||status.offset>ticket.size){done('复制会话与当前目标不匹配');return;}item.resume.offset=status.offset;if(status.phase==='done')finish();else chunk();});}
    else call('create',{path:item.to,size:ticket.size,fingerprint:'copy:'+ticket.version.substring(0,200)+':'+ticket.size,replaceVersion:item.replace||''},function(error,result){if(error){done(error);return;}item.resume={id:result.id,key:result.key,offset:result.offset||0};save(chunk);});
}
function runCopyPlan(t,state){
    var plan=state.plan;
    function save(next){jobSave(t,state,next);}
    function end(){if(state.move)t.moveComplete=!state.retained&&!plan.items.some(function(item){return item.skipped;});taskState(t,'done',state.retained?'已保留 '+state.retained+' 个仍有内容的源目录':'');if(current())refresh(true);}
    function cleanup(){if(!state.move){end();return;}if(typeof state.cleanup!=='number')state.cleanup=plan.items.length-1;function next(){if(!taskGate(t,next))return;if(state.cleanup<0){end();return;}var item=plan.items[state.cleanup];if(!item.directory||item.path===item.to){state.cleanup--;save(next);return;}t.inflight=request('DELETE','/api/entry?'+query({path:item.path,empty:1}),null,function(error){if(error)state.retained=(state.retained||0)+1;state.cleanup--;save(next);});}next();}
    function begin(){if(plan)taskProgress(t,plan.bytes,plan.total);if(!plan){jobPlan(state.root,t,function(error,value){if(error){jobFail(t,error);return;}state.plan=plan=value;save(step);});}else step();}
    function step(){if(!taskGate(t,step))return;if(plan.index>=plan.items.length){cleanup();return;}var item=plan.items[plan.index],to=item.to||(item.relative?join(state.target,item.relative):state.target);
        function advance(error){if(t.state==='cancelled')return;if(error){jobFail(t,error);return;}plan.bytes+=item.size;plan.index++;taskProgress(t,plan.bytes,plan.total);save(function(){setTimeout(step,0);});}
        function removeSource(){if(!state.move){advance();return;}request('GET','/api/upload-target?path='+enc(item.to),null,function(error,info){if(error||!info.exists||!item.receipt||info.version!==item.receipt.version){advance(error||'目标已改变，保留源文件');return;}request('GET','/api/upload-target?path='+enc(item.path),null,function(problem,source){if(problem){advance(problem);return;}if(!source.exists){item.deleted=true;save(function(){advance();});return;}if(source.version!==item.sourceVersion){advance('源文件已改变，保留原件');return;}t.inflight=request('DELETE','/api/entry?'+query({path:item.path,version:item.sourceVersion}),null,function(issue){if(!issue){item.deleted=true;save(function(){advance();});}else advance(issue);});});});}
        function copied(error,result){if(error){advance(error);return;}if(state.move){if(!result||!result.version){advance('目标未返回版本，保留源文件');return;}item.receipt={version:result.version};save(removeSource);}else advance();}
        if(item.deleted){advance();return;}if(item.receipt){removeSource();return;}
        if(item.directory){item.to=to;t.inflight=request('POST','/api/mkdir?path='+enc(to),null,function(error){if(!error){advance();return;}request('GET','/api/upload-target?path='+enc(to),null,function(problem,info){if(!problem&&info.exists&&info.directory)advance();else advance(problem||error);});});return;}
        jobTicket(item,function(error,ticket){if(t.state==='cancelled')return;if(error){jobFail(t,error);return;}
            function write(problem,chosen,replace){if(t.state==='cancelled')return;if(problem){jobFail(t,problem);return;}if(!chosen){item.skipped=true;advance();return;}item.to=chosen;item.replace=replace||'';save(function(){var mount=findMount(chosen);if(mount&&mount.browser)t.inflight=copyToBrowserMount(ticket,chosen,copied,item.replace,t,item,state);else if(session.resumableUploads)copyNativeChunked(t,state,item,ticket,copied);else t.inflight=request('POST','/api/copy?'+query({path:item.path,to:chosen,version:ticket.version,replaceVersion:item.replace}),null,copied);});}
            if(item.resume||item.mountCache)write(null,item.to,item.replace);else if(state.approved&&state.approved.path===to){var approval=state.approved;delete state.approved;write(null,to,approval.version);}else resolveBatchConflict(to,false,write);
        },t);
    }begin();
}
function legacyUploadXHR(path){
    if(session&&session.resumableUploads&&window.FileReader&&window.Uint8Array){var transport=null,headers={},target='';return {upload:{},open:function(method,url){target=url;},setRequestHeader:function(k,v){headers[k]=v;},send:function(file){if(!canResumeUpload(file))throw new Error('此浏览器缺少分块读取能力');transport=resumableXHR(path,'');transport.open('PUT',target,true);var self=this;transport.upload.onprogress=function(event){if(self.upload.onprogress)self.upload.onprogress(event);};transport.onload=function(){self.status=transport.status;self.responseText=transport.responseText;if(self.onload)self.onload();};transport.onerror=function(){if(self.onerror)self.onerror();};transport.onabort=function(){if(self.onabort)self.onabort();};transport.send(file);this.task=transport.task;},abort:function(){if(transport)transport.abort();}};}
    var api={upload:{},status:0,responseText:''},headers={},method='',url='',file=null,xhr=null,t=null,stopped=false;
    api.open=function(m,u){method=m;url=u;};api.setRequestHeader=function(k,v){headers[k]=v;};
    function start(){if(stopped)return;xhr=new XMLHttpRequest();xhr.open(method,url,true);for(var k in headers)if(headers.hasOwnProperty(k))xhr.setRequestHeader(k,headers[k]);
        xhr.upload.onprogress=function(e){if(stopped||t.state==='paused')return;if(e.lengthComputable)taskProgress(t,e.loaded,e.total);if(api.upload.onprogress)api.upload.onprogress(e);};
        xhr.onload=function(){if(stopped||t.state==='paused')return;api.status=xhr.status;api.responseText=xhr.responseText;taskState(t,xhr.status>=200&&xhr.status<300?'done':'failed',xhr.status>=400?'上传失败；重试将重新发送当前文件':'');if(api.onload)api.onload();};
        xhr.onerror=xhr.ontimeout=function(){if(stopped||t.state==='paused')return;taskState(t,'failed','连接中断；重试将重新发送当前文件');if(api.onerror)api.onerror();};xhr.send(file);
    }
    api.send=function(value){file=value;t=taskCreate('上传',file.name,path,file.size);api.task=t;t.pause=function(){taskState(t,'paused');if(xhr)xhr.abort();taskRelease(t);};t.resumeRun=function(){jobStart(t,start);};t.retry=t.resumeRun;t.cancel=api.abort;taskQueue(t,start);};
    api.abort=function(){stopped=true;if(xhr)xhr.abort();taskState(t,'cancelled');if(api.onabort)api.onabort();};return api;
}

function renameVersioned(path,to,version,done,directory){var mount=findMount(path);if(!mount||!mount.browser||mount.movable)return request('POST','/api/rename?'+query({path:path,to:to,version:version||''}),null,done);var t=startCopyJob({path:path,name:base(path),directory:!!directory,version:version||''},to,null,true),timer=setInterval(function(){if(/^(done|failed|cancelled)$/.test(t.state)){clearInterval(timer);done(t.state==='done'?null:t.error||'重命名已停止');}},150);return t;}
