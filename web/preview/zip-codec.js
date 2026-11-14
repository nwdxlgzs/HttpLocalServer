// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// Shared by the dedicated ZIP worker and adversarial fixture tests. No eval,
// filesystem paths, recursive archive opening or allocation from unchecked sizes.
(function(root){
'use strict';
var L={source:64*1048576,entry:32*1048576,total:256*1048576,entries:8192,metadata:4*1048576,depth:32,ratio:200};
function fail(s){throw new Error('ZIP 预览已拒绝：'+s);}
function u16(b,p){return b[p]|b[p+1]<<8;}function u32(b,p){return (u16(b,p)+u16(b,p+2)*65536)>>>0;}
function u64(b,p){var v=u32(b,p)+u32(b,p+4)*4294967296;if(v>9007199254740991)fail('ZIP64 数字超出安全范围');return v;}
function bounds(b,p,n){if(p<0||n<0||p>b.length||n>b.length-p)fail('截断的压缩包');}
function utf8(b){try{var s=root.fflate.strFromU8(b),check=root.fflate.strToU8(s);if(check.length!==b.length)fail('文件名不是有效 UTF-8');for(var i=0;i<b.length;i++)if(check[i]!==b[i])fail('文件名不是有效 UTF-8');return s;}catch(ignore){fail('文件名不是有效 UTF-8');}}
function nameOf(b){var s=utf8(b);if(!s||s.length>1024||s.charAt(0)==='/'||/[\\:\x00-\x1f\x7f]/.test(s))fail('不安全的文件路径');var parts=s.split('/');if(parts[parts.length-1]==='')parts.pop();if(parts.length>L.depth)fail('目录嵌套过深');for(var i=0;i<parts.length;i++)if(!parts[i]||parts[i]==='.'||parts[i]==='..'||/[. ]$/.test(parts[i]))fail('不安全的路径分量');return s;}
function crc(b){var c=-1;for(var i=0;i<b.length;i++){c^=b[i];for(var j=0;j<8;j++)c=(c>>>1)^((c&1)?0xedb88320:0);}return(c^-1)>>>0;}
function Archive(bytes){
    var b=bytes instanceof Uint8Array?bytes:new Uint8Array(bytes),end=-1,i,p,h,at,count,cs,co,ce,started=new Date().getTime(),sum=0,entries=Object.create(null),fold=Object.create(null),ranges=[];
    if(b.length<22||b.length>L.source)fail('客户端压缩包上限为 64 MiB');
    for(i=b.length-22;i>=Math.max(0,b.length-65557);i--)if(u32(b,i)===0x06054b50&&i+22+u16(b,i+20)===b.length){end=i;break;}
    if(end<0)fail('缺少结束记录');if(u16(b,end+4)||u16(b,end+6)||u16(b,end+8)!==u16(b,end+10))fail('不支持分卷 ZIP');count=u16(b,end+10);cs=u32(b,end+12);co=u32(b,end+16);ce=end;
    if(count===65535||cs===4294967295||co===4294967295){bounds(b,end-20,20);if(u32(b,end-20)!==0x07064b50||u32(b,end-16)||u32(b,end-4)!==1)fail('ZIP64 定位记录无效');p=u64(b,end-12);bounds(b,p,56);if(u32(b,p)!==0x06064b50||u64(b,p+4)<44||u64(b,p+4)!==end-20-p-12||u32(b,p+16)||u32(b,p+20)||u64(b,p+24)!==u64(b,p+32))fail('ZIP64 结束记录无效');count=u64(b,p+32);cs=u64(b,p+40);co=u64(b,p+48);ce=p;}
    if(count>L.entries||cs>L.metadata||co>ce||cs!==ce-co)fail('中央目录超出预算或边界');bounds(b,co,cs);at=co;
    for(i=0;i<count;i++){
        if(new Date().getTime()-started>2000)fail('目录检查超时');bounds(b,at,46);if(u32(b,at)!==0x02014b50)fail('中央目录记录无效');
        var flags=u16(b,at+8),method=u16(b,at+10),nl=u16(b,at+28),el=u16(b,at+30),cl=u16(b,at+32),record=46+nl+el+cl;
        if(flags&~0x080e)fail('加密或不支持的标志');if(method!==0&&method!==8)fail('不支持此压缩算法');bounds(b,at,record);if(record>ce-at)fail('中央目录条目越界');
        var name=nameOf(b.subarray(at+46,at+46+nl)),item={path:name,size:u32(b,at+24),compressed:u32(b,at+20),offset:u32(b,at+42),crc:u32(b,at+16),method:method,directory:name.charAt(name.length-1)==='/'},disk=u16(b,at+34),zip64=false;
        for(var ex=at+46+nl;ex<at+46+nl+el;){bounds(b,ex,4);var tag=u16(b,ex),len=u16(b,ex+2);ex+=4;if(len>at+46+nl+el-ex)fail('扩展字段越界');if(tag===1){if(zip64)fail('重复的 ZIP64 扩展');zip64=true;p=ex;var keys=['size','compressed','offset'];for(var k=0;k<keys.length;k++)if(item[keys[k]]===4294967295){if(ex+len-p<8)fail('ZIP64 尺寸缺失');item[keys[k]]=u64(b,p);p+=8;}if(disk===65535){if(ex+len-p<4)fail('ZIP64 分卷值缺失');disk=u32(b,p);}}ex+=len;}
        var type=(u32(b,at+38)>>>16)&61440;if(type&&type!==32768&&type!==16384)fail('符号链接或特殊文件');
        if(disk||item.compressed>L.source||item.size>L.entry||item.size>L.total-sum||item.size>Math.max(1048576,item.compressed*L.ratio))fail('展开体积、压缩比或总量超限');
        if(method===0&&item.size!==item.compressed||!item.compressed&&item.size||item.directory&&item.size)fail('条目尺寸不一致');sum+=item.size;
        bounds(b,item.offset,30);p=item.offset;var ln=u16(b,p+26),le=u16(b,p+28);if(u32(b,p)!==0x04034b50||u16(b,p+6)!==flags||u16(b,p+8)!==method||ln!==nl)fail('本地头和中央目录不一致');bounds(b,p+30,ln+le);for(k=0;k<ln;k++)if(b[p+30+k]!==b[at+46+k])fail('本地文件名不一致');item.offset=p+30+ln+le;
        if(item.offset>co||item.compressed>co-item.offset)fail('文件数据越界');var last=item.offset+item.compressed;
        if(!(flags&8)){if(u32(b,p+14)!==item.crc||u32(b,p+18)!==4294967295&&u32(b,p+18)!==item.compressed||u32(b,p+22)!==4294967295&&u32(b,p+22)!==item.size)fail('本地尺寸或校验码不一致');}
        else{h=last;bounds(b,h,12);if(u32(b,h)===0x08074b50)h+=4;if(u32(b,h)!==item.crc)fail('数据描述校验码错误');h+=4;var wide=u32(b,at+20)===4294967295||u32(b,at+24)===4294967295;bounds(b,h,wide?16:8);if((wide?u64(b,h):u32(b,h))!==item.compressed||(wide?u64(b,h+8):u32(b,h+4))!==item.size)fail('数据描述尺寸错误');last=h+(wide?16:8);if(last>co)fail('数据描述越界');}
        ranges.push([p,last]);var key=name.toLowerCase().replace(/\/$/,'');if(fold[key])fail('重名或含糊路径');fold[key]=true;entries[name]=item;at+=record;
    }
    if(at!==ce)fail('中央目录尾部异常');ranges.sort(function(a,b){return a[0]-b[0];});for(i=1;i<ranges.length;i++)if(ranges[i][0]<ranges[i-1][1])fail('文件数据重叠');
    for(var n in entries)if(Object.prototype.hasOwnProperty.call(entries,n)){var parts=n.split('/'),prefix='';for(i=0;i<parts.length-1;i++){prefix+=(prefix?'/':'')+parts[i];if(entries[prefix])fail('文件和目录冲突');}}
    this.bytes=b;this.entries=entries;this.expanded=sum;this.work=0;
}
Archive.prototype.read=function(path){var entry=this.entries[path];if(!entry||entry.directory)fail('不存在此文件');if(entry.size>L.total-this.work)fail('累计展开工作量超限');this.work+=entry.size;var packed=this.bytes.subarray(entry.offset,entry.offset+entry.compressed),out;
    if(!entry.method)out=new Uint8Array(packed);else out=root.fflate.inflateSync(packed,{out:new Uint8Array(entry.size+1)});
    if(out.length!==entry.size||crc(out)!==entry.crc)fail('实际展开尺寸或 CRC 不匹配');return out;
};
root.HLSZip={Archive:Archive,limits:L,crc:crc};
}(typeof self!=='undefined'?self:this));
