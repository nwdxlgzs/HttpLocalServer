// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// Blob URLs are created inside this opaque origin, never borrowed from the
// authenticated parent origin (modern browsers partition blob URL access).
(function(){'use strict';var ready=false,urls=[];
function replace(value,map){for(var key in map)if(Object.prototype.hasOwnProperty.call(map,key))value=value.split(key).join(map[key]);return value;}
addEventListener('message',function(event){if(ready||event.source!==parent||!event.data||event.data.hlsZipInit!==1)return;ready=true;var m=event.data,map=Object.create(null),total=0;
    try{if(typeof m.html!=='string'||m.html.length>4*1048576||!Array.isArray(m.assets)||m.assets.length>300)throw new Error('ZIP page budget');
        for(var i=0;i<m.assets.length;i++){var a=m.assets[i],value=a.data;total+=typeof value==='string'?value.length*2:value.byteLength;if(total>96*1048576)throw new Error('ZIP page resource budget');if(typeof value==='string')value=replace(value,map);var url=URL.createObjectURL(new Blob([value],{type:a.mime}));map[a.token]=url;urls.push(url);}
        var html=replace(m.html,map);document.open();document.write(html);document.close();
    }catch(error){document.body.textContent='ZIP 网页预览失败：'+error.message;}
});
addEventListener('unload',function(){for(var i=0;i<urls.length;i++)URL.revokeObjectURL(urls[i]);});
parent.postMessage({hlsZipSandbox:'ready'},'*');
}());
