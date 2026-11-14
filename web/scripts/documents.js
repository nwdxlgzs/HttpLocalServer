// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
function safeDocumentURL(value,entry,image){
    value=String(value||'').replace(/^\s+|\s+$/g,'');
    var decoded=value;try{decoded=decodeURIComponent(value);}catch(ignore){}
    decoded=decoded.replace(/[\u0000-\u0020\u007f]/g,'');
    if(/^(javascript|vbscript|data|file|blob):/i.test(decoded)||decoded.indexOf('\\')>=0)return '';
    if(/^[a-z][a-z0-9+.-]*:/i.test(decoded)){if(image)return '';return /^(https?:|mailto:)/i.test(decoded)?value:'';}
    if(value.indexOf('//')===0)return image?'':'https:'+value;
    if(value.charAt(0)==='#')return value;
    var suffix='',at=value.search(/[?#]/);if(at>=0){suffix=value.substring(at);value=value.substring(0,at);}
    var parts=(value.charAt(0)==='/'?value:join(parent(entry.path),value)).split('/'),logical=[],i;
    for(i=0;i<parts.length;i++){if(!parts[i]||parts[i]==='.')continue;if(parts[i]==='..'){if(!logical.length)return '';logical.pop();}else{try{logical.push(decodeURIComponent(parts[i]));}catch(ignore){return '';}}}
    var encoded=[];for(i=0;i<logical.length;i++)encoded.push(enc(logical[i]));return '/view/'+encoded.join('/')+suffix;
}
function markdownDocument(source,entry){
    var renderer=new marked.Renderer();
    renderer.html=function(raw){return esc(raw);};
    renderer.link=function(href,title,label){var safe=safeDocumentURL(href,entry,false);return safe?'<a href="'+esc(safe)+'" target="_blank" rel="noopener noreferrer"'+(title?' title="'+esc(title)+'"':'')+'>'+label+'</a>':label;};
    renderer.image=function(href,title,label){var safe=safeDocumentURL(href,entry,true);return safe?'<img src="'+esc(safe)+'" alt="'+esc(label||'')+'"'+(title?' title="'+esc(title)+'"':'')+'>':esc(label||'');};
    var article=doc.createElement('article');article.className='markdown-document';
    article.innerHTML=marked.parse(source,{renderer:renderer,mangle:false,headerIds:false,gfm:true,breaks:false,async:false});$('preview').appendChild(article);
}
function jsonDocument(source){
    var data;try{data=JSON.parse(source);}catch(error){var invalid=doc.createElement('pre');invalid.className='text-document';text(invalid,'JSON 格式无效\n\n'+source);$('preview').appendChild(invalid);return;}
    var panel=doc.createElement('div');panel.className='json-document';var counter=0;
    function scalar(value){if(value===null)return 'null';if(typeof value==='string'){var shown=value.length>2048?value.substring(0,2048)+'…':value;return JSON.stringify(shown);}return String(value);}
    function row(key,value,depth,path){
        var holder=doc.createElement('div');holder.className='json-node';var compound=value!==null&&typeof value==='object',line=doc.createElement(compound?'button':'div');line.className='json-line';line.style.paddingLeft=(12+depth*15)+'px';
        var keys=compound?Object.keys(value):[],array=Object.prototype.toString.call(value)==='[object Array]';
        line.innerHTML=(compound?'<span class="json-toggle">›</span>':'<span class="json-toggle"></span>')+'<span class="json-key">'+esc(key)+'</span><span class="json-colon">: </span><span class="json-value '+(typeof value)+'">'+esc(compound?(array?'['+keys.length+' 项]':'{'+keys.length+' 项}'):scalar(value))+'</span>';line.title=path;
        holder.appendChild(line);
        if(compound){
            var children=doc.createElement('div');children.className='json-children hidden';holder.appendChild(children);var opened=false,loaded=0;
            function next(){var end=Math.min(keys.length,loaded+100);for(;loaded<end;loaded++){if(counter>=5000)break;counter++;children.appendChild(row(keys[loaded],value[keys[loaded]],depth+1,path+'.'+keys[loaded]));}
                if(loaded<keys.length&&counter<5000){var more=doc.createElement('button');more.className='json-more';text(more,'再显示 '+Math.min(100,keys.length-loaded)+' 项');on(more,'click',function(){children.removeChild(more);next();});children.appendChild(more);}else if(counter>=5000){var note=doc.createElement('div');note.className='preview-info';text(note,'已达 5000 项展开上限，请下载查看完整内容');children.appendChild(note);}
            }
            on(line,'click',function(){if(depth>=64){toast('JSON 嵌套层级超过预览上限');return;}opened=!opened;if(opened&&!children.firstChild)next();show(children,opened);line.setAttribute('aria-expanded',opened?'true':'false');line.querySelector('.json-toggle').className='json-toggle'+(opened?' open':'');});line.setAttribute('aria-expanded','false');
            if(depth===0){opened=true;next();show(children,true);line.setAttribute('aria-expanded','true');line.querySelector('.json-toggle').className='json-toggle open';}
        }
        return holder;
    }
    panel.appendChild(row('$',data,0,'$'));$('preview').appendChild(panel);
}
function documentPreview(entry,generation,kind){
    var xhr=new XMLHttpRequest();previewXHR=xhr;xhr.open('GET','/api/preview-text?path='+enc(entry.path),true);
    xhr.onreadystatechange=function(){if(xhr.readyState!==4||generation!==previewGeneration)return;previewXHR=null;if(xhr.status!==200){text($('preview'),'无法预览此文档');return;}
        var truncated=xhr.getResponseHeader('X-HLS-Preview-Truncated')==='1';
        if(kind==='markdown')markdownDocument(xhr.responseText,entry);
        else if(kind==='json'&&!truncated)jsonDocument(xhr.responseText);
        else{var pre=doc.createElement('pre');pre.className='text-document';text(pre,xhr.responseText);$('preview').appendChild(pre);}
        if(canManage(entry)&&!truncated){var edit=doc.createElement("button");edit.className="document-edit-button";text(edit,"编辑文本");on(edit,"click",function(){editText(entry);});$("preview").appendChild(edit);}
        captureDocument(entry,xhr.responseText,generation);if(truncated){var note=doc.createElement('div');note.className='preview-limit';text(note,kind==='json'?'JSON 超过 2 MiB，仅显示开头文本':'仅显示前 2 MiB');$('preview').appendChild(note);}
    };xhr.send(null);
}
