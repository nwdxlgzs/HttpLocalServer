// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// All downloaded/relayed files share ONE page-local retained-byte budget.
var p2pCacheMiB=64,p2pCacheSeconds=3600,p2pCacheBytes=0,p2pCacheClock=0,p2pCacheTimer=0,p2pCachedBlocks=Object.create(null),p2pCachedFiles=Object.create(null);
function p2pCacheName(meta){return JSON.stringify([meta.path,meta.version]);}
function p2pCacheClear(){p2pCachedBlocks=Object.create(null);p2pCachedFiles=Object.create(null);p2pCacheBytes=0;clearTimeout(p2pCacheTimer);p2pCacheTimer=0;}
function p2pCacheTrim(){
    var now=new Date().getTime(),key,block,oldest,oldKey,referenced=Object.create(null);
    for(key in p2pCachedBlocks){block=p2pCachedBlocks[key];if(p2pCachedFiles[block.file]===undefined||now-p2pCachedFiles[block.file]>=p2pCacheSeconds*1000){p2pCacheBytes-=block.bytes.length;delete p2pCachedBlocks[key];}}
    while(p2pCacheBytes>p2pCacheMiB*1048576){oldest=Infinity;oldKey=null;for(key in p2pCachedBlocks)if(p2pCachedBlocks[key].used<oldest){oldest=p2pCachedBlocks[key].used;oldKey=key;}if(oldKey===null)break;p2pCacheBytes-=p2pCachedBlocks[oldKey].bytes.length;delete p2pCachedBlocks[oldKey];}
    for(key in p2pCachedBlocks)referenced[p2pCachedBlocks[key].file]=true;
    for(key in p2pCachedFiles)if(!referenced[key])delete p2pCachedFiles[key];
    if(p2pCacheBytes&&!p2pCacheTimer)p2pCacheTimer=setTimeout(function(){p2pCacheTimer=0;p2pCacheTrim();},Math.min(5000,p2pCacheSeconds*1000));
}
function p2pCacheConfigure(mib,seconds){
    if(typeof mib==='number'&&mib%1===0&&mib>=0&&mib<=64)p2pCacheMiB=mib;
    if(typeof seconds==='number'&&seconds%1===0&&seconds>=1&&seconds<=86400)p2pCacheSeconds=seconds;
    p2pCacheTrim();
}
function p2pCacheTouch(meta,age){var name=p2pCacheName(meta);if(p2pCachedFiles[name]===undefined)return;var when=new Date().getTime()-Math.max(0,age||0);p2pCachedFiles[name]=Math.max(p2pCachedFiles[name],when);}
function p2pCachePut(meta,index,bytes){
    if(!p2pSending()||!p2pCacheMiB||bytes.length>p2pCacheMiB*1048576)return;
    var name=p2pCacheName(meta),key=name+':'+index,old=p2pCachedBlocks[key];if(old)p2pCacheBytes-=old.bytes.length;
    p2pCachedFiles[name]=new Date().getTime();p2pCachedBlocks[key]={file:name,index:index,bytes:bytes,used:++p2pCacheClock};p2pCacheBytes+=bytes.length;p2pCacheTrim();
}
function p2pCacheGet(meta,index){p2pCacheTrim();var block=p2pCachedBlocks[p2pCacheName(meta)+':'+index];if(!block)return null;block.used=++p2pCacheClock;return block.bytes;}
function p2pCacheRanges(meta){
    if(!p2pSending())return [];p2pCacheTrim();var name=p2pCacheName(meta),indices=[],key,ranges=[],i,index;
    for(key in p2pCachedBlocks)if(p2pCachedBlocks[key].file===name)indices.push(p2pCachedBlocks[key].index);indices.sort(function(a,b){return a-b;});
    for(i=0;i<indices.length;i++){index=indices[i];if(ranges.length&&ranges[ranges.length-1][1]===index)ranges[ranges.length-1][1]++;else ranges.push([index,index+1]);}return ranges.slice(0,256);
}
