// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// SHA-256 fallback for ordinary HTTP, where WebCrypto may be unavailable.
function p2pHashEngine(input,done,yielding){
    var constants=[0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2],state=[0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19];
    var length=input.length,padded=Math.ceil((length+9)/64)*64,bytes=new Uint8Array(padded),words=new Uint32Array(64),at=0,view=new DataView(bytes.buffer);bytes.set(input);bytes[length]=128;view.setUint32(padded-8,Math.floor(length/536870912));view.setUint32(padded-4,(length*8)>>>0);
    function rotate(x,n){return (x>>>n)|(x<<(32-n));}
    function step(){
        var until=yielding?Math.min(padded,at+65536):padded,i,a,b,c,d,e,f,g,h,x,y,t1,t2;
        for(;at<until;at+=64){for(i=0;i<16;i++)words[i]=view.getUint32(at+i*4);for(i=16;i<64;i++){x=words[i-15];y=words[i-2];words[i]=(words[i-16]+(rotate(x,7)^rotate(x,18)^(x>>>3))+words[i-7]+(rotate(y,17)^rotate(y,19)^(y>>>10)))>>>0;}
            a=state[0];b=state[1];c=state[2];d=state[3];e=state[4];f=state[5];g=state[6];h=state[7];
            for(i=0;i<64;i++){t1=(h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+constants[i]+words[i])>>>0;t2=((rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c)))>>>0;h=g;g=f;f=e;e=(d+t1)>>>0;d=c;c=b;b=a;a=(t1+t2)>>>0;}
            state[0]=(state[0]+a)>>>0;state[1]=(state[1]+b)>>>0;state[2]=(state[2]+c)>>>0;state[3]=(state[3]+d)>>>0;state[4]=(state[4]+e)>>>0;state[5]=(state[5]+f)>>>0;state[6]=(state[6]+g)>>>0;state[7]=(state[7]+h)>>>0;
        }
        if(at<padded){setTimeout(step,0);return;}var result='';for(i=0;i<8;i++)result+=('00000000'+state[i].toString(16)).slice(-8);done(result);
    }step();
}
var p2pHashWorker=null,p2pHashWorkerFailed=false,p2pHashSequence=0,p2pHashPending=Object.create(null);
function p2pPortableDigest(bytes){
    return new Promise(function(resolve,reject){
        if(bytes.length>1048576){reject(p2pError('文件块超出校验上限'));return;}
        function fallback(){p2pHashEngine(bytes,resolve,true);}
        if(!p2pHashWorker&&!p2pHashWorkerFailed&&window.Worker&&window.Blob&&window.URL&&window.URL.createObjectURL){
            var href;try{
                href=window.URL.createObjectURL(new Blob(['var hash='+p2pHashEngine.toString()+';onmessage=function(e){hash(new Uint8Array(e.data.bytes),function(value){postMessage({id:e.data.id,hash:value});},false);};'],{type:'application/javascript'}));p2pHashWorker=new Worker(href);
                p2pHashWorker.onmessage=function(event){var pending=p2pHashPending[event.data.id];if(!pending)return;delete p2pHashPending[event.data.id];clearTimeout(pending.timer);pending.resolve(event.data.hash);};
                p2pHashWorker.onerror=function(){p2pHashWorkerFailed=true;if(p2pHashWorker)p2pHashWorker.terminate();p2pHashWorker=null;var key,pending;for(key in p2pHashPending){pending=p2pHashPending[key];clearTimeout(pending.timer);pending.fallback();}p2pHashPending=Object.create(null);};
            }catch(ignore){p2pHashWorkerFailed=true;}finally{if(href)window.URL.revokeObjectURL(href);}
        }
        if(!p2pHashWorker){fallback();return;}
        var id=++p2pHashSequence,copy=new Uint8Array(bytes);p2pHashPending[id]={resolve:resolve,fallback:fallback,timer:setTimeout(function(){if(!p2pHashPending[id])return;delete p2pHashPending[id];fallback();},15000)};
        try{p2pHashWorker.postMessage({id:id,bytes:copy.buffer},[copy.buffer]);}catch(ignore){clearTimeout(p2pHashPending[id].timer);delete p2pHashPending[id];fallback();}
    });
}
