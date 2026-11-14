// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
var siteAttention=[],siteAttentionMap=Object.create(null),siteAttentionClick='locate',siteAnnouncement='',dismissedNotice=0,folderGeneration=0,folderLoading=false,folderOffset=0,folderTotal=0,folderParent='';
function isAttention(path){return Object.prototype.hasOwnProperty.call(siteAttentionMap,path);}
function attentionAction(entry){return isAttention(entry.path)?'取消关注':'标记为关注文件';}
function setAttention(entry){
    if(!session||!session.localAdmin||!entry||entry.directory)return;
    var marked=!isAttention(entry.path);
    request('POST','/api/admin/attention',{path:entry.path,marked:marked},function(error,data){
        if(error){toast(error);return;}
        applySite(data);toast(marked?'已标记关注文件':'已取消关注');
    });
}
function renderAttention(){
    var list=$('attention-files'),i;list.innerHTML='';
    for(i=0;i<siteAttention.length;i++){
        var path=siteAttention[i],button=doc.createElement('button'),downloading=siteAttentionClick==='download';button.className='attention-file';button.setAttribute('data-attention-path',path);button.title=(downloading?'下载：':'在文件夹中定位：')+path;
        var star=doc.createElement('span');star.className='attention-star';star.setAttribute('aria-hidden','true');text(star,'★');button.appendChild(star);
        var label=doc.createElement('span');text(label,base(path));button.appendChild(label);button.setAttribute('aria-label',(downloading?'下载关注文件 ':'定位关注文件 ')+base(path));list.appendChild(button);
    }
    show(list,!!siteAttention.length);
}
function applySite(data){
    if(typeof p2pPolicy==='function')p2pPolicy(data.p2pEnabled,data.p2pCacheMiB,data.p2pCacheSeconds);
    if(session&&data.zipPreview!==undefined)session.zipPreview=data.zipPreview;thumbnailPolicy(data.thumbnailsEnabled&&data.previewAllowed);var prior=siteRevision,paths=data.attentionPaths||[],action=data.attentionClick==='download'?'download':'locate',changed=action!==siteAttentionClick,i;siteAttentionClick=action;
    siteRevision=data.revision||0;siteTitle=data.title||'在线文件下载';siteAnnouncement=data.announcement||'';if(!standalonePath)doc.title=siteTitle;
    if(paths.length!==siteAttention.length)changed=true;else for(i=0;i<paths.length;i++)if(paths[i]!==siteAttention[i]){changed=true;break;}
    if(changed){siteAttention=paths.slice(0);siteAttentionMap=Object.create(null);for(i=0;i<siteAttention.length;i++)siteAttentionMap[siteAttention[i]]=true;renderAttention();}
    text($('notice-title'),siteAttention.length?'关注文件 · '+siteAttention.length:'公告');
    text($('notice-message'),siteAnnouncement);$('notice-message').title=siteAnnouncement;show($('notice-message'),!!siteAnnouncement);
    var visible=!!(siteAnnouncement||siteAttention.length)&&dismissedNotice!==siteRevision;
    show($('site-notice'),visible);$('app').className=$('app').className.replace(/\s*has-notice\b/g,'')+(visible?' has-notice':'');
    if(current()){renderTabs();schedule();}
    if(prior&&prior!==siteRevision)request('GET','/api/session',null,function(problem,fresh){if(!problem){session=fresh;updateSessionUI();}});
}
var themeMotionTimer=0;
function applyTheme(mode){
    var switcher=$('theme-switcher'),previous=themeMode;
    themeMode=mode;var modes=['auto','light','dark'],i;for(i=0;i<modes.length;i++){var button=$('theme-'+modes[i]);if(button){button.className=mode===modes[i]?'active':'';button.setAttribute('aria-pressed',mode===modes[i]?'true':'false');}}
    if(switcher){
        switcher.setAttribute('data-mode',mode);
        if(switcher.className.indexOf('theme-ready')<0)raf(function(){if(switcher.className.indexOf('theme-ready')<0)switcher.className+=' theme-ready';});
        else if(previous!==mode){clearTimeout(themeMotionTimer);switcher.className=switcher.className.replace(/\s*theme-animating\b/g,'')+' theme-animating';themeMotionTimer=setTimeout(function(){switcher.className=switcher.className.replace(/\s*theme-animating\b/g,'');},360);}
    }
    var dark=mode==='dark'||mode==='auto'&&window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches;
    doc.body.className=doc.body.className.replace(/\s*theme-dark\b/g,'')+(dark?' theme-dark':'');if(typeof updateContentPolicies==='function')updateContentPolicies();
    try{localStorage.setItem('hls-theme',mode);}catch(ignore){}
}
function setShowHidden(value,save){
    value=!!value;if(save!==false)try{localStorage.setItem('hls-show-hidden',value?'1':'0');}catch(ignore){}
    if(showHidden===value)return;showHidden=value;folderGeneration++;show($('folder-menu'),false);
    if(standalonePath)return;
    for(var i=0;i<tabs.length;i++){tabs[i].views=[];reset(tabs[i]);}if(current())activate();
}
function updateSessionUI(){
    if(!session)return;if(typeof updateContentPolicies==='function')updateContentPolicies();if(typeof p2pPolicy==='function')p2pPolicy(session.p2pEnabled&&(!standalonePath||window.parent===window),session.p2pCacheMiB,session.p2pCacheSeconds);applyMounts(session.mounts);updateMountButtons();thumbnailPolicy(session.thumbnailsEnabled&&session.previewAllowed);if(standalonePath){if(!session.previewAllowed){clearPreview();if(drawerMode!=='preview')drawer('preview');text($('preview'),'主机已禁用文件预览');}else if(standaloneBlocked&&selected)inlinePreview(selected);standaloneBlocked=!session.previewAllowed;}else if(!session.previewAllowed&&drawerMode==='preview'){drawer('preview');}
    text($('client-ip'),session.clientIP);$('host-settings').innerHTML=uiIcon('settings')+'<span>设置</span>';
    show($('host-settings'),!!session.localAdmin);if(!session.localAdmin)closeSettings();
    text($('permission'),session.writeMode==='isolated'?(session.localAdmin?'本机管理员 · 可管理整个共享目录':'专属管理目录\n'+session.writableRoot):{readonly:'只读',shared:'共享写入'}[session.writeMode]);
    text($('write-mode'),{readonly:'只读',isolated:'独立权限写入',shared:'共享写入'}[session.writeMode]);
    show($('own'),session.writeMode==='isolated'&&!session.localAdmin);
    if(current()){buttons();schedule();}
}
function refreshSite(){
    request('GET','/api/site',null,function(error,data){
        if(!error)applySite(data);
    });
}
function closeSettings(){show($('settings-shade'),false);$('mcp-token').value='';$('settings-error').textContent='';}
function openSettings(){
    if(!session||!session.localAdmin)return;
    request('GET','/api/admin/settings',null,function(error,data){
        if(error){toast(error);return;}
        $('setting-password').value='';$('setting-clear-password').checked=false;text($('password-state'),data.loginRequired?'已设置口令':'未设置口令');$('setting-hotlink').value=data.hotlinkSeconds||0;
        $('setting-title').value=data.title;$('setting-write').value=data.writeMode;$('setting-ip').value=data.ipLimit;$('setting-owner').value=data.ownerLimit;$('setting-upload').value=data.maxUploadMiB;$('setting-ip-upload').value=data.ipUploadMiB||0;$('setting-ip-upload-unit').value='mb';var limitLines=[],ip;for(ip in data.ipUploadOverrides)if(data.ipUploadOverrides.hasOwnProperty(ip))limitLines.push(ip+'='+data.ipUploadOverrides[ip]+'MB');$('setting-ip-upload-overrides').value=limitLines.join('\n');
        $('setting-zip-preview').value=data.zipPreview||'off';$('setting-preview').checked=data.previewAllowed;$('setting-thumbnails').checked=data.thumbnailsEnabled;$('setting-magika').checked=data.magikaEnabled;$('setting-mcp').checked=data.mcpEnabled;$('setting-p2p').checked=!!data.p2pEnabled;$('setting-p2p-cache').value=data.p2pCacheMiB;$('setting-p2p-expiry').value=data.p2pCacheSeconds;
        $('setting-announcement').value=data.announcement;$('setting-attention-click').value=data.attentionClick||'locate';
        $('mcp-url').value=window.location.protocol+'//'+window.location.host+data.mcpPath;$('mcp-token').value=data.mcpToken;$('mcp-token').type='password';text($('reveal-mcp'),'显示令牌');text($('settings-error'),'');
        show($('settings-shade'),true);$('setting-title').focus();loadNameReviews();loadBlockedIPs();if(typeof operationsLoad==='function')operationsLoad(data);
    });
}
function saveSettings(event){
    event.preventDefault();
    var fields=[['setting-p2p-cache',0,64],['setting-p2p-expiry',1,86400],['setting-hotlink',0,86400],['setting-ip',0,1000000],['setting-owner',0,1000000],['setting-upload',1,1048576],['setting-ip-upload',0,1073741824]],i,value;
    for(i=0;i<fields.length;i++){value=Number($(fields[i][0]).value);if(!isFinite(value)||Math.floor(value)!==value||value<fields[i][1]||value>fields[i][2]){text($('settings-error'),'请输入范围内的整数');$(fields[i][0]).focus();return;}}
    var allowance=Number($('setting-ip-upload').value)*($('setting-ip-upload-unit').value==='gb'?1024:1);if(allowance>1073741824){text($('settings-error'),'上传额度过大');return;}
    var overrides={},lines=$('setting-ip-upload-overrides').value.split(/\r?\n/),j,match;for(j=0;j<lines.length;j++){if(!lines[j].replace(/\s/g,''))continue;match=/^\s*(.+?)\s*=\s*(\d+)\s*(MB|GB)?\s*$/i.exec(lines[j]);if(!match){text($('settings-error'),'每行使用 IP=数值MB 或 IP=数值GB');return;}var amount=Number(match[2])*((match[3]||'MB').toUpperCase()==='GB'?1024:1);if(amount>1073741824){text($('settings-error'),'单独 IP 额度过大');return;}overrides[match[1]]=amount;}
    var update={zipPreview:$('setting-zip-preview').value,hotlinkSeconds:Number($('setting-hotlink').value),ipUploadOverrides:overrides,ipUploadMiB:allowance,title:$('setting-title').value,writeMode:$('setting-write').value,ipLimit:Number($('setting-ip').value),ownerLimit:Number($('setting-owner').value),maxUploadMiB:Number($('setting-upload').value),previewAllowed:$('setting-preview').checked,thumbnailsEnabled:$('setting-thumbnails').checked,magikaEnabled:$('setting-magika').checked,mcpEnabled:$('setting-mcp').checked,p2pEnabled:$('setting-p2p').checked,p2pCacheMiB:Number($('setting-p2p-cache').value),p2pCacheSeconds:Number($('setting-p2p-expiry').value),announcement:$('setting-announcement').value,attentionClick:$('setting-attention-click').value};
    if(typeof operationsSave==='function')try{operationsSave(update);}catch(problem){text($('settings-error'),problem.message);return;}
    if($('setting-password').value||$('setting-clear-password').checked)update.loginPassword=$('setting-clear-password').checked?'':$('setting-password').value;
    $('save-settings').disabled=true;request('POST','/api/admin/settings',update,function(error){$('save-settings').disabled=false;if(error){text($('settings-error'),error);return;}closeSettings();request('GET','/api/session',null,function(problem,data){if(!problem){session=data;updateSessionUI();}});refreshSite();toast('设置已应用');});
}
function moreMenu(event){
    event.stopPropagation();var menu=$('context'),anchor=$('more-button');
    if(menu.getAttribute('data-path')==='__more'&&menu.className.indexOf('hidden')<0){show(menu,false);return;}
    function draw(group){
        menu.innerHTML='';menu.setAttribute('data-path','__more');
        function action(label,run,branch){var button=doc.createElement('button');button.className=branch?'menu-branch':'';text(button,label);if(branch===true){var arrow=doc.createElement('span');arrow.className='menu-arrow';arrow.innerHTML=uiIcon('right');button.appendChild(arrow);}on(button,'click',function(e){e.stopPropagation();if(!branch)show(menu,false);run();});menu.appendChild(button);}
        if(group){action('‹ 返回',function(){draw('');},'back');var caption=doc.createElement('div');caption.className='menu-caption';text(caption,group);menu.appendChild(caption);}
        if(!group){
            action('传输任务',taskPanel);
            if(session&&session.writable)action('上传文件夹',function(){if('webkitdirectory' in $('folder-input'))$('folder-input').click();else toast('此浏览器不支持选择文件夹');});
            action('下载当前目录',function(){draw('下载当前目录');},true);
            if(canDirectoryPick()||session&&session.localAdmin||providerMounts.length)action('挂载目录',function(){draw('挂载目录');},true);
            action('浏览选项',function(){draw('浏览选项');},true);
            action('共享与访问',function(){draw('共享与访问');},true);
        }else if(group==='下载当前目录'){
            var entry={path:current().path,name:current().path==='/'?siteTitle:base(current().path),directory:true};
            if(canDirectorySave())action('保存到本地文件夹',function(){saveDirectory(entry);});
            action('下载为 ZIP',function(){downloadDirectory(entry);});
        }else if(group==='挂载目录'){
            if(canDirectoryPick())action('挂载浏览器文件夹',chooseBrowserMount);
            if(session&&session.localAdmin)action('挂载主机目录或分区',mountHostPath);
            if(providerMounts.length||session&&session.localAdmin&&sharedMounts.length)action('管理挂载',manageMounts);
        }else if(group==='浏览选项'){
            action((showHidden?'✓ ':'')+'显示隐藏文件和文件夹',function(){setShowHidden(!showHidden);});
            action((autoRefresh?'✓ ':'')+'目录更新时自动刷新',function(){setAutoRefresh(!autoRefresh);toast(autoRefresh?'目录更新时将自动刷新':'目录更新时仅提示刷新');});
            action('显示公告',function(){dismissedNotice=0;refreshSite();});
        }else if(group==='共享与访问'){
            action('访问地址与二维码',showURLCodes);
            action('恢复下载会话',function(){request('POST','/api/access/activate',{},function(error){toast(error||'下载会话已恢复');});});
            if(p2pEnabled&&canP2P())action(p2pSendEnabled?'本页面关闭 PCDN 发送':'本页面开启 PCDN 发送',function(){p2pSetSending(!p2pSendEnabled);});
            if(session&&session.writeMode==='isolated'&&!session.localAdmin)action('提交或修改姓名',editUploadName);
        }
        show(menu,true);var box=anchor.getBoundingClientRect();menu.style.left=Math.max(8,Math.min(box.left,window.innerWidth-menu.offsetWidth-8))+'px';menu.style.top=Math.max(8,Math.min(box.bottom+8,window.innerHeight-menu.offsetHeight-8))+'px';
    }draw('');
}
function positionFolderMenu(){var menu=$('folder-menu');if(menu.className.indexOf('hidden')>=0)return;var rect=$('address').getBoundingClientRect(),width=Math.max(1,Math.min(rect.width||rect.right-rect.left,window.innerWidth-16));menu.style.width=width+'px';menu.style.left=Math.max(8,Math.min(rect.left,window.innerWidth-width-8))+'px';menu.style.top=(rect.bottom+5)+'px';menu.style.maxHeight=Math.max(70,Math.min(310,window.innerHeight-rect.bottom-13))+'px';}
function toggleFolders(){
    var menu=$('folder-menu');if(menu.className.indexOf('hidden')<0){show(menu,false);return;}
    folderGeneration++;folderParent=parent(current().path);folderOffset=0;folderTotal=1;folderLoading=false;menu.innerHTML='<div class="menu-caption">同级文件夹</div>';show(menu,true);
    positionFolderMenu();
    loadFolders(folderGeneration);
}
function loadFolders(generation){
    if(folderLoading||folderOffset>=folderTotal)return;folderLoading=true;
    request('GET','/api/list?'+query({path:folderParent,offset:folderOffset,limit:100,sort:'name',order:'asc',showHidden:showHidden?1:0}),null,function(error,data){
        if(generation!==folderGeneration)return;folderLoading=false;if(error){toast(error);return;}
        folderTotal=data.total;folderOffset+=data.entries.length;var menu=$('folder-menu'),i;
        for(i=0;i<data.entries.length;i++)if(data.entries[i].directory)(function(entry){var button=doc.createElement('button');button.innerHTML=uiIcon('folder')+'<span>'+esc(entry.name)+'</span>';if(entry.path===current().path)button.className='current-folder';on(button,'click',function(){show(menu,false);navigate(entry.path,'');});menu.appendChild(button);}(data.entries[i]));
        if(menu.children.length===1){var message=doc.createElement('div');message.className='menu-caption';text(message,'没有同级文件夹');menu.appendChild(message);}
        if(folderOffset<folderTotal&&menu.scrollHeight<=menu.clientHeight)loadFolders(generation);
    });
}
function initEnhancements(){
    try{var saved=localStorage.getItem('hls-theme');if(saved==='light'||saved==='dark'||saved==='auto')themeMode=saved;}catch(ignore){}applyTheme(themeMode);
    try{showHidden=localStorage.getItem('hls-show-hidden')==='1';}catch(ignore){}
    on(window,'storage',function(event){if(event.key==='hls-show-hidden')setShowHidden(event.newValue==='1',false);});
    if(window.matchMedia){var scheme=window.matchMedia('(prefers-color-scheme: dark)'),changed=function(){if(themeMode==='auto')applyTheme('auto');};if(scheme.addEventListener)scheme.addEventListener('change',changed);else if(scheme.addListener)scheme.addListener(changed);}
    $('close-settings').innerHTML=uiIcon('close');on($('host-settings'),'click',openSettings);on($('more-button'),'click',moreMenu);on($('settings-form'),'submit',saveSettings);
    on($('close-settings'),'click',closeSettings);on($('cancel-settings'),'click',closeSettings);on($('settings-shade'),'click',function(event){if(event.target===$('settings-shade'))closeSettings();});
    on($('settings-panel')||doc.querySelector('.settings-panel'),'keydown',function(event){if(event.keyCode===27)closeSettings();});
    on($('reveal-mcp'),'click',function(){var hidden=$('mcp-token').type==='password';$('mcp-token').type=hidden?'text':'password';text(this,hidden?'隐藏令牌':'显示令牌');});
    on($('copy-mcp'),'click',function(){var config=JSON.stringify({mcpServers:{HttpLocalServer:{type:'http',url:$('mcp-url').value,headers:{Authorization:'Bearer '+$('mcp-token').value}}}},null,2);var area=doc.createElement('textarea');area.value=config;area.style.position='fixed';area.style.left='-10000px';doc.body.appendChild(area);area.select();var ok=false;try{ok=doc.execCommand('copy');}catch(ignore){}doc.body.removeChild(area);toast(ok?'连接配置已复制':'复制失败，请手动复制入口和令牌');});
    on($('dismiss-notice'),'click',function(){dismissedNotice=siteRevision;show($('site-notice'),false);$('app').className=$('app').className.replace(/\s*has-notice\b/g,'');schedule();});
    function attentionPath(event){var node=event.target;while(node&&node!==$('attention-files')){if(node.getAttribute&&node.getAttribute('data-attention-path'))return node.getAttribute('data-attention-path');node=node.parentNode;}return '';}
    on($('attention-files'),'click',function(event){var path=attentionPath(event);if(!path)return;if(siteAttentionClick==='download')download({path:path,name:base(path),directory:false});else revealFile(path);});
    on($('attention-files'),'contextmenu',function(event){
        var path=attentionPath(event);if(!path||!session.localAdmin)return;event.preventDefault();var menu=$('context');menu.innerHTML='';menu.setAttribute('data-path','__attention');
        var cancel=doc.createElement('button');text(cancel,'取消关注');on(cancel,'click',function(){show(menu,false);setAttention({name:base(path),path:path});});menu.appendChild(cancel);show(menu,true);
        menu.style.left=Math.max(0,Math.min(event.clientX,window.innerWidth-menu.offsetWidth-8))+'px';menu.style.top=Math.max(0,Math.min(event.clientY,window.innerHeight-menu.offsetHeight-8))+'px';
    });
    on($('notice-message'),'click',function(){if(siteAnnouncement)modal('公告',siteAnnouncement,null,[{label:'关闭',run:function(){}}]);});
    on(window,'resize',positionFolderMenu);on($('folder-menu'),'scroll',function(){if(this.scrollHeight-this.scrollTop-this.clientHeight<60)loadFolders(folderGeneration);});
    on(doc,'click',function(event){if(!$('folder-menu').contains(event.target))show($('folder-menu'),false);});
    on(doc,'keydown',function(event){if(event.keyCode===27){closeSettings();show($('folder-menu'),false);}});
    $('theme-auto').innerHTML=uiIcon('system');$('theme-light').innerHTML=uiIcon('sun');$('theme-dark').innerHTML=uiIcon('moon');on($('reload-blocked'),'click',loadBlockedIPs);
    on($('theme-auto'),'click',function(){applyTheme('auto');});on($('theme-light'),'click',function(){applyTheme('light');});on($('theme-dark'),'click',function(){applyTheme('dark');});
    initMounts();initCollaboration();
}
