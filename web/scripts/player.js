// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
function playerIcon(name){
    var paths={backward:'M4 8V3m0 5h5M4 8a9 9 0 1 1-1 8M10 11v6m3-6h3v6h-3Z',forward:'M20 8V3m0 5h-5M20 8a9 9 0 1 0 1 8M8 11v6m3-6h3v6h-3Z',replay:'M4 9V3m0 6h6M4 9a9 9 0 1 1-1 7',collapse:'M9 3v6H3M15 3v6h6M21 15h-6v6M3 15h6v6'};
    return paths[name]?'<svg class="ui-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="'+paths[name]+'"/></svg>':uiIcon(name);
}
function playerBufferAhead(media){
    var position=media.currentTime||0;
    for(var i=0;i<media.buffered.length;i++)try{if(media.buffered.start(i)<=position+.05&&media.buffered.end(i)>position)return media.buffered.end(i)-position;}catch(ignore){}
    return 0;
}
function playerBufferTarget(media,stalls){
    var remaining=isFinite(media.duration)?Math.max(0,media.duration-media.currentTime):Infinity;
    return Math.min(remaining,Math.min(20,8+Math.max(0,stalls-1)*4)*Math.max(.5,media.playbackRate||1));
}
function mediaPreview(entry,url,kind){
    var stage=doc.createElement('div'),media=doc.createElement(kind),isVideo=kind==='video',generation=previewGeneration,destroyed=false;
    stage.className='media-player '+(isVideo?'player-video':'player-audio');stage.tabIndex=0;stage.setAttribute('role','region');stage.setAttribute('aria-label',(isVideo?'视频':'音频')+'播放器：'+entry.name);
    stage.innerHTML='<div class="player-header"><span class="player-kind">'+uiIcon(isVideo?'play':'music')+'</span><div><strong class="player-title"></strong><span class="player-description">'+(isVideo?'视频':'音频')+'</span></div><span class="player-format"></span></div><div class="player-surface">'+(isVideo?'<button class="player-center" aria-label="播放视频">'+uiIcon('play')+'</button>':'<div class="player-album"><span class="player-disc">'+uiIcon('music')+'</span><div class="player-wave" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i><i></i><i></i></div></div>')+'<div class="player-buffering hidden" role="status"><i></i><span>正在载入</span></div><div class="player-error hidden"><strong>暂时无法播放</strong><p></p><button class="player-retry">重试</button><button class="player-download">下载文件</button></div></div><div class="player-controls"><div class="player-time"><span class="player-elapsed">0:00</span><span class="player-remaining">0:00</span></div><div class="player-timeline"><div class="player-track"><div class="player-buffered"></div><div class="player-played"></div></div><input class="player-seek" type="range" min="0" max="10000" step="1" value="0" aria-label="播放进度"><span class="player-tooltip hidden"></span></div><div class="player-toolbar"><div class="player-transport"><button class="player-play" aria-label="播放" title="播放 / 暂停（空格）">'+uiIcon('play')+'</button><button class="player-backward" aria-label="后退 10 秒" title="后退 10 秒">'+playerIcon('backward')+'</button><button class="player-forward" aria-label="前进 10 秒" title="前进 10 秒">'+playerIcon('forward')+'</button></div><div class="player-volume-group"><button class="player-mute" aria-label="静音" title="静音（M）">'+uiIcon('volume')+'</button><input class="player-volume" type="range" min="0" max="100" step="1" value="100" aria-label="音量"></div><select class="player-speed" aria-label="播放速度" title="播放速度"><option value="0.5">0.5×</option><option value="0.75">0.75×</option><option value="1" selected>1×</option><option value="1.25">1.25×</option><option value="1.5">1.5×</option><option value="2">2×</option></select>'+(isVideo?'<button class="player-fullscreen" aria-label="全屏" title="全屏（F）">'+uiIcon('fullscreen')+'</button>':'')+'</div><div class="player-message" role="status"></div></div>';
    text(stage.querySelector('.player-title'),entry.name);stage.querySelector('.player-title').title=entry.name;
    var ext=entry.name.lastIndexOf('.')>=0?entry.name.split('.').pop().toUpperCase():'MEDIA';text(stage.querySelector('.player-format'),ext.length<=8?ext:kind.toUpperCase());
    var surface=stage.querySelector('.player-surface'),controls=stage.querySelector('.player-controls'),play=stage.querySelector('.player-play'),center=stage.querySelector('.player-center');
    var seek=stage.querySelector('.player-seek'),played=stage.querySelector('.player-played'),buffered=stage.querySelector('.player-buffered'),tooltip=stage.querySelector('.player-tooltip');
    var volume=stage.querySelector('.player-volume'),mute=stage.querySelector('.player-mute'),speed=stage.querySelector('.player-speed'),elapsed=stage.querySelector('.player-elapsed'),remaining=stage.querySelector('.player-remaining');
    var message=stage.querySelector('.player-message'),loading=stage.querySelector('.player-buffering'),errorPanel=stage.querySelector('.player-error'),full=stage.querySelector('.player-fullscreen');
    var scrubbing=false,hideTimer=0,fullMode=false,bufferKey='',captured=false,captureAttempts=0,captureTime=-1;
    var wantsPlayback=false,bufferHold=false,bufferTimer=0,waitingTimer=0,stalls=0;
    // Let the native bounded media cache fetch ahead, including HTTP Range on seek.
    media.preload='auto';media.setAttribute('playsinline','');media.setAttribute('webkit-playsinline','');media.setAttribute('aria-label',entry.name);media.controls=false;
    if(!isVideo)media.style.display='none';surface.insertBefore(media,surface.firstChild);$('preview').appendChild(stage);
    function finiteDuration(){return isFinite(media.duration)&&media.duration>0?media.duration:0;}
    function setClass(name,enabled){stage.className=stage.className.replace(new RegExp('\\s*'+name+'\\b','g'),'')+(enabled?' '+name:'');}
    function setLoading(value,label){if(destroyed)return;show(loading,value);if(label)text(loading.querySelector('span'),label);}
    function timeline(position){
        var duration=finiteDuration(),percent=duration?Math.max(0,Math.min(100,position/duration*100)):0;
        played.style.width=percent+'%';text(elapsed,timeLabel(position));text(remaining,timeLabel(duration));seek.setAttribute('aria-valuetext',timeLabel(position)+' / '+timeLabel(duration));
    }
    function updateBuffer(){
        var duration=finiteDuration(),html='',key='',i,start,end;
        if(duration)for(i=0;i<media.buffered.length;i++)try{start=Math.max(0,media.buffered.start(i)/duration*100);end=Math.min(100,media.buffered.end(i)/duration*100);key+=start.toFixed(2)+':'+end.toFixed(2)+';';html+='<i style="left:'+start+'%;width:'+Math.max(0,end-start)+'%"></i>';}catch(ignore){}
        if(key!==bufferKey){bufferKey=key;buffered.innerHTML=html;}
    }
    function armHide(){
        clearTimeout(hideTimer);hideTimer=0;setClass('controls-hidden',false);
        if(fullMode&&!media.paused&&!media.ended&&!bufferHold)hideTimer=setTimeout(function(){if(destroyed||media.paused||scrubbing||bufferHold||controls.contains(doc.activeElement))return;setClass('controls-hidden',true);},2600);
    }
    function state(){
        if(destroyed)return;var paused=!bufferHold&&media.paused||media.ended,mode=media.ended?'replay':paused?'play':'pause';
        if(play._mode!==mode){play._mode=mode;play.innerHTML=playerIcon(mode);if(center)center.innerHTML=playerIcon(mode==='pause'?'play':mode);}
        play.setAttribute('aria-label',media.ended?'重新播放':paused?'播放':'暂停');if(center)center.setAttribute('aria-label',media.ended?'重新播放视频':'播放视频');
        setClass('is-playing',!paused);setClass('has-ended',media.ended);
        if(!scrubbing){seek.value=finiteDuration()?Math.round(media.currentTime/finiteDuration()*10000):0;timeline(media.currentTime);}
        seek.disabled=!finiteDuration();var volumePercent=Math.round((media.muted?0:media.volume)*100);volume.value=volumePercent;volume.style.setProperty('--volume-percent',volumePercent+'%');
        var silent=media.muted||!media.volume;if(mute._silent!==silent){mute._silent=silent;mute.innerHTML=uiIcon(silent?'muted':'volume');}mute.setAttribute('aria-label',silent?'取消静音':'静音');mute.setAttribute('aria-pressed',silent?'true':'false');updateBuffer();
    }
    function cancelBuffer(){
        clearTimeout(waitingTimer);waitingTimer=0;clearInterval(bufferTimer);bufferTimer=0;bufferHold=false;setClass('is-buffering',false);
    }
    function playMedia(ready){
        if(!ready&&session&&session.hotlinkSeconds&&typeof renewMediaAccess==='function'){renewMediaAccess(function(){if(!destroyed)playMedia(true);});return;}
        var result=media.play();if(result&&result['catch'])result['catch'](function(problem){if(!destroyed){wantsPlayback=false;cancelBuffer();setLoading(false);text(message,problem&&problem.name==='NotAllowedError'?'点击播放按钮开始播放':'无法播放此编码，可下载后打开');state();}});
    }
    function checkBuffer(){
        if(destroyed||!wantsPlayback){cancelBuffer();return;}
        var ahead=playerBufferAhead(media),target=playerBufferTarget(media,stalls);
        if(media.readyState>=3&&ahead+.05>=target){cancelBuffer();setLoading(false);playMedia();state();}
        else setLoading(true,'正在缓冲 · '+Math.floor(ahead/Math.max(.5,media.playbackRate||1))+' 秒');
    }
    function waitForBuffer(){
        if(destroyed||!wantsPlayback||media.ended||media.seeking||bufferHold||waitingTimer)return;
        // A short readyState transition should not flash a spinner or stop playback.
        waitingTimer=setTimeout(function(){
            waitingTimer=0;if(destroyed||!wantsPlayback||media.ended||media.seeking)return;
            stalls=Math.min(4,stalls+1);bufferHold=true;setClass('is-buffering',true);media.pause();armHide();state();checkBuffer();
            if(bufferHold)bufferTimer=setInterval(checkBuffer,250);
        },450);
    }
    function toggle(){
        if(destroyed)return;show(errorPanel,false);text(message,'');
        if(bufferHold||wantsPlayback&&!media.paused&&!media.ended){wantsPlayback=false;cancelBuffer();setLoading(false);media.pause();state();}
        else {cancelBuffer();wantsPlayback=true;if(media.ended)media.currentTime=0;playMedia();}armHide();
    }
    function prepareSeek(){var resume=bufferHold&&wantsPlayback;cancelBuffer();setLoading(false);if(resume)playMedia();}
    function seekTo(destination){function commit(){if(destroyed)return;prepareSeek();try{media.currentTime=destination;}catch(ignore){}state();}if(session&&session.hotlinkSeconds&&typeof renewMediaAccess==='function')renewMediaAccess(commit);else commit();}
    function jump(seconds){if(finiteDuration())seekTo(Math.max(0,Math.min(finiteDuration(),media.currentTime+seconds)));armHide();}
    function setSeek(){if(finiteDuration())try{var destination=Number(seek.value)/10000*finiteDuration();seekTo(destination);}catch(ignore){}scrubbing=false;timeline(media.currentTime);armHide();}
    function fullscreenElement(){return doc.fullscreenElement||doc.webkitFullscreenElement||doc.msFullscreenElement||doc.mozFullScreenElement;}
    function exitFullscreen(){var exit=doc.exitFullscreen||doc.webkitExitFullscreen||doc.msExitFullscreen||doc.mozCancelFullScreen;if(exit){var result=exit.call(doc);if(result&&result['catch'])result['catch'](function(){});}}
    function syncFullscreen(){fullMode=fullscreenElement()===stage;setClass('is-fullscreen',fullMode);if(full){full.innerHTML=playerIcon(fullMode?'collapse':'fullscreen');full.setAttribute('aria-label',fullMode?'退出全屏':'全屏');full.title=fullMode?'退出全屏（Esc）':'全屏（F）';full.setAttribute('aria-pressed',fullMode?'true':'false');}armHide();}
    function toggleFullscreen(){
        if(fullMode){exitFullscreen();return;}var enter=stage.requestFullscreen||stage.webkitRequestFullscreen||stage.msRequestFullscreen||stage.mozRequestFullScreen;
        if(enter){try{var result=enter.call(stage);if(result&&result['catch'])result['catch'](function(){if(!destroyed)text(message,'浏览器未允许全屏，请重试');});}catch(ignore){text(message,'此浏览器暂时无法进入全屏');}}
        else if(media.webkitEnterFullscreen)media.webkitEnterFullscreen();else text(message,'此浏览器不支持全屏');
    }
    function captureFrame(){
        if(!isVideo||captured||!thumbnailsEnabled||destroyed||generation!==previewGeneration||media.seeking||media.readyState<2||captureAttempts>=8)return;
        var threshold=Math.min(1,finiteDuration()*.1);if(media.currentTime<threshold||Math.abs(media.currentTime-captureTime)<.5)return;
        captureTime=media.currentTime;captureAttempts++;captured=captureVisual(entry,media,generation);
    }
    on(play,'click',toggle);if(center)on(center,'click',function(event){event.stopPropagation();stage.focus();toggle();});
    if(isVideo){on(media,'click',function(){stage.focus();toggle();});on(media,'dblclick',function(event){event.preventDefault();toggleFullscreen();});on(full,'click',toggleFullscreen);}
    on(stage.querySelector('.player-backward'),'click',function(){jump(-10);});on(stage.querySelector('.player-forward'),'click',function(){jump(10);});
    on(seek,'input',function(){scrubbing=true;timeline(Number(seek.value)/10000*finiteDuration());});on(seek,'change',setSeek);on(seek,'blur',function(){if(scrubbing)setSeek();});
    on(seek,'mousemove',function(event){if(!finiteDuration())return;var rect=seek.getBoundingClientRect(),fraction=Math.max(0,Math.min(1,(event.clientX-rect.left-7)/Math.max(1,rect.width-14)));text(tooltip,timeLabel(fraction*finiteDuration()));tooltip.style.left=Math.max(24,Math.min(rect.width-24,fraction*rect.width))+'px';show(tooltip,true);});on(seek,'mouseleave',function(){show(tooltip,false);});
    function setVolume(){media.volume=Number(volume.value)/100;media.muted=false;state();}on(volume,'input',setVolume);on(volume,'change',setVolume);
    on(mute,'click',function(){media.muted=!media.muted;if(!media.volume&&!media.muted)media.volume=.5;state();});on(speed,'change',function(){media.playbackRate=Number(speed.value);});
    var stateEvents=['timeupdate','play','pause','ended','volumechange','durationchange'],i;
    for(i=0;i<stateEvents.length;i++)on(media,stateEvents[i],state);
    on(media,'loadedmetadata',function(){text(stage.querySelector('.player-description'),isVideo&&media.videoWidth?media.videoWidth+' × '+media.videoHeight+' · '+timeLabel(media.duration):'音频 · '+timeLabel(media.duration));state();});
    on(media,'progress',function(){updateBuffer();if(bufferHold)checkBuffer();});on(media,'waiting',waitForBuffer);on(media,'seeking',function(){setLoading(true,'正在定位');});
    on(media,'seeked',function(){if(!bufferHold)setLoading(false);state();captureFrame();if(wantsPlayback&&media.readyState<3)waitForBuffer();});on(media,'loadeddata',function(){if(!bufferHold)setLoading(false);state();});on(media,'canplay',function(){if(bufferHold)checkBuffer();else if(!waitingTimer)setLoading(false);});on(media,'playing',function(){if(bufferHold)return;clearTimeout(waitingTimer);waitingTimer=0;wantsPlayback=true;setLoading(false);show(errorPanel,false);armHide();});
    on(media,'timeupdate',captureFrame);on(media,'pause',function(){armHide();captureFrame();});on(media,'ended',function(){wantsPlayback=false;cancelBuffer();setLoading(false);armHide();});
    on(media,'error',function(){if(destroyed)return;wantsPlayback=false;cancelBuffer();setLoading(false);show(errorPanel,true);text(errorPanel.querySelector('p'),media.error&&media.error.code===2?'网络读取中断，可以重试。':'浏览器暂不支持此文件的编码。');state();});
    on(stage.querySelector('.player-retry'),'click',function(){show(errorPanel,false);media.load();toggle();});on(stage.querySelector('.player-download'),'click',function(){download(entry);});
    on(stage,'mousemove',armHide);on(stage,'touchstart',armHide);on(stage,'focusin',armHide);
    on(stage,'keydown',function(event){
        var tag=event.target.tagName.toLowerCase();if(tag==='input'||tag==='select'||tag==='button')return;
        if(event.keyCode===32||event.keyCode===75){event.preventDefault();toggle();}
        else if(event.keyCode===37||event.keyCode===39){event.preventDefault();jump(event.keyCode===37?-5:5);}
        else if(event.keyCode===38||event.keyCode===40){event.preventDefault();media.volume=Math.max(0,Math.min(1,media.volume+(event.keyCode===38?.05:-.05)));media.muted=false;state();}
        else if(event.keyCode===77){media.muted=!media.muted;state();}else if(event.keyCode===70&&isVideo)toggleFullscreen();
    });
    var fullEvents=['fullscreenchange','webkitfullscreenchange','MSFullscreenChange','mozfullscreenchange'];for(i=0;i<fullEvents.length;i++)on(doc,fullEvents[i],syncFullscreen);
    media.src=url;state();
    mediaCleanup=function(){destroyed=true;wantsPlayback=false;cancelBuffer();clearTimeout(hideTimer);if(fullscreenElement()===stage)exitFullscreen();for(var j=0;j<fullEvents.length;j++)doc.removeEventListener(fullEvents[j],syncFullscreen,false);media.pause();media.removeAttribute('src');media.load();};
}
