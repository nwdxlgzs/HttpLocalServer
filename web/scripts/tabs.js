// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
function saveScroll(){var t=current();if(t){t.scroll=$('viewport').scrollTop;if(t.history[t.position])t.history[t.position].scroll=t.scroll;}}
function newTab(path,initialQuery,content){if(tabs.length>=12){toast('已打开 12 个标签页，请先关闭多余标签再打开预览');return;}saveScroll();var t={id:nextID++,content:content||'',path:path||'/',q:initialQuery||'',sort:'name',order:'asc',layout:'list',history:[],position:0,pages:{},loaded:0,total:0,loading:false,ready:!!content,generation:0,selected:null,scroll:0,views:[]};t.history=[{path:t.path,q:t.q,sort:'name',order:'asc',scroll:0}];tabs.push(t);active=tabs.length-1;activate();return t;}
function tabButton(target){while(target&&target!==$('tabs')){if(target.getAttribute&&target.getAttribute('data-tab')!==null)return target;target=target.parentNode;}return null;}
function updateTabButtons(){var box=$('tabs-window'),max=Math.max(0,$('tabs').offsetWidth-box.clientWidth);tabScroll=Math.max(0,Math.min(tabScroll,max));box.scrollLeft=tabScroll;$('tab-prev').disabled=tabScroll<=1;$('tab-next').disabled=tabScroll>=max-1;}
var tabHoldUntil=0,tabHoldTimer=0,tabOverlap=18,tabDrag=null,tabDropSettling=false,tabLastActiveID=0,tabLastCount=0;
function holdTabWidth(){tabHoldUntil=new Date().getTime()+850;clearTimeout(tabHoldTimer);tabHoldTimer=setTimeout(function(){tabHoldUntil=0;layoutTabs(true);},870);}
function layoutTabs(ensure){
    if(tabDrag&&tabDrag.moved)return;
    var bar=$('tabbar').clientWidth,count=tabs.length,plus=36,preferred=200,minimum=104,available=Math.max(60,bar-plus);
    var fitted=Math.max(minimum,Math.min(preferred,Math.floor((available+tabOverlap*Math.max(0,count-1))/Math.max(1,count))));
    tabWidth=new Date().getTime()<tabHoldUntil?Math.min(tabWidth,fitted):fitted;
    var total=count*tabWidth-Math.max(0,count-1)*tabOverlap;tabOverflow=total>available;
    var control=tabOverflow?28:0,windowWidth=tabOverflow?Math.max(30,bar-plus-56):total,box=$('tabs-window'),items=$('tabs').querySelectorAll('.tab'),i;
    show($('tab-prev'),tabOverflow);show($('tab-next'),tabOverflow);box.style.left=control+'px';box.style.width=windowWidth+'px';$('tabs').style.width=total+'px';
    for(i=0;i<items.length;i++){
        var item=items[i];item.style.width=tabWidth+'px';item.style.marginRight=i<count-1?-tabOverlap+'px':'0';
        var activeShape=i===active;
        if(item._shapeWidth!==tabWidth||item._shapeActive!==activeShape){
            item._shapeWidth=tabWidth;item._shapeActive=activeShape;item.querySelector('.tab-shape').setAttribute('viewBox','0 0 '+tabWidth+' 42');
            item.querySelector('.tab-shape path').setAttribute('d',(activeShape?'M0 42Q10 42 10 32V13':'M10 42V13')+'Q10 1 22 1H'+(tabWidth-22)+'Q'+(tabWidth-10)+' 1 '+(tabWidth-10)+' 13'+(activeShape?'V32Q'+(tabWidth-10)+' 42 '+tabWidth+' 42':'V42'));
        }
    }
    $('tab-next').style.left=(control+windowWidth)+'px';$('new-tab').style.left=(control+windowWidth+control+2)+'px';$('new-tab').disabled=count>=12;
    if(ensure&&items[active]){var left=active*(tabWidth-tabOverlap),right=left+tabWidth;if(left<tabScroll)tabScroll=left;else if(right>tabScroll+windowWidth)tabScroll=right-windowWidth;}
    if(!tabOverflow)tabScroll=0;updateTabButtons();
}
function renderTabs(){
    if(tabDrag&&tabDrag.moved)return;
    var box=$('tabs'),existing=box.querySelectorAll('.tab'),old={},used={},i,t,node,id,position;
    for(i=0;i<existing.length;i++)old[existing[i].getAttribute('data-tab-id')]={node:existing[i],left:existing[i].getBoundingClientRect().left};
    for(i=0;i<existing.length;i++){existing[i]._tabMotion=(existing[i]._tabMotion||0)+1;existing[i].style.transition='none';existing[i].style.transform='';}
    for(i=0;i<tabs.length;i++){
        t=tabs[i];id=String(t.id);used[id]=true;node=old[id]?old[id].node:doc.createElement('button');
        if(!old[id]){node.innerHTML='<svg class="tab-shape" viewBox="0 0 210 41" preserveAspectRatio="none" aria-hidden="true" focusable="false"><path d="M0 41C8 41 10 37 10 29V13Q10 1 23 1H187Q200 1 200 13V29C200 37 202 41 210 41"/></svg><span class="tab-label"></span><span class="close" role="button" tabindex="0" title="关闭标签页" aria-label="关闭标签页">'+uiIcon('close')+'</span>';}
        node.className='tab'+(i===active?' active':'')+(!old[id]?' tab-enter':'');node.setAttribute('data-tab',i);node.setAttribute('data-tab-id',id);node.setAttribute('draggable','false');node.setAttribute('role','tab');node.setAttribute('aria-selected',i===active?'true':'false');node.title=t.path;
        text(node.querySelector('.tab-label'),t.path==='/'?siteTitle:base(t.path));node.querySelector('.close').setAttribute('data-close',i);node.querySelector('.close').setAttribute('aria-disabled',tabs.length===1?'true':'false');
        if(box.children[i]!==node)box.insertBefore(node,box.children[i]||null);
    }
    for(id in old)if(old.hasOwnProperty(id)&&!used[id])box.removeChild(old[id].node);
    var activeID=current()?current().id:0;
    layoutTabs(!tabDropSettling&&(activeID!==tabLastActiveID||tabs.length!==tabLastCount));
    tabLastActiveID=activeID;tabLastCount=tabs.length;
    var reduced=window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    for(i=0;i<tabs.length;i++){
        node=box.children[i];position=old[String(tabs[i].id)];
        node.style.transition='';
        if(position&&!reduced){var delta=position.left-node.getBoundingClientRect().left;if(Math.abs(delta)>1){node.style.transition='none';node.style.transform='translateX('+delta+'px)';node.getBoundingClientRect();(function(el,motion){raf(function(){if(el._tabMotion!==motion||tabDrag&&tabDrag.moved)return;el.style.transition='';el.style.transform='';});}(node,node._tabMotion));}}
        else if(!position)(function(el){raf(function(){el.className=el.className.replace(' tab-enter','');});}(node));
    }
}
function tabDragPoint(event,id){
    var points=event.changedTouches,i;
    if(points){for(i=0;i<points.length;i++)if(id===null||points[i].identifier===id)return{x:points[i].clientX,y:points[i].clientY,id:points[i].identifier};return null;}
    if(id!==null&&event.pointerId!==undefined&&event.pointerId!==id)return null;
    return{x:event.clientX,y:event.clientY,id:event.pointerId===undefined?null:event.pointerId};
}
function startTabDrag(event){
    if(tabDrag||tabs.length<2||event.isPrimary===false||typeof event.button==='number'&&event.button!==0)return;
    var node=event.target;
    while(node&&node!==$('tabs')){if(node.getAttribute&&node.getAttribute('data-close')!==null)return;node=node.parentNode;}
    node=tabButton(event.target);if(!node)return;
    var point=tabDragPoint(event,null),rect=node.getBoundingClientRect();if(!point)return;
    tabDrag={id:Number(node.getAttribute('data-tab-id')),node:node,pointer:point.id,x:point.x,y:point.y,startX:point.x,startY:point.y,grab:point.x-rect.left,moved:false,frame:false,timer:0};
}
function paintTabDrag(){
    var drag=tabDrag;if(!drag||!drag.moved)return;drag.frame=false;
    var rail=$('tabs-window'),rect=rail.getBoundingClientRect(),step=drag.step;
    var left=Math.max(0,Math.min(drag.maximum,drag.x-rect.left+rail.scrollLeft-drag.grab));
    drag.destination=Math.max(0,Math.min(drag.items.length-1,Math.floor(left/step+.5)));
    for(var i=0;i<drag.items.length;i++){
        var shift=0;
        if(i===drag.source)shift=left-drag.origins[i];
        else if(drag.destination>drag.source&&i>drag.source&&i<=drag.destination)shift=-step;
        else if(drag.destination<drag.source&&i<drag.source&&i>=drag.destination)shift=step;
        var value='translateX('+shift+'px)';if(drag.items[i].style.transform!==value)drag.items[i].style.transform=value;
    }
}
function scheduleTabDrag(){
    var drag=tabDrag;if(!drag||drag.frame)return;drag.frame=true;
    raf(function(){if(tabDrag===drag)paintTabDrag();});
}
function scrollTabDrag(){
    var drag=tabDrag;if(!drag||!drag.moved)return;
    var rect=$('tabs-window').getBoundingClientRect(),bar=$('tabbar').getBoundingClientRect(),edge=Math.min(36,rect.width/4),speed=0;
    if(drag.y<bar.top-30||drag.y>bar.bottom+30)return;
    if(drag.x<rect.left+edge)speed=-Math.min(18,4+(rect.left+edge-drag.x)*.4);
    else if(drag.x>rect.right-edge)speed=Math.min(18,4+(drag.x-rect.right+edge)*.4);
    if(speed){var previous=tabScroll;tabScroll+=speed;updateTabButtons();if(previous!==tabScroll)scheduleTabDrag();}
}
function moveTabDrag(event){
    var drag=tabDrag;if(!drag)return;
    var point=tabDragPoint(event,drag.pointer);if(!point)return;drag.x=point.x;drag.y=point.y;
    if(!drag.moved){
        if(Math.abs(point.x-drag.startX)+Math.abs(point.y-drag.startY)<6)return;
        drag.items=$('tabs').querySelectorAll('.tab');drag.origins=[];drag.source=-1;
        for(var i=0;i<drag.items.length;i++){
            var item=drag.items[i];drag.origins.push(item.offsetLeft);
            item._tabMotion=(item._tabMotion||0)+1;item.style.transform='';item.style.transition='';
            if(Number(item.getAttribute('data-tab-id'))===drag.id)drag.source=i;
        }
        if(drag.source<0){finishTabDrag(false);return;}
        drag.step=drag.items.length>1?drag.origins[1]-drag.origins[0]:tabWidth-tabOverlap;
        drag.maximum=drag.origins[drag.items.length-1];drag.destination=drag.source;drag.moved=true;
        drag.node.className+=' dragging';drag.node.setAttribute('aria-grabbed','true');
        doc.body.className+=' tab-dragging';
        if(drag.pointer!==null&&drag.node.setPointerCapture)try{drag.node.setPointerCapture(drag.pointer);}catch(ignore){}
        drag.timer=setInterval(scrollTabDrag,40);
    }
    if(event.cancelable!==false)event.preventDefault();scheduleTabDrag();
}
function finishTabDrag(commit,event){
    var drag=tabDrag;if(!drag)return;
    var point=event?tabDragPoint(event,drag.pointer):null;if(event&&!point)return;
    if(point){drag.x=point.x;drag.y=point.y;}
    if(drag.moved)paintTabDrag();
    tabDrag=null;clearInterval(drag.timer);
    if(drag.pointer!==null&&drag.node.releasePointerCapture)try{drag.node.releasePointerCapture(drag.pointer);}catch(ignore){}
    if(!drag.moved)return;
    lastDrop=new Date().getTime();doc.body.className=doc.body.className.replace(/\s*tab-dragging\b/g,'');
    drag.node.className=drag.node.className.replace(/\s*dragging\b/g,'');drag.node.removeAttribute('aria-grabbed');
    var bar=$('tabbar').getBoundingClientRect();
    if(commit&&drag.y>=bar.top-30&&drag.y<=bar.bottom+30&&drag.destination!==drag.source){
        var activeID=current().id,moving=tabs.splice(drag.source,1)[0];tabs.splice(drag.destination,0,moving);
        for(var i=0;i<tabs.length;i++)if(tabs[i].id===activeID)active=i;
    }
    tabDropSettling=true;renderTabs();tabDropSettling=false;
}
if(window.PointerEvent){
    on($('tabs'),'pointerdown',startTabDrag);on(doc,'pointermove',moveTabDrag);
    on(doc,'pointerup',function(event){finishTabDrag(true,event);});on(doc,'pointercancel',function(event){finishTabDrag(false,event);});
    on($('tabs'),'lostpointercapture',function(event){if(tabDrag&&event.pointerId===tabDrag.pointer)finishTabDrag(false);});
}else{
    on($('tabs'),'mousedown',startTabDrag);on(doc,'mousemove',moveTabDrag);on(doc,'mouseup',function(event){finishTabDrag(true,event);});
    on($('tabs'),'touchstart',startTabDrag);on($('tabs'),'touchmove',moveTabDrag);
    on(doc,'touchend',function(event){finishTabDrag(true,event);});on(doc,'touchcancel',function(event){finishTabDrag(false,event);});
}
on($('tabs'),'dragstart',function(event){event.preventDefault();});
on(window,'blur',function(){finishTabDrag(false);});on(window,'resize',function(){finishTabDrag(false);});
on(doc,'keydown',function(event){if(event.keyCode===27&&tabDrag){finishTabDrag(false);event.preventDefault();}});
function activate(){scheduleBrowse();updateDirectoryNotice();var t=current();selected=t.selected;chrome();if(typeof syncContentTabs==='function')syncContentTabs();if(t.content)return;t.restoreScroll=t.scroll;render();if(!standalonePath){if(t.pendingFocus)loadFocused(t);else if(!t.ready)loadPage(t,0);}}
function reset(t){t.selection=Object.create(null);t.stamp="";t.needsRefresh=false;t.aliasChanged=false;t.forceRefresh=false;t.generation++;if(t.focusXHR)t.focusXHR.abort();t.focusXHR=null;t.pendingFocus='';t.revealIndex=null;t.restoreScroll=null;t.pages={};t.loaded=0;t.total=0;t.loading=false;t.ready=false;t.error='';t.selected=null;t.scroll=0;if(t===current()){selected=null;$('viewport').scrollTop=0;}}
function viewKey(t){return t.path+'|'+t.q+'|'+t.sort+'|'+t.order+'|'+showHidden;}function remember(t){if(t.ready){t.views.unshift({key:viewKey(t),pages:t.pages,loaded:t.loaded,total:t.total,stamp:t.stamp,aliasRevision:t.aliasRevision,needsRefresh:t.needsRefresh,aliasChanged:t.aliasChanged,time:new Date().getTime()});if(t.views.length>3)t.views.pop();}}
function navigate(path,q,historyMove,focusPath){var t=current();if(t.content){newTab(path||parent(t.path),q);return;}if(!historyMove)saveScroll();remember(t);if(!historyMove){var next={path:normalized(path),q:q||'',sort:t.sort,order:t.order,scroll:0};if(t.path===next.path&&t.q===next.q)return;t.history=t.history.slice(0,t.position+1);t.history.push(next);if(t.history.length>128)t.history.shift();t.position=t.history.length-1;}var state=t.history[t.position];t.path=state.path;t.q=state.q;t.sort=state.sort;t.order=state.order;reset(t);t.pendingFocus=focusPath||'';t.scroll=focusPath?0:state.scroll||0;var vi;for(vi=0;!focusPath&&vi<t.views.length;vi++)if(t.views[vi].key===viewKey(t)&&new Date().getTime()-t.views[vi].time<10000){t.pages=t.views[vi].pages;t.loaded=t.views[vi].loaded;t.total=t.views[vi].total;t.stamp=t.views[vi].stamp;t.aliasRevision=t.views[vi].aliasRevision;t.needsRefresh=t.views[vi].needsRefresh;t.aliasChanged=t.views[vi].aliasChanged;t.ready=true;break;}activate();}
function moveHistory(delta){var t=current();if(t.position+delta<0||t.position+delta>=t.history.length)return;saveScroll();t.position+=delta;navigate('','',true);}
function chrome(){var t=current(),parts=t.path.split('/'),html='<button data-path="/">共享目录</button>',path='',i;for(i=1;i<parts.length;i++){if(!parts[i])continue;path+='/'+parts[i];html+='<span>›</span><button data-path="'+esc(path)+'">'+esc(parts[i])+'</button>';} $('crumbs').innerHTML=html;text($('heading'),t.q?'搜索“'+t.q+'”':t.path==='/'?siteTitle:base(t.path));text($('subtitle'),t.q?'搜索范围：'+t.path:t.path==='/'?'共享目录':t.path);$('search').value=t.q;$('path-input').value=t.path;$('back').disabled=t.position===0;$('forward').disabled=t.position===t.history.length-1;$('up').disabled=!!t.content||t.path==='/'&&!t.q;show($('clear-search'),!!t.q);$('main').className='main'+(t.layout==='grid'?' grid':'')+(drawerMode?' with-drawer':'');$('list-view').className=t.layout==='list'?'active':'';$('grid-view').className=t.layout==='grid'?'active':'';renderTabs();buttons();try{if(window.history.replaceState)window.history.replaceState(null,'','#'+enc(t.content?parent(t.path):t.path)+(t.q?'?q='+enc(t.q):''));}catch(ignore){}}
