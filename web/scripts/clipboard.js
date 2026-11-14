// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
var fileClipboard=null,clipboardPasteAt=0;
function clipboardWrite(value){fileClipboard=value;try{if(value)localStorage.setItem('hls-file-clipboard',JSON.stringify(value));else localStorage.removeItem('hls-file-clipboard');}catch(ignore){}if(typeof schedule==='function')schedule();}
function clipboardItems(){return fileClipboard?(fileClipboard.items||[fileClipboard]):[];}
function clipboardSet(items,cut){
    var kept=[],i,item;for(i=0;i<items.length&&kept.length<10000;i++){item=items[i];if(!item||frozenEntry(item)||cut&&(!canManage(item)||!canMoveEntry(item)))continue;kept.push({path:item.path,name:item.name,directory:!!item.directory,version:item.version||''});}
    if(!kept.length){toast(cut?'没有可剪切的项目':'没有可复制的项目');return;}
    var first=kept[0],value={path:first.path,name:first.name,directory:first.directory,version:first.version,mode:cut?'cut':'copy',id:new Date().getTime()+'-'+Math.random().toString(36).substring(2),time:new Date().getTime()};if(kept.length>1)value.items=kept;
    clipboardWrite(value);toast((cut?'已剪切 ':'已复制 ')+(kept.length===1?'“'+first.name+'”':kept.length+' 项')+'，可切换标签后粘贴');
}
function copyEntry(entry){clipboardSet(entry?[entry]:[],false);}
function cutEntry(entry){clipboardSet(entry?[entry]:[],true);}
function cutSelection(){clipboardSet(selectedEntries(),true);}
function clipboardMoved(id,path){if(!fileClipboard||fileClipboard.id!==id)return;var items=clipboardItems(),left=[];for(var i=0;i<items.length;i++)if(items[i].path!==path)left.push(items[i]);if(!left.length){clipboardWrite(null);return;}var value=fileClipboard;value.items=left;value.path=left[0].path;value.name=left[0].name;value.directory=left[0].directory;clipboardWrite(value);}
function clipboardCut(entry){if(!fileClipboard||fileClipboard.mode!=='cut')return false;var items=clipboardItems();for(var i=0;i<items.length;i++)if(items[i].path===entry.path)return true;return false;}
function pasteEntry(destination){
    if(!fileClipboard||!canWrite(destination))return;
    if(fileClipboard.mode==='cut'){var id=fileClipboard.id,items=clipboardItems().slice(0);for(var i=0;i<items.length;i++)if(!canManage(items[i])||!canMoveEntry(items[i])||items[i].directory&&withinPath(items[i].path,destination)){toast('目标不可移动，或位于所选文件夹内部');return;}batchAction('move',items,destination,function(entry){clipboardMoved(id,entry.path);});return;}
    if(fileClipboard.items){pasteSelection(destination);return;}
    var source=fileClipboard,name=source.name;
    if(source.directory&&withinPath(source.path,destination)){toast('不能复制到自身内部');return;}
    if(parent(source.path)===destination){var dot=source.directory?-1:name.lastIndexOf('.');name=dot>0?name.substring(0,dot)+' - 副本'+name.substring(dot):name+' - 副本';}
    modal('粘贴到 '+destination,'输入目标名称；冲突按任务中心策略处理。',name,[cancel,{label:'粘贴',primary:true,run:function(chosen){if(!chosen||chosen==='.'||chosen==='..'||/[\\/:]/.test(chosen)){toast('名称无效');return;}startCopyJob(source,join(destination,chosen));}}]);
}
function copyToBrowserMount(ticket,path,done,replacement,task,item,state){
    var cache=item&&item.mountCache||null,stopped=false,at=0,inflight=null;
    function fail(error,result){if(stopped)return;done(error,result);}
    function save(next){if(item){item.mountCache=cache;jobSave(task,state,next);}else next();}
    function next(){if(stopped||task&&!taskGate(task,next))return;if(at===ticket.size){mountCachePublish(cache,replacement,task,fail);return;}inflight=request('POST',transferURL(cache,'copy-chunk'),{source:ticket.path||item&&item.path||path,version:ticket.version,offset:at},function(error,result){if(stopped)return;if(error){fail(error);return;}if(result.offset!==Math.min(ticket.size,at+1048576)){fail('挂载复制偏移无效');return;}at=result.offset;if(task)taskProgress(task,(state?state.plan.bytes:0)+at,task.total);save(function(){setTimeout(next,0);});});}
    if(cache)request('GET',transferURL(cache,'status'),null,function(error,status){if(error){fail(error);return;}if(status.receipt){fail(null,status.receipt);return;}at=status.offset;next();});else mountCacheCreate(path,ticket.size,function(error,result){if(error){fail(error);return;}cache=result;save(next);});
    return {abort:function(){stopped=true;if(inflight)inflight.abort();transferCancel(cache);}};
}
function initClipboard(){
    function load(value){try{if(value&&value.length>4*1048576)throw new Error('Clipboard too large');var item=JSON.parse(value||'null'),items=item&&(item.items||[item]),age=item&&new Date().getTime()-item.time;if(!items||!items.length||items.length>10000||!isFinite(age)||age<0||age>=86400000)throw new Error('Invalid clipboard');for(var i=0;i<items.length;i++){var e=items[i];if(!e||typeof e.path!=='string'||e.path.charAt(0)!=='/'||e.path.length>4096||typeof e.name!=='string'||!e.name||e.name==='.'||e.name==='..'||/[\\/:]/.test(e.name))throw new Error('Invalid entry');}fileClipboard=item;}catch(ignore){fileClipboard=null;}if(typeof schedule==='function')schedule();}
    try{load(localStorage.getItem('hls-file-clipboard'));}catch(ignore){}
    on(window,'storage',function(event){if(event.key==='hls-file-clipboard')load(event.newValue);});
    on(doc,'keydown',function(event){var tag=(event.target.tagName||'').toLowerCase();if(tag==='input'||tag==='textarea'||tag==='select'||event.target.isContentEditable||!current()||!session)return;
        if((event.ctrlKey||event.metaKey)&&!event.altKey){if(event.keyCode===88&&selected){event.preventDefault();cutSelection();}else if(event.keyCode===67&&selected){event.preventDefault();if(typeof copySelection==='function')copySelection();else copyEntry(selected);}else if(event.keyCode===86&&fileClipboard){var stamp=new Date().getTime();setTimeout(function(){if(clipboardPasteAt<stamp)pasteEntry(current().path);},30);}}
    });
    on(doc,'paste',function(event){clipboardPasteAt=new Date().getTime();var tag=(event.target.tagName||'').toLowerCase();if(tag==='input'||tag==='textarea'||event.target.isContentEditable)return;var data=event.clipboardData||window.clipboardData;if(data&&data.files&&data.files.length&&current()){event.preventDefault();upload(data.files,uploadTarget(current().path));}else if(fileClipboard&&current()){event.preventDefault();pasteEntry(current().path);}});
    on($('viewport'),'contextmenu',function(event){if(selectedFrom(event)||!current()||current().content||current().q||!canWrite(current().path))return;event.preventDefault();event.stopPropagation();showCreationMenu(current().path,event.clientX,event.clientY,true);});
}
