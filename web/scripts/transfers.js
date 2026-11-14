// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
function download(entry){if(!entry&&typeof selectedEntries==='function'&&selectedEntries().length>1){batchDownload();return;}entry=entry||selected;if(!entry)return;if(frozenEntry(entry)){toast('主机配置已冻结');return;}if(entry.directory){if(canDirectorySave())saveDirectory(entry);else downloadDirectory(entry);return;}request('GET','/api/download-ticket?path='+enc(entry.path),null,function(error,data){if(error){toast(error);return;}var link=doc.createElement('a');link.href=data.url;link.style.display='none';doc.body.appendChild(link);link.click();setTimeout(function(){doc.body.removeChild(link);},1000);if(typeof taskCreate==='function'){var ordinary=taskCreate('普通下载',entry.name,entry.path,data.size);taskState(ordinary,'initiated');}records.unshift({name:entry.name,path:entry.path,time:new Date().getTime()});if(records.length>100)records.pop();saveRecords();if(drawerMode==='records')renderRecords();});}
function saveRecords(){try{localStorage.setItem('hls-download-records-v2',JSON.stringify(records));}catch(ignore){}}function renderRecords(){
        var html='',i,record;for(i=0;i<records.length;i++){record=records[i];html+='<div class="record">'+uiIcon(record.archive||record.direct?'folder':'download')+'<div><strong>'+esc(record.name)+'</strong><small>'+esc(new Date(record.time).toLocaleString())+' · '+(record.p2p?'已完成协作下载':record.direct?'已保存到本地文件夹':record.archive?'已生成 ZIP':'已发起下载')+'</small><button data-record="'+i+'">'+(record.direct?'再次保存目录':record.archive?'重新打包':'再次下载')+'</button></div></div>';}
        $('records').innerHTML=html||'<div class="records-empty">'+uiIcon('history')+'<p>暂无下载记录</p></div>';
    }
function modal(title,message,value,actions){modalEscape=null;text($('modal-title'),title);text($('modal-text'),message||'');show($('modal-input'),value!==null);$('modal-input').value=value||'';$('modal-actions').innerHTML='';var i;for(i=0;i<actions.length;i++)(function(action){var b=doc.createElement('button');text(b,action.label);if(action.primary)b.className='primary';on(b,'click',function(){var val=$('modal-input').value;modalEscape=null;show($('modal-shade'),false);action.run(val);});$('modal-actions').appendChild(b);})(actions[i]);show($('modal-shade'),true);setTimeout(function(){if(value!==null){$('modal-input').focus();$('modal-input').select();}else if($('modal-actions').firstChild)$('modal-actions').firstChild.focus();},20);}
var cancel={label:'取消',run:function(){}};function refresh(preserve){if(current()&&current().content){if(preserve!==true&&current().contentFrame)current().contentFrame.contentWindow.location.reload();return;}var t=current(),scroll=preserve===true?$('viewport').scrollTop:0,chosen=preserve===true?selected:null,m=metrics(),offset=preserve===true?Math.floor(Math.floor(scroll/m.height)*m.cols/100)*100:0;t.views=[];reset(t);t.forceRefresh=true;t.scroll=scroll;t.restoreScroll=scroll;t.selected=chosen;selected=chosen;chrome();render();loadPage(t,offset);updateDirectoryNotice();}
function mutation(method,url,done){request(method,url,null,function(error,data){if(error){toast(error);return;}var i;for(i=0;i<tabs.length;i++)if(tabs[i]!==current()){reset(tabs[i]);tabs[i].views=[];}refresh();if(done)done(data);});}
function newEntryMenu(event){
    if($('mkdir').disabled)return;var r=$('mkdir').getBoundingClientRect();
    showCreationMenu(current().path,r.left,r.bottom+4,false);
    if(event)event.stopPropagation();
}
function showCreationMenu(path,x,y,paste){
    if(!current()||current().content||current().q||!canWrite(path))return;
    var menu=$('context'),actions=[{label:'新建文件',icon:'plus',run:function(){createEntry(path,false);}},{label:'新建文件夹',icon:'folderplus',run:function(){createEntry(path,true);}}];
    if(paste&&fileClipboard)actions.push({label:'粘贴',run:function(){pasteEntry(path);}});
    menu.innerHTML='';menu.setAttribute('data-path','__create');
    for(var i=0;i<actions.length;i++)(function(action){var b=doc.createElement('button');if(action.icon)b.innerHTML=uiIcon(action.icon);var label=doc.createElement('span');text(label,action.label);b.appendChild(label);on(b,'click',function(){show(menu,false);action.run();});menu.appendChild(b);})(actions[i]);
    show(menu,true);menu.style.left=Math.max(8,Math.min(x,window.innerWidth-menu.offsetWidth-8))+'px';menu.style.top=Math.max(8,Math.min(y,window.innerHeight-menu.offsetHeight-8))+'px';
}
function mkdir(){if(!$('mkdir').disabled)createEntry(current().path,true);}
function createEntry(path,directory){
    if(!canWrite(path))return;
    var title=directory?'新建文件夹':'新建文件',initial=directory?'新建文件夹':'新建文本文档.txt';
    function promptName(value,message){modal(title,message||'输入'+(directory?'文件夹':'文件')+'名称',value,[cancel,{label:'创建',primary:true,run:function(name){
        if(!name||name==='.'||name==='..'||/[\\/:<>"|?*\x00-\x1f]/.test(name)||/[. ]$/.test(name)||name.length>255){promptName(name,'请输入有效的名称，不要包含路径或特殊字符');return;}
        var destination=join(path,name);
        if(!canWrite(path)||isFrozenPath(destination)){promptName(name,'此名称不可写入');return;}
        function complete(error){
            if(error){promptName(name,error);return;}
            for(var i=0;i<tabs.length;i++)if(!tabs[i].content&&tabs[i]!==current()){reset(tabs[i]);tabs[i].views=[];}
            if(current()&&!current().content&&current().path===path)refresh();
            toast('已创建'+(directory?'文件夹':'文件')+'“'+name+'”');
        }
        if(directory){request('POST','/api/mkdir?path='+enc(destination),null,complete);return;}
        var mount=findMount(destination);
        if(mount&&mount.browser){
            var transport=mountedXHR(destination,'',taskCreate('新建文件',name,destination,0));
            transport.onload=function(){var data;try{data=JSON.parse(transport.responseText);}catch(ignore){data={};}complete(transport.status===201||transport.status===200?null:data.error||'创建失败');};
            transport.onerror=function(){complete('连接中断，请检查文件是否已创建');};
            // Empty files require no File/Blob/FileReader APIs, including IE11.
            transport.send({name:name,size:0,lastModified:0});
        }else request('PUT','/api/upload?path='+enc(destination),null,complete);
    }}]);}
    promptName(initial,'');
}
function rename(){if(!canManage(selected))return;var entry=selected;modal('重命名',entry.name,entry.name,[cancel,{label:'保存',primary:true,run:function(name){if(!name||name===entry.name)return;var ext=function(s){var i=s.lastIndexOf('.');return i<=0?'':s.substring(i+1).toLowerCase();};function commit(chosen){renameVersioned(entry.path,join(parent(entry.path),chosen),entry.version||'',function(error){if(error)toast(error);else refresh(true);},entry.directory);}if(!entry.directory&&ext(name)!==ext(entry.name)){request('GET','/api/type?path='+enc(entry.path),null,function(error,type){if(error){toast(error);return;}var suggestion=type.extension?name.replace(/\.[^.]+$/,'')+'.'+type.extension:name;if(suggestion===name||!type.extension){modal('更改扩展名','是否保留新名称“'+name+'”？',null,[cancel,{label:'保留新名称',primary:true,run:function(){commit(name);}}]);return;}modal('确认文件扩展名','识别为 '+type.label+'，推荐名称：'+suggestion,null,[cancel,{label:'保留名称',run:function(){commit(name);}},{label:'使用推荐后缀',primary:true,run:function(){commit(suggestion);}}]);});}else commit(name);}}]);}
function remove(){if(typeof selectedEntries==='function'&&selectedEntries().length>1){batchDelete();return;}if(!canManage(selected))return;var entry=selected;modal('删除'+(entry.directory?'文件夹':'文件'),'“'+entry.name+'”'+(entry.directory?'及其内容':'')+(session.recycleBin?'将移入回收站。':'将被永久删除。'),null,[cancel,{label:'删除',primary:true,run:function(){mutation('DELETE','/api/entry?'+query({path:entry.path,recursive:1}));}}]);}
var uploadCancel=null;
function upload(files,target,options){
    if(!files||!files.length)return;
    if(typeof managedUpload==='function'&&canResumeUpload(files[0].file||files[0])){if(uploadBusy){if(managedUpload.pending.length>=32){toast('上传批次队列已满');return;}var queuedFiles=[],queueIndex;for(queueIndex=0;queueIndex<files.length;queueIndex++)queuedFiles.push(files[queueIndex]);var waiting=taskCreate('上传批次',files.length+' 个文件',target||current().path,files.length,'items'),queued={files:queuedFiles,target:target||current().path,options:options||{},task:waiting};waiting.cancel=function(){queued.cancelled=true;taskState(waiting,'cancelled');};managedUpload.pending.push(queued);toast('已加入上传等待队列');return;}managedUpload(files,target,options);return;}
    if(uploadBusy){toast('正在上传，请等待当前队列完成');return;}
    options=options||{};target=target||current().path;
    if(!session||!session.writable){toast('此共享为只读，无法上传');return;}
    if(options.drop)target=uploadTarget(target);
    if(!target||!canWrite(target)){toast('此位置不可写入');return;}
    var list=[],i,total=files.length,completed=0,failed=0,cancelled=false,finished=false;
    if(total>10000){toast('一次最多上传 10000 个文件，请分批上传');return;}
    for(i=0;i<files.length;i++){
        var source=files[i].file||files[i],relative=files[i].relativePath||source.webkitRelativePath||source.name,parts=relative.split('/'),j;
        if(parts.length>64||relative.length>4096){toast('文件夹路径过长或层级过深');return;}
        for(j=0;j<parts.length;j++)if(!parts[j]||parts[j]==='.'||parts[j]==='..'||/[\\:]/.test(parts[j])){toast('文件夹包含无效路径');return;}
        if(isFrozenPath(join(target,relative))){failed++;continue;}
        list.push({file:source,relativePath:relative});
    }
    uploadBusy=true;buttons();show($('upload-status'),true);
    function finish(){
        if(finished)return;finished=true;uploadBusy=false;uploadXHR=null;uploadCancel=null;
        show($('upload-status'),false);$('file-input').value='';$('folder-input').value='';modalEscape=null;
        thumbnailInvalidate(target);refresh();
        toast(cancelled?'上传已取消':(completed?'已上传 '+completed+' 个文件到 '+target:'没有文件上传成功')+(failed?' · '+failed+' 个未上传':''));
        if(completed&&session.writeMode==='isolated'&&!session.localAdmin)promptUploadName();
    }
    uploadCancel=function(){cancelled=true;list=[];show($('modal-shade'),false);if(uploadXHR)uploadXHR.abort();else finish();};
    function next(){
        if(cancelled||!list.length){finish();return;}
        var item=list.shift(),file=item.file,name=file.name,folder=item.relativePath.substring(0,item.relativePath.length-file.name.length);
        function destinationFor(chosen){return join(target,folder+chosen);}
        function sendFile(chosen,version,checked){
            if(cancelled){finish();return;}
            if(!version&&!checked){request('GET','/api/upload-target?'+query({path:destinationFor(chosen),size:file.size}),null,function(error,info){if(cancelled){finish();return;}if(error){failed++;toast(error);next();return;}if(info.exists){if(taskConflict==='ask')conflict(chosen,false,info);else resolveBatchConflict(destinationFor(chosen),false,function(problem,path,replacement){if(problem||!path){failed++;if(problem)toast(problem);next();return;}sendFile(base(path),replacement,true);});}else sendFile(chosen,'',true);});return;}
            var destination=destinationFor(chosen),xhr=findMount(destination)&&findMount(destination).browser?mountedXHR(destination,version):canResumeUpload(file)?resumableXHR(destination,version):legacyUploadXHR(destination),values={path:destination,parents:1};
            if(options.drop)values.drop=1;if(version)values.replaceVersion=version;
            uploadXHR=xhr;buttons();show($('upload-status'),true);
            text($('upload-label'),(total-list.length)+' / '+total+' · '+chosen+' → '+target+' · 0%');$('upload-progress').style.width='0';
            xhr.open('PUT','/api/upload?'+query(values),true);xhr.setRequestHeader('X-HLS-Token',session.token);xhr.setRequestHeader('Content-Type','application/octet-stream');
            xhr.upload.onprogress=function(event){if(event.lengthComputable){var percent=Math.floor(event.loaded/event.total*100);text($('upload-label'),(total-list.length)+' / '+total+' · '+chosen+' → '+target+' · '+percent+'%');$('upload-progress').style.width=percent+'%';}};
            xhr.onload=function(){
                uploadXHR=null;var data;try{data=JSON.parse(xhr.responseText);}catch(ignore){data={};}
                if(xhr.status===409){conflict(chosen,!!version);return;}
                if(xhr.status!==201&&xhr.status!==200){failed++;toast(data.error||'上传失败');next();return;}
                completed++;var type=data.type||{},actual=data.path||destination;
                thumbnailInvalidate(actual);
                if(type.mismatch&&type.suggestedName!==chosen){
                    modal('确认文件扩展名','“'+chosen+'”识别为 '+type.label+'。推荐名称：'+type.suggestedName,null,[{label:'保留名称',run:next},{label:'补齐推荐后缀',primary:true,run:function(){renameVersioned(actual,join(parent(actual),type.suggestedName),data.version||'',function(error){if(error)toast(error);next();});}}]);modalEscape=next;
                }else next();
            };
            xhr.onerror=function(){uploadXHR=null;request('GET','/api/upload-target?path='+enc(destination),null,function(error,info){if(cancelled){finish();return;}if(!error&&info.exists)conflict(chosen,!!version,info);else{failed++;toast('上传连接中断');next();}});};
            xhr.onabort=function(){uploadXHR=null;cancelled=true;finish();};xhr.send(file);
        }
        function conflict(chosen,changed,known){
            function explain(error,ticket){
                if(cancelled){finish();return;}
                var actions=[{label:'跳过此文件',run:function(){failed++;next();}},{label:'使用其他名称',primary:true,run:function(){var dot=chosen.lastIndexOf('.'),suggestion=dot>0?chosen.substring(0,dot)+' (1)'+chosen.substring(dot):chosen+' (1)';modal('上传为其他名称','同名文件将保留。',suggestion,[{label:'跳过',run:function(){failed++;next();}},{label:'上传',primary:true,run:function(value){if(!value||/[\\/]/.test(value)){failed++;toast('请输入不含路径分隔符的文件名');next();return;}sendFile(value,'');}}]);modalEscape=function(){failed++;next();};}}];
                if(!error&&ticket.version)actions.push({label:'替换此版本',run:function(){sendFile(chosen,ticket.version);}});
                modal(changed?'文件已发生变化':'存在同名文件',changed?'文件在上传期间被修改，本次替换未生效。请重新选择。':'“'+chosen+'”已存在或正在被写入。选择其他名称，或确认替换当前版本。',null,actions);modalEscape=function(){failed++;next();};
            }
            if(known)explain(null,known);else request('GET','/api/upload-target?'+query({path:destinationFor(chosen),size:file.size}),null,explain);
        }
        sendFile(name,'');
    }
    next();
}
