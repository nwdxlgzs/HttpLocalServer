// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
var clientZipBytes=0;
function contentURL(entry,kind){if(!/^(office|pdf|zip|editor)$/.test(kind))return '/preview?'+query({path:entry.path,embedded:1});return '/_hls/viewer?'+query({path:entry.path,kind:kind,format:(entry.type||{}).extension||''});}
function contentTab(entry,kind){
    if(tabs.length>=12){toast('已打开 12 个标签页，请先关闭多余标签再打开预览');return;}
    var reserve=kind==='zip'&&session.zipPreview==='client'?64*1048576:0;
    if(reserve>64*1048576-clientZipBytes){toast('客户端 ZIP 预览共用 64 MiB 压缩数据预算，请关闭其他压缩包或使用主机模式');return;}
    if(drawerMode==='preview')clearPreview();var t=newTab(entry.path,'',kind);if(!t)return;t.zipReservation=reserve;clientZipBytes+=reserve;
}
function cleanupContentTab(t){
    if(t.zipReservation){clientZipBytes=Math.max(0,clientZipBytes-t.zipReservation);t.zipReservation=0;}
    if(t.zipGrant&&session)request('POST','/api/zip/close?id='+enc(t.zipGrant),{},function(){});
    if(t.contentFrame){try{t.contentFrame.contentWindow.postMessage({hlsViewer:'dispose'},window.location.protocol+'//'+window.location.host);}catch(ignore){}if(t.contentFrame.parentNode)t.contentFrame.parentNode.removeChild(t.contentFrame);t.contentFrame=null;}
}
function sendContentPolicy(t){if(!t.contentFrame)return;try{t.contentFrame.contentWindow.postMessage({hlsViewer:'policy',dark:doc.body.className.indexOf('theme-dark')>=0,allowed:!!session&&session.previewAllowed,zipMode:session?session.zipPreview:'off',thumbnails:!!session&&session.thumbnailsEnabled},window.location.protocol+'//'+window.location.host);}catch(ignore){}}
function updateContentPolicies(){for(var i=0;i<tabs.length;i++)sendContentPolicy(tabs[i]);}
function syncContentTabs(){
    var t=current(),content=!!t.content,box=$('content-tabs'),i;
    if(!box){box=doc.createElement('main');box.id='content-tabs';box.className='main content-tabs hidden';$('main').parentNode.appendChild(box);}
    $('app').className=$('app').className.replace(/ preview-active/g,'')+(content?' preview-active':'');show($('main'),!content);show(box,content);if(content)show($('drawer'),false);else show($('drawer'),!!drawerMode);
    for(i=0;i<tabs.length;i++)if(tabs[i].contentFrame){show(tabs[i].contentFrame,tabs[i]===t);try{tabs[i].contentFrame.contentWindow.postMessage({hlsViewer:'visibility',active:tabs[i]===t},window.location.protocol+'//'+window.location.host);}catch(ignore){}}
    if(content&&!t.contentFrame){
        var iframe=doc.createElement('iframe');iframe.className='content-frame';iframe.title=base(t.path);iframe.src=contentURL({path:t.path},t.content);iframe.setAttribute('allow','clipboard-write; fullscreen');iframe.setAttribute('allowfullscreen','');
        t.contentFrame=iframe;on(iframe,'load',function(){sendContentPolicy(t);});box.appendChild(iframe);
    }
    if(content){sendContentPolicy(t);text($('count'),t.content==='zip'?'ZIP 压缩包预览':'文件预览');}
    $('search').disabled=content;$('path-input').disabled=content;$('folder-dropdown').disabled=content;$('list-view').disabled=content;$('grid-view').disabled=content;
}
on(window,'message',function(event){
    if(event.origin!==window.location.protocol+'//'+window.location.host||!event.data||event.data.hlsViewer!=='ready'&&event.data.hlsViewer!=='zip-open'&&event.data.hlsViewer!=='dirty'&&event.data.hlsViewer!=='thumbnail'&&event.data.hlsViewer!=='open-editor')return;
    for(var i=0;i<tabs.length;i++)if(tabs[i].contentFrame&&tabs[i].contentFrame.contentWindow===event.source){if(event.data.hlsViewer==='open-editor'){if(event.data.path===tabs[i].path)editText({path:event.data.path,name:base(event.data.path)});return;}if(event.data.hlsViewer==='thumbnail'){if(thumbnailsEnabled&&event.data.path===tabs[i].path)receiveThumbnail(event.data);return;}if(event.data.hlsViewer==='dirty')tabs[i].dirty=!!event.data.dirty;if(event.data.hlsViewer==='zip-open')tabs[i].zipGrant=String(event.data.id||'');sendContentPolicy(tabs[i]);return;}
});
