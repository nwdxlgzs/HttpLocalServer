// SPDX-License-Identifier: Apache-2.0
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const M=require('../.deps/preview-tooling/node_modules/@thednp/dommatrix/dist/dommatrix.cjs');
function Canvas(){}for(const name of ['save','restore','setTransform','transform','translate','scale','rotate'])Canvas.prototype[name]=function(){};
const c=vm.createContext({DOMMatrix:M,CanvasRenderingContext2D:Canvas,Promise,console,setTimeout,Uint8Array,Float64Array,ArrayBuffer});c.window=c;c.globalThis=c;
vm.runInContext(fs.readFileSync('web/preview/legacy-canvas.js','utf8'),c);
for(const matrix of [new M([2,1,.5,3,40,-12]),new M().translate(20,-4,8).rotate(14,32,81).scale(1.5,.7,2)]){
 const identity=matrix.multiply(matrix.inverse()).toFloat64Array();for(let i=0;i<16;i++)assert(Math.abs(identity[i]-(i%5===0?1:0))<1e-10,'General matrix inverse');
 const copy=new M(matrix.toFloat64Array());copy.invertSelf();assert(Math.abs(copy.a-matrix.inverse().a)<1e-12);
}
assert(Number.isNaN(new M([0,0,0,0,0,0]).inverse().a),'Singular matrix inverse is NaN');
const ctx=new Canvas();ctx.translate(10,20);ctx.save();ctx.scale(2,3);assert.equal(ctx.getTransform().a,2);ctx.restore();assert.equal(ctx.getTransform().a,1);assert.equal(ctx.getTransform().e,10);
console.log('Legacy PDF canvas passed: 2D/4D inverse, singular matrices, mutable operations and tracked transforms');
