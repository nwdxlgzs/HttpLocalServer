// SPDX-License-Identifier: Apache-2.0
const {make,local,until,get}=require('./cache_jobs_test.cjs');
const assert=require('assert/strict'),fs=require('fs'),path=require('path');
async function run(){
 const folder=process.argv[3],original=fs.readFileSync(path.join(folder,'source.bin'));
 let c=await make(false),ask=c.request,injected=false;
 c.request=(method,url,body,done)=>{if(url==='/api/resume/copy-chunk'&&body.offset===1048576&&!injected){injected=true;setTimeout(()=>done('Injected lost connection'),0);return {abort(){}};}return ask(method,url,body,done);};
 const entry={path:'/source.bin',name:'source.bin',size:original.length,directory:false};let task=c.startCopyJob(entry,'/copy-page.bin');await until(()=>task.state==='failed');assert(task.recovery.plan.items[0].resume.offset===1048576);
 let restored=JSON.parse(local.get('hls-tasks-v1')).find(x=>x.id===task.id);c=await make(false);c.transferTasks.push(restored);c.jobRestore(restored);await until(()=>!!restored.recovery);c.jobResume(restored);await until(()=>restored.state==='done');assert.equal(fs.readFileSync(path.join(folder,'copy-page.bin')).compare(original),0);assert(!c.calls.some(x=>x.url==='/api/resume/copy-chunk'&&JSON.parse(x.body).offset===0),'Page recovery does not recopy acknowledged prefix');
 task=c.startCopyJob(entry,'/moved-page.bin',null,true);await until(()=>/^(done|failed)$/.test(task.state));assert.equal(task.state,'done',task.error);assert(!fs.existsSync(path.join(folder,'source.bin')));assert.equal(fs.readFileSync(path.join(folder,'moved-page.bin')).compare(original),0);
 fs.writeFileSync(path.join(folder,'source.bin'),original);c=await make(false);ask=c.request;let changed=false;c.request=(method,url,body,done)=>{if(url==='/api/upload-target?path=%2Fsource.bin'&&!changed){changed=true;fs.writeFileSync(path.join(folder,'source.bin'),'changed-after-copy');}return ask(method,url,body,done);};task=c.startCopyJob(entry,'/protected-move.bin',null,true);await until(()=>/^(done|failed)$/.test(task.state));assert.equal(task.state,'failed');assert(task.error.includes('改变'));assert.equal(fs.readFileSync(path.join(folder,'source.bin')).toString(),'changed-after-copy');assert.equal(fs.readFileSync(path.join(folder,'protected-move.bin')).compare(original),0);
 console.log('Copy/move HTTP jobs passed: chunk checkpoint across pages, exact output, verified move cleanup and changed-source preservation');
}
run().catch(e=>{console.error(e);process.exitCode=1;});
