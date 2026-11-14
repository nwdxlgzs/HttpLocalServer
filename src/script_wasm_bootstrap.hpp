// SPDX-License-Identifier: Apache-2.0
#pragma once
namespace hls {
static constexpr char wasm_bootstrap[]=R"JS(
(function(api) {
    'use strict';
    var native=api.wasm, modules=new WeakMap();
    function Module(bytes) {
        if(!(this instanceof Module))throw new TypeError('Use new WebAssembly.Module');
        var copy;
        if(bytes instanceof ArrayBuffer)copy=bytes.slice(0);
        else if(ArrayBuffer.isView(bytes))copy=bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength);
        else throw new TypeError('Expected Wasm bytes');
        var info=native.inspect(copy);modules.set(this,{bytes:copy,info:info});
    }
    Module.imports=function(module){var value=modules.get(module);if(!value)throw new TypeError('Expected Module');return value.info.imports.map(function(x){return Object.assign({},x);});};
    Module.exports=function(module){var value=modules.get(module);if(!value)throw new TypeError('Expected Module');return value.info.exports.map(function(x){return Object.assign({},x);});};
    function Instance(module,imports) {
        if(!(this instanceof Instance))throw new TypeError('Use new WebAssembly.Instance');
        var value=modules.get(module);if(!value)throw new TypeError('Expected Module');
        var handle=native.instantiate(value.bytes,imports||{});this.exports=handle.exports;this.close=handle.close;
    }
    var web={Module:Module,Instance:Instance,CompileError:TypeError,LinkError:TypeError,RuntimeError:TypeError};
    web.validate=function(bytes){try{new Module(bytes);return true;}catch(e){return false;}};
    web.compile=function(bytes){return Promise.resolve().then(function(){return new Module(bytes);});};
    web.instantiate=function(bytes,imports){return Promise.resolve().then(function(){if(modules.has(bytes))return new Instance(bytes,imports);var module=new Module(bytes);return {module:module,instance:new Instance(module,imports)};});};
    // Explicit sandboxed Preview1 subset: arguments, clocks, random and bounded
    // stdout/stderr. No file descriptors, preopened directories, sockets or process.
    native.wasi=function(options) {
        options=options||{};var instance=null,strings=(options.args||api.args).map(String),output=['',''];
        var encoded=strings.map(function(s){return new Uint8Array(api.text.encode(s+'\0'));});
        var total=encoded.reduce(function(n,s){return n+s.length;},0);
        if(total>65536||strings.length>128)throw new RangeError('WASI argument budget exceeded');
        function memory(){if(!instance||!instance.exports.memory)throw new Error('Call wasi.initialize(instance) first');return instance.exports.memory.buffer;}
        function region(pointer,length){pointer=pointer>>>0;length=length>>>0;var b=memory();if(pointer>b.byteLength||length>b.byteLength-pointer)throw new RangeError('WASI memory access out of bounds');return new Uint8Array(b,pointer,length);}
        function u32(pointer,value){var bytes=region(pointer,4);new DataView(bytes.buffer,bytes.byteOffset,4).setUint32(0,value,true);}
        function text(fd,bytes){if(fd!==1&&fd!==2)return 8;if(output[fd-1].length+bytes.length>65536)return 27;output[fd-1]+=api.text.decode(bytes);return 0;}
        var imports={
            args_sizes_get:function(count,size){u32(count,encoded.length);u32(size,total);return 0;},
            args_get:function(pointers,bytes){encoded.forEach(function(s,i){u32(pointers+i*4,bytes);region(bytes,s.length).set(s);bytes+=s.length;});return 0;},
            environ_sizes_get:function(count,size){u32(count,0);u32(size,0);return 0;},environ_get:function(){return 0;},
            fd_write:function(fd,iovs,count,written){if(count>1024)return 28;var view=new DataView(memory()),n=0;region(iovs,count*8);for(var i=0;i<count;i++){var p=view.getUint32(iovs+i*8,true),len=view.getUint32(iovs+i*8+4,true),rc=text(fd,region(p,len));if(rc)return rc;n+=len;}u32(written,n);return 0;},
            fd_read:function(){return 8;},fd_close:function(){return 8;},fd_seek:function(){return 8;},fd_fdstat_get:function(){return 8;},fd_prestat_get:function(){return 8;},fd_prestat_dir_name:function(){return 8;},
            path_open:function(){return 76;},path_filestat_get:function(){return 76;},fd_filestat_get:function(){return 8;},
            random_get:function(pointer,length){if(length>65536)return 28;var bytes=region(pointer,length);for(var i=0;i<length;){var token=api.crypto.randomToken();for(var j=0;j+1<token.length&&i<length;j+=2)bytes[i++]=parseInt(token.slice(j,j+2),16);}return 0;},
            clock_time_get:function(clock,precision,pointer){if(clock!==0&&clock!==1)return 28;var n=BigInt(Date.now())*1000000n,b=region(pointer,8);new DataView(b.buffer,b.byteOffset,8).setBigUint64(0,n,true);return 0;},
            proc_exit:function(code){throw new Error('WASI proc_exit '+code);},sched_yield:function(){return 0;},poll_oneoff:function(){return 52;}
        };
        return {wasiImport:imports,initialize:function(value){instance=value;},get stdout(){return output[0];},get stderr(){return output[1];}};
    };
    globalThis.WebAssembly=web;
})(globalThis.hls);
)JS";
}
