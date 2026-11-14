// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
var fileDrag=null,dropNode=null,dropScrollTimer=0,dropScrollDirection=0;
function withinPath(root,path){return path===root||root==='/'&&path.charAt(0)==='/'||path.indexOf(root+'/')===0;}
function uploadTarget(path){
    if(!session||!session.writable)return '';
    if(session.writeMode==='isolated'&&!session.localAdmin){
        var own=session.uploadRoot||session.writableRoot;
        return withinPath(own,path)?path:own;
    }
    return canWrite(path)?path:'';
}
function dragFiles(event){
    var data=event.dataTransfer;if(!data)return false;
    if(data.files&&data.files.length)return true;
    var types=data.types,i;if(types)for(i=0;i<types.length;i++)if(types[i]==='Files')return true;
    return false;
}
function dropLocation(event){
    var node=event.target,entry,path=current()?current().path:'/',mark=null;
    while(node&&node!==doc){
        if(node.getAttribute&&node.getAttribute('data-tab-id')!==null){var id=Number(node.getAttribute('data-tab-id'));for(var i=0;i<tabs.length;i++)if(tabs[i].id===id){if(tabs[i].content)return {path:'',node:node,invalid:true};path=tabs[i].path;mark=node;break;}break;}
        if(node===$('up')&&!node.disabled){path=parent(current().path);mark=node;break;}
        if(node.getAttribute&&node.getAttribute('data-index')!==null){
            entry=entryAt(current(),Number(node.getAttribute('data-index')));
            if(entry&&entry.directory){path=entry.path;mark=node;}break;
        }
        if(node.getAttribute&&node.getAttribute('data-path')!==null&&$('crumbs').contains(node)){path=node.getAttribute('data-path');mark=node;break;}
        if(node===$('own')){path=session.uploadRoot||session.writableRoot;mark=node;break;}
        if(node===$('home')){path='/';mark=node;break;}
        node=node.parentNode;
    }
    return {path:path,node:mark};
}
function clearFileDrop(){
    if(dropNode)dropNode.className=dropNode.className.replace(/\s*drop-target\b/g,'');dropNode=null;
    var hint=$('file-drop-hint');if(hint)show(hint,false);
    clearInterval(dropScrollTimer);dropScrollTimer=0;dropScrollDirection=0;
}
function showFileDrop(path,node,moving,allowed){
    if(dropNode!==node){if(dropNode)dropNode.className=dropNode.className.replace(/\s*drop-target\b/g,'');dropNode=node;if(node)node.className+=' drop-target';}
    var hint=$('file-drop-hint');if(!hint){hint=doc.createElement('div');hint.id='file-drop-hint';hint.className='file-drop-hint';hint.setAttribute('role','status');hint.innerHTML='<span class="drop-symbol"></span><div><strong></strong><span></span></div>';$('app').appendChild(hint);}
    hint.className='file-drop-hint'+(allowed?'':' drop-denied');
    hint.querySelector('.drop-symbol').innerHTML=uiIcon(moving?'folder':'upload');
    text(hint.querySelector('strong'),allowed?(moving?'移动到文件夹':'松开以上传文件或文件夹'):'此位置不可写入');
    text(hint.querySelector('span'),path||'只读共享');show(hint,true);
}
function moveFile(entry,directory){moveFiles([entry],directory);}
function moveFiles(items,directory){
    if(!directory||!canWrite(directory)){toast('此位置不可移动文件');return;}for(var i=0;i<items.length;i++){var entry=items[i];if(!canManage(entry)||!canMoveEntry(entry)||entry.directory&&withinPath(entry.path,directory)){toast('此位置不可移动文件夹或文件');return;}}
    batchAction('move',items,directory);
}
function canDropFiles(items,directory){if(!directory||!canWrite(directory))return false;for(var i=0;i<items.length;i++)if(!canManage(items[i])||!canMoveEntry(items[i])||items[i].directory&&withinPath(items[i].path,directory))return false;return true;}
function initFileDrag(){
    on($('spacer'),'dragstart',function(event){
        var entry=selectedFrom(event);if(!canManage(entry)||!canMoveEntry(entry)){event.preventDefault();return;}
        var items=typeof selectionHas==='function'&&selectionHas(entry)?selectedEntries():[entry];if(!canDropFiles(items,parent(entry.path))){event.preventDefault();return;}fileDrag={entry:entry,items:items};if(items.length===1)choose(entry);
        try{event.dataTransfer.setData('text/plain',entry.path);}catch(ignore){try{event.dataTransfer.setData('Text',entry.path);}catch(ignored){}}
        event.dataTransfer.effectAllowed='move';
    });
    on(doc,'dragover',function(event){
        var external=dragFiles(event);if(!external&&!fileDrag)return;
        event.preventDefault();var location=dropLocation(event),path=external?uploadTarget(location.path):location.path;
        var allowed=external?!location.invalid&&!!path&&(!uploadBusy||session&&session.resumableUploads&&window.FileReader):!location.invalid&&canDropFiles(fileDrag.items,path);
        if(event.dataTransfer)event.dataTransfer.dropEffect=allowed?(external?'copy':'move'):'none';
        showFileDrop(path||location.path,location.node,!!fileDrag,allowed);
        var viewport=$('viewport'),rect=viewport.getBoundingClientRect(),direction=event.clientY>rect.top&&event.clientY<rect.top+40?-1:event.clientY<rect.bottom&&event.clientY>rect.bottom-40?1:0;
        if(direction!==dropScrollDirection){clearInterval(dropScrollTimer);dropScrollTimer=0;dropScrollDirection=direction;if(direction)dropScrollTimer=setInterval(function(){viewport.scrollTop+=direction*15;},40);}
    });
    on(doc,'dragleave',function(event){if(!event.relatedTarget&&(event.clientX<=0||event.clientY<=0||event.clientX>=window.innerWidth||event.clientY>=window.innerHeight))clearFileDrop();});
    on(doc,'drop',function(event){
        var external=dragFiles(event),moving=fileDrag;fileDrag=null;if(!external&&!moving)return;
        event.preventDefault();var location=dropLocation(event);clearFileDrop();if(location.invalid){toast('请拖放到文件夹标签');return;}
        if(external){
            collectDroppedFiles(event.dataTransfer,function(error,files){if(error){toast(error);return;}if(!files.length){toast('文件夹内没有可上传的文件');return;}upload(files,location.path,{drop:true});});
        }else moveFiles(moving.items||[moving.entry],location.path);
    });
    on(doc,'dragend',function(){fileDrag=null;clearFileDrop();});on(window,'blur',function(){fileDrag=null;clearFileDrop();});
}
function collectDroppedFiles(data,done){
    var queue=[],files=[],items=data.items,i,entry,visited=0,bytes=0,stopped=false;
    // Capture entries synchronously: the browser clears DataTransfer after drop.
    if(items)for(i=0;i<items.length;i++){entry=items[i].webkitGetAsEntry?items[i].webkitGetAsEntry():null;if(entry)queue.push({entry:entry,prefix:'',depth:0});}
    if(!queue.length){for(i=0;i<data.files.length;i++)files.push(data.files[i]);done(null,files);return;}
    function fail(error){if(stopped)return;stopped=true;done(error);}
    function next(){
        if(stopped)return;if(!queue.length){stopped=true;done(null,files);return;}
        if(++visited>20000){fail('文件夹项目过多，请分批上传');return;}
        var item=queue.shift(),path=item.prefix+item.entry.name;if(typeof isFrozenPath==='function'&&isFrozenPath('/'+path)){setTimeout(next,0);return;}
        if(item.depth>=64||path.length>4096){fail('文件夹路径过长或层级过深');return;}
        if(item.entry.isFile)item.entry.file(function(file){
            if(stopped)return;bytes+=path.length;
            if(files.length>=10000||bytes>8*1024*1024){fail('一次最多上传 10000 个文件，请分批上传');return;}
            files.push({file:file,relativePath:path});if(visited%32===0)setTimeout(next,0);else next();
        },function(){fail('无法读取文件：'+path);});
        else if(item.entry.isDirectory){
            var reader=item.entry.createReader();
            function read(){reader.readEntries(function(entries){
                if(stopped)return;var j;
                for(j=0;j<entries.length;j++){if(queue.length+visited>=20000){fail('文件夹项目过多，请分批上传');return;}queue.push({entry:entries[j],prefix:path+'/',depth:item.depth+1});}
                if(entries.length)setTimeout(read,0);else setTimeout(next,0);
            },function(){fail('无法读取文件夹：'+path);});}
            read();
        }else next();
    }
    next();
}
