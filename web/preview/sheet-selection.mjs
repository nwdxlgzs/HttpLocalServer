// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
export function columnName(col){let result='';for(;col>0;col=Math.floor((col-1)/26))result=String.fromCharCode(65+(col-1)%26)+result;return result;}
export function cellName(cell){return columnName(cell.col)+cell.row;}
function overlaps(a,b){return a.top<=b.bottom&&a.bottom>=b.top&&a.left<=b.right&&a.right>=b.left;}
export function mergedSelection(state,merges){
 if(!state||!merges.length)return state;
 const next=structuredClone(state);let changed=false;
 for(const area of next.areas){if(area.kind!=='cells')continue;let again=true,work=0;while(again){again=false;for(const m of merges){if(++work>100000)throw new Error('合并单元格选择超出处理预算');if(!overlaps(area,m))continue;const expanded={top:Math.min(area.top,m.top),bottom:Math.max(area.bottom,m.bottom),left:Math.min(area.left,m.left),right:Math.max(area.right,m.right)};if(Object.keys(expanded).some(k=>area[k]!==expanded[k])){Object.assign(area,expanded);again=changed=true;}}}}
 for(const m of merges){const a=next.activeCell;if(a.row>=m.top&&a.row<=m.bottom&&a.col>=m.left&&a.col<=m.right&&(a.row!==m.top||a.col!==m.left)){next.activeCell={row:m.top,col:m.left};changed=true;break;}}
 return changed?next:state;
}
export function contextText(context){
 if(!context)return '';if(typeof context.text==='string')return context.text;
 const cells=context.cells||[];if(!cells.length)return '';
 // A single merged cell copies its value once, without empty covered cells.
 if(cells.length===1)return cells[0].displayText;
 const bounds=context.selection?.areas?.find(a=>a.kind==='cells');
 const top=bounds?.top||Math.min(...cells.map(c=>c.address.row)),bottom=bounds?.bottom||Math.max(...cells.map(c=>c.address.row)),left=bounds?.left||Math.min(...cells.map(c=>c.address.col)),right=bounds?.right||Math.max(...cells.map(c=>c.address.col));
 if((bottom-top+1)*(right-left+1)>10000)throw new Error('一次最多复制 10000 个单元格');
 const map=new Map(cells.map(c=>[c.address.row+':'+c.address.col,c.displayText]));const rows=[];
 for(let row=top;row<=bottom;row++){const values=[];for(let col=left;col<=right;col++)values.push(map.get(row+':'+col)||'');rows.push(values.join('\t'));}return rows.join('\n');
}

export function attachSheetSelection(viewer,field,address,V){
 let dead=false,merges=[],sheet=-1,loading=null,normalizing=false,last=null;
 function update(context){if(dead||!context?.selection)return;last=context;if(!normalizing){const expanded=mergedSelection(context.selection,merges);if(expanded!==context.selection){normalizing=true;try{viewer.setSelection(expanded);}finally{normalizing=false;}context=viewer.getSelectionContext({maxCells:10000,maxTextCharacters:1048576})||context;last=context;}}
  const active=context.selection.activeCell,range=context.selection.areas[context.selection.activeAreaIndex],merge=merges.find(m=>active.row>=m.top&&active.row<=m.bottom&&active.col>=m.left&&active.col<=m.right);
  address.value=merge?cellName({row:merge.top,col:merge.left})+':'+cellName({row:merge.bottom,col:merge.right}):range?.kind==='cells'&&(range.top!==range.bottom||range.left!==range.right)?cellName({row:range.top,col:range.left})+':'+cellName({row:range.bottom,col:range.right}):cellName(active);
  const cell=context.cells?.find(c=>c.address.row===(merge?.top||active.row)&&c.address.col===(merge?.left||active.col));if(document.activeElement!==field)field.value=cell?.displayText||'';
 }
 async function changed(){const target=viewer.sheetIndex;if(target===sheet)return loading;sheet=target;merges=[];field.value='';address.value='';loading=viewer.workbook.getWorksheet(target).then(model=>{if(dead||sheet!==target)return;const ranges=model.mergeCells||[];if(ranges.length>4096)throw new Error('合并区域数量超出选择处理预算');merges=ranges;const context=viewer.getSelectionContext({maxCells:10000,maxTextCharacters:1048576});if(context)update(context);});return loading;}
 function selection(context){if(!context?.selection)return;Promise.resolve(changed()).then(()=>{if(context.sheetIndex===viewer.sheetIndex)update(context);}).catch(error=>{if(!dead)V.error(error);});}
 function selectedText(){const native=window.getSelection();if(native&&V.stage.contains(native.anchorNode)&&String(native))return String(native);if(document.activeElement===field&&field.selectionEnd>field.selectionStart)return field.value.slice(field.selectionStart,field.selectionEnd);const context=viewer.getSelectionContext({maxCells:10000,maxTextCharacters:1048576})||last;return contextText(context);}
 return {changed,selection,selectedText,destroy(){dead=true;merges=[];last=null;}};
}
