// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
var sharedMounts=[],providerMounts=[],providerTimer=0,providerPolling=false;
function canDirectoryPick(){return typeof window.Promise==='function'&&window.isSecureContext===true&&typeof window.showDirectoryPicker==='function'&&window.FileSystemDirectoryHandle&&window.FileSystemFileHandle&&typeof window.FileSystemDirectoryHandle.prototype.getDirectoryHandle==='function'&&typeof window.FileSystemDirectoryHandle.prototype.entries==='function';}
function canDirectorySave(){return canDirectoryPick()&&typeof window.FileSystemFileHandle.prototype.createWritable==='function';}
function findMount(path){var best=null;for(var i=0;i<sharedMounts.length;i++)if((sharedMounts[i].path==='/'||withinPath(sharedMounts[i].path,path))&&(!best||sharedMounts[i].path.length>best.path.length))best=sharedMounts[i];return best;}
function isMountRoot(path){for(var i=0;i<sharedMounts.length;i++)if(path==='/'||sharedMounts[i].path===path||withinPath(path,sharedMounts[i].path))return true;return false;}
function canMoveEntry(entry){if(!entry)return false;var m=findMount(entry.path);return !m||!m.browser||m.writable&&m.online!==false;}
function mountBadge(entry){var mount=findMount(entry.path);if(!mount||mount.path!==entry.path)return '';return '<span class="mount-badge'+(mount.online?'':' offline')+'">'+(mount.browser?(mount.online?'挂载 · 在线':'挂载 · 离线'):'主机挂载')+(mount.writable?' · 读写':' · 只读')+'</span>';}
function applyMounts(mounts){if(!mounts)return;sharedMounts=mounts;}
function updateMountButtons(){show($('mount-button'),canDirectoryPick());}
function initMounts(){
    $('mount-button').innerHTML=uiIcon('folderplus');on($('mount-button'),'click',chooseBrowserMount);updateMountButtons();
    on(window,'beforeunload',function(event){if(providerMounts.length){event.preventDefault();event.returnValue='挂载目录仍在共享并复制到主机';return event.returnValue;}});
}
function mountDestination(name){
    var path=current().path;
    if(session.writeMode==='isolated')path=uploadTarget(path);
    else if(session.writeMode==='readonly'&&!session.localAdmin)path=session.mountRoot;
    return join(path,name);
}
function chooseBrowserMount(){
    if(!canDirectoryPick())return;
    // Preserve transient activation: the picker is called by the button itself.
    var picking;try{picking=window.showDirectoryPicker({mode:'read'});}catch(error){toast(error.message);return;}
    picking.then(function(handle){
        var writable=canDirectorySave()&&session.writable&&typeof handle.requestPermission==='function';
        var actions=[cancel,{label:'只读挂载',primary:true,run:function(path){registerProvider(handle,path,false);}}];
        if(writable)actions.push({label:'读写挂载',run:function(path){handle.requestPermission({mode:'readwrite'}).then(function(permission){if(permission==='granted')registerProvider(handle,path,true);else toast('未获得目录写入权限');},function(error){toast(error.message);});}});
        modal('挂载文件夹','文件将逐步复制到主机。访问中的文件优先复制；关闭本页后挂载离线，主机已有副本保留。',mountDestination(handle.name),actions);
    },function(error){if(error.name!=='AbortError')toast(error.message||'无法选择文件夹');});
}
function registerProvider(handle,path,writable){
    if(!path)return;request('POST','/api/mount/register',{path:normalized(path),browser:browserID,writable:writable,movable:writable&&typeof window.FileSystemHandle!=='undefined'&&typeof window.FileSystemHandle.prototype.move==='function'},function(error,data){
        if(error){toast(error);return;}var provider={handle:handle,id:data.id,key:data.key,path:data.path,writable:data.writable,cache:{},writes:{},copies:{},stopped:false,copied:0,failed:0};providerMounts.push(provider);startProviderPoll();request('GET','/api/mounts',null,function(problem,value){if(!problem){applyMounts(value.mounts);refresh(true);}});toast('目录已挂载，正在后台复制到主机');replicateTree(provider);
    });
}
function mountHostPath(){
    modal('挂载主机目录或分区','输入主机上的绝对路径，例如 D:\\ 或 /mnt/data。默认只读挂载。','',[cancel,{label:'下一步',primary:true,run:function(source){if(!source)return;modal('共享挂载位置','在网页中显示的路径','/挂载目录',[cancel,{label:'只读挂载',primary:true,run:function(path){request('POST','/api/mount/native',{source:source,path:path,writable:false},mountDone);}},{label:'读写挂载',run:function(path){request('POST','/api/mount/native',{source:source,path:path,writable:true},mountDone);}}]);}}]);
}
function mountDone(error){if(error){toast(error);return;}request('GET','/api/mounts',null,function(problem,data){if(!problem){applyMounts(data.mounts);refresh(true);}});}
function manageMounts(){
    var actions=[{label:'关闭',run:function(){}}],list=session.localAdmin?sharedMounts:providerMounts;
    for(var i=0;i<list.length;i++)(function(mount){var provider=null;for(var j=0;j<providerMounts.length;j++)if(providerMounts[j].path===mount.path)provider=providerMounts[j];actions.push({label:'卸载 '+mount.path,run:function(){request('POST','/api/mount/remove',{path:mount.path,key:provider?provider.key:''},function(error){if(error){toast(error);return;}if(provider)provider.stopped=true;for(var j=providerMounts.length-1;j>=0;j--)if(providerMounts[j]===provider)providerMounts.splice(j,1);mountDone();});}});}(list[i]));
    modal('管理挂载',list.length+' 个挂载；卸载保留主机已有副本。',null,actions);
}
function providerHandle(provider,path,create,file){
    var parts=path.split('/'),chain=Promise.resolve(provider.handle),i;
    for(i=0;i<parts.length;i++)if(parts[i]){
        if(isFrozenPath('/'+parts[i])||parts[i]==='.'||parts[i]==='..'||/[\\:]/.test(parts[i]))return Promise.reject(new Error('无效目录路径'));
        (function(name,last){chain=chain.then(function(directory){return last&&file?directory.getFileHandle(name,{create:!!create}):directory.getDirectoryHandle(name,{create:!!create});});}(parts[i],i===parts.length-1));
    }
    return chain;
}
function fileVersion(provider,file){return provider.id+':'+file.size+':'+file.lastModified;}
function providerFile(provider,path){return providerHandle(provider,path,false,true).then(function(handle){return handle.getFile().then(function(file){return {handle:handle,file:file,version:fileVersion(provider,file)};});});}
function rawMountRequest(method,url,body,type,range){
    return new Promise(function(resolve,reject){var xhr=new XMLHttpRequest();xhr.open(method,url,true);xhr.timeout=120000;if(type)xhr.responseType=type;if(range)xhr.setRequestHeader('Range','bytes='+range.first+'-'+range.last);if(method!=='GET')xhr.setRequestHeader('X-HLS-Token',session.token);xhr.onload=function(){if(range&&(xhr.status!==206||xhr.getResponseHeader('Content-Range')!=='bytes '+range.first+'-'+range.last+'/'+range.total||!xhr.response||xhr.response.byteLength!==range.last-range.first+1)){reject(new Error('挂载暂存范围不匹配'));return;}if(xhr.status>=200&&xhr.status<300)resolve(type?xhr.response:xhr.responseText);else{var message='挂载请求失败 ('+xhr.status+')';try{message=JSON.parse(xhr.responseText).error||message;}catch(ignore){}var error=new Error(message);error.status=xhr.status;reject(error);}};xhr.onerror=xhr.ontimeout=function(){reject(new Error('挂载连接中断'));};xhr.send(body||null);});
}
function providerCachedWrite(provider,job){
    var writer=null,before='',handle=null,at=0,checking=false;
    function read(first,last){return rawMountRequest('GET',providerURL('payload',provider,job),null,'arraybuffer',{first:first,last:last-1,total:job.size});}
    function conflict(){var error=new Error('目标文件已改变，未覆盖');error.status=409;throw error;}
    function next(){if(at===job.size){if(checking)return providerFile(provider,job.path).then(function(now){if(now.version!==before)conflict();return {size:job.size,version:now.version};});return handle.getFile().then(function(file){if(fileVersion(provider,file)!==before)conflict();return writer.close();}).then(function(){writer=null;provider.cache={};return providerFile(provider,job.path);}).then(function(value){return {size:job.size,version:value.version};});}
        var first=at,last=Math.min(job.size,at+262144);return read(first,last).then(function(bytes){if(!checking)return writer.write(bytes);return handle.getFile().then(function(file){if(fileVersion(provider,file)!==before)conflict();return p2pReadBlob(file.slice(first,last));}).then(function(original){var incoming=new Uint8Array(bytes);if(original.length!==incoming.length)conflict();for(var i=0;i<incoming.length;i++)if(original[i]!==incoming[i])conflict();});}).then(function(){at=last;return next();});
    }
    return providerFile(provider,job.path).then(function(value){handle=value.handle;before=value.version;checking=!job.version||job.version!==before;if(checking&&value.file.size!==job.size)conflict();},function(error){if(error.name!=='NotFoundError')throw error;if(job.version)throw error;return providerHandle(provider,parent(job.path),true,false).then(function(directory){return directory.getFileHandle(base(job.path),{create:true});}).then(function(value){handle=value;return handle.getFile();}).then(function(file){before=fileVersion(provider,file);if(file.size)conflict();});}).then(function(){if(checking)return;return handle.createWritable({mode:'exclusive'}).then(function(value){writer=value;});}).then(next)['catch'](function(error){if(writer)return writer.abort()['catch'](function(){}).then(function(){throw error;});throw error;});
}
function providerURL(route,provider,job){return '/api/mount/'+route+'?'+query({mount:provider.id,key:provider.key,id:job.id});}
function startProviderPoll(){clearTimeout(providerTimer);if(!providerPolling&&providerMounts.length)providerTimer=setTimeout(pollProviders,30);}
function pollProviders(){
    if(providerPolling||!providerMounts.length)return;providerPolling=true;var providers=[],i;
    for(i=0;i<providerMounts.length;i++)if(!providerMounts[i].stopped){providers.push({id:providerMounts[i].id,key:providerMounts[i].key});var writes=providerMounts[i].writes;for(var key in writes)if(writes.hasOwnProperty(key)&&new Date().getTime()-writes[key].time>120000){writes[key].writer.abort()['catch'](function(){});delete writes[key];}}
    request('POST','/api/mount/poll',{providers:providers},function(error,data){providerPolling=false;
        if(!error&&data.removed)for(var gone=0;gone<data.removed.length;gone++)for(var n=providerMounts.length-1;n>=0;n--)if(providerMounts[n].id===data.removed[gone]){providerMounts[n].stopped=true;providerMounts.splice(n,1);toast('挂载已卸载；已完成文件保留在共享目录');}
        if(!error&&data.jobs)for(var j=0;j<data.jobs.length;j++)runProviderJob(data.jobs[j]);
        clearTimeout(providerTimer);providerTimer=setTimeout(pollProviders,(error?4000:data.jobs&&data.jobs.length?80:900)+Math.floor(Math.random()*200));
    });
}
function providerListing(provider,path,hidden){
    var key=path+'|'+hidden,cached=provider.cache[key],now=new Date().getTime();if(cached&&cached.pending)return cached.pending;
    if(cached&&now-cached.time<5000)return Promise.resolve(cached);
    var value={time:now},promise=providerHandle(provider,path,false,false).then(function(directory){
        var iterator=directory.entries(),items=[],stamp=0,bytes=0,count=0;
        function next(){return iterator.next().then(function(result){
            if(result.done)return {time:new Date().getTime(),items:items,stamp:provider.id+':'+items.length+':'+stamp};
            var name=result.value[0],handle=result.value[1];if(++count>100000)throw new Error('挂载目录超过 100000 个项目');
            var frozen=isFrozenPath('/'+name),itemHidden=frozen||name.charAt(0)==='.';
            if(!hidden&&itemHidden)return next();
            bytes+=name.length*2+100;if(bytes>8*1024*1024)throw new Error('挂载目录信息超过 8 MiB');
            return (handle.kind==='file'&&!isFrozenPath('/'+name)?handle.getFile():Promise.resolve(null)).then(function(file){
                var item={name:name,frozen:frozen,directory:handle.kind==='directory',size:file?file.size:0,modified:file?Math.floor(file.lastModified/1000):0,version:file?fileVersion(provider,file):'',hidden:itemHidden},hash=name+':'+(file?file.lastModified+':'+file.size:'d'),i;
                for(i=0;i<hash.length;i++)stamp=(stamp*31+hash.charCodeAt(i))>>>0;items.push(item);
                if(count%64===0)return new Promise(function(resolve){setTimeout(resolve,0);}).then(next);return next();
            });
        });}
        return next();
    });
    value.pending=promise;provider.cache[key]=value;
    return promise.then(function(result){var keys=Object.keys(provider.cache);if(keys.length>8)delete provider.cache[keys[0]];provider.cache[key]=result;return result;},function(error){delete provider.cache[key];throw error;});
}
function runProviderJob(job){
    var provider=null;for(var i=0;i<providerMounts.length;i++)if(providerMounts[i].id===job.mount)provider=providerMounts[i];if(!provider||provider.stopped)return;
    var mutating=!/^(list|stamp|stat|p2p-hashes|materialize)$/.test(job.op),operation;if(mutating){var previous=provider.mutations||Promise.resolve();operation=previous.then(function(){return providerOperation(provider,job);});provider.mutations=operation.then(function(){},function(){});}else operation=providerOperation(provider,job);operation.then(function(result){return rawMountRequest('POST',providerURL('reply',provider,job),JSON.stringify(result||{}));},function(error){var status=error.status||({NotFoundError:404,NotAllowedError:403,NotReadableError:409,InvalidModificationError:409,QuotaExceededError:507})[error.name]||502;return rawMountRequest('POST',providerURL('reply',provider,job)+'&status='+status,JSON.stringify({error:error.message||'挂载目录操作失败'}));})['catch'](function(){});
}
function providerSearch(provider,path,query,hidden){
    var key=path+'|'+query+'|'+hidden,cache=provider.searchCache||(provider.searchCache={}),old=cache[key];if(old&&old.pending)return old.pending;if(old&&new Date().getTime()-old.time<5000)return Promise.resolve(old);
    var queue=[{path:path,depth:0}],items=[],scanned=0,bytes=0,truncated=false,needle=query.toLowerCase();
    function step(){if(!queue.length)return Promise.resolve({items:items,stamp:provider.id+':search:'+items.length,truncated:truncated,time:new Date().getTime()});var directory=queue.pop();return providerListing(provider,directory.path,hidden).then(function(list){
        for(var i=0;i<list.items.length;i++){
            var source=list.items[i],relative=join(directory.path,source.name);if(++scanned>100000||bytes>8*1024*1024){truncated=true;queue=[];break;}
            if(source.name.toLowerCase().indexOf(needle)>=0){var item={},field;for(field in source)if(source.hasOwnProperty(field))item[field]=source[field];item.relative=relative;items.push(item);bytes+=relative.length*2+source.name.length*2+128;}
            if(!source.frozen&&source.directory&&directory.depth<64)queue.push({path:relative,depth:directory.depth+1});
        }
        return step();
    });}
    var promise=step().then(function(result){return providerListing(provider,path,hidden).then(function(directory){result.stamp=directory.stamp;return result;});});cache[key]={pending:promise};return promise.then(function(result){provider.searchCache={};provider.searchCache[key]=result;return result;},function(error){delete cache[key];throw error;});
}
function providerProtectTree(provider,path){
    if(isFrozenPath(path))return Promise.reject(new Error('主机配置已冻结'));
    provider.cache={};var pending=[path],count=0;
    function scan(){if(!pending.length)return Promise.resolve();var at=pending.pop();return providerListing(provider,at,true).then(function(list){
        for(var i=0;i<list.items.length;i++){var item=list.items[i],child=join(at,item.name);if(++count>100000)throw new Error('保护检查超过目录上限');if(item.frozen||isFrozenPath(child))throw new Error('目录含冻结的配置，操作已跳过');if(item.directory)pending.push(child);}
        return scan();
    });}
    return providerHandle(provider,path,false,false).then(scan,function(error){if(error.name==='TypeMismatchError')return;throw error;});
}
function p2pMountedSource(meta){
    if(!meta||!meta.source||!meta.sourceMount||!meta.sourceVersion||!meta.sourcePath)return null;
    for(var i=0;i<providerMounts.length;i++){var p=providerMounts[i];if(!p.stopped&&p.id===meta.sourceMount&&join(p.path,meta.sourcePath.substring(1))===meta.path&&!isFrozenPath(meta.path))return p;}return null;
}
function p2pSourceRead(meta,index){
    var provider=p2pMountedSource(meta);if(!provider)return Promise.resolve(null);
    if(index%1||index<0||index>=meta.pieces)return Promise.reject(p2pError('源文件块越界'));
    return providerFile(provider,meta.sourcePath).then(function(value){
        if(value.version!==meta.sourceVersion||value.file.size!==meta.size)throw p2pError('挂载源文件已改变',409);
        return p2pReadBlob(value.file.slice(index*1048576,Math.min(meta.size,(index+1)*1048576)));
    });
}
function providerHashes(provider,job){
    if(job.start%16||job.start<0||job.start>=1048576)return Promise.reject(new Error('无效校验页'));
    return providerFile(provider,job.path).then(function(value){
        if(value.version!==job.version)throw p2pError('挂载文件版本已改变',409);
        var count=Math.ceil(value.file.size/1048576),end=Math.min(count,job.start+16),digests=[],index=job.start;
        if(index>=count)throw new Error('校验页超出文件');
        function next(){if(index>=end)return providerFile(provider,job.path).then(function(now){if(now.version!==job.version)throw p2pError('挂载文件版本已改变',409);return {start:job.start,hashes:digests};});var start=index++*1048576;
            return p2pReadBlob(value.file.slice(start,Math.min(value.file.size,start+1048576))).then(p2pDigest).then(function(hash){digests.push(hash);return next();});
        }return next();
    });
}
function providerOperation(provider,job){
    var op=job.op,path=job.path||'/',state;
    if(op==='list'||op==='stamp')return (job.q?providerSearch(provider,path,job.q,job.showHidden):providerListing(provider,path,job.showHidden)).then(function(list){
        if(op==='stamp')return {stamp:list.stamp};var items=list.items.slice(0),q=(job.q||'').toLowerCase();if(q)items=items.filter(function(item){return item.name.toLowerCase().indexOf(q)>=0;});
        items.sort(function(a,b){if(a.directory!==b.directory)return a.directory?-1:1;var left=job.sort==='size'?a.size:job.sort==='modified'?a.modified:job.sort==='type'?a.name.substring(a.name.lastIndexOf('.')):a.name.toLowerCase(),right=job.sort==='size'?b.size:job.sort==='modified'?b.modified:job.sort==='type'?b.name.substring(b.name.lastIndexOf('.')):b.name.toLowerCase(),v=left<right?-1:left>right?1:0;return job.order==='desc'?-v:v;});
        var offset=job.offset||0,focus=-1;if(job.focus)for(var j=0;j<items.length;j++)if(join(provider.path,path==='/'?items[j].name:path.substring(1)+'/'+items[j].name)===job.focus){focus=j;offset=Math.floor(j/job.limit)*job.limit;break;}
        var result={entries:items.slice(offset,offset+job.limit),offset:offset,total:items.length,truncated:!!list.truncated,stamp:list.stamp};if(focus>=0){result.focusIndex=focus;result.focusPath=job.focus;}return result;
    });
    if(op==='p2p-hashes')return providerHashes(provider,job);
    if(op==='stat')return providerFile(provider,path).then(function(value){return {directory:false,size:value.file.size,modified:Math.floor(value.file.lastModified/1000),version:value.version};},function(error){if(error.name==='TypeMismatchError'||path==='/')return providerHandle(provider,path,false,false).then(function(){return {directory:true};});throw error;});
    if(op==='materialize')return replicateFile(provider,path,true,job.version).then(function(){return {ok:true};});
    if(!provider.writable)return Promise.reject(new Error('挂载目录为只读'));
    if(op==='write-cache')return providerCachedWrite(provider,job);
    if(op==='mkdir')return providerHandle(provider,path,true,false).then(function(){provider.cache={};return {ok:true};});
    if(op==='delete')return providerProtectTree(provider,path).then(function(){return job.version?providerFile(provider,path).then(function(value){if(value.version!==job.version.replace(/^mount:/,'')){var error=new Error('源文件已改变，保留原件');error.status=409;throw error;}}):Promise.resolve();}).then(function(){return providerHandle(provider,parent(path),false,false).then(function(directory){return directory.removeEntry(base(path),{recursive:!job.empty&&!!job.recursive});});}).then(function(){provider.cache={};return {ok:true};});
    if(op==='move')return providerProtectTree(provider,path).then(function(){
        return providerHandle(provider,path,false,true).then(function(handle){return handle.getFile().then(function(file){if(job.version&&job.version.replace(/^mount:/,'')!==fileVersion(provider,file)){var error=new Error('源文件已改变');error.status=409;throw error;}return handle;});},function(error){if(error.name==='TypeMismatchError')return providerHandle(provider,path,false,false);throw error;});
    }).then(function(handle){if(typeof handle.move!=='function')throw new Error('此浏览器不支持移动该挂载项目');return providerHandle(provider,parent(job.to),false,false).then(function(destination){
        return destination.getFileHandle(base(job.to)).then(function(){var conflict=new Error('目标已存在；挂载移动不会覆盖文件');conflict.status=409;throw conflict;},function(error){if(error.name!=='NotFoundError')throw error;return handle.move(destination,base(job.to));});
    });}).then(function(){provider.cache={};return {ok:true};});
    if(op==='write-begin')return providerFile(provider,path).then(function(existing){if(!job.version||existing.version!==job.version){var conflict=new Error('文件已存在或版本已改变');conflict.status=409;throw conflict;}return existing;},function(error){if(error.name!=='NotFoundError')throw error;if(job.version)throw error;return providerHandle(provider,parent(path),true,false).then(function(directory){return directory.getFileHandle(base(path),{create:true});}).then(function(handle){return {handle:handle,created:true};});}).then(function(value){return value.handle.createWritable({mode:'exclusive'}).then(function(writer){provider.writes[job.upload]={writer:writer,path:path,handle:value.handle,version:job.version,created:value.created,bytes:0,time:new Date().getTime()};return {ok:true};});});
    state=provider.writes[job.upload];if(!state)return Promise.reject(new Error('上传已过期'));
    state.time=new Date().getTime();
    if(op==='write-chunk'){if(job.offset!==state.bytes)return Promise.reject(new Error('上传块顺序不匹配'));return rawMountRequest('GET',providerURL('payload',provider,job),null,'arraybuffer').then(function(bytes){if(bytes.byteLength!==job.bytes)throw new Error('上传块长度不匹配');return state.writer.write(bytes);}).then(function(){state.bytes+=job.bytes;return {ok:true};});}
    if(op==='write-abort'){delete provider.writes[job.upload];return state.writer.abort().then(function(){return {ok:true};});}
    if(op==='write-end')return state.handle.getFile().then(function(file){if(state.version&&fileVersion(provider,file)!==state.version){var error=new Error('文件在上传期间已改变');error.status=409;throw error;}return state.writer.close();}).then(function(){delete provider.writes[job.upload];provider.cache={};return replicateFile(provider,state.path,true);}).then(function(){return providerFile(provider,state.path);}).then(function(value){return {size:value.file.size,version:value.version};});
    return Promise.reject(new Error('未知挂载操作'));
}
function replicateFile(provider,path,priority,expected){
    if(provider.stopped)return Promise.reject(new Error('挂载已停止'));
    if(provider.copies[path])return provider.copies[path];
    var promise=providerFile(provider,path).then(function(value){
        if(expected&&expected!==value.version){var error=new Error('文件已改变，请重新访问');error.status=409;throw error;}
        if(provider.completed&&provider.completed[path]===value.version)return;
        return new Promise(function(resolve,reject){var xhr=canResumeUpload(value.file)?resumableXHR(provider.path+path,'',null,'挂载复制',priority):typeof legacyUploadXHR==='function'?legacyUploadXHR(provider.path+path):new XMLHttpRequest();xhr.open('PUT','/api/upload?'+query({path:provider.path+path,mirror:provider.key,sourceVersion:value.version}),true);xhr.setRequestHeader('X-HLS-Token',session.token);if(priority)try{xhr.setRequestHeader('Priority','u=0');}catch(ignore){}
            xhr.onload=function(){if(xhr.status===200||xhr.status===201){if(!provider.completed)provider.completed={};provider.completed[path]=value.version;provider.copied++;resolve();}else{var error=new Error('复制到主机失败 ('+xhr.status+')');error.status=xhr.status;try{error.message=JSON.parse(xhr.responseText).error||error.message;}catch(ignore){}reject(error);}};xhr.onerror=function(){reject(new Error('复制到主机的连接中断'));};xhr.send(value.file);
        });
    });
    provider.copies[path]=promise;return promise.then(function(value){delete provider.copies[path];return value;},function(error){delete provider.copies[path];throw error;});
}
function replicateTree(provider){
    var stack=[{path:'/',iterator:provider.handle.entries(),count:0}],count=0;
    function next(){
        if(provider.stopped||!stack.length)return;var frame=stack[stack.length-1];
        frame.iterator.next().then(function(value){
            if(value.done){stack.pop();if(!frame.count&&frame.path!=='/')request('POST','/api/mount/mirror-directory',{path:provider.path+frame.path,key:provider.key},function(){setTimeout(next,0);});else setTimeout(next,0);return;}
            frame.count++;
            if(++count>100000){toast('挂载后台复制已达 100000 项，未复制文件仍可按需读取');return;}
            var name=value.value[0],handle=value.value[1],path=join(frame.path,name);if(isFrozenPath(path)){setTimeout(next,0);return;}
            if(handle.kind==='directory'){if(stack.length<64)stack.push({path:path,iterator:handle.entries(),count:0});setTimeout(next,0);return;}
            replicateFile(provider,path,false).then(function(){setTimeout(next,80);},function(error){provider.failed++;if(error.status===413){toast('此 IP 上传额度不足，后台复制已暂停');return;}setTimeout(next,1000);});
        },function(){provider.failed++;});
    }
    next();
}
function mountCacheCreate(path,size,done){request('POST','/api/transfer-cache/create',{path:path,name:base(path),purpose:'upload',size:size},done);}
function mountCachePublish(cache,version,t,done){if(t){taskRelease(t);taskState(t,'queued','正在提交到挂载目录');}request('POST',transferURL(cache,'publish-mount'),{version:version||''},done);}
function mountUploadRestore(t){
    if(!t.job||!t.job.cache)return;t.cancel=function(){transferCancel(t.job.cache);taskState(t,'cancelled');};
    t.continueJob=function(){mountCachePublish(t.job.cache,t.job.version,t,function(error){if(error)jobFail(t,error);else taskState(t,'done');});};
    request('GET',transferURL(t.job.cache,'status'),null,function(error,status){if(error)return;if(status.receipt)taskState(t,'done');else if(status.offset===t.total)taskState(t,'needs-output','源文件已暂存，点击继续提交到挂载目录');});
}
function mountedXHR(destination,version,existing){
    var transport={upload:{},status:0,responseText:'',open:function(){},setRequestHeader:function(){}},cache=null,file=null,at=0,stopped=false,finished=false,t=existing,activeXHR=null;
    function save(){t.job={type:'mount-upload',version:version||'',cache:cache};taskSave();}
    function end(error,result){if(stopped||finished)return;finished=true;transport.status=error?409:201;transport.responseText=JSON.stringify(error?{error:error}:result||{});taskState(t,error?'failed':'done',error||'');if(transport.onload)transport.onload();}
    function publish(){mountCachePublish(cache,version,t,function(error,result){end(error,result);});}
    function next(){if(stopped||!taskGate(t,next))return;if(at===file.size){publish();return;}var first=at,last=Math.min(file.size,at+1048576),reader=new FileReader();reader.onerror=function(){end('无法读取源文件');};reader.onload=function(){if(stopped)return;activeXHR=transferWrite(cache,first,new Uint8Array(reader.result),function(error,result){activeXHR=null;if(stopped)return;if(error){end(error);return;}at=result.offset;taskProgress(t,at,file.size);if(transport.upload.onprogress)transport.upload.onprogress({lengthComputable:true,loaded:at,total:file.size});save();setTimeout(next,0);});};reader.readAsArrayBuffer(resumeSlice(file,first,last));}
    function verify(status){var offset=0,chain='';if(status.receipt){end(null,status.receipt);return;}at=status.offset;if(at<0||at>file.size){end('挂载续传偏移无效');return;}function step(){if(stopped||!taskGate(t,step))return;if(offset===at){if(chain!==(status.chain||'')){end('已暂存内容与源文件不同，拒绝续传');return;}next();return;}var last=Math.min(at,offset+1048576),reader=new FileReader();reader.onerror=function(){end('无法核对源文件');};reader.onload=function(){resumeDigest(new Uint8Array(reader.result),function(piece){var material=chain+piece,bytes=new Uint8Array(material.length);for(var i=0;i<material.length;i++)bytes[i]=material.charCodeAt(i);p2pHashEngine(bytes,function(hash){chain=hash;offset=last;setTimeout(step,0);},false);});};reader.readAsArrayBuffer(resumeSlice(file,offset,last));}step();}
    function begin(){if(cache){request('GET',transferURL(cache,'status'),null,function(error,status){if(error){end(error);return;}verify(status);});}else mountCacheCreate(destination,file.size,function(error,result){if(error){end(error);return;}cache=result;save();next();});}
    transport.send=function(source){file=source;stopped=false;if(existing&&existing.source&&existing.source.modified&&file.lastModified&&existing.source.modified!==file.lastModified){transport.status=409;transport.responseText=JSON.stringify({error:'源文件修改时间不匹配，请重新选择原文件'});if(transport.onload)transport.onload();return;}t=t||taskCreate('挂载上传',file.name,destination,file.size);transport.task=t;cache=t.job&&t.job.cache||null;t.source={name:file.name,size:file.size,modified:file.lastModified||0};taskControls(t);t.cancel=transport.abort;t.retry=function(){finished=false;jobStart(t,begin);};save();taskState(t,'queued');taskQueue(t,begin);};
    transport.abort=function(){if(stopped)return;stopped=true;if(activeXHR)activeXHR.abort();transferCancel(cache);taskState(t,'cancelled');if(transport.onabort)transport.onabort();};return transport;
}
