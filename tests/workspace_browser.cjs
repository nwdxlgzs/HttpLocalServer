// SPDX-License-Identifier: Apache-2.0
const fs=require('fs'),assert=require('assert/strict');
const {chromium}=require('../.deps/preview-tooling/node_modules/playwright-core');
const base=process.argv[2],out='.deps/workspace-'+(base.startsWith('https')?'https':'http');fs.mkdirSync(out,{recursive:true});
(async()=>{
 const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,args:['--no-proxy-server']});
 const context=await browser.newContext({viewport:{width:1280,height:800},ignoreHTTPSErrors:true});
 if(process.env.HLS_PREVIEW_SOURCES==='1')await context.route(url=>['/','/_hls/'].includes(new URL(url).pathname),r=>r.fulfill({body:fs.readFileSync('.deps/web-v22.html'),contentType:'text/html'}));
 const page=await context.newPage(),errors=[];page.on('pageerror',e=>errors.push(String(e)));
 const button=name=>page.getByRole('button',{name,exact:true});
 async function path(value){await page.keyboard.press('Control+l');await page.locator('#path-input').fill(value);await page.locator('#path-input').press('Enter');await page.waitForFunction(p=>document.querySelector('#path-input').value===p&&/个项目/.test(document.querySelector('#count').textContent),value);}
 async function begin(name){await page.locator('#mkdir').click();await button('新建文件').click();await page.locator('#modal-input').fill(name);await button('创建').click();}
 async function listing(path){return (await context.request.get(base+'/api/list?path='+encodeURIComponent(path)+'&refresh=1')).json();}
 async function waitCreated(path,name){for(let i=0;i<100;i++){const items=await listing(path);if(items.entries.some(e=>e.name===name))return;await page.waitForTimeout(60);}throw Error('Missing '+path+'/'+name);}
 async function settings(){await page.locator('#host-settings').click();await page.locator('#settings-shade').waitFor({state:'visible'});}
 async function writeMode(mode){await settings();await page.selectOption('#setting-write',mode);await page.locator('#save-settings').click();await page.locator('#settings-shade').waitFor({state:'hidden'});await page.waitForFunction(w=>document.querySelector('#mkdir').disabled===!w,mode!=='readonly');}
 try{
  await page.goto(base+'/_hls/');await path('/NewFiles');
  assert.equal((await listing('/NewFiles')).total,4);assert.equal(await page.locator('.file.frozen-entry').count(),0);
  await page.locator('#more-button').click();await button('浏览选项').click();await page.getByRole('button',{name:/显示隐藏文件和文件夹/}).click();
  await page.waitForTimeout(250);assert.equal(await page.locator('.file.frozen-entry').count(),0);await page.screenshot({path:out+'/frozen-revealed.png'});
  await page.locator('#more-button').click();await button('浏览选项').click();await page.getByRole('button',{name:/显示隐藏文件和文件夹/}).click();await page.waitForTimeout(250);assert.equal(await page.locator('.file.frozen-entry').count(),0);
  console.log('PASS frozen config, lock and state entries follow per-browser hidden preference');
  await begin('empty.txt');await waitCreated('/NewFiles','empty.txt');const empty=(await listing('/NewFiles')).entries.find(e=>e.name==='empty.txt');assert.equal(empty.size,0);
  await begin('keep.txt');await page.locator('#modal-shade').waitFor({state:'visible'});assert.equal(await context.request.get(base+'/download?path=/NewFiles/keep.txt').then(r=>r.text()),'keep existing contents');
  await button('取消').click();console.log('PASS empty file creation and no-clobber collision');
  const v=await page.locator('#viewport').boundingBox();await page.mouse.click(v.x+v.width-20,v.y+v.height-20,{button:'right'});await button('新建文件夹').click();await page.locator('#modal-input').fill('CreatedFolder');await button('创建').click();await waitCreated('/NewFiles','CreatedFolder');
  await page.locator('#mkdir').click();await button('新建文件').waitFor();await page.waitForTimeout(220);await page.screenshot({path:out+'/new-menu.png'});await page.keyboard.press('Escape');
  await writeMode('readonly');assert(await page.locator('#mkdir').isDisabled());await writeMode('shared');console.log('PASS blank-space folder creation and read-only directory controls');
  await settings();
  for(const theme of ['theme-light','theme-dark']){
   await page.evaluate(id=>document.querySelector('#'+id).click(),theme);
   for(const width of [1280,390,240]){
    await page.setViewportSize({width,height:740});
    const geometry=await page.evaluate(()=>['reload-blocked','reload-name-reviews'].map(id=>{const e=document.querySelector('#'+id),s=getComputedStyle(e),r=e.getBoundingClientRect();return {height:r.height,width:r.width,padding:s.padding,border:s.border,borderRadius:s.borderRadius,color:s.color,margin:s.margin};}));
    assert.deepEqual(geometry[0],geometry[1]);
   }
  }
  await page.setViewportSize({width:390,height:740});await page.locator('#reload-name-reviews').scrollIntoViewIfNeeded();await page.screenshot({path:out+'/settings-reviews.png'});await page.locator('#close-settings').click();console.log('PASS matching settings review panels and refresh controls in both themes at desktop/mobile widths');
  // Keep a pending-review badge present across the production browse poll.
  await page.route('**/api/browse',async r=>{const response=await r.fetch(),data=await response.json();data.pendingNames=123;r.fulfill({response,json:data});});
  await page.reload();await page.locator('#host-settings .review-count').waitFor({timeout:20000});
  for(const theme of ['theme-light','theme-dark'])for(const width of [600,390,320,280,240])for(const expanded of [false,true]){
   await page.setViewportSize({width,height:740});await page.locator('#'+theme).click();
   await page.evaluate(open=>document.querySelector('.connection').classList.toggle('mobile-expanded',open),expanded);
   const geometry=await page.locator('#host-settings').evaluate(e=>{const a=e.getBoundingClientRect(),b=e.querySelector('.ui-icon').getBoundingClientRect();return {dx:Math.abs((a.left+a.right-b.left-b.right)/2),dy:Math.abs((a.top+a.bottom-b.top-b.bottom)/2),width:a.width,icon:b.width,badge:getComputedStyle(e.querySelector('.review-count')).position};});
   assert(geometry.dx<=1&&geometry.dy<=1&&geometry.width===36&&geometry.icon===20&&geometry.badge==='absolute',JSON.stringify({width,theme,expanded,geometry}));
  }
  await page.evaluate(()=>document.querySelector('.connection').classList.remove('mobile-expanded'));await page.setViewportSize({width:390,height:740});await page.screenshot({path:out+'/mobile-settings.png'});console.log('PASS centered mobile settings icon with review badge at 600/390/320/280/240px, expanded/collapsed, light/dark');
  await page.setViewportSize({width:1280,height:800});await page.evaluate(async()=>{window.showDirectoryPicker=async()=>{const root=await navigator.storage.getDirectory(),handle=await root.getDirectoryHandle('BrowserFiles',{create:true});handle.requestPermission=async()=> 'granted';for(const name of ['config.json','.hls-config-lock-config.json']){const f=await handle.getFileHandle(name,{create:true}),w=await f.createWritable();await w.write('private');await w.close();}await handle.getDirectoryHandle('.hls-state-provider',{create:true});return handle;};});
  await page.locator('#mount-button').click();await page.locator('#modal-input').fill('/BrowserFiles');await button('读写挂载').click();await waitCreated('/','BrowserFiles');await path('/BrowserFiles');await begin('mounted-empty.txt');await waitCreated('/BrowserFiles','mounted-empty.txt');
  assert.equal(await page.evaluate(async()=>{const root=await navigator.storage.getDirectory(),dir=await root.getDirectoryHandle('BrowserFiles'),file=await(await dir.getFileHandle('mounted-empty.txt')).getFile();return file.size;}),0);
  const mountedVisible=await listing('/BrowserFiles');assert.equal(mountedVisible.total,2);const mountedAll=await context.request.get(base+'/api/list?path=/BrowserFiles&showHidden=1').then(r=>r.json());assert.equal(mountedAll.total,4);assert(mountedAll.entries.filter(e=>e.name!=='mounted-empty.txt').every(e=>!e.frozen));console.log('PASS actual browser filesystem mount publishes empty files and applies hidden frozen listing policy');
  assert.deepEqual(errors,[]);
 }finally{await context.close();await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
