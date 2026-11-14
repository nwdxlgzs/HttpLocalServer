// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// ZIP64 records are written locally; the HTTP server only sends ordinary file ranges.
var zipBusy=false,zipCancel=null,zipOwner=null;
var crcTable=(function(){var result=new Uint32Array(256),i,j,value;for(i=0;i<256;i++){value=i;for(j=0;j<8;j++)value=value&1?0xedb88320^(value>>>1):value>>>1;result[i]=value>>>0;}return result;}());
function crcUpdate(crc,data){for(var i=0;i<data.length;i++)crc=crcTable[(crc^data[i])&255]^(crc>>>8);return crc>>>0;}
function utf8Bytes(value){var encoded=unescape(encodeURIComponent(value)),bytes=new Uint8Array(encoded.length);for(var i=0;i<encoded.length;i++)bytes[i]=encoded.charCodeAt(i);return bytes;}
function zipRecord(length){var bytes=new Uint8Array(length),view=new DataView(bytes.buffer);return{bytes:bytes,u16:function(at,value){view.setUint16(at,value,true);},u32:function(at,value){view.setUint32(at,value>>>0,true);},u64:function(at,value){view.setUint32(at,value>>>0,true);view.setUint32(at+4,Math.floor(value/4294967296),true);}};}
function zipStamp(seconds){var date=new Date((seconds||new Date().getTime()/1000)*1000),year=Math.max(1980,Math.min(2107,date.getFullYear()));return{time:date.getHours()*2048+date.getMinutes()*32+Math.floor(date.getSeconds()/2),date:(year-1980)*512+(date.getMonth()+1)*32+date.getDate()};}
function chooseZipSink(name,done){
    if(window.showSaveFilePicker){
        var picking;try{picking=window.showSaveFilePicker({suggestedName:name,types:[{description:'ZIP archive',accept:{'application/zip':['.zip']}}]});}catch(ignore){picking=null;}
        if(picking){picking.then(function(handle){zipDiskSink(handle,null,done);},function(error){done(error);});return;}
    }
    transferCreate(arguments.length>2?arguments[2]:'/',name,function(error,cache){if(error){done(new Error(error));return;}cache.offset=0;cache.serverJournal=true;done(null,zipCacheSink(cache,name));});

}
function zipCacheSink(cache,name){
    return {disk:false,cache:cache,write:function(bytes,next){if(!bytes.length){next();return;}transferWrite(cache,cache.offset,bytes,function(error){if(error){next(new Error(error));return;}cache.offset+=bytes.length;next();});},checkpoint:function(position,next){if(position!==cache.offset){next(new Error('ZIP 暂存偏移不一致'));return;}next(null,{position:position,cache:true});},close:function(next){request('POST',transferURL(cache,'complete'),{},function(error,result){if(error){next(new Error(error));return;}var a=doc.createElement('a');a.href=result.url;a.download=name;a.style.display='none';doc.body.appendChild(a);a.click();doc.body.removeChild(a);next();});},abort:function(){}};
}
function zipCompressorState(deflater){var state={},key;for(key in deflater.s)if(deflater.s.hasOwnProperty(key)){var value=deflater.s[key];state[key]=value&&value.buffer?new value.constructor(value):value;}return {format:'fflate-0.8.2-hls-1',o:{level:4,mem:0},s:state,b:new Uint8Array(deflater.b)};}
function zipDiskSink(handle,saved,done){
    var writer=null,baseline=null,abandoned=false;
    function verify(){return handle.getFile().then(function(file){if(file.size!==baseline.size||file.lastModified!==baseline.modified)throw new Error('ZIP 输出文件已被其他程序修改，拒绝覆盖');});}
    handle.getFile().then(function(file){baseline={size:file.size,modified:file.lastModified};if(saved&&(saved.size!==file.size||saved.modified!==file.lastModified))throw new Error('ZIP 输出文件已被其他程序修改，拒绝覆盖');return handle.createWritable({keepExistingData:!!saved,mode:'exclusive'});}).then(function(value){writer=value;if(saved)return writer.truncate(saved.position).then(function(){return writer.seek(saved.position);});}).then(function(){
        done(null,{disk:true,handle:handle,baseline:baseline,write:function(bytes,next){writer.write(bytes).then(function(){next();},next);},close:function(next){verify().then(function(){return writer.close();}).then(function(){writer=null;next();},next);},abort:function(){abandoned=true;if(writer){writer.abort()['catch'](function(){});writer=null;}},checkpoint:function(position,next){var current=writer;writer=null;verify().then(function(){return current.close();}).then(function(){return handle.getFile();}).then(function(file){baseline={size:file.size,modified:file.lastModified};var info={size:file.size,modified:file.lastModified,position:position};if(abandoned)throw new Error('ZIP 已取消');return handle.createWritable({keepExistingData:true,mode:'exclusive'}).then(function(value){writer=value;if(abandoned){writer.abort()['catch'](function(){});throw new Error('ZIP 已取消');}return writer.seek(position);}).then(function(){next(null,info);});})['catch'](function(error){if(current)current.abort()['catch'](function(){});next(error);});}});
    },function(error){if(writer)writer.abort()['catch'](function(){});done(error);});
}
function resumeArchive(t,state){
    if(state.cache){request('GET',transferURL(state.cache,'status'),null,function(error,status){if(error){jobFail(t,error);return;}if(status.complete){var a=doc.createElement('a');a.href=transferURL(state.cache,'read');doc.body.appendChild(a);a.click();doc.body.removeChild(a);taskState(t,'done');return;}var at=state.checkpoint?state.checkpoint.position:0;request('POST',transferURL(state.cache,'trim'),{offset:at},function(problem){if(problem){jobFail(t,problem);return;}state.cache.offset=at;runArchive(state.root,state.filename,zipCacheSink(state.cache,state.filename),t,state);});});return;}
    var saved=state.checkpoint?state.checkpoint.output:state.output;if(saved&&!state.checkpoint)saved={size:saved.size,modified:saved.modified,position:0};zipDiskSink(state.handle,saved,function(error,sink){if(error){jobFail(t,error);return;}runArchive(state.root,state.filename,sink,t,state);});
}
function downloadDirectory(entry){
    if(!window.Uint8Array||!window.Blob||!window.fflate){toast('当前浏览器不支持本地 ZIP 打包，请分别下载文件');return;}
    var filename=(entry.name||base(entry.path)||'共享目录').replace(/[\\/:*?"<>|]/g,'_')+'.zip';
    chooseZipSink(filename,function(error,sink){if(error){if(error.name!=='AbortError')toast('无法创建 ZIP 文件');return;}var t=taskCreate('ZIP 下载',filename,entry.path,0),state={type:'zip',root:entry,filename:filename,handle:sink.handle||null,output:sink.baseline||null,plan:null,checkpoint:null,cache:sink.cache||null};if(sink.disk||sink.cache)jobAttach(t,'zip',state);runArchive(entry,filename,sink,t,state);},entry.path);
}
function runArchive(root,filename,sink,recoverTask,recovery){
    var cancelled=false,finished=false,xhr=null,position=0,compressedTotal=0,inputTotal=0,files=0,metadataBytes=0,central=[],frames=[],seen=Object.create(null),rootName=root.name||base(root.path),lastName='',cap=100000;
    var task=recoverTask||(typeof taskCreate==='function'?taskCreate('ZIP 下载',filename,root.path,0):null),plan=recovery&&recovery.plan,checkpointAt=32*1048576,entryRecovery=recovery&&recovery.checkpoint&&recovery.checkpoint.entry||null;
    if(task){taskControls(task);task.checkpointError=fail;task.cancel=function(){fail();};if(!sink.disk&&!sink.cache)task.retry=function(){downloadDirectory(root);};}
    if(recovery&&recovery.checkpoint){var saved=recovery.checkpoint;position=saved.position;inputTotal=saved.inputTotal;files=saved.files;central=saved.central.slice(0);plan.index=saved.index;checkpointAt=Math.max(32*1048576,position*4);for(var n=0;n<central.length;n++)metadataBytes+=central[n].name.length+100;}
    rootName=rootName==='共享目录'?siteTitle:rootName;
    function focus(){if(task&&!task.slot)return;zipOwner=task||sink;zipBusy=true;zipCancel=function(){fail();};show($('zip-status'),true);text($('zip-title'),filename);}
    function fail(error){if(finished)return;if(!error&&sink.cache)transferCancel(sink.cache);if(task)taskState(task,error?'failed':'cancelled',error?(error.message||String(error)):'');finished=true;cancelled=true;if(xhr)xhr.abort();sink.abort();central=[];frames=[];if(zipOwner===(task||sink)){zipBusy=false;zipCancel=null;zipOwner=null;show($('zip-status'),false);}if(error)toast(error.message||String(error));}
    function emit(bytes,next){if(cancelled)return;position+=bytes.length;sink.write(bytes,function(error){if(error)fail(error);else next();});}
    function progress(name,percentage){focus();if(task)taskProgress(task,inputTotal,plan?plan.total:0);lastName=name;text($('zip-message'),files+' 个文件 · '+size(inputTotal)+' → '+size(position)+' · '+name);$('zip-progress').style.width=Math.max(0,Math.min(100,percentage))+'%';}
    function beginEntry(path,name,directory,modified,url,length,next){
        if(cancelled)return;focus();
        if(central.length>=cap){fail(new Error('ZIP 条目超过 '+cap+' 项'));return;}
        var encoded=utf8Bytes(name),stamp=zipStamp(modified),offset=position;
        metadataBytes+=encoded.length+100;if(metadataBytes>16*1024*1024){fail(new Error('ZIP 目录信息超过 16 MiB 上限'));return;}
        if(encoded.length>65535){fail(new Error('ZIP 内的文件名过长'));return;}
        var header=zipRecord(30+encoded.length+20);header.u32(0,0x04034b50);header.u16(4,45);header.u16(6,0x808);header.u16(8,8);header.u16(10,stamp.time);header.u16(12,stamp.date);header.u32(18,0xffffffff);header.u32(22,0xffffffff);header.u16(26,encoded.length);header.u16(28,20);header.bytes.set(encoded,30);header.u16(30+encoded.length,1);header.u16(32+encoded.length,16);
        var crc=0xffffffff,compressed=0,uncompressed=0,pending=[],deflater=new fflate.Deflate({level:4,mem:0},function(data){compressed+=data.length;pending.push(data);});
        var continuing=entryRecovery&&entryRecovery.path===path?entryRecovery:null;entryRecovery=null;
        if(continuing){if(continuing.engine.format!=='fflate-0.8.2-hls-1'||continuing.length!==length){fail(new Error('ZIP 压缩恢复版本不匹配'));return;}offset=continuing.offset;crc=continuing.crc;compressed=continuing.compressed;uncompressed=continuing.uncompressed;deflater.s=continuing.engine.s;deflater.b=continuing.engine.b;deflater.o=continuing.engine.o;}
        function checkpoint(next){
            if(!task||!recovery||!sink.checkpoint||(task.state!=='paused'&&(sink.cache?recovery.checkpoint&&recovery.checkpoint.entry&&inputTotal-recovery.checkpoint.inputTotal<1048576:inputTotal<checkpointAt))){next();return;}
            var entry={path:path,length:length,offset:offset,crc:crc,compressed:compressed,uncompressed:uncompressed,engine:zipCompressorState(deflater)};
            sink.checkpoint(position,function(error,output){if(error){fail(error);return;}recovery.checkpoint={index:plan.index,position:position,inputTotal:inputTotal,files:files,central:central.slice(0),output:output,entry:entry};if(!sink.cache)checkpointAt=Math.max(checkpointAt*4,inputTotal*4);jobSave(task,recovery,next);});
        }
        function flush(done){var index=0;function write(){if(cancelled)return;if(index<pending.length){emit(pending[index++],write);}else{pending=[];done();}}write();}
        function finish(){
            var checksum=(crc^0xffffffff)>>>0,descriptor=zipRecord(24);descriptor.u32(0,0x08074b50);descriptor.u32(4,checksum);descriptor.u64(8,compressed);descriptor.u64(16,uncompressed);
            emit(descriptor.bytes,function(){central.push({name:encoded,stamp:stamp,crc:checksum,compressed:compressed,length:uncompressed,offset:offset,directory:directory});if(!directory)files++;next();});
        }
        function receive(data,last){if(cancelled)return;crc=crcUpdate(crc,data);uncompressed+=data.length;inputTotal+=data.length;try{deflater.push(data,last);}catch(error){fail(error);return;}flush(function(){if(last)finish();else checkpoint(function(){setTimeout(read,0);});});}
        function read(){
            if(task&&!taskGate(task,read))return;
            if(cancelled)return;var first=uncompressed,end=Math.min(length-1,first+256*1024-1);xhr=new XMLHttpRequest();xhr.open('GET',url+(url.indexOf('?')<0?'?':'&')+'chunk='+first,true);xhr.responseType='arraybuffer';xhr.timeout=0;xhr.setRequestHeader('Range','bytes='+first+'-'+end);xhr.setRequestHeader('Cache-Control','no-cache');
            xhr.onprogress=function(event){if(event.loaded>256*1024){xhr.abort();fail(new Error('服务器返回了超出范围的文件块'));}};
            xhr.onload=function(){if(cancelled)return;if(xhr.status!==206){fail(new Error(xhr.status===409?'文件在打包过程中发生变化，请重新打包':'读取文件块失败 ('+xhr.status+')'));return;}if(xhr.getResponseHeader('Content-Range')!=='bytes '+first+'-'+end+'/'+length){fail(new Error('服务器返回的文件范围不匹配'));return;}var bytes=new Uint8Array(xhr.response||new ArrayBuffer(0));if(bytes.length!==end-first+1){fail(new Error('文件块长度不匹配'));return;}progress(name,length?end/length*100:100);receive(bytes,end+1===length);};
            xhr.onerror=xhr.ontimeout=function(){fail(new Error('打包下载连接中断'));};xhr.send(null);
        }
        if(continuing){if(uncompressed===length)finish();else read();}else emit(header.bytes,function(){if(!length)receive(new Uint8Array(0),true);else read();});
    }
    function addFile(entry,relative,next){
        if(seen[entry.path]){fail(new Error('目录在打包过程中发生变化，请重新打包'));return;}seen[entry.path]=true;
        xhr=request('GET','/api/download-ticket?path='+enc(entry.path),null,function(error,ticket){if(cancelled)return;if(error){fail(new Error(error));return;}if(typeof ticket.size!=='number'||ticket.size<0||ticket.size>9007199254740991){fail(new Error('文件大小无法安全表示'));return;}beginEntry(entry.path,relative,false,entry.modified,ticket.url,ticket.size,next);});
    }
    function pushDirectory(path,relative,modified,next){
        if(frames.length>=128){fail(new Error('目录层级超过 128 层'));return;}
        beginEntry(path,relative+'/',true,modified,'',0,function(){frames.push({path:path,relative:relative,offset:0,total:null,items:[],index:0});next();});
    }
    function step(){
        if(task&&!taskGate(task,step))return;
        if(cancelled)return;
        if(plan){
            if(plan.index>=plan.items.length){finishZip();return;}var planned=plan.items[plan.index],relative=rootName+(planned.relative?'/'+planned.relative:'');
            function nextPlanned(){plan.index++;var stopping=task&&task.state==='paused';if(sink.checkpoint&&(sink.cache||inputTotal>=checkpointAt||stopping)){sink.checkpoint(position,function(error,output){if(error){fail(error);return;}recovery.checkpoint={index:plan.index,position:position,inputTotal:inputTotal,files:files,central:central.slice(0),output:output};checkpointAt=Math.max(checkpointAt*4,position*4);jobSave(task,recovery,function(){setTimeout(step,0);});});}else setTimeout(step,0);}
            if(planned.directory)beginEntry(planned.path,relative+'/',true,planned.modified,'',0,nextPlanned);
            else jobTicket(planned,function(error,ticket){if(error){fail(new Error(error));return;}beginEntry(planned.path,relative,false,planned.modified,ticket.url,ticket.size,nextPlanned);},task);return;
        }
        if(!frames.length){finishZip();return;}
        var frame=frames[frames.length-1];
        if(frame.index<frame.items.length){var item=frame.items[frame.index++],name=item.name;if(frozenEntry(item)){setTimeout(step,0);return;}if(name==='.'||name==='..'||/[\\/]/.test(name)){fail(new Error('目录包含不安全的文件名'));return;}var relative=frame.relative+'/'+name;
            if(item.directory)pushDirectory(item.path,relative,item.modified,step);else addFile(item,relative,step);return;
        }
        if(frame.total!==null&&frame.offset>=frame.total){frames.pop();step();return;}
        xhr=request('GET','/api/list?'+query({path:frame.path,offset:frame.offset,limit:100,sort:'name',order:'asc',showHidden:1}),null,function(error,data){
            if(cancelled)return;if(error){fail(new Error(error));return;}if(!data.entries||data.truncated){fail(new Error('目录超过服务器列表上限，无法完整打包'));return;}
            if(frame.total!==null&&frame.total!==data.total){fail(new Error('目录在打包过程中发生变化，请重新打包'));return;}
            frame.total=data.total;frame.offset+=data.entries.length;frame.items=data.entries;frame.index=0;
            if(!data.entries.length&&frame.offset<frame.total){fail(new Error('目录分页不完整'));return;}setTimeout(step,0);
        });
    }
    function finishZip(){
        var centralStart=position,index=0;
        function next(){
            if(cancelled)return;
            if(index<central.length){var item=central[index++],name=item.name,header=zipRecord(46+name.length+28);header.u32(0,0x02014b50);header.u16(4,45);header.u16(6,45);header.u16(8,0x808);header.u16(10,8);header.u16(12,item.stamp.time);header.u16(14,item.stamp.date);header.u32(16,item.crc);header.u32(20,0xffffffff);header.u32(24,0xffffffff);header.u16(28,name.length);header.u16(30,28);header.u32(38,item.directory?16:0);header.u32(42,0xffffffff);header.bytes.set(name,46);var at=46+name.length;header.u16(at,1);header.u16(at+2,24);header.u64(at+4,item.length);header.u64(at+12,item.compressed);header.u64(at+20,item.offset);emit(header.bytes,next);return;}
            var centralSize=position-centralStart,zip64Offset=position,end=zipRecord(98);end.u32(0,0x06064b50);end.u64(4,44);end.u16(12,45);end.u16(14,45);end.u64(24,central.length);end.u64(32,central.length);end.u64(40,centralSize);end.u64(48,centralStart);end.u32(56,0x07064b50);end.u64(64,zip64Offset);end.u32(72,1);end.u32(76,0x06054b50);end.u16(84,0xffff);end.u16(86,0xffff);end.u32(88,0xffffffff);end.u32(92,0xffffffff);
            emit(end.bytes,function(){sink.close(function(error){if(error){fail(error);return;}finished=true;if(task){task.total=plan?plan.total:inputTotal;taskState(task,'done');}central=[];if(zipOwner===(task||sink)){zipBusy=false;zipCancel=null;zipOwner=null;show($('zip-status'),false);}records.unshift({name:filename,path:root.path,time:new Date().getTime(),archive:true});if(records.length>100)records.pop();saveRecords();if(drawerMode==='records')renderRecords();toast('ZIP 已生成 · '+files+' 个文件');});});
        }
        next();
    }
    function start(){focus();text($('zip-message'),'正在读取目录…');$('zip-progress').style.width='0%';if(task)taskProgress(task,inputTotal,plan?plan.total:0);if(recovery){if(plan)step();else jobPlan(root,task,function(error,value){if(error){fail(new Error(error));return;}recovery.plan=plan=value;if(sink.disk||sink.cache)jobSave(task,recovery,step);else step();});}else pushDirectory(root.path,rootName,root.modified,step);}
    if(task)jobStart(task,start,'directory-ui');else start();
}
on($('cancel-zip'),'click',function(){if(zipCancel)zipCancel();});
