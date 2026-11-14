// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// Development only. Packed outputs are committed; ordinary builds need no Node.
const fs=require('fs'),path=require('path');
const root=path.resolve(__dirname,'../..'),modules=path.join(root,'.deps/preview-tooling/node_modules');
const esbuild=require(path.join(modules,'esbuild')),babel=require(path.join(modules,'@babel/core'));
const out=path.join(root,'web/preview/vendor/legacy');fs.mkdirSync(out,{recursive:true});
function noDynamicImport({types:t}) {
    function reject(p) {
        p.replaceWith(t.callExpression(t.memberExpression(t.identifier('globalThis'),t.identifier('HLSLegacyImport')),
            [p.node.source||p.node.arguments[0]]));
    }
    return {visitor:{
        CallExpression(p){if(p.node.callee.type==='Import')reject(p);},
        ImportExpression(p){reject(p);}
    }};
}

function es5(code){return babel.transformSync(code,{filename:'preview-legacy.js',sourceType:'script',presets:[[require.resolve('@babel/preset-env',{paths:[modules]}),{targets:{ie:'11'},modules:false,bugfixes:true}]],plugins:[noDynamicImport],comments:false,compact:true,babelrc:false,configFile:false}).code;}
async function bundle(options){const result=await esbuild.build({...options,bundle:true,format:'iife',target:'es2020',platform:'browser',external:['node:*'],write:false,logLevel:'warning'});return result.outputFiles[0].text;}
(async()=>{
    let core=await bundle({stdin:{contents:"import 'core-js/actual';import 'whatwg-fetch';import {TextDecoder,TextEncoder} from 'text-encoding';if(typeof globalThis.TextDecoder==='undefined')globalThis.TextDecoder=TextDecoder;if(typeof globalThis.TextEncoder==='undefined')globalThis.TextEncoder=TextEncoder;import * as Streams from 'web-streams-polyfill';for(const key in Streams)if(typeof globalThis[key]==='undefined')globalThis[key]=Streams[key];import DOMMatrix from '@thednp/dommatrix';if(typeof globalThis.DOMMatrix==='undefined')globalThis.DOMMatrix=DOMMatrix;",resolveDir:modules}});
    core+='\nif(typeof window!=="undefined"){'+fs.readFileSync(path.join(modules,'path2d-polyfill/dist/path2d-polyfill.min.js'),'utf8')+'}\n';
    core+=fs.readFileSync(path.join(root,'web/preview/legacy-canvas.js'),'utf8');
    fs.writeFileSync(path.join(out,'core.js'),es5(core));
    for(const [name,globalName] of [['pdf','HLSPDF'],['pdf.worker','pdfjsWorker']]){
        const code=await bundle({entryPoints:[path.join(modules,'pdfjs-dist/legacy/build/'+name+'.mjs')],globalName});
        fs.writeFileSync(path.join(out,name+'.js'),(name==='pdf.worker'?"importScripts('core.js');\n":'')+es5(code));
    }
    for(const [file,globalName] of [['openjpeg_nowasm_fallback','HLSJPEG2000'],['jbig2_nowasm_fallback','HLSJBIG2']]){
        const code=await bundle({entryPoints:[path.join(modules,'pdfjs-dist/wasm/'+file+'.js')],globalName});
        fs.writeFileSync(path.join(out,file+'.js'),es5(code));
    }
    for(const name of ['core-js','@thednp/dommatrix','path2d','path2d-polyfill','whatwg-fetch','web-streams-polyfill','text-encoding','@babel/runtime']){
        const directory=path.join(modules,name);if(!fs.existsSync(directory))continue;const info=JSON.parse(fs.readFileSync(path.join(directory,'package.json')));
        const license=['LICENSE','LICENSE.txt','LICENSE.md'].find(n=>fs.existsSync(path.join(directory,n)));if(license)fs.copyFileSync(path.join(directory,license),path.join(root,'licenses',name.replace(/[@/]/g,'-')+'-'+info.version+'.txt'));
    }
    console.log('Built ES5 PDF viewer and worker with pinned compatibility dependencies');
})().catch(error=>{console.error(error);process.exitCode=1;});
