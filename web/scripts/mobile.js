// SPDX-License-Identifier: Apache-2.0
var mobileTouch=null,mobileTouchTimer=0;
on($('spacer'),'touchstart',function(e){if(!e.touches||e.touches.length!==1)return;var point=e.touches[0],entry=selectedFrom(e);if(!entry)return;mobileTouch={entry:entry,x:point.clientX,y:point.clientY,moved:false,menu:false};mobileTouchTimer=setTimeout(function(){if(!mobileTouch||mobileTouch.moved)return;mobileTouch.menu=true;choose(entry);showContext(entry,point.clientX,point.clientY);},550);});
on($('spacer'),'touchmove',function(e){if(!mobileTouch||!e.touches.length)return;var p=e.touches[0];if(Math.abs(p.clientX-mobileTouch.x)>10||Math.abs(p.clientY-mobileTouch.y)>10){mobileTouch.moved=true;clearTimeout(mobileTouchTimer);}});
on($('spacer'),'touchend',function(e){clearTimeout(mobileTouchTimer);var touch=mobileTouch;mobileTouch=null;if(!touch||touch.moved)return;e.preventDefault();if(touch.menu)return;var entry=touch.entry;if(frozenEntry(entry)){choose(entry);return;}if(entry.directory){navigate(entry.path,'');return;}choose(entry);preview(entry,false);});
on($('spacer'),'touchcancel',function(){clearTimeout(mobileTouchTimer);mobileTouch=null;});
on($('connection'),'click',function(){if(window.innerWidth>600)return;var panel=doc.querySelector('.connection');panel.className=panel.className.indexOf('mobile-expanded')>=0?'connection':'connection mobile-expanded';});
