// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
function cachedFile(t,path){
    var key,page,i;
    for(key in t.pages)if(Object.prototype.hasOwnProperty.call(t.pages,key)){
        page=t.pages[key];for(i=0;i<page.items.length;i++)if(page.items[i].path===path)return{entry:page.items[i],index:Number(key)+i};
    }
    return null;
}
function revealFile(path){
    var t=current();if(!t)return;
    clearTimeout(searchTimer);endEdit();
    if(drawerMode)drawer(drawerMode);
    var directory=parent(path),same=t.path===directory&&!t.q,found=same?cachedFile(t,path):null;
    if(found){t.revealIndex=found.index;choose(found.entry);$('viewport').focus();return;}
    if(same){saveScroll();remember(t);reset(t);t.pendingFocus=path;activate();}
    else navigate(directory,'',false,path);
}
function loadFocused(t){
    if(t.loading||!t.pendingFocus)return;
    var path=t.pendingFocus,generation=t.generation;t.loading=true;
    if(t===current())renderCount();
    t.focusXHR=request('GET','/api/list?'+query({path:t.path,browser:browserID,focus:path,limit:100,sort:t.sort,order:t.order,refresh:1,showHidden:showHidden?1:0}),null,function(error,data){
        if(generation!==t.generation)return;
        t.focusXHR=null;t.pendingFocus='';t.loading=false;
        var index=data.focusIndex,offset=data.offset,entries=data.entries,relative=index-offset;
        if(!error&&(Object.prototype.toString.call(entries)!=='[object Array]'||typeof data.total!=='number'||typeof offset!=='number'||offset%100!==0||typeof index!=='number'||Math.floor(index)!==index||relative<0||relative>=entries.length||!entries[relative]||entries[relative].path!==path))error='服务器返回了无法识别的定位响应';
        if(error){
            if(t===current())toast('无法定位文件：'+error);
            loadPage(t,0);return;
        }
        t.pages={};t.pages[offset]={items:entries,used:++cacheClock};t.loaded=offset+entries.length;t.total=data.total;t.truncated=data.truncated;t.indexing=false;t.ready=true;t.stamp=data.stamp||'';t.aliasRevision=data.aliasRevision||0;t.error='';
        t.selected=entries[relative];t.revealIndex=index;
        if(t===current()){choose(t.selected);$('viewport').focus();}
    });
}
