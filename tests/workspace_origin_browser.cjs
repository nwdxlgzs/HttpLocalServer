// SPDX-License-Identifier: Apache-2.0
const assert=require('assert/strict');
const {chromium}=require('../.deps/preview-tooling/node_modules/playwright-core');
(async()=>{
    const app=process.argv[2],control=process.argv[3];
    const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,args:['--no-proxy-server']});
    const context=await browser.newContext({ignoreHTTPSErrors:true});
    try{
        const page=await context.newPage();await page.goto(app+'/');assert.match(await page.textContent('body'),/ZIP app/);
        const same=await page.evaluate(async()=>{const r=await fetch('/api/admin/settings');return r.status;});assert.equal(same,403);
        const cross=await page.evaluate(async control=>{try{const r=await fetch(control+'/api/admin/settings',{credentials:'include'});return {status:r.status,body:await r.text()};}catch(e){return {blocked:true};}},control);assert.equal(cross.blocked,true);
        const host=await context.newPage();await host.goto(control+'/_hls/');await host.locator('#host-settings').waitFor({state:'visible'});
        const api=await host.evaluate(async()=>{const session=await fetch('/api/session').then(r=>r.json()),settings=await fetch('/api/admin/settings').then(r=>r.json());return {host:session.localAdmin,wasm:settings.wasmEngine,token:!!session.token};});
        assert.equal(api.host,true);assert.equal(api.wasm,'WAMR 2.4.5');assert.equal(api.token,true);
        const denied=await host.evaluate(async()=>[await fetch('/client.js').then(r=>r.status),await fetch('/api/app/info').then(r=>r.status)]);assert.deepEqual(denied,[404,404]);
        console.log('Browser origin boundary passed: app has no admin privileges; localhost console works; CORS blocks cross-origin token reads; app assets absent from console');
    }finally{await context.close();await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
