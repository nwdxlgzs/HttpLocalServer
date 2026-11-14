// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
import {DocxScrollViewer} from './vendor/office/docx.mjs';
import {XlsxViewer} from './vendor/office/xlsx.mjs';
import {math} from './vendor/office/math.mjs';
import {tiff} from './vendor/office/tiff.mjs';
import {openPresentation} from './presentation.mjs';
import {attachSheetSelection} from './sheet-selection.mjs';
const assets='/_hls/preview-assets/';
window.HLSOfficeModern={async open(name,bytes,V){
 const format=name.split('.').pop().toLowerCase(),generation=V.generation();
 if(!(bytes instanceof ArrayBuffer)||bytes.byteLength>32*1048576)throw new Error('Office 文件预览上限为 32 MiB');
 const guard=new Worker(assets+'zip-worker.js');
 await new Promise((resolve,reject)=>{const timer=setTimeout(()=>{guard.terminate();reject(new Error('Office 容器检查超时'));},5000);guard.onmessage=event=>{clearTimeout(timer);guard.terminate();event.data.error?reject(new Error(event.data.error)):resolve();};guard.onerror=()=>{clearTimeout(timer);guard.terminate();reject(new Error('Office 容器检查失败'));};guard.postMessage({id:1,op:'open',bytes});});
 if(generation!==V.generation()||!V.alive())return;
 const host=document.createElement('div');host.className='office-host';V.stage.appendChild(host);document.body.classList.add('office-document');
 let viewer=null,dead=false,selection=null,resizeTimer=0,fitMode=format==='xlsx'?'actual':'width',observer=null;
 function counts(){if(dead||!viewer)return;V.resetPages(format==='xlsx'?viewer.sheetCount:viewer.pageCount,format==='xlsx'?viewer.sheetIndex:viewer.topVisiblePage);V.element('zoom').textContent=Math.round(viewer.getScale()*100)+'%';V.element('fit').value=fitMode;if(selection)selection.changed().catch(V.error);}
 function selectedText(){if(selection)return selection.selectedText();const native=window.getSelection();if(native&&host.contains(native.anchorNode)&&String(native))return String(native);const context=viewer?.getSelectionContext({maxTextCharacters:1048576});return context?.text||'';}
 function context({originalEvent,getContext}){originalEvent.preventDefault();const point={clientX:originalEvent.clientX,clientY:originalEvent.clientY,preventDefault(){}};getContext().then(c=>{if(dead)return;const actions=[],value=selectedText();if(value)actions.push({label:format==='xlsx'?'复制所选单元格':'复制选中文字',run:()=>V.copy(value)});let target=originalEvent.target,canvas=target.tagName==='CANVAS'?target:null;while(!canvas&&target&&target!==host){canvas=target.querySelector?.('canvas');target=target.parentElement;}if(canvas){let rect=null;if(c?.bounds&&format==='docx'){const b=c.bounds,f=viewer.getScale()*96/72;rect={x:b.xPt*f,y:b.yPt*f,width:b.widthPt*f,height:b.heightPt*f};}actions.push({label:rect?'复制所选内容图片':'复制当前页图片',run:()=>V.copyCanvas(canvas,rect)});}V.contextMenu(point,actions);}).catch(V.error);}
 const opts={useGoogleFonts:false,workerTimeoutMs:10000,wasmUrl:assets+'vendor/office/'+format+'_parser_bg.wasm',enableTextSelection:true,enableElementSelection:true,enableHyperlinks:false,enableMediaPlayback:false,math,tiff,
 resourceLimits:{maxArchiveEntryBytes:16*1048576,maxTotalInflatedBytes:128*1048576,maxArchiveEntries:4096},xlsxWorksheetLimits:{maxRows:100000,maxCells:200000,maxOwnedUtf8Bytes:16*1048576,maxJsonBytes:32*1048576},imageResources:{decodedByteBudget:32*1048576,strategy:'strict',resolution:'display'},
 onPageChange:counts,onSheetChange:counts,onVisiblePageChange:counts,onScaleChange:counts,onSelectionContextChange:c=>selection?.selection(c),onContextMenu:format==='pptx'?undefined:context,onError:V.error,container:host,overscan:1,gap:18,paddingTop:18,paddingBottom:18,paddingLeft:innerWidth<=600?10:24,paddingRight:innerWidth<=600?10:24,enableZoom:true,showZoomSlider:false,refitOnResize:false,zoomMin:.05,zoomMax:4};
 if(format==='pptx'){const timer=setTimeout(()=>{V.engine()?.destroy();V.error(new Error('Office 预览加载超时，已停止解析'));},20000);try{await openPresentation(bytes,host,V,opts);}finally{clearTimeout(timer);}return;}
 function safe(fn){try{return Promise.resolve(fn()).catch(V.error);}catch(error){V.error(error);return Promise.resolve();}}
 function fit(mode=fitMode){if(dead||!viewer)return;fitMode=mode;V.element('fit').value=mode;if(mode==='actual')viewer.setScale(1);else if(mode==='page')viewer.fitPage();else if(mode==='width')viewer.fitWidth();counts();}
 function resize(){clearTimeout(resizeTimer);resizeTimer=setTimeout(()=>{if(dead||!viewer)return;safe(async()=>{await viewer.relayout();if(fitMode!=='custom')fit();});},120);}
 V.engine({destroy(){dead=true;clearTimeout(resizeTimer);observer?.disconnect();window.removeEventListener('resize',resize);selection?.destroy();viewer?.destroy();guard.terminate();},prev(){if(!viewer)return;go((format==='xlsx'?viewer.sheetIndex:viewer.topVisiblePage)-1);},next(){if(!viewer)return;go((format==='xlsx'?viewer.sheetIndex:viewer.topVisiblePage)+1);},go,zoom(factor){if(!viewer||dead)return;fitMode='custom';V.element('fit').value=fitMode;safe(()=>viewer.setScale(Math.max(.05,Math.min(4,viewer.getScale()*factor)))).then(counts);},fit,find(value){safe(async()=>{const matches=await viewer.findText(value);await viewer.findNext();counts();V.status(matches.length?'找到 '+matches.length+' 处':'没有找到匹配文字');});},selectedText});
 function go(index){if(!viewer||dead)return;if(format==='docx')viewer.scrollToPage(Math.max(0,Math.min(viewer.pageCount-1,index)),{behavior:'auto'});else safe(()=>viewer.goToSheet(index)).then(()=>selection.changed()).then(counts);}
 const timer=setTimeout(()=>{if(!dead){V.engine().destroy();V.error(new Error('Office 预览加载超时，已停止解析'));}},20000);
 try{
  if(format==='docx'){viewer=new DocxScrollViewer(host,opts);await viewer.load(bytes);}
  else{document.body.classList.add('sheet-document');const bar=document.createElement('div');bar.className='cell-bar';const address=document.createElement('input');address.className='cell-address';address.setAttribute('aria-label','单元格或区域地址');address.title='输入单元格或区域地址，按 Enter 跳转';address.addEventListener('change',()=>safe(async()=>{const reference=address.value.trim().toUpperCase();viewer.setSelection(reference);await viewer.scrollToCell(reference.split(':')[0]);}));const field=document.createElement('textarea');field.className='cell-text';field.readOnly=true;field.setAttribute('aria-label','所选单元格文字');bar.append(address,field);host.appendChild(bar);const grid=document.createElement('div');grid.className='sheet-grid';host.appendChild(grid);viewer=new XlsxViewer(grid,{...opts,container:grid,enableSelection:true});selection=attachSheetSelection(viewer,field,address,V);await viewer.load(bytes);await selection.changed();}
  if(dead||generation!==V.generation())return;counts();if(format==='docx')fit('width');else await viewer.relayout();V.ready();window.addEventListener('resize',resize);if(window.ResizeObserver){observer=new ResizeObserver(resize);observer.observe(host);}
 }catch(error){selection?.destroy();viewer?.destroy();throw error;}finally{clearTimeout(timer);}
}};
