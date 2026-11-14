// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// Adapt the pinned Silurus text layer without changing its render or find indexes.
export function readingOrder(records){
 const sorted=records.slice().sort((a,b)=>a.top-b.top||a.left-b.left||a.index-b.index),rows=[];
 for(const record of sorted){let row=rows[rows.length-1];if(!row||Math.abs(record.top-row.top)>Math.max(2,Math.min(record.height,row.height)*.4)){row={top:record.top,height:record.height,items:[]};rows.push(row);}row.items.push(record);}
 const result=[];for(let i=0;i<rows.length;i++){rows[i].items.sort((a,b)=>a.left-b.left||a.index-b.index);for(const record of rows[i].items){record.row=i;result.push(record);}}return result;
}
export function attachPresentationSelection(viewer,presentation,canvas,V){
 const layer=viewer.textLayer,build=viewer._buildTextLayer;let records=[],generation=0,dead=false;
 if(!layer||typeof build!=='function')return {destroy(){},selectedText(){return '';},copy(){V.copy('');}};
 layer.classList.add('ppt-reading-surface');
 function activeRange(){const selection=window.getSelection();return selection&&!selection.isCollapsed&&selection.rangeCount&&layer.contains(selection.anchorNode)&&layer.contains(selection.focusNode)?selection.getRangeAt(0):null;}
 function contents(){const range=activeRange(),items=[];if(!range)return items;for(const record of records){if(!range.intersectsNode(record.node))continue;if(record.image){items.push({...record,value:''});continue;}const part=document.createRange();part.selectNodeContents(record.node);if(range.compareBoundaryPoints(Range.START_TO_START,part)>0)part.setStart(range.startContainer,range.startOffset);if(range.compareBoundaryPoints(Range.END_TO_END,part)<0)part.setEnd(range.endContainer,range.endOffset);const value=String(part);if(value)items.push({...record,value});}return items;}
 function selectedText(){const items=contents();let value='',last=-1;for(const item of items){if(item.image)continue;if(value)value+=item.row===last?' ':'\n';value+=item.value;last=item.row;}return value;}
 function payload(){const items=contents(),body=document.createElement('div');let line=null,last=-1,text='',textRow=-1;for(const item of items){if(item.row!==last){line=document.createElement('div');body.appendChild(line);last=item.row;}else line.appendChild(document.createTextNode(' '));if(item.image){const img=document.createElement('img');img.src=item.node.src;img.alt=item.node.alt;img.width=Math.round(Math.min(600,item.width));img.height=Math.round(img.width*item.height/item.width);line.appendChild(img);}else{line.appendChild(document.createTextNode(item.value));if(text)text+=textRow===item.row?' ':'\n';text+=item.value;textRow=item.row;}}return {text,html:body.innerHTML,images:items.some(item=>item.image)};}
 function highlight(){const range=activeRange();for(const item of records)if(item.image)item.wrap.classList.toggle('selected-resource',!!range&&range.intersectsNode(item.node));}
 function copy(){const data=payload();if(data.html)V.copyRich(data.html,data.text);else{const native=window.getSelection();if(native&&!native.isCollapsed&&String(native)){V.copy(String(native));return;}const context=viewer.getSelectionContext({maxTextCharacters:1048576});if(context?.text)V.copy(context.text);else if(context?.bounds){const b=context.bounds,f=canvas.clientWidth/presentation.slideWidth;V.copyCanvas(canvas,{x:b.x*f,y:b.y*f,width:b.width*f,height:b.height*f});}else V.copy('');}}
 function nativeCopy(event){const data=payload();if(!data.html||!event.clipboardData)return;if(data.text.length>1048576||data.html.length>6*1048576){event.preventDefault();V.status('所选内容过大，请分段复制',true);return;}event.clipboardData.setData('text/plain',data.text);event.clipboardData.setData('text/html',data.html);event.preventDefault();}
 function arrange(){
  const selection=window.getSelection(),anchor=selection?.anchorNode,focus=selection?.focusNode,a=selection?.anchorOffset,b=selection?.focusOffset,restore=activeRange();
  records=readingOrder(records);const fragment=document.createDocumentFragment();for(const item of records){item.wrap.dataset.readingRow=item.row;fragment.appendChild(item.wrap);}layer.replaceChildren(fragment);
  if(restore&&anchor?.isConnected&&focus?.isConnected){selection.setBaseAndExtent(anchor,a,focus,b);}highlight();
 }
 function imageRecords(elements,width,height){const result=[];let pixels=0,bytes=0;for(const e of elements){if(result.length>=32)break;if(e.type!=='picture'&&e.type!=='chart')continue;if(!(e.width>0&&e.height>0))continue;const f=width/presentation.slideWidth,x=Math.max(0,e.x*f),y=Math.max(0,e.y*f),w=Math.min(width-x,e.width*f),h=Math.min(height-y,e.height*f);if(w<=0||h<=0)continue;
   // Copy the already bounded, rendered surface: do not decode original images again.
   const sx=canvas.width/width,sy=canvas.height/height,rotation=e.rotation||0,angle=rotation*Math.PI/180,rw=Math.abs(w*Math.cos(angle))+Math.abs(h*Math.sin(angle)),rh=Math.abs(w*Math.sin(angle))+Math.abs(h*Math.cos(angle)),left=Math.max(0,x+(w-rw)/2),top=Math.max(0,y+(h-rh)/2),cw=Math.min(width-left,rw),ch=Math.min(height-top,rh),density=Math.min(sx,1,Math.sqrt(1048576/(cw*ch)));
   const out=document.createElement('canvas');out.width=Math.max(1,Math.ceil(cw*density));out.height=Math.max(1,Math.ceil(ch*density));if(pixels+out.width*out.height>2*1048576)continue;pixels+=out.width*out.height;out.getContext('2d').drawImage(canvas,left*sx,top*sy,cw*sx,ch*sy,0,0,out.width,out.height);const url=out.toDataURL('image/png');out.width=out.height=1;if(bytes+url.length>4*1048576)continue;bytes+=url.length;
   const wrap=document.createElement('div');wrap.className='ppt-selection-resource';wrap.style.cssText=`position:absolute;left:${left}px;top:${top}px;width:${cw}px;height:${ch}px;`;const img=document.createElement('img');img.src=url;img.alt=e.name||'图片';img.draggable=false;wrap.appendChild(img);result.push({node:img,wrap,image:true,left,top,width:cw,height:ch,index:records.length+result.length});
  }return result;}
 viewer._buildTextLayer=function(target,runs,width,height){
  build.call(this,target,runs,width,height);if(dead)return;const token=++generation,base=target.getBoundingClientRect();records=[];
  // Each run keeps the renderer's shape transform and original run index. Separate
  // wrappers let DOM Range follow visual rows instead of a shape's entire body.
  for(const node of Array.from(target.querySelectorAll('[data-ooxml-selection-run]'))){const box=node.getBoundingClientRect(),wrap=node.parentElement.cloneNode(false);wrap.style.pointerEvents='none';node.style.pointerEvents='auto';records.push({node,wrap,image:false,left:box.left-base.left,top:box.top-base.top,width:box.width,height:box.height,index:records.length});}
  for(const item of records)item.wrap.appendChild(item.node);arrange();
  // The parser stays in its bounded slide cache; only current-page image metadata escapes.
  const slide=this.slideIndex;
  if(presentation._slides?.withSlide)presentation._slides.withSlide(slide,model=>model.elements.filter(e=>e.type==='picture'||e.type==='chart').map(e=>({type:e.type,x:e.x,y:e.y,width:e.width,height:e.height,rotation:e.rotation,name:e.name}))).then(elements=>{if(dead||token!==generation||slide!==viewer.slideIndex)return;records.push(...imageRecords(elements,width,height));arrange();},V.error);
 };
 document.addEventListener('selectionchange',highlight);document.addEventListener('copy',nativeCopy);
 return {selectedText,copy,hasImages(){return contents().some(item=>item.image);},destroy(){dead=true;generation++;viewer._buildTextLayer=build;document.removeEventListener('selectionchange',highlight);document.removeEventListener('copy',nativeCopy);records=[];}};
}
