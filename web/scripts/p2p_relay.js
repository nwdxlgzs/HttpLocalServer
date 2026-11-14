// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
function p2pRelayStop(){
    ++p2pRelayEpoch;clearTimeout(p2pRelayTimer);p2pRelayTimer=0;p2pRelayBusy=false;
    for(var i=0;i<p2pRelayJobs.length;i++)p2pRelayJobs[i].stop();p2pRelayJobs=[];p2pRelayJob=null;
    if(p2pRelayPage){p2pAPI(p2pRelayPage,'relay-leave',{})['catch'](function(){});p2pRelayPage=null;}
}
function p2pRelayUpdate(){
    if(!session||!canP2P()||!p2pSending()||p2pJob&&!p2pJob.stopped){if(p2pRelayPage||p2pRelayJob||p2pRelayTimer||p2pRelayBusy)p2pRelayStop();return;}
    if(p2pRelayBusy||p2pRelayTimer)return;
    var epoch=p2pRelayEpoch;p2pRelayBusy=true;
    function later(delay){if(epoch!==p2pRelayEpoch)return;p2pRelayBusy=false;p2pRelayTimer=setTimeout(function(){p2pRelayTimer=0;p2pRelayUpdate();},delay);}
    var join=p2pRelayPage?Promise.resolve(p2pRelayPage):p2pAPI(null,'relay-join',{});
    join.then(function(page){
        if(epoch!==p2pRelayEpoch){p2pAPI(page,'relay-leave',{})['catch'](function(){});return null;}p2pRelayPage=page;
        var sources=[];if(typeof providerMounts!=='undefined')for(var i=0;i<providerMounts.length;i++)if(!providerMounts[i].stopped)sources.push({id:providerMounts[i].id,key:providerMounts[i].key});return p2pAPI(page,'relay-poll',{sources:sources});
    }).then(function(data){
        if(epoch!==p2pRelayEpoch||!data)return;
        var assignments=data.assignments||(data.assignment?[data.assignment]:[]),next=[],i,j,job;
        if(assignments.length>4)throw p2pError('PCDN 分配超过上限');
        for(i=0;i<assignments.length;i++){
            var meta=assignments[i];if(!p2pValidMeta(meta))throw p2pError('PCDN 元数据无效');job=null;
            for(j=0;j<p2pRelayJobs.length;j++)if(!p2pRelayJobs[j].stopped&&p2pRelayJobs[j].meta.id===meta.id)job=p2pRelayJobs[j];
            next.push(job||new PeerRelay(meta));
        }
        for(i=0;i<p2pRelayJobs.length;i++)if(next.indexOf(p2pRelayJobs[i])<0)p2pRelayJobs[i].stop();
        p2pRelayJobs=next;p2pRelayJob=next[0]||null;
        later(2200+Math.floor(Math.random()*900));
    })['catch'](function(error){if(epoch!==p2pRelayEpoch)return;p2pRelayStop();if(error.status===401||error.status===403)return;p2pRelayTimer=setTimeout(function(){p2pRelayTimer=0;p2pRelayUpdate();},6000+Math.floor(Math.random()*3000));});
}
function p2pValidMeta(meta){return meta&&typeof meta.path==='string'&&typeof meta.version==='string'&&typeof meta.id==='string'&&/^[a-f0-9]{48}$/.test(meta.id)&&typeof meta.key==='string'&&meta.size>0&&meta.size<=1099511627776&&meta.pieceBytes===1048576&&meta.pieces===Math.ceil(meta.size/1048576)&&meta.maxPeers>=1&&meta.maxPeers<=8&&meta.cacheMiB>=0&&meta.cacheMiB<=64;}
function PeerRelay(meta){
    PeerDownload.call(this,{path:meta.path,name:meta.path},{size:meta.size,version:meta.version},{disk:false});this.meta=meta;p2pCacheConfigure(meta.cacheMiB,meta.cacheSeconds);this.relay=true;this.complete=true;this.wanted=null;this.pending=null;this.lastUse=new Date().getTime();this.network=new PeerNetwork(this);
}
PeerRelay.prototype=Object.create(PeerDownload.prototype);PeerRelay.prototype.constructor=PeerRelay;
PeerRelay.prototype.render=function(){p2pCacheTrim();var touched=p2pCachedFiles[p2pCacheName(this.meta)]||this.lastUse;if(!this.stopped&&new Date().getTime()-touched>=p2pCacheSeconds*1000)this.stop();p2pSenderUI();};
PeerRelay.prototype.availability=function(){return p2pSending()&&typeof p2pMountedSource==='function'&&p2pMountedSource(this.meta)?[[0,this.meta.pieces]]:PeerDownload.prototype.availability.call(this);};
PeerRelay.prototype.readPiece=function(index){
    var self=this;if(self.stopped||!p2pSending())return Promise.resolve(null);self.lastUse=new Date().getTime();
    var cached=p2pCacheGet(self.meta,index);if(cached)return Promise.resolve(cached);
    if(self.pending)return self.pending.index===index?self.pending.promise:Promise.resolve(null);
    var work=self.hash(index).then(function(expected){if(self.stopped||!p2pSending())return null;return (typeof p2pSourceRead==='function'?p2pSourceRead(self.meta,index):Promise.resolve(null)).then(function(local){return local||self.origin(index);}).then(function(bytes){return p2pDigest(bytes).then(function(actual){if(actual!==expected)throw p2pError('PCDN 文件块校验失败');if(self.stopped||!p2pSending())return null;self.cachePiece(index,bytes);if(self.network)self.network.announce();return bytes;});});});
    self.pending={index:index,promise:work.then(function(bytes){self.pending=null;return bytes;},function(error){self.pending=null;if(error.status===401||error.status===403||error.status===409)self.stop();return null;})};return self.pending.promise;
};
PeerRelay.prototype.fail=function(){this.stop();};
PeerRelay.prototype.stop=function(){if(this.stopped)return;this.stopped=true;if(this.xhr)this.xhr.abort();if(this.network)this.network.close();p2pAPI(this.meta,'leave',{})['catch'](function(){});this.cache=Object.create(null);this.cacheOrder=[];this.cacheBytes=0;this.hashPages=Object.create(null);};
