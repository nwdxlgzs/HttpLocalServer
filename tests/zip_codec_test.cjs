// SPDX-License-Identifier: Apache-2.0
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const fflate=require('../web/vendor/fflate.js');
const c=vm.createContext({Uint8Array,Uint16Array,Uint32Array,Int32Array,ArrayBuffer,DataView,TextDecoder,TextEncoder,console});
c.self=c;vm.runInContext(fs.readFileSync('web/vendor/fflate.js','utf8'),c);vm.runInContext(fs.readFileSync('web/preview/zip-codec.js','utf8'),c);
const Archive=c.HLSZip.Archive;
function zip(files){return Buffer.from(fflate.zipSync(files,{level:4}));}
function denied(bytes,reason){assert.throws(()=>new Archive(bytes),reason);}
const valid=zip({'site/index.html':Buffer.from('<h1>hello</h1>'),'site/a.css':Buffer.from('body{color:red}'),'中文.txt':Buffer.from('内容')});
let a=new Archive(valid);assert.equal(Buffer.from(a.read('site/index.html')).toString(),'<h1>hello</h1>');assert.equal(Buffer.from(a.read('中文.txt')).toString(),'内容');
denied(zip({'../bad':Buffer.from('no')}),'Traversal');denied(zip({'/bad':Buffer.from('no')}),'Absolute path');denied(zip({'A':Buffer.from('a'),'a':Buffer.from('b')}),'Case collision');denied(zip({'a':Buffer.from('a'),'a/b':Buffer.from('b')}),'File-directory collision');
denied(zip({'bomb':Buffer.alloc(2*1048576,65)}),'Expansion ratio');
const forged=zip({'forged':Buffer.alloc(500000,65)}),central=forged.indexOf(Buffer.from('504b0102','hex'));
forged.writeUInt32LE(10,22);forged.writeUInt32LE(10,central+24);a=new Archive(forged);assert.throws(()=>a.read('forged'),'Actual output exceeds declared bytes');
const oversized=Buffer.from(valid),p=oversized.indexOf(Buffer.from('504b0102','hex'));oversized.writeUInt32LE(0x7fffffff,p+24);denied(oversized,'No allocation from untrusted size');
const encrypted=Buffer.from(valid);encrypted.writeUInt16LE(1,p+8);denied(encrypted,'Encrypted');
let rejected=0;for(let i=0;i<1000;i++){const b=Buffer.from(valid);b[(i*97)%b.length]^=1<<(i%8);try{const z=new Archive(b);for(const n of Object.keys(z.entries))if(!z.entries[n].directory)z.read(n);}catch{rejected++;}}
assert(rejected>500);console.log('ZIP client codec passed: exact bytes, bombs, forged output, paths, encryption and 1000 bounded mutations');
