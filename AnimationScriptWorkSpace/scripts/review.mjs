import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(import.meta.url);
let chromium;
try{({chromium}=require('playwright-core'));}catch{({chromium}=require(path.join(root,'../.deps/preview-tooling/node_modules/playwright-core')));}
const mime={'.html':'text/html; charset=utf-8','.css':'text/css','.js':'application/javascript','.svg':'image/svg+xml','.png':'image/png','.webm':'video/webm','.wav':'audio/wav','.woff2':'font/woff2','.json':'application/json'};
const server=http.createServer((req,res)=>{
 const pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
 const file=path.resolve(root,'.'+pathname);
 if(!file.startsWith(root+path.sep)||!fs.existsSync(file)||!fs.statSync(file).isFile()){res.writeHead(404);return res.end();}
 res.writeHead(200,{'Content-Type':mime[path.extname(file)]||'application/octet-stream','Content-Length':fs.statSync(file).size});fs.createReadStream(file).pipe(res);
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const url=`http://127.0.0.1:${server.address().port}`;
const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,args:['--hide-scrollbars']});
const report={passed:true,variants:[]};
try{
 for(const variant of (process.argv.slice(2).length?process.argv.slice(2):['main','overview','teaser','vertical'])){
  const cfg=JSON.parse(fs.readFileSync(path.join(root,'data/scenes.json'),'utf8').replace(/^\uFEFF/,'')).variants[variant];
  const page=await browser.newPage({viewport:{width:cfg.width,height:cfg.height},deviceScaleFactor:1});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));page.on('response',r=>{if(r.status()>=400)errors.push(`${r.status()} ${new URL(r.url()).pathname}`);});
  await page.goto(url+(variant==='main'?'/index.html':`/edits/${variant}.html`),{waitUntil:'domcontentloaded',timeout:45000});
  await page.evaluate(()=>document.fonts.ready);
  await page.evaluate(()=>Promise.all([...document.images].map(img=>img.decode().catch(()=>{}))));
  const out=path.join(root,'review',variant);fs.mkdirSync(out,{recursive:true});
  const rows=[];
  const seek=async t=>page.evaluate(t=>{
   window.__timelines[window.__film.variant].totalTime(t,false);
   document.querySelectorAll('.clip[data-start]').forEach(el=>{
    const start=Number(el.dataset.start),end=start+Number(el.dataset.duration);
    el.style.visibility=t>=start&&t<end?'visible':'hidden';
   });
  },t);
  for(const scene of cfg.scenes){
   const at=scene.start+Math.min(3,scene.duration*.6);await seek(at);
   await page.screenshot({path:path.join(out,scene.id+'.png')});
   const issues=await page.evaluate(id=>{
    const scene=document.getElementById('scene-'+id),v=window.__film;const issues=[];
    for(const el of scene.querySelectorAll('h1,h2,h3,h4,p,code,.tag,.fine,.tech-note')){
     if(!el.textContent.trim()||getComputedStyle(el).display==='none'||getComputedStyle(el).opacity==='0')continue;
     const range=document.createRange();range.selectNodeContents(el);
     const r=range.getBoundingClientRect();if(!r.width||!r.height)continue;
     if(r.left<30||r.right>v.width-30||r.top<30||r.bottom>v.height-90)issues.push({text:el.textContent.trim().slice(0,80),rect:{left:r.left,top:r.top,right:r.right,bottom:r.bottom}});
    }return issues;
   },scene.id);
   rows.push({id:scene.id,at,issues});
  }
  const target=cfg.scenes[Math.floor(cfg.scenes.length/2)].start+2;
  await seek(target);const before=await page.screenshot();
  await seek(cfg.duration-1);await seek(0);await seek(target);const after=await page.screenshot();
  const byteIdentical=before.equals(after);
  let seekDifference={maximumChannelDelta:0,changedPixels:0,changedPixelFraction:0};
  if(!byteIdentical){
   fs.writeFileSync(path.join(out,'seek-before.png'),before);fs.writeFileSync(path.join(out,'seek-after.png'),after);
   seekDifference=await page.evaluate(async ([a,b])=>{
    const decode=src=>new Promise((resolve,reject)=>{const image=new Image();image.onload=()=>resolve(image);image.onerror=reject;image.src='data:image/png;base64,'+src;});
    const images=await Promise.all([decode(a),decode(b)]),canvas=document.createElement('canvas');
    canvas.width=images[0].width;canvas.height=images[0].height;const c=canvas.getContext('2d');
    c.drawImage(images[0],0,0);const first=c.getImageData(0,0,canvas.width,canvas.height).data;
    c.clearRect(0,0,canvas.width,canvas.height);c.drawImage(images[1],0,0);const second=c.getImageData(0,0,canvas.width,canvas.height).data;
    let maximumChannelDelta=0,changedPixels=0;
    for(let i=0;i<first.length;i+=4){let difference=0;for(let channel=0;channel<3;channel++)difference=Math.max(difference,Math.abs(first[i+channel]-second[i+channel]));maximumChannelDelta=Math.max(maximumChannelDelta,difference);if(difference)changedPixels++;}
    return {maximumChannelDelta,changedPixels,changedPixelFraction:changedPixels/(canvas.width*canvas.height)};
   },[before.toString('base64'),after.toString('base64')]);
  }
  // Chromium may round a few anti-aliased edge samples by 1/255 on re-seek.
  // Larger changes, or drift across >=0.01% of pixels, still fail.
  const deterministic=byteIdentical||(seekDifference.maximumChannelDelta<=1&&seekDifference.changedPixelFraction<.0001);
  const passed=!errors.length&&rows.every(r=>!r.issues.length)&&deterministic;
  report.variants.push({variant,passed,errors,deterministic,byteIdentical,seekDifference,scenes:rows});report.passed&&=passed;
  console.log(`${variant}: ${rows.length} scenes; bounds=${rows.filter(r=>!r.issues.length).length}/${rows.length}; seek=${deterministic}; errors=${errors.length}`);
  await page.close();
 }
}finally{await browser.close();server.close();}
fs.writeFileSync(path.join(root,'review/visual-verification.json'),JSON.stringify(report,null,2));
if(!report.passed)process.exitCode=1;
