/* Frame time is the only clock. HyperFrames owns all media playback. */
(function(){
 'use strict';
 const film=JSON.parse(document.getElementById('film-data').textContent);
 document.querySelectorAll('.recorded-frame').forEach(wrapper=>{
  const reference=document.querySelector('#'+wrapper.dataset.reference+' .media-frame');
  const r=reference.getBoundingClientRect();
  Object.assign(wrapper.style,{left:r.left+'px',top:r.top+'px',width:r.width+'px',height:r.height+'px',zIndex:'5000'});
 });
 const tl=gsap.timeline({paused:true});
 const ease=['power3.out','sine.out','expo.out'];
 for(const [index,scene] of film.scenes.entries()){
  const root=document.getElementById('scene-'+scene.id);
  const enter=scene.start;
  if(index)tl.from(root,{opacity:0,x:index%7===0?35:0,duration:.35,ease:'sine.inOut'},enter);
  if(scene.kind==='outro')tl.to('.film-edition,.film-counter',{color:'#A8BCD4',duration:.35,ease:'sine.inOut'},enter);
  root.querySelectorAll('.motion').forEach((el,i)=>tl.from(el,{opacity:0,y:el.classList.contains('media-frame')?22:16,scale:el.classList.contains('brand-lockup')?.98:1,duration:.45,ease:ease[i%3]},scene.start+.06+Math.min(i*.045,.36)));
  const key=scene.sourceId||scene.id;
  if(window.HLSTechnical)HLSTechnical.animate(tl,root,scene.start,scene.duration,{...scene,id:key});
  if(!['S07','S12','S17'].includes(key))root.querySelectorAll('.screen-image').forEach(img=>tl.to(img,{scale:1.022,duration:Math.max(1,scene.duration-1.1),ease:'none'},scene.start+.9));
  root.querySelectorAll('.data-block').forEach((el,i)=>{
   tl.from(el,{opacity:0,x:-25,duration:.6,ease:'power2.out'},scene.start+1.1+i*.32);
   if(i>=3)tl.to(el,{backgroundColor:'#D7F2E8',borderColor:'#9BCAB8',color:'#18324D',duration:.45},scene.start+3+i*.3);
  });
  root.querySelectorAll('.transfer-progress i').forEach(el=>tl.from(el,{scaleX:.1,duration:Math.min(5,scene.duration-2),ease:'power1.inOut'},scene.start+1));
  root.querySelectorAll('.task-progress i').forEach((el,i)=>tl.from(el,{scaleX:0,duration:2+i*.3,ease:'power2.out'},scene.start+1.5+i*.2));
  root.querySelectorAll('.scan-line').forEach(el=>tl.from(el,{scaleX:0,duration:2.4,ease:'power2.inOut'},scene.start+1.3));
  root.querySelectorAll('.network-dot').forEach(el=>tl.to(el,{y:60,duration:1.3,repeat:Math.max(0,Math.floor((scene.duration-2)/2.2)-1),repeatDelay:.9,ease:'none'},scene.start+1.3));
  root.querySelectorAll('.terminal-result').forEach((el,i)=>tl.from(el,{opacity:0,y:8,duration:.4},scene.start+1.2+i*.55));
  if(scene.kind==='address'){
   const scan=root.querySelector('.scan-beam'),result=root.querySelector('.scan-result'),link=root.querySelector('.scan-link');
   const reveal=scene.start+Math.min(3.3,scene.duration*.48);
   tl.from(scan,{scaleX:.2,opacity:0,duration:.25,ease:'power2.out'},scene.start+.65);
   tl.to(scan,{y:175,duration:1.1,yoyo:true,repeat:1,ease:'sine.inOut'},scene.start+.7);
   tl.from(link,{opacity:0,y:25,duration:.32,ease:'power3.out'},reveal-.8);
   tl.from(result,{opacity:0,x:24,duration:.4,ease:'power3.out'},reveal);
   tl.from(root.querySelector('.phone-result-page'),{y:12,duration:.5,ease:'power2.out'},reveal+.1);
   const route=root.querySelector('.qr-route i');tl.to(route,{x:95,duration:1.15,repeat:1,repeatDelay:.4,ease:'none'},scene.start+.7);
  }
 }
 document.querySelectorAll('.recorded-frame').forEach(wrapper=>{
  const start=Number(wrapper.dataset.footageStart);
  tl.from(wrapper,{opacity:0,duration:.3,ease:'sine.out'},start);
 });
 tl.from('.film-progress i',{scaleX:0,duration:film.duration,ease:'none'},0);
 window.__timelines=window.__timelines||{};
 window.__timelines[film.variant]=tl;
 // Read-only metadata for deterministic production QA.
 window.__film={variant:film.variant,duration:film.duration,width:film.width,height:film.height,scenes:film.scenes};
}());
