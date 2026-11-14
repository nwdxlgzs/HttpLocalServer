// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// Track native canvas transforms on browsers predating getTransform().
(function(g){
    g.HLSLegacyImport=function(value){return new Promise(function(resolve,reject){
        var name=String(value).split('/').pop(),globalName=name==='openjpeg_nowasm_fallback.js'?'HLSJPEG2000':name==='jbig2_nowasm_fallback.js'?'HLSJBIG2':'';
        if(!globalName){reject(new Error('Unsupported legacy PDF module'));return;}
        if(g[globalName]){resolve(g[globalName]);return;}
        var url='/_hls/preview-assets/vendor/legacy/'+name;
        if(typeof g.importScripts==='function'){try{g.importScripts(url);resolve(g[globalName]);}catch(e){reject(e);}return;}
        var script=g.document.createElement('script');script.src=url;script.onload=function(){resolve(g[globalName]);};script.onerror=function(){reject(new Error('PDF image decoder unavailable'));};g.document.head.appendChild(script);
    });};
    if(!g.crypto&&g.msCrypto)g.crypto=g.msCrypto;
    if(!g.AbortController){
        function Signal(){this.aborted=false;this.reason=undefined;this.listeners=[];}
        Signal.prototype.addEventListener=function(name,run){if(name==='abort')this.listeners.push(run);};
        Signal.prototype.removeEventListener=function(name,run){var i=this.listeners.indexOf(run);if(i>=0)this.listeners.splice(i,1);};
        Signal.prototype.throwIfAborted=function(){if(this.aborted)throw this.reason;};
        g.AbortController=function(){this.signal=new Signal();};
        g.AbortController.prototype.abort=function(reason){var signal=this.signal;if(signal.aborted)return;signal.aborted=true;signal.reason=reason||new Error('Aborted');var list=signal.listeners.slice();signal.listeners=[];for(var i=0;i<list.length;i++)list[i]({type:'abort',target:signal});if(signal.onabort)signal.onabort({type:'abort',target:signal});};
        Signal.abort=function(reason){var controller=new g.AbortController();controller.abort(reason);return controller.signal;};
        Signal.timeout=function(ms){var controller=new g.AbortController();setTimeout(function(){controller.abort(new Error('Timeout'));},ms);return controller.signal;};
        Signal.any=function(signals){var controller=new g.AbortController();for(var i=0;i<signals.length;i++)(function(s){if(s.aborted)controller.abort(s.reason);else s.addEventListener('abort',function(){controller.abort(s.reason);});})(signals[i]);return controller.signal;};g.AbortSignal=Signal;
        // IE ignores event option dictionaries. Modern engines running the
        // fallback also need the synthetic signal removed before WebIDL checks.
        var eventTargets=[g.EventTarget&&g.EventTarget.prototype,g.Window&&g.Window.prototype,g.Document&&g.Document.prototype,g.Element&&g.Element.prototype];
        for(var et=0;et<eventTargets.length;et++)(function(proto){if(!proto||!Object.prototype.hasOwnProperty.call(proto,'addEventListener'))return;var native=proto.addEventListener;
            proto.addEventListener=function(name,listener,options){var target=this;if(options&&options.signal instanceof Signal){var signal=options.signal;if(signal.aborted)return;var cleaned={capture:!!options.capture,once:!!options.once,passive:!!options.passive};native.call(target,name,listener,cleaned);signal.addEventListener('abort',function(){target.removeEventListener(name,listener,cleaned.capture);});return;}return native.call(target,name,listener,options);};
        })(eventTargets[et]);
    }
    if(typeof g.window==='undefined'||!g.CanvasRenderingContext2D)return;
    var p=g.CanvasRenderingContext2D.prototype,M=g.DOMMatrix;
    if(M){
        function copyMatrix(target,source){for(var column=1;column<=4;column++)for(var row=1;row<=4;row++)target['m'+column+row]=source['m'+column+row];target.a=target.m11;target.b=target.m12;target.c=target.m21;target.d=target.m22;target.e=target.m41;target.f=target.m42;return target;}
        if(!M.prototype.inverse)M.prototype.inverse=function(){
            var rows=[],r,c,k,pivot,tmp,factor,result=new M();
            for(r=0;r<4;r++){rows[r]=[];for(c=0;c<8;c++)rows[r][c]=c<4?this['m'+(c+1)+(r+1)]:(c-4===r?1:0);}
            for(c=0;c<4;c++){pivot=c;for(r=c+1;r<4;r++)if(Math.abs(rows[r][c])>Math.abs(rows[pivot][c]))pivot=r;
                if(!isFinite(rows[pivot][c])||rows[pivot][c]===0){for(r=1;r<=4;r++)for(k=1;k<=4;k++)result['m'+r+k]=NaN;result.a=result.b=result.c=result.d=result.e=result.f=NaN;return result;}
                tmp=rows[c];rows[c]=rows[pivot];rows[pivot]=tmp;factor=rows[c][c];for(k=0;k<8;k++)rows[c][k]/=factor;
                for(r=0;r<4;r++)if(r!==c){factor=rows[r][c];for(k=0;k<8;k++)rows[r][k]-=factor*rows[c][k];}
            }
            for(r=0;r<4;r++)for(c=0;c<4;c++)result['m'+(c+1)+(r+1)]=rows[r][c+4];return copyMatrix(result,result);
        };
        if(!M.prototype.invertSelf)M.prototype.invertSelf=function(){return copyMatrix(this,this.inverse());};
        if(!M.prototype.preMultiplySelf)M.prototype.preMultiplySelf=function(other){return copyMatrix(this,new M(other.toFloat64Array?other.toFloat64Array():[other.a,other.b,other.c,other.d,other.e,other.f]).multiply(this));};
        if(!g.DOMMatrixReadOnly)g.DOMMatrixReadOnly=M;
    }
    if(!p.getTransform&&M){
        function matrix(c){return c._hlsMatrix||(c._hlsMatrix=new M());}
        function wrap(name,update){var native=p[name];p[name]=function(){update(this,arguments);return native.apply(this,arguments);};}
        wrap('save',function(c){(c._hlsMatrices||(c._hlsMatrices=[])).push(new M(matrix(c).toFloat64Array()));});
        wrap('restore',function(c){if(c._hlsMatrices&&c._hlsMatrices.length)c._hlsMatrix=c._hlsMatrices.pop();});
        wrap('setTransform',function(c,a){c._hlsMatrix=new M(a.length===1?[a[0].a,a[0].b,a[0].c,a[0].d,a[0].e,a[0].f]:Array.prototype.slice.call(a));});
        wrap('transform',function(c,a){c._hlsMatrix=matrix(c).multiply(new M(Array.prototype.slice.call(a)));});
        wrap('translate',function(c,a){c._hlsMatrix=matrix(c).translate(a[0],a[1]);});
        wrap('scale',function(c,a){c._hlsMatrix=matrix(c).scale(a[0],a[1]);});
        wrap('rotate',function(c,a){c._hlsMatrix=matrix(c).rotate(a[0]*180/Math.PI);});
        p.getTransform=function(){return new M(matrix(this).toFloat64Array());};
        if(!p.resetTransform)p.resetTransform=function(){this.setTransform(1,0,0,1,0,0);};
    }
    if(!g.DOMPoint){g.DOMPoint=function(x,y,z,w){this.x=x||0;this.y=y||0;this.z=z||0;this.w=w===undefined?1:w;};g.DOMPoint.prototype.matrixTransform=function(m){return m.transformPoint(this);};}
    if(!g.ImageData){g.ImageData=function(bytes,width,height){var canvas=document.createElement('canvas'),ctx=canvas.getContext('2d');var image=ctx.createImageData(width,height||bytes.length/4/width);image.data.set(bytes);return image;};}
}(typeof globalThis!=='undefined'?globalThis:this));
