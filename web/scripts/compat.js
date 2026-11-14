// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// Only the ES2015 methods used by the pinned Markdown renderer need shims in IE11.
(function(){
    'use strict';
    if(!String.prototype.repeat)Object.defineProperty(String.prototype,'repeat',{value:function(count){count=Number(count);if(!isFinite(count)||count<0||count>4194304)throw new RangeError('repeat count');count=Math.floor(count);var value=String(this),result='';while(count){if(count&1)result+=value;count=Math.floor(count/2);if(count)value+=value;}return result;},configurable:true,writable:true});
    if(!Array.prototype.includes)Object.defineProperty(Array.prototype,'includes',{value:function(value,from){var length=this.length>>>0,index=Number(from)||0;if(index<0)index=Math.max(0,length+index);for(;index<length;index++)if(this[index]===value||this[index]!==this[index]&&value!==value)return true;return false;},configurable:true,writable:true});
}());
