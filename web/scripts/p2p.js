// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// Sender preference belongs to this document only: never persist it to storage.
var p2pSendActive=0,p2pRateTokens=0,p2pRateTime=0;
var p2pJob=null,p2pEnabled=false,p2pSendEnabled=true,p2pSendEpoch=0,p2pRelayPage=null,p2pRelayJob=null,p2pRelayJobs=[],p2pRelayTimer=0,p2pRelayBusy=false,p2pRelayEpoch=0,p2pPageClosed=false;
function canP2P(){return typeof window.Promise==='function'&&typeof window.RTCPeerConnection==='function'&&window.FileReader&&window.ArrayBuffer&&window.DataView;}
function p2pSending(){return p2pEnabled&&p2pSendEnabled&&!p2pPageClosed&&p2pCacheMiB>0;}
function p2pSenderUI(){var button=$('p2p-sender');if(!button)return;show(button,p2pEnabled&&canP2P());text(button,p2pSendEnabled?'PCDN · 开启':'PCDN · 已关闭');button.setAttribute('aria-pressed',p2pSendEnabled?'true':'false');button.title='仅此页面生效，刷新或重新打开后恢复开启';}
function p2pPolicy(enabled,mib,seconds){p2pEnabled=!!enabled;p2pCacheConfigure(mib,seconds);if(!p2pEnabled){p2pCacheClear();if(p2pJob)p2pJob.stop('主机已关闭 PCDN');}p2pSenderUI();if(typeof p2pRelayUpdate==='function')p2pRelayUpdate();}
function p2pSetSending(enabled){p2pSendEnabled=!!enabled;++p2pSendEpoch;p2pSenderUI();if(!p2pSendEnabled){p2pRelayStop();p2pCacheClear();}if(p2pJob&&p2pJob.network)p2pJob.network.announce();p2pRelayUpdate();toast(p2pSendEnabled?'本页面 PCDN 发送已开启':'本页面 PCDN 发送已关闭，后续任务均不发送');}
function p2pError(message,status){var error=new Error(message);error.status=status||0;return error;}
function p2pAPI(meta,route,values){
    return new Promise(function(resolve,reject){
        var body={},key;for(key in values)if(Object.prototype.hasOwnProperty.call(values,key))body[key]=values[key];
        if(meta){body.id=meta.id;body.key=meta.key;}
        var xhr=new XMLHttpRequest();xhr.open('POST','/api/p2p/'+route,true);xhr.timeout=15000;xhr.setRequestHeader('Content-Type','application/json');xhr.setRequestHeader('X-HLS-Token',session.token);
        xhr.onprogress=function(event){if(event.loaded>1048576){xhr.abort();reject(p2pError('协作控制响应过大'));}};
        xhr.onload=function(){var data;try{data=JSON.parse(xhr.responseText);}catch(ignore){reject(p2pError('无法识别协作响应',xhr.status));return;}if(xhr.status<200||xhr.status>=300){reject(p2pError(data.error||'协作请求失败',xhr.status));return;}resolve(data);};
        xhr.onerror=xhr.ontimeout=xhr.onabort=function(){reject(p2pError('协作连接中断'));};xhr.send(JSON.stringify(body));
    });
}
function p2pReadBlob(blob){return new Promise(function(resolve,reject){var reader=new FileReader();reader.onload=function(){resolve(new Uint8Array(reader.result));};reader.onerror=reader.onabort=function(){reject(p2pError('无法读取本地文件块'));};reader.readAsArrayBuffer(blob);});}
function p2pDigest(bytes){if(window.crypto&&window.crypto.subtle&&typeof window.crypto.subtle.digest==='function')return window.crypto.subtle.digest('SHA-256',bytes).then(function(buffer){var values=new Uint8Array(buffer),result='',i;for(i=0;i<values.length;i++)result+=('0'+values[i].toString(16)).slice(-2);return result;});return p2pPortableDigest(bytes);}
function p2pHas(ranges,index){for(var i=0;i<ranges.length;i++)if(index>=ranges[i][0]&&index<ranges[i][1])return true;return false;}
function p2pRanges(value,pieces){
    if(!value||!Array.isArray(value)||value.length>256)return [];
    var result=[],end=0,i,a,b;for(i=0;i<value.length;i++){if(!Array.isArray(value[i])||value[i].length!==2)return [];a=value[i][0];b=value[i][1];if(typeof a!=='number'||typeof b!=='number'||a%1||b%1||a<end||a<0||a>=b||b>pieces)return [];result.push([a,b]);end=b;}return result;
}
function PeerNetwork(job){
    this.job=job;this.receivedBase=job.peerBytes;this.sentBase=job.sentBytes;this.peers=Object.create(null);this.known=[];this.ack=0;this.timer=0;this.closed=false;this.polling=false;this.lastPoll=0;this.requestID=0;this.sendActive=0;this.tokens=job.meta.uploadKiB?job.meta.uploadKiB*1024:0;this.tokenTime=new Date().getTime();this.poll();
}
PeerNetwork.prototype.authorized=function(){return !this.closed&&new Date().getTime()-this.lastPoll<8000;};
PeerNetwork.prototype.count=function(){var n=0,id;for(id in this.peers)if(this.peers[id].channel&&this.peers[id].channel.readyState==='open')n++;return n;};
PeerNetwork.prototype.drop=function(id){var peer=this.peers[id];if(!peer)return;delete this.peers[id];peer.closed=true;clearTimeout(peer.timer);if(peer.pending){clearTimeout(peer.pending.timer);peer.pending.resolve(null);peer.pending=null;}try{peer.pc.close();}catch(ignore){}};
PeerNetwork.prototype.close=function(){if(this.closed)return;this.closed=true;clearTimeout(this.timer);var id;for(id in this.peers)this.drop(id);this.known=[];};
PeerNetwork.prototype.poll=function(){
    var self=this;if(self.closed||self.polling)return;self.polling=true;
    p2pAPI(self.job.meta,'poll',{ack:self.ack,have:self.job.availability(),wanted:self.job.paused||self.job.complete?null:self.job.wanted,received:self.job.peerBytes-self.receivedBase,sent:self.job.sentBytes-self.sentBase}).then(function(data){
        self.polling=false;if(self.closed)return;self.lastPoll=new Date().getTime();var i,message,alive=Object.create(null),list=data.peers||[];
        p2pCacheConfigure(data.cacheMiB,data.cacheSeconds);if(typeof data.activityAgeMs==='number')p2pCacheTouch(self.job.meta,data.activityAgeMs);
        self.known=[];for(i=0;i<list.length&&i<16;i++)if(typeof list[i].id==='string'&&/^[a-f0-9]{48}$/.test(list[i].id)){alive[list[i].id]=true;list[i].have=p2pRanges(list[i].have,self.job.meta.pieces);list[i].relay=list[i].relay===true;self.known.push(list[i]);if(self.peers[list[i].id]){self.peers[list[i].id].have=list[i].have;self.peers[list[i].id].relay=list[i].relay;}}
        // Discovery is deliberately a subset; do not close healthy channels
        // merely because another subset was returned this time.
        list=data.messages||[];for(i=0;i<list.length&&i<16;i++){message=list[i];if(message.sequence<=self.ack)continue;self.ack=message.sequence;self.signal(message);}
        self.connectWanted();self.job.render();self.timer=setTimeout(function(){self.poll();},1800+Math.floor(Math.random()*700));
    },function(error){self.polling=false;if(self.closed)return;if(error.status===401||error.status===403||error.status===404||error.status===409){self.job.fail(error);return;}if(!self.authorized()){var id;for(id in self.peers)self.drop(id);}self.timer=setTimeout(function(){self.poll();},2500+Math.floor(Math.random()*1000));});
};
PeerNetwork.prototype.gather=function(peer){
    return new Promise(function(resolve,reject){var finished=false;
        function end(error){if(finished)return;finished=true;clearTimeout(timer);peer.pc.removeEventListener('icegatheringstatechange',changed);if(error)reject(error);else resolve(peer.pc.localDescription.sdp);}
        function changed(){if(peer.closed)end(p2pError('节点已离线'));else if(peer.pc.iceGatheringState==='complete')end();}
        var timer=setTimeout(function(){if(peer.pc.localDescription)end();else end(p2pError('节点连接超时'));},4000);peer.pc.addEventListener('icegatheringstatechange',changed);changed();
    });
};
PeerNetwork.prototype.create=function(id,initiator){
    if(this.closed||!this.authorized()||Object.keys(this.peers).length>=this.job.meta.maxPeers)return null;
    var self=this,peer={id:id,pc:new RTCPeerConnection({iceServers:[],iceCandidatePoolSize:0}),channel:null,have:[],pending:null,closed:false,initiator:initiator,serving:false,badUntil:0};self.peers[id]=peer;
    peer.timer=setTimeout(function(){if(!peer.channel||peer.channel.readyState!=='open')self.drop(id);},12000);
    peer.pc.onconnectionstatechange=function(){if(peer.pc.connectionState==='failed'||peer.pc.connectionState==='closed')self.drop(id);};
    if(initiator)self.channel(peer,peer.pc.createDataChannel('hls-p2p-v1',{ordered:true}));else peer.pc.ondatachannel=function(event){if(event.channel.label!=='hls-p2p-v1'||peer.channel){event.channel.close();return;}self.channel(peer,event.channel);};return peer;
};
PeerNetwork.prototype.connectWanted=function(){
    if(this.closed||this.job.paused||this.job.complete||this.job.wanted===null)return;
    var i,other,peer,self=this;for(i=0;i<self.known.length;i++){
        other=self.known[i];if(!p2pHas(other.have,self.job.wanted)&&!other.relay||self.peers[other.id])continue;
        peer=self.create(other.id,true);if(!peer)break;peer.have=other.have;peer.relay=other.relay;
        (function(item){item.pc.createOffer().then(function(description){return item.pc.setLocalDescription(description);}).then(function(){return self.gather(item);}).then(function(sdp){if(!item.closed&&!self.closed)return p2pAPI(self.job.meta,'signal',{to:item.id,type:'offer',sdp:sdp});})['catch'](function(){self.drop(item.id);});})(peer);
    }
};
PeerNetwork.prototype.signal=function(message){
    var self=this,id=message.from;if(typeof id!=='string'||!/^[a-f0-9]{48}$/.test(id)||typeof message.sdp!=='string'||message.sdp.length>32768)return;
    var peer=self.peers[id];if(message.type==='offer'){
        if(peer&&peer.initiator){if(self.job.meta.id<id)return;self.drop(id);peer=null;}
        if(peer)return;peer=self.create(id,false);if(!peer)return;
        peer.pc.setRemoteDescription({type:'offer',sdp:message.sdp}).then(function(){return peer.pc.createAnswer();}).then(function(answer){return peer.pc.setLocalDescription(answer);}).then(function(){return self.gather(peer);}).then(function(sdp){if(!peer.closed&&!self.closed)return p2pAPI(self.job.meta,'signal',{to:id,type:'answer',sdp:sdp});})['catch'](function(){self.drop(id);});
    }else if(message.type==='answer'&&peer&&peer.initiator&&peer.pc.signalingState==='have-local-offer')peer.pc.setRemoteDescription({type:'answer',sdp:message.sdp})['catch'](function(){self.drop(id);});
};
PeerNetwork.prototype.channel=function(peer,channel){
    var self=this;peer.channel=channel;channel.binaryType='arraybuffer';channel.bufferedAmountLowThreshold=65536;
    channel.onopen=function(){if(!self.authorized()){self.drop(peer.id);return;}clearTimeout(peer.timer);channel.send(JSON.stringify({t:'have',ranges:self.job.availability()}));self.job.render();};
    channel.onclose=channel.onerror=function(){self.drop(peer.id);};
    channel.onmessage=function(event){
        if(!self.authorized()){self.drop(peer.id);return;}
        if(typeof event.data==='string'){
            if(event.data.length>8192){self.drop(peer.id);return;}var value;try{value=JSON.parse(event.data);}catch(ignore){self.drop(peer.id);return;}if(!value||typeof value!=='object'){self.drop(peer.id);return;}
            if(value.t==='have'){peer.have=p2pRanges(value.ranges,self.job.meta.pieces);return;}
            if(value.t==='get'){
                if(typeof value.index!=='number'||value.index%1||value.index<0||value.index>=self.job.meta.pieces||typeof value.req!=='number'||value.req%1||value.req<1||value.req>2147483647){self.drop(peer.id);return;}
                self.serve(peer,value);return;
            }
            var pending=peer.pending;if(!pending||value.req!==pending.req)return;
            if(value.t==='miss'){clearTimeout(pending.timer);peer.pending=null;pending.resolve(null);return;}
            if(value.t==='data'){
                var length=Math.min(self.job.meta.pieceBytes,self.job.meta.size-pending.index*self.job.meta.pieceBytes);
                if(pending.bytes||value.index!==pending.index||value.size!==length){self.drop(peer.id);return;}
                pending.bytes=new Uint8Array(length);pending.at=0;
            }
        }else{
            var waiting=peer.pending;if(!waiting||!waiting.bytes)return;
            if(!(event.data instanceof ArrayBuffer)||event.data.byteLength<9||event.data.byteLength>16384){self.drop(peer.id);return;}
            var view=new DataView(event.data),req=view.getUint32(0),offset=view.getUint32(4),bytes=new Uint8Array(event.data,8);
            if(req!==waiting.req||offset!==waiting.at||waiting.at+bytes.length>waiting.bytes.length){self.drop(peer.id);return;}
            waiting.bytes.set(bytes,waiting.at);waiting.at+=bytes.length;if(waiting.at===waiting.bytes.length){clearTimeout(waiting.timer);peer.pending=null;waiting.resolve({bytes:waiting.bytes,peer:peer});}
        }
    };
};
PeerNetwork.prototype.serve=function(peer,message){
    var self=this,channel=peer.channel,epoch=p2pSendEpoch;if(!p2pSending()||peer.serving||p2pSendActive>=2||!self.job.relay&&!p2pHas(self.job.availability(),message.index)){channel.send(JSON.stringify({t:'miss',req:message.req}));return;}
    peer.serving=true;self.sendActive++;p2pSendActive++;var ended=false;
    function finish(){if(ended)return;ended=true;peer.serving=false;self.sendActive--;p2pSendActive--;}
    self.job.readPiece(message.index).then(function(bytes){
        if(self.closed||peer.closed||!self.authorized()||!p2pSending()||epoch!==p2pSendEpoch){finish();return;}
        if(!bytes){channel.send(JSON.stringify({t:'miss',req:message.req}));finish();return;}
        channel.send(JSON.stringify({t:'data',req:message.req,index:message.index,size:bytes.length}));var offset=0;
        function send(){
            if(self.closed||peer.closed||!self.authorized()||!p2pSending()||epoch!==p2pSendEpoch||channel.readyState!=='open'){finish();return;}
            var burst=0;
            while(burst++<16){
            var now=new Date().getTime(),rate=self.job.meta.uploadKiB*1024,limit=rate?Math.max(16384,rate):0;
            if(rate){if(!p2pRateTime)p2pRateTokens=limit;p2pRateTokens=Math.min(limit,p2pRateTokens+(now-p2pRateTime)*rate/1000);p2pRateTime=now;}
            var n=Math.min(16376,bytes.length-offset),maximum=peer.pc.sctp&&peer.pc.sctp.maxMessageSize;if(maximum&&maximum<16384)n=Math.min(n,maximum-8);
            if(n<=0){self.drop(peer.id);finish();return;}
            if(channel.bufferedAmount>262144||rate&&p2pRateTokens<n){setTimeout(send,20);return;}
            var packet=new Uint8Array(n+8),view=new DataView(packet.buffer);view.setUint32(0,message.req);view.setUint32(4,offset);packet.set(bytes.subarray(offset,offset+n),8);
            try{channel.send(packet.buffer);}catch(ignore){self.drop(peer.id);finish();return;}
            if(rate)p2pRateTokens-=n;offset+=n;self.job.sentBytes+=n;
            if(offset===bytes.length){p2pCacheTouch(self.job.meta,0);finish();self.job.render();return;}
            }
            setTimeout(send,0);
        }send();
    },function(){if(!peer.closed&&channel.readyState==='open')channel.send(JSON.stringify({t:'miss',req:message.req}));finish();});
};
PeerNetwork.prototype.fetch=function(index){
    var self=this;if(!self.authorized())return Promise.resolve(null);self.job.wanted=index;self.connectWanted();
    var started=new Date().getTime(),wait=self.known.some(function(p){return p.relay;})?7000:self.known.some(function(p){return p2pHas(p.have,index);})?4000:0;
    return new Promise(function(resolve){function choose(){
        if(self.closed||self.job.paused){resolve(null);return;}
        var id,peer,pass;for(pass=0;pass<2;pass++)for(id in self.peers){peer=self.peers[id];if(peer.closed||peer.pending||!peer.channel||peer.channel.readyState!=='open'||peer.badUntil>new Date().getTime()||!(pass===0?p2pHas(peer.have,index):peer.relay))continue;
            var req=++self.requestID;if(req>2147483647){self.requestID=req=1;}
            var pending={req:req,index:index,resolve:resolve,bytes:null,timer:0};peer.pending=pending;
            pending.timer=setTimeout(function(){if(peer.pending===pending){peer.pending=null;peer.badUntil=new Date().getTime()+10000;resolve(null);}},peer.relay?15000:5000);
            try{peer.channel.send(JSON.stringify({t:'get',req:req,index:index}));}catch(ignore){self.drop(peer.id);}return;
        }
        if(new Date().getTime()-started<wait){setTimeout(choose,60);return;}resolve(null);
    }choose();});
};
PeerNetwork.prototype.announce=function(){var id,peer,wire=JSON.stringify({t:'have',ranges:this.job.availability()});for(id in this.peers){peer=this.peers[id];if(peer.channel&&peer.channel.readyState==='open'&&peer.channel.bufferedAmount<262144)try{peer.channel.send(wire);}catch(ignore){this.drop(id);}}};
