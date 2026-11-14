import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const delivery=JSON.parse(fs.readFileSync(path.join(root,'data/delivery.json'),'utf8'));
const variants=process.argv.slice(2).length?process.argv.slice(2):['teaser','overview','vertical','main'];
const workers=process.env.HLS_RENDER_WORKERS||'4';
fs.mkdirSync(path.join(root,'review'),{recursive:true});fs.mkdirSync(path.join(root,'exports'),{recursive:true});
for(const variant of variants){
 const profile=delivery.variants[variant];
 if(!profile)throw new Error('Unknown variant: '+variant);
 const args=['scripts/hf.mjs','render',...(variant==='main'?[]:['--composition',`edits/${variant}.html`]),'--output','exports/'+profile.file,'--resolution',profile.resolution,'--fps',String(delivery.fps),'--quality','delivery','--workers',workers,'--video-frame-format','png','--gpu','--video-bitrate','35M'];
 const log=fs.createWriteStream(path.join(root,'review',`render-${variant}-4k.log`));
 const child=spawn(process.execPath,args,{cwd:root,windowsHide:true,env:process.env});
 console.log(`Rendering ${variant} → exports/${profile.file} (${profile.width}×${profile.height})`);
 let buffer='',lastCode='',lastBucket=-1;
 child.stdout.on('data',chunk=>{
  log.write(chunk);buffer+=chunk.toString();const lines=buffer.split(/\r?\n/);buffer=lines.pop();
  for(const line of lines){
   if(line.startsWith('@hf-progress ')){
    try{const p=JSON.parse(line.slice(13));const bucket=Math.floor(p.pct/10);if(p.code!==lastCode||bucket!==lastBucket){console.log(`${variant}: ${p.code} ${p.pct}%${p.done!=null?` (${p.done}/${p.total})`:''}`);lastCode=p.code;lastBucket=bucket;}}catch{}
   }else if(/Render complete|rendered in|render failed|Error:|✗/.test(line))console.log(line.trim());
  }
 });
 child.stderr.on('data',chunk=>log.write(chunk));
 const code=await new Promise((resolve,reject)=>{child.on('exit',resolve);child.on('error',reject);});
 log.end();if(code!==0)throw new Error(`${variant} render failed (${code}); see review/render-${variant}-4k.log`);
 console.log(`Completed ${variant}`);
}
