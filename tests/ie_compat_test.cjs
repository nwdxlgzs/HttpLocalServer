// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// ES5 parsing + IE11-like missing APIs. This is not an IE rendering engine.
const fs=require('fs'),vm=require('vm'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..');
const acorn=require('internal/deps/acorn/acorn/dist/acorn');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'resources/web-manifest.json'),'utf8'));
let parsed=0;
for(const name of Object.keys(manifest.sources).filter(p=>p.endsWith('.js'))){
    acorn.parse(fs.readFileSync(path.join(root,name),'utf8'),{ecmaVersion:5});parsed++;
}
const accessSource=fs.readFileSync(path.join(root,'src/access.cpp'),'utf8');
const loginScript=/<script>([\s\S]*?)<\/script>/.exec(accessSource);
assert(loginScript,'Inline login script is included in the compatibility review');
acorn.parse(loginScript[1],{ecmaVersion:5});
const elements={},events=[],timeouts=[],sent=[];
function node(id){return elements[id]||(elements[id]={id,style:{},className:'',value:'',textContent:'',innerHTML:'',children:[],attrs:{},clientWidth:1280,clientHeight:720,
    addEventListener(name,run){events.push({id,name,run});},removeEventListener(){},setAttribute(k,v){this.attrs[k]=String(v);},getAttribute(k){return this.attrs[k]===undefined?null:this.attrs[k];},
    appendChild(child){this.children.push(child);child.parentNode=this;return child;},insertBefore(child){return this.appendChild(child);},removeChild(child){this.children.splice(this.children.indexOf(child),1);},
    querySelector(selector){return node(id+selector);},querySelectorAll(){return [];},contains(other){return other===this;},focus(){},select(){},click(){},getBoundingClientRect(){return {left:0,top:0,right:1280,bottom:720,width:1280,height:720};}
});}
node('sidebar').appendChild(node('side-records'));node('commands').appendChild(node('records-button'));
const doc=node('document');doc.documentMode=11;doc.body=node('body');doc.documentElement=node('html');doc.getElementById=node;doc.querySelector=node;doc.querySelectorAll=()=>[];doc.createElement=tag=>node(tag+'-'+Math.random());
function XHR(){this.open=(method,url)=>{this.method=method;this.url=url;};this.setRequestHeader=()=>{};this.send=()=>sent.push({method:this.method,url:this.url});this.abort=()=>{};}
const ctx=vm.createContext({console,document:doc,XMLHttpRequest:XHR,location:{protocol:'http:',host:'127.0.0.1:8000',pathname:'/',search:'',hash:'',reload(){}},navigator:{},innerWidth:1280,innerHeight:720,
    localStorage:{getItem(){return null;},setItem(){}},addEventListener(name,run){events.push({id:'window',name,run});},removeEventListener(){},
    setTimeout(fn){timeouts.push(fn);return timeouts.length;},clearTimeout(){},setInterval(){return 1;},clearInterval(){},
    Promise:undefined,Symbol:undefined,Map:undefined,Set:undefined,WeakMap:undefined,TextEncoder:undefined,TextDecoder:undefined,fetch:undefined,ResizeObserver:undefined,IntersectionObserver:undefined,File:undefined,
});
vm.runInContext('window=this;Array.from=undefined;Object.assign=undefined;Array.prototype.includes=undefined;String.prototype.repeat=undefined;',ctx);
function load(name){vm.runInContext(fs.readFileSync(path.join(root,name),'utf8'),ctx,{filename:name});}
load('web/scripts/compat.js');load('web/vendor/marked.js');load('web/vendor/fflate.js');
vm.runInContext("if(![1,NaN].includes(NaN)||'xy'.repeat(3)!=='xyxyxy')throw Error('Compatibility shim failure');",ctx);
const output=vm.runInContext("marked.parse('# 中文标题\\n\\n- one\\n- two\\n\\n| A | B |\\n| - | - |\\n| 1 | 2 |\\n\\n`code` and **bold**',{async:false,mangle:false,headerIds:false})",ctx);
assert(output.includes('<h1>中文标题</h1>')&&output.includes('<table>')&&output.includes('<strong>bold</strong>'),'Markdown works without Set, Symbol, Promise, Object.assign or Array.from');
vm.runInContext("marked.use({hooks:{preprocess:function(s){return '# hook\\n\\n'+s;}}});if(marked.parse('ok',{async:false}).indexOf('hook')<0)throw Error('IE hook membership failed');",ctx);
vm.runInContext("var sample='中文 UTF-8 and gzip';var encoded=fflate.strToU8(sample);if(fflate.strFromU8(fflate.gunzipSync(fflate.gzipSync(encoded)))!==sample)throw Error('Codec fallback failed');",ctx);
const packer=fs.readFileSync(path.join(root,'tools/assets/pack_web.py'),'utf8');
const match=/SCRIPTS=\[([^\]]+)\]/.exec(packer);assert(match,'Find application source order');
const scripts=Array.from(match[1].matchAll(/'([^']+)'/g),m=>m[1]);for(const script of scripts)load('web/scripts/'+script);
for(const ordinaryPath of ['/config.json','/.hls-config-lock-config.json','/nested/.HLS-CONFIG-LOCK-SETTINGS.JSON']){
    assert(!ctx.isFrozenPath(ordinaryPath),'Frozen status comes from server dependency metadata, not a filename rule');
    assert(!ctx.frozenEntry({path:ordinaryPath}),'Client does not invent frozen entries for ordinary names');
}
assert(ctx.frozenEntry({path:'/actual-dependency',frozen:true}),'Client preserves server dependency metadata');
assert(sent.some(r=>r.url==='/api/session'),'Full application reaches its session request with IE11-like APIs');
assert.equal(node('mount-button').className.includes('hidden'),true,'Unsupported directory APIs stay hidden');
while(timeouts.length&&node('theme-switcher').className.indexOf('theme-ready')<0)timeouts.shift()();
ctx.applyTheme('dark');assert.equal(node('theme-switcher').attrs['data-mode'],'dark');assert(node('body').className.includes('theme-dark'));assert(node('theme-switcher').className.includes('theme-animating'),'Theme transition gets an explicit animation state');
ctx.applyTheme('auto');assert(!node('body').className.includes('theme-dark'),'System mode falls back to light when matchMedia is absent');
for(const name of Object.keys(manifest.sources).filter(p=>p.endsWith('.css'))){
    const css=fs.readFileSync(path.join(root,name),'utf8');assert(!/(?:^|[;{])\s*(?:gap|row-gap|column-gap)\s*:/.test(css),name+' must not rely on flex gap');
    for(const block of css.matchAll(/[^{}]+\{([^{}]*)\}/g))if(/position\s*:\s*sticky/.test(block[1]))assert(/position\s*:\s*relative/.test(block[1]),name+' has a non-sticky fallback');
}
console.log('IE static checks passed: '+parsed+' ES5 sources, full startup without modern globals, Markdown/hooks, UTF-8/gzip fallbacks, capability gating, themes and CSS spacing/sticky fallbacks');
