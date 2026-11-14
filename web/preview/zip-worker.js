// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
'use strict';
importScripts('fflate.js','zip-codec.js');
var archive=null;
self.onmessage=function(event){var m=event.data;
    try{if(m.op==='open'){archive=new HLSZip.Archive(m.bytes);var list=[],names=Object.keys(archive.entries);for(var i=0;i<names.length;i++){var e=archive.entries[names[i]];list.push({path:e.path,size:e.size,directory:e.directory});}self.postMessage({id:m.id,entries:list,expanded:archive.expanded});}
        else if(m.op==='read'){if(!archive)throw new Error('压缩包未打开');var data=archive.read(m.path);if(data.byteOffset||data.byteLength!==data.buffer.byteLength)data=new Uint8Array(data);self.postMessage({id:m.id,bytes:data.buffer},[data.buffer]);}
        else throw new Error('未知压缩包操作');
    }catch(error){self.postMessage({id:m.id,error:error.message||String(error)});}
};
