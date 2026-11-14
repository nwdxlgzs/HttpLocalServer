// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
function p2pMemorySink(name,limit){
    var chunks=[],length=0;return {disk:false,existing:null,write:function(bytes,index){if(length+bytes.length>limit)return Promise.reject(p2pError('浏览器暂存已达上限，请选择保存到磁盘'));chunks[index]=bytes;length+=bytes.length;return Promise.resolve();},checkpoint:function(){return Promise.resolve();},read:function(index){return Promise.resolve(chunks[index]||null);},close:function(total){
        var blob=new Blob(chunks,{type:'application/octet-stream'}),href=(window.URL||window.webkitURL).createObjectURL(blob),link=doc.createElement('a');link.href=href;link.download=name;link.style.display='none';doc.body.appendChild(link);link.click();doc.body.removeChild(link);setTimeout(function(){(window.URL||window.webkitURL).revokeObjectURL(href);},60000);return Promise.resolve();
    },pause:function(){return Promise.resolve();},resume:function(){return Promise.resolve();},release:function(){chunks=[];}};
}
function p2pDiskSink(handle){
    var writer=null,snapshot=null,baseline=null;var sink={disk:true,handle:handle,existing:null,persisted:0};
    function verify(){return handle.getFile().then(function(file){if(baseline&&(baseline.size!==file.size||baseline.modified!==file.lastModified))throw p2pError('本地输出文件已被修改，拒绝覆盖');});}
    function open(){return verify().then(function(){return handle.createWritable({keepExistingData:true}).then(function(value){writer=value;});});}
    sink.write=function(bytes,index,pieceBytes){if(!writer)return Promise.reject(p2pError('本地写入未就绪'));return writer.write({type:'write',position:index*pieceBytes,data:bytes});};
    sink.checkpoint=function(completed,reopen){
        var saving=writer;writer=null;return verify().then(function(){return saving?saving.close():null;}).then(function(){return handle.getFile();}).then(function(file){snapshot=file;baseline={size:file.size,modified:file.lastModified};sink.persisted=completed;var saved=sink.onCheckpoint?sink.onCheckpoint(file):Promise.resolve();return saved.then(function(){return reopen?open():null;});})['catch'](function(error){if(saving)saving.abort()['catch'](function(){});throw error;});
    };
    sink.read=function(index,pieceBytes,total){if(!snapshot||index>=sink.persisted)return Promise.resolve(null);return p2pReadBlob(snapshot.slice(index*pieceBytes,Math.min(total,(index+1)*pieceBytes)));};
    sink.close=function(total){return writer.truncate(total).then(function(){return sink.checkpoint(Math.ceil(total/1048576),false);});};
    sink.pause=function(completed){return sink.checkpoint(completed,false);};sink.resume=open;sink.release=function(){snapshot=null;sink.existing=null;};
    return handle.getFile().then(function(file){sink.existing=file;baseline={size:file.size,modified:file.lastModified};return open();}).then(function(){return sink;});
}
function p2pMemoryLimit(){
    var limit=window.innerWidth<=720?16*1048576:32*1048576,memory=Number(navigator.deviceMemory||0);if(memory>0)limit=Math.min(limit,Math.max(4*1048576,memory*8*1048576));return limit;
}
function p2pCanWriteDisk(){return window.isSecureContext===true&&typeof window.showSaveFilePicker==='function';}
function canP2PDownload(entry){return !!(entry&&!frozenEntry(entry)&&!entry.directory&&p2pEnabled&&canP2P()&&entry.size>0&&entry.size<=1099511627776&&(p2pCanWriteDisk()||entry.size<=p2pMemoryLimit()));}
function p2pDownload(entry){
    entry=entry||selected;if(!canP2PDownload(entry)){toast('此文件请使用普通下载；大文件 P2P 需要支持本地文件写入的浏览器和安全上下文');return;}
    if(p2pJob&&!p2pJob.stopped){toast('已有 P2P 下载，请先结束当前任务');return;}
    var output;
    // Explicit P2P choice may open the browser's standard save dialog. Never
    // ask the recipient whether to share: page-wide PCDN policy owns that.
    if(p2pCanWriteDisk()){
        try{output=window.showSaveFilePicker({suggestedName:entry.name}).then(p2pDiskSink);}catch(error){toast(error.message||'无法选择本地文件');return;}
    }else output=Promise.resolve(p2pMemorySink(entry.name,p2pMemoryLimit()));
    output.then(function(sink){
        request('GET','/api/download-ticket?p2p=1&path='+enc(entry.path),null,function(error,ticket){
            if(error||!ticket.size||ticket.size>1099511627776||!sink.disk&&ticket.size>p2pMemoryLimit()){sink.pause(0).then(sink.release,sink.release);toast(error||'文件超出当前浏览器的 P2P 下载能力，请使用普通下载');return;}
            if(p2pJob&&!p2pJob.stopped){sink.pause(0).then(sink.release,sink.release);return;}
            p2pRelayStop();p2pJob=new PeerDownload(entry,ticket,sink);p2pJob.start();
        });
    },function(error){if(error.name!=='AbortError')toast(error.message||'无法准备下载文件');});
}
function PeerDownload(entry,ticket,sink,existingTask){
    var self=this;this.task=existingTask||(typeof taskCreate==='function'?taskCreate('P2P 下载',entry.name,entry.path,ticket.size):null);if(this.task){this.task.pause=function(){if(!self.paused)self.pause();};this.task.resumeRun=function(){if(self.paused)self.resume();};this.task.retry=function(){self.resume();};this.task.cancel=function(){self.stop();};}
    if(this.task&&sink.disk&&sink.handle&&typeof jobAttach==='function'){var state={type:'p2p',root:entry,handle:sink.handle,version:ticket.version,size:ticket.size,output:{size:sink.existing.size,modified:sink.existing.lastModified}};this.task.job={type:'p2p'};jobSave(this.task,state);sink.onCheckpoint=function(file){state.output={size:file.size,modified:file.lastModified};return new Promise(function(resolve){jobSave(self.task,state,resolve);});};}
    this.entry=entry;this.ticket=ticket;this.sink=sink;this.meta=null;this.network=null;this.next=0;this.wanted=0;this.peerBytes=0;this.originBytes=0;this.localBytes=0;this.sentBytes=0;this.cache=Object.create(null);this.cacheOrder=[];this.cacheBytes=0;this.hashPages=Object.create(null);this.hashOrder=[];this.stopped=false;this.paused=false;this.complete=false;this.running=false;this.xhr=null;this.seedTimer=0;this.error='';this.savedAt=0;this.checkpointAt=32;this.started=new Date().getTime();this.total=ticket.size;
}
PeerDownload.prototype.start=function(){
    var self=this;if(self.stopped)return;if(self.task&&!self.task.slot){jobStart(self.task,function(){self.start();});return;}
    self.running=true;self.error='';self.render();p2pAPI(null,'join',{path:self.entry.path,version:self.ticket.version}).then(function(meta){
        if(self.stopped){p2pAPI(meta,'leave',{})['catch'](function(){});return;}
        if(meta.version!==self.ticket.version||meta.size!==self.total||meta.pieceBytes!==1048576||meta.pieces!==Math.ceil(self.total/1048576)||meta.maxPeers<1||meta.maxPeers>8||meta.cacheMiB<0||meta.cacheMiB>64)throw p2pError('协作元数据不匹配');
        self.running=false;if(self.paused){self.settlePause();return;}self.meta=meta;p2pCacheConfigure(meta.cacheMiB,meta.cacheSeconds);p2pCacheTouch(meta,0);self.network=new PeerNetwork(self);self.step();
    })['catch'](function(error){self.running=false;self.fail(error);});
};
PeerDownload.prototype.render=function(){
    if(this.stopped)return;show($('p2p-status'),true);var done=Math.min(this.total,this.next*1048576),percent=this.total?done/this.total*100:0;
    if(this.task){taskProgress(this.task,done,this.total);taskState(this.task,this.complete?'done':this.error?'failed':this.paused?'paused':this.task.slot?'running':'queued',this.error);}
    text($('p2p-title'),(this.complete?'下载完成 · ':this.paused?'已暂停 · ':'正在下载 · ')+this.entry.name);
    text($('p2p-message'),this.error||size(done)+' / '+size(this.total)+' · 节点 '+(this.network?this.network.count():0)+' · 来自节点 '+size(this.peerBytes)+' · 主机 '+size(this.originBytes)+(this.localBytes?' · 本地续传 '+size(this.localBytes):'')+(this.sentBytes?' · 已分享 '+size(this.sentBytes):''));
    $('p2p-progress').style.width=percent+'%';$('p2p-progress').setAttribute('aria-valuenow',Math.round(percent));
    text($('p2p-pause'),this.paused?'继续':'暂停');show($('p2p-pause'),!this.complete);text($('p2p-cancel'),'取消');
};
PeerDownload.prototype.cachePiece=function(index,bytes){
    if(this.meta)p2pCachePut(this.meta,index,bytes);
};
PeerDownload.prototype.availability=function(){
    return this.meta?p2pCacheRanges(this.meta):[];
};
PeerDownload.prototype.readPiece=function(index){
    return Promise.resolve(!this.stopped&&this.meta?p2pCacheGet(this.meta,index):null);
};
PeerDownload.prototype.hash=function(index){
    var self=this,start=Math.floor(index/16)*16,page=self.hashPages[start];if(page)return Promise.resolve(page[index-start]);
    return p2pAPI(self.meta,'hashes',{start:start}).then(function(data){
        if(data.start!==start||!Array.isArray(data.hashes)||data.hashes.length!==Math.min(16,self.meta.pieces-start))throw p2pError('文件校验清单不匹配');
        for(var i=0;i<data.hashes.length;i++)if(typeof data.hashes[i]!=='string'||!/^[a-f0-9]{64}$/.test(data.hashes[i]))throw p2pError('文件校验值无效');
        self.hashPages[start]=data.hashes;self.hashOrder.push(start);if(self.hashOrder.length>16)delete self.hashPages[self.hashOrder.shift()];return data.hashes[index-start];
    });
};
PeerDownload.prototype.origin=function(index,attempt){
    var self=this;return new Promise(function(resolve,reject){
        if(self.stopped||self.paused){reject(p2pError('下载已暂停'));return;}
        if(self.task&&self.meta.sourceMount){taskRelease(self.task);taskState(self.task,'queued','等待挂载内容');}var start=index*self.meta.pieceBytes,end=Math.min(self.total,start+self.meta.pieceBytes)-1,xhr=new XMLHttpRequest();self.xhr=xhr;
        xhr.open('GET',self.meta.url,true);xhr.responseType='arraybuffer';xhr.timeout=30000;xhr.setRequestHeader('Range','bytes='+start+'-'+end);xhr.setRequestHeader('Cache-Control','no-cache');
        xhr.onprogress=function(event){if(event.loaded>self.meta.pieceBytes){xhr.abort();reject(p2pError('文件块超出上限'));}};
        xhr.onload=function(){self.xhr=null;if(xhr.status!==206){reject(p2pError(xhr.status===409?'文件已变化，请重新开始':'主机文件块请求失败',xhr.status));return;}if(xhr.getResponseHeader('Content-Range')!=='bytes '+start+'-'+end+'/'+self.total){reject(p2pError('主机文件范围不匹配'));return;}var bytes=new Uint8Array(xhr.response||new ArrayBuffer(0));if(bytes.length!==end-start+1){reject(p2pError('文件块长度不匹配'));return;}self.originBytes+=bytes.length;if(self.task&&!self.task.slot){if(self.stopped||self.paused){reject(p2pError('下载已暂停'));return;}jobStart(self.task,function(){resolve(bytes);});}else resolve(bytes);};
        xhr.onerror=xhr.ontimeout=xhr.onabort=function(){self.xhr=null;reject(p2pError('主机下载连接中断'));};xhr.send(null);
    })['catch'](function(error){if(!self.paused&&!self.stopped&&!error.status&&(attempt||0)<2)return new Promise(function(resolve){setTimeout(resolve,400);}).then(function(){return self.origin(index,(attempt||0)+1);});throw error;});
};
PeerDownload.prototype.acquire=function(index,expected){
    var self=this,existing=self.sink.existing,start=index*self.meta.pieceBytes,end=Math.min(self.total,start+self.meta.pieceBytes);
    function network(){return (self.network?self.network.fetch(index):Promise.resolve(null)).then(function(reply){
        if(!reply)return self.origin(index);
        return p2pDigest(reply.bytes).then(function(actual){if(actual===expected){self.peerBytes+=reply.bytes.length;return reply.bytes;}reply.peer.badUntil=new Date().getTime()+60000;self.network.drop(reply.peer.id);return self.origin(index);});
    });}
    if(existing&&existing.size>=end)return p2pReadBlob(existing.slice(start,end)).then(function(bytes){return p2pDigest(bytes).then(function(actual){if(actual===expected){self.localBytes+=bytes.length;return bytes;}return network();});},network);
    return network();
};
PeerDownload.prototype.step=function(){
    var self=this;if(self.stopped||self.paused||self.running||self.complete||!self.meta)return;self.running=true;var index=self.next,bytes,expected;
    if(index>=self.meta.pieces){self.running=false;self.finish();return;}
    self.wanted=index;
    self.hash(index).then(function(hash){expected=hash;if(self.stopped||self.paused)throw p2pError('下载已暂停');return self.acquire(index,hash);}).then(function(value){bytes=value;return p2pDigest(bytes);}).then(function(hash){
        if(self.stopped||self.paused)throw p2pError('下载已暂停');if(hash!==expected)throw p2pError('文件块校验失败，请重新下载');return self.sink.write(bytes,index,self.meta.pieceBytes);
    }).then(function(){
        self.next++;self.running=false;
        if(self.stopped){self.dispose();return;}
        self.cachePiece(index,bytes);if(self.network)self.network.announce();self.render();if(self.paused){self.settlePause();return;}
        // Exponentially spaced checkpoints avoid repeatedly copying a growing
        // browser COW file at fixed intervals (quadratic local disk traffic).
        if(self.sink.disk&&self.next>=self.checkpointAt&&self.next<self.meta.pieces&&(!self.sink.existing||self.sink.existing.size<32*1048576)){self.running=true;self.checkpointAt=self.next*4;self.sink.checkpoint(self.next,true).then(function(){self.savedAt=self.next;self.running=false;if(self.stopped)self.dispose();else if(self.paused)self.settlePause();else self.step();},function(error){self.running=false;if(self.stopped)self.dispose();else self.fail(error);});}else setTimeout(function(){self.step();},0);
    })['catch'](function(error){self.running=false;if(self.stopped){self.dispose();return;}if(self.paused){self.settlePause();return;}self.fail(error);});
};
PeerDownload.prototype.dispose=function(saved){var self=this;if(self.disposing)return;self.disposing=true;function release(){self.sink.release();}if(saved)release();else self.sink.pause(self.next).then(release,release);};
PeerDownload.prototype.settlePause=function(){var self=this;if(self.running)return;self.running=true;self.sink.pause(self.next).then(function(){self.savedAt=self.next;self.running=false;if(self.task)taskRelease(self.task);if(self.stopped)self.dispose(true);else self.render();},function(error){self.running=false;if(self.stopped)self.dispose(true);else{self.error=error.message||'无法保留下载进度';self.render();}});};
PeerDownload.prototype.pause=function(){if(this.complete||this.stopped)return;if(this.paused){this.resume();return;}this.paused=true;if(this.xhr)this.xhr.abort();if(!this.running)this.settlePause();this.render();};
PeerDownload.prototype.resume=function(){
    var self=this;if(!self.paused||self.running||self.stopped)return;self.error='';self.paused=false;
    function run(){self.running=true;if(self.network){self.network.close();self.network=null;}if(self.meta)p2pAPI(self.meta,'leave',{})['catch'](function(){});
        self.sink.resume().then(function(){if(self.stopped)return;return new Promise(function(resolve,reject){request('POST','/api/access/activate',{},function(error){if(error)reject(p2pError(error));else resolve();});});}).then(function(){self.running=false;if(self.stopped)self.dispose();else self.start();},function(error){self.running=false;if(self.stopped)self.dispose();else self.fail(error);});
    }if(self.task)jobStart(self.task,run);else run();
};
PeerDownload.prototype.fail=function(error){
    if(this.stopped)return;this.error=error.message||String(error);this.paused=true;if(this.network){this.network.close();this.network=null;}if(this.xhr)this.xhr.abort();if(!this.running)this.settlePause();this.render();
};
PeerDownload.prototype.finish=function(){
    var self=this;self.running=true;self.sink.close(self.total).then(function(){
        self.running=false;if(self.stopped){self.dispose(true);return;}self.complete=true;self.wanted=null;self.sink.existing=null;self.render();if(self.network)self.network.announce();
        records.unshift({name:self.entry.name,path:self.entry.path,time:new Date().getTime(),p2p:true});if(records.length>100)records.pop();saveRecords();if(drawerMode==='records')renderRecords();
        // Output lifetime and PCDN retention are independent. The page cache
        // remains available to future host assignments after this job closes.
        self.stop();
    },function(error){self.running=false;if(self.stopped)self.dispose();else self.fail(error);});
};
PeerDownload.prototype.stop=function(message){
    if(this.stopped)return;if(this.task)taskState(this.task,this.complete?'done':'cancelled');this.stopped=true;clearTimeout(this.seedTimer);if(this.xhr)this.xhr.abort();if(this.network)this.network.close();if(this.meta)p2pAPI(this.meta,'leave',{})['catch'](function(){});
    if(!this.running)this.dispose(this.complete);this.cache=Object.create(null);this.cacheOrder=[];this.cacheBytes=0;this.hashPages=Object.create(null);show($('p2p-status'),false);if(message)toast(message);if(typeof p2pRelayUpdate==='function')setTimeout(p2pRelayUpdate,0);
};

function resumePeerJob(t,state){
    if(p2pJob&&!p2pJob.stopped){jobFail(t,'请先结束当前 P2P 下载');return;}
    state.handle.getFile().then(function(file){if(state.output&&(file.size!==state.output.size||file.lastModified!==state.output.modified))throw new Error('本地输出文件已被修改，拒绝覆盖');return p2pDiskSink(state.handle);}).then(function(sink){request('GET','/api/download-ticket?p2p=1&path='+enc(state.root.path),null,function(error,ticket){if(error||ticket.version!==state.version||ticket.size!==state.size){sink.pause(0).then(sink.release,sink.release);jobFail(t,error||'源文件已改变，拒绝继续旧任务');return;}p2pRelayStop();p2pJob=new PeerDownload(state.root,ticket,sink,t);p2pJob.start();});},function(error){jobFail(t,error);});
}
