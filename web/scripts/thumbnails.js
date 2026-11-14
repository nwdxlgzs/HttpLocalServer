// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// Browser-memory only. No thumbnail bytes leave this page or enter localStorage.
var thumbnailCache=Object.create(null),thumbnailBytes=0,thumbnailClock=0,thumbnailRevision=0,thumbnailsEnabled=false;
function thumbnailKey(entry){return entry.path+'|'+entry.size+'|'+entry.modified;}
function thumbnailPolicy(enabled){
    enabled=!!enabled;
    if(thumbnailsEnabled===enabled)return;
    thumbnailsEnabled=enabled;
    if(!enabled){thumbnailCache=Object.create(null);thumbnailBytes=0;}
    thumbnailRevision++;if(current())schedule();
}
function thumbnailImage(entry){
    if(!thumbnailsEnabled)return '';
    var value=thumbnailCache[thumbnailKey(entry)];if(!value||entry.version&&value.version!==entry.version)return '';value.used=++thumbnailClock;
    return '<img class="file-thumbnail" src="'+value.data+'" alt="" draggable="false">';
}
function thumbnailStore(entry,canvas,generation){
    if(!thumbnailsEnabled||generation!==previewGeneration)return false;
    var data;try{data=canvas.toDataURL('image/jpeg',.8);}catch(ignore){return false;}
    if(data.length>65536||data.indexOf('data:image/jpeg;')!==0)return false;
    var key=thumbnailKey(entry),old=thumbnailCache[key],count=0,item,oldest,oldestKey;
    if(old)thumbnailBytes-=old.data.length;
    thumbnailCache[key]={data:data,version:entry.version||'',used:++thumbnailClock};thumbnailBytes+=data.length;
    for(item in thumbnailCache)if(Object.prototype.hasOwnProperty.call(thumbnailCache,item))count++;
    while(count>64||thumbnailBytes>2*1024*1024){
        oldest=Infinity;oldestKey='';for(item in thumbnailCache)if(Object.prototype.hasOwnProperty.call(thumbnailCache,item)&&thumbnailCache[item].used<oldest){oldest=thumbnailCache[item].used;oldestKey=item;}
        if(!oldestKey)break;thumbnailBytes-=thumbnailCache[oldestKey].data.length;delete thumbnailCache[oldestKey];count--;
    }
    thumbnailRevision++;if(current())schedule();if(typeof standalonePath!=='undefined'&&standalonePath&&window.parent!==window)window.parent.postMessage({hlsViewer:'thumbnail',path:entry.path,size:entry.size,modified:entry.modified,version:entry.version||'',data:data},window.location.protocol+'//'+window.location.host);return true;
}
function receiveThumbnail(value){if(!thumbnailsEnabled||!value||typeof value.data!=='string'||value.data.length>65536||!/^data:image\/jpeg;base64,[A-Za-z0-9+/=]+$/.test(value.data)||typeof value.path!=='string'||!isFinite(value.size)||!isFinite(value.modified))return;thumbnailStore({path:value.path,size:value.size,modified:value.modified,version:value.version||''},{toDataURL:function(){return value.data;}},previewGeneration);}
function thumbnailInvalidate(path){
    var key,changed=false;for(key in thumbnailCache)if(Object.prototype.hasOwnProperty.call(thumbnailCache,key)&&(key.indexOf(path+'|')===0||key.indexOf(path+'/')===0)){thumbnailBytes-=thumbnailCache[key].data.length;delete thumbnailCache[key];changed=true;}
    if(changed){thumbnailRevision++;if(current())schedule();}
}
function captureVisual(entry,element,generation){
    if(!thumbnailsEnabled||generation!==previewGeneration)return false;
    var width=element.videoWidth||element.naturalWidth,height=element.videoHeight||element.naturalHeight;
    if(!width||!height||element.videoWidth&&(element.readyState<2||element.seeking))return false;
    var canvas=doc.createElement('canvas'),scale=Math.min(192/width,128/height,1);canvas.width=Math.max(1,Math.round(width*scale));canvas.height=Math.max(1,Math.round(height*scale));var context=canvas.getContext('2d');if(!context)return false;
    context.fillStyle='#f7f9fc';context.fillRect(0,0,canvas.width,canvas.height);
    try{
        context.drawImage(element,0,0,canvas.width,canvas.height);
        if(element.videoWidth){
            var pixels=context.getImageData(0,0,canvas.width,canvas.height).data,sum=0,bright=0,count=0,i,luma;
            for(i=0;i<pixels.length;i+=16){luma=(pixels[i]*3+pixels[i+1]*6+pixels[i+2])/10;sum+=luma;if(luma>35)bright++;count++;}
            if(count&&sum/count<12&&bright/count<.015)return false;
        }
    }catch(ignore){return false;}
    return thumbnailStore(entry,canvas,generation);
}
function captureDocument(entry,source,generation){
    if(!thumbnailsEnabled||generation!==previewGeneration)return;
    var canvas=doc.createElement('canvas');canvas.width=160;canvas.height=112;var context=canvas.getContext('2d');if(!context)return;
    context.fillStyle='#fbfcfe';context.fillRect(0,0,160,112);context.fillStyle='#506681';context.font='9px Consolas,monospace';
    var lines=source.substring(0,1600).split(/\r?\n/),i;for(i=0;i<Math.min(lines.length,10);i++)context.fillText(lines[i].substring(0,32),8,13+i*10,144);
    thumbnailStore(entry,canvas,generation);
}
