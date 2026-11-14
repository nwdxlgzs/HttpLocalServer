import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const data = JSON.parse(fs.readFileSync(path.join(root, 'data/scenes.json'), 'utf8'));
const techContext = {window:{}};
vm.runInNewContext(fs.readFileSync(path.join(root, 'js/technical.js'), 'utf8'), techContext);
const tech = techContext.window.HLSTechnical;
const esc = v => String(v ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const symbols = {
  file:'<path d="M8 3h11l7 7v19H8zM19 3v8h7M12 17h10m-10 5h8"/>',
  folder:'<path d="M3 9h10l3 4h13v13H3zM3 9V6h10l3 3"/>',
  browser:'<rect x="3" y="4" width="26" height="23" rx="4"/><path d="M3 11h26m-21-4h1m3 0h1m3 0h1"/>',
  server:'<rect x="5" y="4" width="22" height="24" rx="4"/><path d="M6 12h20M6 20h20M10 8h1m-1 8h1m-1 8h1"/>',
  laptop:'<rect x="5" y="4" width="22" height="18" rx="2"/><path d="M5 22l-3 5h28l-3-5M12 27h8"/>',
  phone:'<rect x="9" y="2" width="14" height="28" rx="3"/><path d="M14 6h4m-4 20h4"/>',
  globe:'<circle cx="16" cy="16" r="13"/><ellipse cx="16" cy="16" rx="6" ry="13"/><path d="M3 16h26M6 8h20M6 24h20"/>',
  check:'<path d="m6 16 7 7L27 8"/>',
  download:'<path d="M16 3v18m-7-7 7 7 7-7M4 23v6h24v-6"/>',
  code:'<path d="m10 8-8 8 8 8m12-16 8 8-8 8M19 3l-6 26"/>'
};
const icon = name => `<div class="icon"><svg viewBox="0 0 32 32" aria-hidden="true">${symbols[name]||symbols.file}</svg></div>`;
const tag = (v, cls='')=>`<span class="tag ${cls}">${esc(v)}</span>`;
const tags = a=>`<div class="tags motion">${a.map(x=>tag(x)).join('')}</div>`;
const modeRows = ()=>`<div class="brand-modes motion"><div class="brand-mode">${icon('folder')}<div><h3>FileTrans</h3><p>共享文件，浏览与交付</p></div></div><div class="brand-mode">${icon('browser')}<div><h3>Server</h3><p>托管网页与轻量应用</p></div></div></div>`;
const frame = (slug, label='', extra='')=>`<div class="capture-stage"><div class="media-frame motion ${extra}" data-capture="${slug}"><img class="screen-image" src="assets/capture/${slug}.png" alt="${esc(label||slug)}"></div><div class="capture-meta motion"><span class="provenance">实际产品界面 · 隔离演示资料</span><span class="detail-label">${esc(label)}</span></div></div>`;
const strips = a=>`<div class="detail-strip motion">${a.map(x=>`<span>${esc(x)}</span>`).join('')}</div>`;

const details = {
 S05:['列表 / 网格','熟悉的路径导航'],S07:['可拖动标签','路径历史','最多 12 个'],S08:['按需获取列表','滚动加载','搜索定位'],
 S12:['文件与文件夹上传','拖放指定目录'],S13:['复制 / 剪切','跨标签移动','回收与恢复按设置启用'],S14:['新建文件','普通文本编辑','Monaco 按能力增强'],
 S16:['Word / PDF','连续页面','选择复制'],S17:['缩略图导航','适配宽度','演示播放'],S18:['工作表切换','合并单元格','选择复制'],
 S19:['Markdown','JSON 树','结构化阅读'],S20:['图片与网页','音视频流式预览','Range 拖动'],S21:['懒缩略图 · 默认关闭','主机统一控制'],
 S23:['ZIP 内相对资源','网页与 Markdown','有界解压'],S26:['公告','多文件关注提示','跳转或下载'],S32:['本机设置','配置持久化','CLI 优先'],
 S33:['静态 HTTP 托管','压缩协商','可选 HTTPS']
};

function body(scene, variant) {
 const key=scene.sourceId||scene.id;
 const technical=tech.render({...scene,id:key,variant});
 if(technical)return technical;
 switch(scene.kind){
 case 'hero':return `<div class="hero-stage"><div class="hero-copy"><div class="eyebrow motion">LOCAL. HTTP. CONNECTED.</div><h1 class="hero-title motion">让本机，成为<em>服务入口</em></h1><p class="motion">文件、网页和轻量应用，沿着同一网络抵达。</p>${tags(['C / C++ CLI','浏览器访问'])}</div><div class="network"><div class="source-box motion"><img src="assets/logo.svg" alt="HttpLocalServer"><div><h3>HttpLocalServer</h3><p>本机内容 · 按规则开放</p></div></div><div class="network-route motion"><i class="network-dot"></i></div><div class="device-row"><div class="device motion">${icon('laptop')}<p>桌面</p></div><div class="device motion">${icon('phone')}<p>手机</p></div><div class="device motion">${icon('browser')}<p>浏览器</p></div></div></div></div>`;
 case 'brand':return `<div class="brand-stage"><div class="eyebrow motion">LOCAL HTTP SERVER · v${data.version}</div><div class="brand-lockup motion"><img class="brand-logo" src="assets/logo.svg" alt=""><h1 class="brand-name">HttpLocalServer</h1></div><div class="brand-line motion">一个程序，两种服务。</div>${modeRows()}<div class="version motion">C / C++ · 一个原生可执行文件</div></div>`;
 case 'outro':return `<div class="outro-stage"><div class="eyebrow motion">LOCAL. HTTP. CONNECTED.</div><div class="brand-lockup motion"><img class="brand-logo" src="assets/logo.svg" alt=""><h1 class="brand-name">HttpLocalServer</h1></div><h2 class="outro-title motion">让本机，成为服务入口。</h2>${modeRows()}<div class="outro-meta motion"><span>Apache 2.0</span><span>github@nwdxlgzs</span><span>v${data.version}</span></div><p class="fine motion">第三方组件仍遵循各自许可与通知要求。</p></div>`;
 case 'terminal':return `<div class="terminal-stage"><div class="side-copy"><div class="terminal-window motion"><div class="terminal-top"><span>TERMINAL · 启动示意</span><div class="terminal-dots"><i></i><i></i><i></i></div></div><div class="terminal-code"><span class="command-line">&gt; HttpLocalServer <strong>--root ./share</strong></span></div><div class="terminal-output"><span class="terminal-result">FileTrans · 0.0.0.0:8000</span><span class="terminal-result ok">HTTP service ready</span></div></div>${tags(['默认 FileTrans','端口与绑定地址可配置'])}<p class="fine motion">右侧为实际程序运行界面。命令行片段为启动示意。</p></div>${frame('manager-list','FileTrans / 在线文件下载')}</div>`;
 case 'address':return `<div class="qr-demo-stage"><div class="qr-source-bank"><div class="web-qr-card motion"><div class="qr-card-head">${icon('browser')}<h3>网页二维码</h3></div><div class="qr-browser-bar"><i></i><i></i><i></i><span>HttpLocalServer</span></div><div class="qr-code-wrap"><img src="assets/capture/qr-example-com.png" alt="https://example.com 二维码"></div><div class="qr-url">https://example.com</div></div><div class="cli-qr-card motion"><div class="terminal-top"><span>CLI</span><span>--show-cli-qr</span></div><div class="qr-code-wrap"><img src="assets/capture/qr-example-com.png" alt="相同示例网址的 CLI 二维码示意"></div><code>https://example.com</code></div><p class="fine motion">网页与 CLI，都能交出入口。</p></div><div class="qr-to-phone motion"><div class="qr-route"><i></i></div><span>扫一扫</span>${icon('phone')}</div><div class="scan-phone motion"><div class="scan-speaker"></div><div class="scan-display"><div class="scan-camera"><div class="scan-camera-label">扫一扫</div><div class="scan-target"><img src="assets/capture/qr-example-com.png" alt=""><div class="scan-corners"></div><div class="scan-beam"></div></div></div><div class="scan-link"><span>识别到网页</span><strong>example.com</strong><span class="scan-open">打开网页 →</span></div><div class="scan-result"><div class="phone-url"><span>⌕</span><strong>example.com</strong><span>↻</span></div><img class="phone-result-page" src="assets/capture/mobile.png" alt="HttpLocalServer 手机端文件管理界面"></div></div><div class="scan-home"></div></div><div class="qr-demo-foot motion">扫码流程示意 · 示例地址 example.com</div></div>`;
 case 'mobile':return `<div class="mobile-stage">${frame('theme-dark','桌面明暗主题 · 真实界面')}<div class="phone-shell motion"><img src="assets/capture/mobile.png" alt="手机界面"></div></div>`;
 case 'download':return `<div class="mechanism-stage"><div class="transfer-route"><div class="endpoint motion">${icon('server')}<h3>主机</h3><p>按区间读取</p></div><div class="transfer-wire"><div class="transfer-code motion">Range: bytes=…</div><div class="block-lane">${Array.from({length:6},(_,i)=>`<div class="data-block ${i<3?'received':''}">${i+1}</div>`).join('')}</div><div class="tags motion">${tag('206 Partial Content')}${tag('保留字节偏移','mint')}</div></div><div class="endpoint motion">${icon('download')}<h3>浏览器</h3><p>管理普通下载</p></div></div><div class="transfer-note motion"><strong>把文件分段传输</strong><div class="transfer-progress"><i></i></div><span>断点续传，按请求继续</span></div><p class="fine motion">机制示意；普通下载的进度、暂停与恢复由浏览器管理。</p></div>`;
 case 'tasks':return `<div class="mechanism-stage"><div class="task-list">${[['培训材料','上传任务','正在传输'],['演示素材','任务可暂停后继续','已暂停'],['目录交付','进度与错误反馈','等待继续']].map((a,i)=>`<div class="task-row motion">${icon(i===2?'folder':'file')}<div><strong>${a[0]}</strong><p>${a[1]}</p></div><div class="task-progress"><i style="width:${[67,41,22][i]}%"></i></div><div class="task-status">${a[2]}</div></div>`).join('')}</div><div class="transfer-note motion"><span>中断后的任务，重新确认再继续</span>${tag('上传任务机制示意','outline')}</div></div>`;
 case 'folders':return `<div class="mechanism-stage"><div class="branch-stage"><div class="folder-source motion">${icon('folder')}<h3>整个文件夹</h3><p class="fine">保留目录结构</p></div><div class="branch-line motion"></div><div class="branch-options"><div class="branch-option motion"><h3>直接保存到所选目录</h3><p>浏览器支持目录句柄与安全本地写入时使用。</p>${tag('按能力提供','mint')}</div><div class="branch-option motion"><h3>浏览器本地打包 ZIP</h3><p>逐项下载，再由本地打包；主机无需先生成压缩包。</p>${tag('兼容路径')}</div></div></div><p class="fine motion">大型目录仍受浏览器能力与本地资源预算约束；图为机制示意。</p></div>`;
 case 'identify':return `<div class="identify-stage"><div class="identity-flow"><div class="identity-file motion">${icon('file')}<div>旅行照片<p>扩展名被移除</p></div></div><div class="identity-scan motion"><span class="eyebrow">MAGIKA · CONTENT INFERENCE</span><strong>从内容寻找类型线索</strong><div class="scan-line"></div><p>推断为图像 → 使用图像预览</p></div>${tags(['格式推断服务 · 默认开启','最近结果缓存'])}<p class="fine motion">识别不是绝对判断；权限和预览策略继续生效。</p></div>${frame('preview-image','图片预览 · 真实界面')}</div>`;
 case 'zip':return `<div class="mechanism-stage"><div class="zip-options">${[['01','关闭','ZIP 预览默认关闭。按需要明确开启。','默认'],['02','主机提供','按条目有界解压，适合零碎内容。','主机承担解压'],['03','下载后本地提供','在浏览器侧处理压缩内容。','客户端承担资源']].map((a,i)=>`<div class="zip-option motion ${i===0?'selected':''}"><div class="zip-no">${a[0]}</div><h3>${a[1]}</h3><p>${a[2]}</p>${tag(a[3],i===0?'mint':'outline')}</div>`).join('')}</div><div class="budget-ribbon motion"><strong>解压有预算</strong><span>条目数 / 体积 / 比例 / 时间</span><span>拒绝异常路径</span></div></div>`;
 default:{
 const list=details[key]||[];
 if(key==='S16'&&variant!=='main')return `<div class="split-stage"><div class="side-copy"><div class="media-symbol motion">${icon('file')}${icon('browser')}</div><h3 class="motion">看清内容，再继续。</h3><p class="motion">文档、演示、表格与媒体，有各自适合的预览方式。</p>${tags(['选择复制','连续阅读'])}</div>${frame(scene.capture,'文档预览 / 实际界面')}</div>`;
 return `<div class="capture-stage">${frame(scene.capture||'manager-list',key==='S33'?'Server / 实际托管的演示网站':'v'+data.version+' / 产品界面')}${list.length?strips(list):''}</div>`;
 }
 }
}

const videoMap={S05:['file-browse',4.96],S06:['theme-switch',4.96],S07:['tabs-drag',4.92],S17:['ppt-navigation',5.8]};
const shotData=[];
for(const [variant,film] of Object.entries(data.variants)){
 let visuals='';let footage='';
 for(const [i,s] of film.scenes.entries()){
  const key=s.sourceId||s.id;
  const start=s.start;
  const dur=s.duration;
  const technical=tech.supported.includes(key);
  const cls=[s.kind,technical?'technical':'',s.kind==='outro'?'dark':''].filter(Boolean).join(' ');
  const chapter=key==='S01'||key==='S02'||key==='S44'?'LOCAL HTTP SERVER':key>='S38'&&key<='S42'?'ENGINEERING':key>='S33'&&key<='S37'?'SERVER':key>='S24'&&key<='S32'?'CONTROL':'FILETRANS';
  visuals+=`<section id="scene-${s.id}" class="scene clip ${cls}" data-start="${start}" data-duration="${dur}" data-track-index="${i%2+1}" data-shot="${s.id}"><div class="scene-content"><div class="scene-ruler"></div><div class="orb one"></div><div class="orb two"></div><header class="scene-header"><div class="wordmark motion"><img src="assets/logo.svg" alt="">HttpLocalServer</div><div class="chapter motion">${String(i+1).padStart(2,'0')} / ${String(film.scenes.length).padStart(2,'0')} · ${chapter}</div></header><div class="scene-heading"><h2 class="motion">${esc(s.title)}</h2><p class="motion">${esc(s.subtitle)}</p></div><div class="stage">${body(s,variant)}</div></div></section>\n`;
  if(variant!=='vertical'&&videoMap[key]&&s.duration>=4.5){
   const [slug,sourceDuration]=videoMap[key];
   const duration=Math.min(sourceDuration,s.duration-1.05);
   footage+=`<div class="recorded-frame" data-reference="scene-${s.id}" data-footage-start="${s.start+.7}"><video id="footage-${s.id}" class="clip" data-start="${s.start+.7}" data-duration="${duration}" data-track-index="4" src="assets/capture/${slug}.webm" muted playsinline preload="auto"></video></div>`;
  }
 }
 const captions=JSON.parse(fs.readFileSync(path.join(root,`assets/audio/${variant}-captions.json`),'utf8'));
 const subtitles=captions.map((c,i)=>`<div id="caption-${i}" class="subtitle clip" data-start="${c.start}" data-duration="${(c.end-c.start).toFixed(4)}" data-track-index="6"><span>${esc(c.text)}</span></div>`).join('\n');
 const payload=JSON.stringify({variant,...film}).replace(/</g,'\\u003c');
 const html=`<!doctype html>\n<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=${film.width}, height=${film.height}"><title>HttpLocalServer · ${variant}</title><link rel="icon" type="image/svg+xml" href="assets/logo.svg"><link rel="stylesheet" href="styles/film.css"><link rel="stylesheet" href="styles/fonts.css"><link rel="stylesheet" href="styles/technical.css"><link rel="stylesheet" href="styles/qr.css"><script src="assets/vendor/gsap.min.js"></script></head><body class="${variant==='vertical'?'portrait':''}"><div id="film" data-composition-id="${variant}" data-start="0" data-duration="${film.duration}" data-width="${film.width}" data-height="${film.height}">${visuals}${footage}${subtitles}<audio id="master-audio" data-start="0" data-duration="${film.duration}" data-track-index="7" src="assets/audio/master-${variant}.wav" data-volume="1" preload="auto"></audio><div class="film-progress"><i></i></div><div class="film-edition">HTTPLOCALSERVER · ${variant==='main'?'PRODUCT FILM':variant==='overview'?'OVERVIEW':variant==='teaser'?'TEASER':'PORTRAIT EDITION'}</div><div class="film-counter">v${data.version}</div></div><script id="film-data" type="application/json">${payload}</script><script src="js/technical.js"></script><script src="js/film.js"></script></body></html>`;
 const out=variant==='main'?'index.html':`edits/${variant}.html`;
 fs.mkdirSync(path.dirname(path.join(root,out)),{recursive:true});
 const resolved=variant==='main'?html:html.replace(/(src|href)="(assets|styles|js)\//g,'$1="../$2/');
 fs.writeFileSync(path.join(root,out),resolved);
 shotData.push({variant,file:out,shots:film.scenes.length,duration:film.duration,width:film.width,height:film.height});
 console.log(`${out}: ${film.scenes.length} shots / ${film.duration}s`);
}
fs.writeFileSync(path.join(root,'data/build-manifest.json'),JSON.stringify(shotData,null,2)+'\n');







