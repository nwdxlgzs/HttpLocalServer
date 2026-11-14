// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
var browserID='page-'+new Date().getTime().toString(36)+'-'+Math.random().toString(36).substring(2)+Math.random().toString(36).substring(2),browseTimer=0,browseBusy=false,browseAgain=false,autoRefresh=false,browseStopped=false;
function updateDirectoryNotice(){
    var t=current(),notice=$('directory-update');if(!notice)return;
    show(notice,!!t&&!!t.needsRefresh);text($('directory-update-label'),t&&t.aliasChanged?'姓名标记已更新':'此目录有更新');
}
function setAutoRefresh(value,save){
    autoRefresh=!!value;if(save!==false)try{localStorage.setItem('hls-auto-refresh',autoRefresh?'1':'0');}catch(ignore){}
    if(autoRefresh)maybeAutoRefresh();
}
function maybeAutoRefresh(){
    var t=current();
    if(!autoRefresh||!t||!t.needsRefresh||doc.hidden||t.loading||uploadBusy||fileDrag||dropNode||tabDrag||$('modal-shade').className.indexOf('hidden')<0||$('settings-shade').className.indexOf('hidden')<0||$('path-input').className.indexOf('hidden')<0||doc.activeElement===$('search'))return;
    refresh(true);
}
function scheduleBrowse(){
    if(browseStopped||!session||standalonePath&&window.parent!==window||!standalonePath&&!current())return;
    clearTimeout(browseTimer);if(browseBusy){browseAgain=true;return;}browseTimer=setTimeout(pollBrowse,250);
}
function pollBrowse(){
    if(browseStopped||!session||!standalonePath&&!current()||browseBusy)return;
    if(standalonePath){browseBusy=true;request('GET','/api/site',null,function(error,data){browseBusy=false;if(!error)applySite(data);finishBrowsePoll();});return;}
    browseBusy=true;var t=current(),generation=t.generation,paths=[],i,owners=[],seen={};
    for(i=0;i<tabs.length;i++)if(paths.indexOf(tabs[i].path)<0)paths.push(tabs[i].content?parent(tabs[i].path):tabs[i].path);
    function addOwner(path){var own=entryOwner(path);if(own&&!seen[own]&&owners.length<64){seen[own]=true;owners.push(own);}}
    addOwner(t.path);
    for(i=0;i<nodes.length;i++)if(nodes[i].style.display!=='none'){
        var index=Number(nodes[i].getAttribute('data-index')),page=t.pages&&t.pages[Math.floor(index/100)*100],entry=page&&page.items[index%100];if(entry)addOwner(entry.path);
    }
    request('POST','/api/browse',{id:browserID,paths:paths,current:t.content?parent(t.path):t.path,showHidden:showHidden,owners:owners},function(error,data){
        browseBusy=false;
        if(!error){
            if(data.site)applySite(data.site);if(data.mounts)applyMounts(data.mounts);
            if(session.localAdmin){var button=$('host-settings');button.innerHTML=uiIcon('settings')+'<span>设置</span>'+(data.pendingNames?'<small class="review-count">'+data.pendingNames+'</small>':'');}
            if(generation===t.generation&&t.stamp){
                if(t.stamp!==data.stamp)t.needsRefresh=true;
                if(data.names){var key,j,items,owner;for(key in t.pages)if(t.pages.hasOwnProperty(key)){items=t.pages[key].items;for(j=0;j<items.length;j++){owner=entryOwner(items[j].path);if(Object.prototype.hasOwnProperty.call(data.names,owner))items[j].ownerName=data.names[owner];}}t.aliasRevision=data.aliasRevision;if(t===current())schedule();}
            }
            if(t===current()){updateDirectoryNotice();maybeAutoRefresh();}
        }
        finishBrowsePoll();
    });
}
function finishBrowsePoll(){clearTimeout(browseTimer);if(!browseStopped)browseTimer=setTimeout(pollBrowse,browseAgain?250:(doc.hidden?20000:12000)+Math.floor(Math.random()*3000));browseAgain=false;}
function promptUploadName(){
    request('GET','/api/alias',null,function(error,data){
        if(error||!data.eligible||data.name||data.pending||uploadBusy||$('modal-shade').className.indexOf('hidden')<0)return;
        modal('留下你的姓名','主机审核通过后，姓名会显示在你的 IP 文件夹和文件旁。可稍后在更多操作中提交。','',[{label:'暂不提交',run:function(){}},{label:'提交审核',primary:true,run:submitUploadName}]);
    });
}
function entryOwner(path){if(path.indexOf('/_clients/')!==0)return '';var end=path.indexOf('/',10);return end<0?path:path.substring(0,end);}
function submitUploadName(name){
    if(!name.replace(/^\s+|\s+$/g,''))return;
    request('POST','/api/alias',{name:name},function(error){toast(error||'姓名已提交，等待主机审核');});
}
function editUploadName(){
    request('GET','/api/alias',null,function(error,data){
        if(error){toast(error);return;}if(!data.eligible){toast('上传文件后可以提交姓名');return;}
        modal('姓名标记',data.pending?'已有姓名等待主机审核；重新提交会更新申请。':'经主机审核后显示，实际目录和权限仍按 IP 管理。',data.pending||data.name||'',[cancel,{label:'提交审核',primary:true,run:submitUploadName}]);
    });
}
function loadNameReviews(){
    if(!session||!session.localAdmin)return;var list=$('name-reviews');text(list,'正在读取…');
    request('GET','/api/admin/aliases',null,function(error,data){
        list.innerHTML='';if(error){text(list,error);return;}
        if(!data.entries.length){text(list,'暂无待审核的姓名');return;}
        for(var i=0;i<data.entries.length;i++)(function(entry){
            var row=doc.createElement('div');row.className='name-review';var info=doc.createElement('div'),name=doc.createElement('strong'),ip=doc.createElement('small');
            text(name,entry.name);text(ip,entry.path);info.appendChild(name);info.appendChild(ip);row.appendChild(info);
            function action(label,value){var button=doc.createElement('button');button.type='button';text(button,label);on(button,'click',function(){
                button.disabled=true;request('POST','/api/admin/aliases',{path:entry.path,revision:entry.revision,action:value},function(problem){if(problem)toast(problem);loadNameReviews();scheduleBrowse();});
            });row.appendChild(button);}
            action('拒绝','reject');action('通过','approve');list.appendChild(row);
        }(data.entries[i]));
    });
}
function initCollaboration(){
    try{autoRefresh=localStorage.getItem('hls-auto-refresh')==='1';}catch(ignore){}
    on($('directory-update-refresh'),'click',function(){refresh(true);});
    on(window,'storage',function(event){if(event.key==='hls-auto-refresh')setAutoRefresh(event.newValue==='1',false);});
    on(doc,'visibilitychange',function(){scheduleBrowse();});on(window,'focus',scheduleBrowse);
    on(window,'pagehide',function(){
        browseStopped=true;clearTimeout(browseTimer);
        if(session&&!standalonePath)request('POST','/api/browse',{id:browserID,paths:[],current:'/',showHidden:showHidden},function(){});
    });
    on(window,'pageshow',function(){browseStopped=false;scheduleBrowse();});
    on($('folder-input'),'change',function(){upload(this.files);});
    on($('reload-name-reviews'),'click',loadNameReviews);
}
