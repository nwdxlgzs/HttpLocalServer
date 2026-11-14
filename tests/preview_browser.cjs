// SPDX-License-Identifier: Apache-2.0
// Run with a real installed Chromium and the exact distributed native program.
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const {chromium}=require('../.deps/preview-tooling/node_modules/playwright-core');
const localSources=process.env.HLS_PREVIEW_SOURCES==='1';
const base=process.argv[2],out=process.argv[3]||'.deps/preview-browser';fs.mkdirSync(out,{recursive:true});
(async()=>{
 const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,args:['--no-proxy-server']});
 try{
  async function sources(context){if(!localSources)return;await context.route('**/_hls/preview-assets/**',async route=>{const url=new URL(route.request().url());let relative=url.pathname.split('/_hls/preview-assets/')[1];let file=path.resolve('web/preview',relative);if(relative==='fflate.js')file=path.resolve('web/vendor/fflate.js');const mime=relative.endsWith('.html')?'text/html':relative.endsWith('.wasm')?'application/wasm':relative.endsWith('.css')?'text/css':/\.(m?js)$/.test(relative)?'text/javascript':'application/octet-stream';if(fs.existsSync(file))await route.fulfill({body:fs.readFileSync(file),contentType:mime});else await route.continue();});}
  for(const legacy of (process.env.HLS_PREVIEW_ZIP_ONLY?[]:[false,true])){
   const context=await browser.newContext({viewport:{width:1200,height:800},permissions:['clipboard-read','clipboard-write'],ignoreHTTPSErrors:true});
   await sources(context);if(legacy)await context.addInitScript(()=>{for(const key of ['WebAssembly','DOMMatrix','DOMPoint','Path2D','ReadableStream','fetch','TextDecoder','TextEncoder','AbortController','AbortSignal'])Object.defineProperty(window,key,{value:undefined,writable:true,configurable:true});});
   const page=await context.newPage(),errors=[];page.on('pageerror',e=>{errors.push(String(e));console.log('BROWSER ERROR',String(e));});page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
   for(const ext of ['docx','pptx','xlsx','pdf']){
    const response=await page.goto(base+'/_hls/viewer?path=/example.'+ext+'&kind='+(ext==='pdf'?'pdf':'office'));assert.equal(response.status(),200,await response.text());
    await page.waitForFunction(()=>document.querySelector('#stage').dataset.ready==='true',null,{timeout:65000});
    const state=await page.evaluate(()=>({status:document.querySelector('#status').textContent,canvases:document.querySelectorAll('#stage canvas').length,text:document.querySelector('#stage').textContent,errors:document.querySelector('#status').className}));
    assert(!state.errors.includes('error'),JSON.stringify(state));
    if(!legacy||ext==='pdf')assert(state.canvases>0,'Rendered canvas: '+ext+' '+JSON.stringify(state));
    else assert(state.text.includes(ext==='xlsx'?'example':ext==='docx'?'Preview text':'Slide text'),'Selectable fallback '+ext);
    if(ext==='pdf')assert(state.text.includes('HttpLocalServer PDF preview'),'PDF text layer');
    if(ext==='pdf'){const pixel=await page.locator('#stage canvas').first().evaluate(canvas=>{const scale=canvas.width/612;return Array.from(canvas.getContext('2d').getImageData(Math.floor(100*scale),Math.floor(250*scale),1,1).data);});assert(Math.abs(pixel[0]-40)<8&&Math.abs(pixel[1]-130)<8&&Math.abs(pixel[2]-210)<8,'JPEG2000 decode '+JSON.stringify(pixel));}
    if(ext==='xlsx'&&!legacy){await page.locator('.cell-address').fill('A2');await page.locator('.cell-address').press('Enter');await page.waitForFunction(()=>window.HLSViewer.engine().selectedText()==='example');await page.locator('#copy').click();await page.waitForFunction(()=>document.querySelector('#status').textContent==='已复制');assert.equal(await page.evaluate(()=>navigator.clipboard.readText()),'example','Spreadsheet cell copy');}
    else{const needle=ext==='docx'?'Preview text':ext==='pptx'?'Slide text':ext==='xlsx'?'example':'HttpLocalServer PDF preview';const selected=await page.evaluate(()=>{const range=document.createRange();range.selectNodeContents(document.querySelector('#stage'));const selection=getSelection();selection.removeAllRanges();selection.addRange(range);return selection.toString();});assert(selected.includes(needle.split(' ')[0]),'Selectable document text '+ext+' '+JSON.stringify({selected,text:state.text}));await page.waitForTimeout(300);assert.equal(await page.evaluate(()=>String(getSelection())),selected,'Idle fitting preserves the selected text');await page.locator('#copy').click();await page.waitForFunction(()=>document.querySelector('#status').textContent==='已复制');assert.equal((await page.evaluate(()=>navigator.clipboard.readText())).replace(/\r\n/g,'\n'),selected.replace(/\r\n/g,'\n'),'Document clipboard copy');}
    await page.screenshot({path:path.join(out,(legacy?'legacy-':'modern-')+ext+'.png')});
    if(ext==='pptx'){await page.locator('#next').click();await page.waitForFunction(()=>document.querySelector('#page').value==='2');}
    console.log('PASS',legacy?'no-Wasm':'native-Wasm',ext,state.status);
   }
   await context.close();
  }
  for(const mode of ['host','client']){
   const context=await browser.newContext({viewport:{width:1200,height:800},permissions:['clipboard-read','clipboard-write'],ignoreHTTPSErrors:true}),page=await context.newPage();await sources(context);page.on('pageerror',e=>console.log('ZIP ERROR',mode,String(e)));page.on('console',m=>{if(m.type()==='error')console.log('ZIP CONSOLE',mode,m.text());});
   await page.goto(base+'/_hls/');const session=await page.request.get(base+'/api/session').then(r=>r.json());
   const changed=await page.request.post(base+'/api/admin/settings',{headers:{'X-HLS-Token':session.token},data:{zipPreview:mode}});assert(changed.ok(),await changed.text());
   await page.goto(base+'/_hls/viewer?path=/website.zip&kind=zip');
   await page.waitForFunction(()=>document.querySelectorAll('.zip-row').length>=3);
   await page.getByRole('button',{name:'site',exact:false}).click();
   await page.getByRole('button',{name:'index.html',exact:false}).click();
   const child=page.frameLocator('#stage iframe');await child.locator('#zip-heading').waitFor();
   await child.locator('body[data-loaded=yes]').waitFor({timeout:20000});
   assert.equal(await child.locator('body').evaluate(el=>getComputedStyle(el).backgroundColor),'rgb(220, 240, 255)');
   const blocked=await child.locator('body').evaluate(async()=>{try{const r=await fetch('/api/admin/settings');return r.status===200;}catch{return false;}});assert(!blocked,'ZIP page cannot read management APIs');
   await page.screenshot({path:path.join(out,'zip-'+mode+'.png')});console.log('PASS ZIP website',mode,'CSS/script/image/fetch and API isolation');
   await context.close();
  }
  {
   const context=await browser.newContext({viewport:{width:1200,height:800},permissions:['clipboard-read','clipboard-write'],ignoreHTTPSErrors:true}),page=await context.newPage();await sources(context);
   await page.goto(base+'/_hls/');const session=await page.request.get(base+'/api/session').then(r=>r.json());
   await page.request.post(base+'/api/admin/settings',{headers:{'X-HLS-Token':session.token},data:{zipPreview:'host'}});await page.reload();
   const archive=page.locator('.file').filter({has:page.locator('.name',{hasText:'website.zip'})});await archive.waitFor();await archive.dblclick();
   await page.waitForFunction(()=>document.querySelectorAll('.tab').length===2&&document.querySelector('#content-tabs iframe'));
   for(let i=0;i<10;i++)await page.locator('#new-tab').click();assert.equal(await page.locator('.tab').count(),12);
   await archive.waitFor();await archive.dblclick();await page.waitForFunction(()=>document.querySelector('#toast').textContent.indexOf('先关闭')>=0);assert.match(await page.locator('#toast').innerText(),/先关闭/);assert.equal(await page.locator('.tab').count(),12);
   await page.locator('.tab').nth(1).locator('.close').click();assert.equal(await page.locator('.tab').count(),11);
   await page.locator('#theme-dark').click();await archive.dblclick();
   await page.frameLocator('#content-tabs iframe').locator('body.dark').waitFor();
   await page.setViewportSize({width:390,height:780});await page.screenshot({path:path.join(out,'tabs-zip-mobile-dark.png')});
   console.log('PASS internal tabs: 12 cap, close cleanup, dark mode and mobile');await context.close();
  }
  console.log('Preview browser checks passed');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
