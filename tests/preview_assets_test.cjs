// SPDX-License-Identifier: Apache-2.0
const fs=require('fs'),path=require('path'),zlib=require('zlib'),crypto=require('crypto'),assert=require('assert/strict');
const acorn=require('internal/deps/acorn/acorn/dist/acorn');
const manifest=JSON.parse(fs.readFileSync('resources/previews-manifest.json','utf8')),packed=fs.readFileSync('resources/previews.bin');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');assert.equal(hash(packed),manifest.sha256);
let expected=0,es5=0,modern=0;
for(const asset of manifest.assets){
 assert.equal(asset.offset,expected);assert(asset.rawBytes<=8*1048576,'Embedded asset has a bounded decode size');
 const raw=zlib.gunzipSync(packed.subarray(asset.offset,asset.offset+asset.bytes));assert.equal(raw.length,asset.rawBytes);assert.equal(hash(raw),asset.sha256);
 const source=/^(fflate|marked)\.js$/.test(asset.path)?'web/vendor/'+asset.path:asset.path==='compat.js'?'web/scripts/compat.js':'web/preview/'+asset.path;assert.equal(Buffer.compare(raw,fs.readFileSync(source)),0,source);expected+=asset.bytes;
 if(/\.(m?js)$/.test(asset.path)){
  const legacy=!asset.path.endsWith('.mjs')&&(asset.path.indexOf('vendor/')!==0||asset.path.indexOf('vendor/legacy/')===0||asset.path==='fflate.js');
  const module=asset.path.endsWith('.mjs')||!legacy;
  acorn.parse(raw.toString(),{ecmaVersion:legacy?5:'latest',sourceType:module?'module':'script'});
  if(legacy)es5++;else modern++;
 }
}
assert.equal(expected,packed.length);for(const [file,sha] of Object.entries(manifest.sources))assert.equal(hash(fs.readFileSync(file)),sha,file);
for(const name of ['zip-sandbox.html','zip-sandbox.js','vendor/legacy/pdf.js','vendor/legacy/pdf.worker.js','vendor/legacy/core.js','vendor/legacy/jbig2_nowasm_fallback.js','vendor/legacy/openjpeg_nowasm_fallback.js'])assert(manifest.assets.some(a=>a.path===name),name);
console.log('Preview assets passed:',manifest.assets.length,'compressed assets;',es5,'ES5 scripts;',modern,'modern modules; exact source hashes and bounded embedded decode');
