// SPDX-License-Identifier: Apache-2.0; Copyright 2026 github@nwdxlgzs
// Deterministic media events and clock: no network/UI injection or real waiting.
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
let now=0,nextTimer=1,playCalls=0,loadCalls=0,ranges=[[0,21]],stage,media;
const timers=new Map();
function schedule(fn,delay,repeat){const id=nextTimer++;timers.set(id,{fn,at:now+delay,repeat});return id;}
function tick(amount){const until=now+amount;while(true){let found;for(const [id,t] of timers)if(t.at<=until&&(!found||t.at<found[1].at))found=[id,t];if(!found)break;const [id,t]=found;now=t.at;if(t.repeat)t.at+=t.repeat;else timers.delete(id);t.fn();}now=until;}
function element(tag){return {tagName:tag.toUpperCase(),className:'',attrs:{},style:{setProperty(name,value){this[name]=value;}},events:{},children:[],queries:{},setAttribute(k,v){this.attrs[k]=v;},removeAttribute(k){delete this.attrs[k];},appendChild(el){this.children.push(el);},insertBefore(el){this.children.unshift(el);},contains(){return false;},querySelector(s){return this.queries[s]||(this.queries[s]=element(s));},focus(){},addEventListener(name,fn){(this.events[name]||(this.events[name]=[])).push(fn);},removeEventListener(name,fn){this.events[name]=(this.events[name]||[]).filter(f=>f!==fn);}};}
function emit(el,event){for(const fn of el.events[event]||[])fn({target:el,preventDefault(){},stopPropagation(){}});}
const preview=element('preview'),doc=element('document');
doc.createElement=tag=>{const el=element(tag);if(tag==='div')stage=el;else if(tag==='video'){
    media=el;Object.assign(el,{currentTime:20,duration:100,readyState:3,playbackRate:1,volume:1,muted:false,paused:true,ended:false,seeking:false,
        play(){playCalls++;this.paused=false;emit(this,'play');emit(this,'playing');return {catch(){}};},pause(){this.paused=true;emit(this,'pause');},load(){loadCalls++;}});
    Object.defineProperty(el,'buffered',{get(){return {length:ranges.length,start(i){return ranges[i][0];},end(i){return ranges[i][1];}};}});
}return el;};
const ctx=vm.createContext({document:doc,doc,session:{hotlinkSeconds:0},previewGeneration:1,thumbnailsEnabled:false,Math,isFinite,Number,RegExp,
    setTimeout(fn,ms){return schedule(fn,ms,0);},clearTimeout(id){timers.delete(id);},setInterval(fn,ms){return schedule(fn,ms,ms);},clearInterval(id){timers.delete(id);},
    $(id){return preview;},on(el,name,fn){el.addEventListener(name,fn);},show(el,yes){el.hidden=!yes;},text(el,value){el.textContent=value;},uiIcon(){return '<svg/>';},timeLabel(n){return String(n);},captureVisual(){return false;},download(){}});
vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/scripts/player.js'),'utf8'),ctx);
ctx.mediaPreview({name:'test.mp4'},'/stream','video');
const volume=stage.querySelector('.player-volume');assert.equal(volume.style['--volume-percent'],'100%');media.volume=.35;emit(media,'volumechange');assert.equal(volume.style['--volume-percent'],'35%');media.muted=true;emit(media,'volumechange');assert.equal(volume.style['--volume-percent'],'0%');media.muted=false;media.volume=1;emit(media,'volumechange');
assert.equal(media.preload,'auto','Native media prefetch starts before buffer exhaustion');
ranges=[[0,21],[60,90]];assert.equal(ctx.playerBufferAhead(media),1,'Only continuous buffer counts, not a distant seek range');
assert.equal(ctx.playerBufferTarget(media,1),8);media.playbackRate=2;assert.equal(ctx.playerBufferTarget(media,1),16,'Safety buffer scales with playback rate');media.currentTime=97;assert.equal(ctx.playerBufferTarget(media,4),3,'Near EOF does not wait for nonexistent future data');media.currentTime=20;media.playbackRate=1;
emit(stage.querySelector('.player-play'),'click');assert.equal(playCalls,1);
emit(media,'waiting');tick(300);assert(!media.paused,'Short stalls do not force buffering');emit(media,'playing');tick(500);assert(!media.paused,'A recovered short stall cancels the pending hold');
emit(media,'waiting');tick(450);assert(media.paused&&stage.className.includes('is-buffering'),'Sustained starvation accumulates a safety buffer');
emit(media,'canplay');assert.equal(playCalls,1,'A few available frames do not repeatedly restart playback');
ranges=[[0,27.8]];emit(media,'progress');assert.equal(playCalls,1);ranges=[[0,28]];emit(media,'progress');assert.equal(playCalls,2,'Eight buffered seconds resume the first sustained stall');
emit(media,'waiting');tick(450);assert(media.paused);ranges=[[0,29]];emit(media,'progress');assert.equal(playCalls,2,'Repeated stalls increase the recovery target');
emit(stage.querySelector('.player-play'),'click');ranges=[[0,90]];emit(media,'progress');tick(1000);assert.equal(playCalls,2,'Explicit pause cancels every automatic resume');
emit(stage.querySelector('.player-play'),'click');ranges=[[0,20.5]];emit(media,'waiting');tick(450);assert(media.paused);
const seek=stage.querySelector('.player-seek');seek.value=7000;emit(seek,'change');assert.equal(media.currentTime,70,'Seek is still handled by native media Range loading');assert(!stage.className.includes('is-buffering'),'Seek discards the old buffer hold');
ctx.mediaCleanup();const previous=playCalls;tick(30000);assert.equal(playCalls,previous);assert.equal(loadCalls,1,'No reloads to chase buffers; cleanup alone releases the source');
console.log('Player state tests passed: early native prefetch, gap-aware buffer, 8–20 second recovery, rate/EOF, pause, seek and cleanup');
