// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
function loadBlockedIPs(){
    if(!session||!session.localAdmin)return;
    request('GET','/api/admin/access',null,function(error,data){
        var box=$('blocked-ips');box.innerHTML='';if(error){text(box,error);return;}
        var items=data.blocked||[];if(!items.length){text(box,'没有被封禁的 IP');return;}
        for(var i=0;i<items.length;i++)(function(item){var row=doc.createElement('div'),label=doc.createElement('span'),button=doc.createElement('button');row.className='blocked-ip';text(label,item.ip+' · 剩余 '+item.retryAfter+' 秒');text(button,'解锁');button.type='button';on(button,'click',function(){request('POST','/api/admin/access',{ip:item.ip},function(problem){if(problem)toast(problem);else loadBlockedIPs();});});row.appendChild(label);row.appendChild(button);box.appendChild(row);}(items[i]));
    });
}
function showURLCodes(){
    request('GET','/api/urls',null,function(error,data){
        if(error){toast(error);return;}var urls=data.urls||[];
        modal('访问地址与二维码','',null,[{label:'关闭',run:function(){}}]);var box=$('modal-text');box.innerHTML='';
        if(!urls.length){text(box,'暂无可用地址');return;}
        var image=doc.createElement('img'),label=doc.createElement('a'),choices=doc.createElement('div');image.className='url-qr';image.alt='网站 URL 二维码';label.className='qr-address';choices.className='qr-choices';box.appendChild(image);box.appendChild(label);box.appendChild(choices);
        function pick(index){image.src=urls[index].qr;text(label,urls[index].url);label.href=urls[index].url;var buttons=choices.querySelectorAll('button');for(var j=0;j<buttons.length;j++)buttons[j].className=j===index?'active':'';}
        for(var i=0;i<urls.length;i++)(function(index){var button=doc.createElement('button');text(button,(index===0?'推荐 · ':'')+urls[index].name+' · '+urls[index].url);on(button,'click',function(){pick(index);});choices.appendChild(button);}(i));
        pick(0);
    });
}

var mediaRenewTime=0,mediaRenewQueue=[],mediaRenewBusy=false;
function renewMediaAccess(done){
    var now=new Date().getTime();if(!mediaRenewBusy&&now-mediaRenewTime<500){if(done)done();return;}
    if(done)mediaRenewQueue.push(done);if(mediaRenewBusy)return;mediaRenewBusy=true;
    request('POST','/api/access/activate',{},function(error){mediaRenewBusy=false;var queue=mediaRenewQueue;mediaRenewQueue=[];if(error){toast(error);return;}mediaRenewTime=new Date().getTime();for(var i=0;i<queue.length;i++)queue[i]();});
}
