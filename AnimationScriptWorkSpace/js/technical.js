/* Original, deterministic technical diagrams for the HttpLocalServer film. */
(function () {
  'use strict';
  const esc = value => String(value == null ? '' : value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const icons = {
    server:'<rect x="5" y="4" width="22" height="24" rx="4"/><path d="M6 12h20M6 20h20"/><path d="M10 8h1m-1 8h1m-1 8h1"/>',
    folder:'<path d="M3 9h10l3 4h13v13H3z"/><path d="M3 9V6h10l3 3"/>',
    browser:'<rect x="3" y="4" width="26" height="23" rx="4"/><path d="M3 11h26m-21-4h1m3 0h1m3 0h1"/>',
    file:'<path d="M8 3h11l7 7v19H8z"/><path d="M19 3v8h7M12 17h10m-10 5h8"/>',
    laptop:'<rect x="6" y="4" width="20" height="18" rx="2"/><path d="M6 22l-3 5h26l-3-5M13 27h6"/>',
    shield:'<path d="M16 3l11 5v8c0 7-11 13-11 13S5 23 5 16V8z"/><path d="m11 16 3 3 7-7"/>',
    code:'<path d="m10 9-7 7 7 7m12-14 7 7-7 7m-3-18-6 24"/>',
    cpu:'<rect x="7" y="7" width="18" height="18" rx="4"/><rect x="12" y="12" width="8" height="8" rx="1"/><path d="M11 2v5m10-5v5M11 25v5m10-5v5M2 11h5m-5 10h5m18-10h5m-5 10h5"/>',
    network:'<rect x="11" y="2" width="10" height="9" rx="2"/><path d="M16 11v8M6 19h20M6 19v4m20-4v4"/><rect x="2" y="23" width="8" height="7" rx="2"/><rect x="22" y="23" width="8" height="7" rx="2"/>',
    media:'<rect x="3" y="4" width="26" height="24" rx="5"/><path d="m13 10 9 6-9 6z"/>',
    book:'<path d="M16 7c-5-5-12-3-12-3v22s7-2 12 3c5-5 12-3 12-3V4s-7-2-12 3zM16 7v22"/>'
  };
  function icon(name) { return '<div class="tech-icon"><svg viewBox="0 0 32 32" aria-hidden="true">'+(icons[name]||icons.file)+'</svg></div>'; }
  function arrow(cls='') { return '<div class="tech-arrow '+cls+'" data-layout-ignore><i class="tech-flow-dot"></i></div>'; }
  function tag(text,cls='') { return '<span class="tech-tag '+cls+'">'+esc(text)+'</span>'; }
  function note(text) { return '<div class="tech-note tech-enter">'+esc(text)+'</div>'; }
  function file(text,cls='') { return '<div class="tech-file '+cls+'"><span>'+esc(text)+'</span></div>'; }
  function node(title,body,ico='server',cls='') { return '<div class="tech-flow-node tech-paper tech-enter '+cls+'"><div class="tech-node-head">'+icon(ico)+'<h3>'+esc(title)+'</h3></div><div class="tech-detail">'+body+'</div></div>'; }
  function frame(html,cls='') { return '<div class="tech-stage '+cls+'">'+html+'</div>'; }
  const builders = {};

  builders.S24 = () => frame('<div class="tech-permissions">'+
    '<div class="tech-paper tech-permission tech-enter"><div class="tech-permission-title"><h3>只读</h3><small>readonly</small></div><p>浏览与下载</p><div class="tech-readonly">读取内容</div>'+tag('默认模式')+'</div>'+
    '<div class="tech-paper tech-permission tech-enter"><div class="tech-permission-title"><h3>共享写入</h3><small>shared</small></div><p>在允许的位置共同工作</p><div class="tech-paths">'+file('/项目资料')+file('/团队提交')+'</div></div>'+
    '<div class="tech-paper tech-permission tech-enter"><div class="tech-permission-title"><h3>独立提交</h3><small>isolated</small></div><p>实际 IP 对应自己的可写目录</p><div class="tech-paths"><div class="tech-path"><span class="tech-person">A</span><code>/_clients/IP-A/</code></div><div class="tech-path"><span class="tech-person">B</span><code>/_clients/IP-B/</code></div></div></div></div>'+note('独立模式下主机保留全共享管理范围；冻结与显式只读规则仍生效。'));

  builders.S25 = () => frame('<div class="tech-lifecycle">'+[
    ['01','先浏览','专属目录可先显示为空','目录未落盘'],['02','开始上传','收到内容才创建目录','按需建立'],['03','姓名审核','主机批准后公开胶囊','审核后显示'],['04','空目录回收','无人浏览且没有活动上传','仅清理空目录']
  ].map((s,i)=>'<div class="tech-life-step tech-enter"><div class="tech-life-no">'+s[0]+'</div><h3>'+s[1]+'</h3><p>'+s[2]+'</p>'+tag(s[3],i===2?'':'blue')+'</div>').join('')+'</div><div class="tech-life-sample tech-enter"><code>/_clients/192.168.50.21/</code>'+tag('林同学')+'</div>'+note('审核后的姓名只改变展示，不改变真实目录名和权限。'));

  builders.S27 = () => frame('<div class="tech-mapping"><div class="tech-map-sources">'+[
    ['资料盘','E:\\Documents'],['项目目录','/srv/projects'],['系统根与卷','显式启用']
  ].map(s=>'<div class="tech-source tech-enter"><strong>'+s[0]+'</strong><code>'+esc(s[1])+'</code></div>').join('')+'</div><div class="tech-map-links">'+arrow()+arrow()+arrow()+'</div><div class="tech-paper tech-enter"><div class="tech-node-head">'+icon('folder')+'<h3>统一网页入口</h3></div><div class="tech-tree"><div>/资料</div><div>/项目</div><div>/Computer</div><div class="tech-indent">C · D · E / 系统根</div><div>/Volumes</div></div></div></div>'+note('服务内映射，无需安装驱动；读写能力与全局权限共同判断。'));

  builders.S28 = () => frame('<div class="tech-mirror">'+
    '<div class="tech-mirror-node tech-paper tech-enter"><div class="tech-node-head">'+icon('browser')+'<h3>提供方浏览器</h3></div><div class="tech-queue">'+file('素材图片.jpg')+file('说明.pdf','tech-priority')+file('会议录像.mp4')+'</div>'+tag('访问文件优先','blue')+'</div>'+arrow()+
    '<div class="tech-mirror-node tech-paper tech-enter"><div class="tech-node-head">'+icon('server')+'<h3>主机完整副本</h3></div><div class="tech-store-list"><div><span class="tech-check">✓</span>说明.pdf</div><div><span class="tech-check">✓</span>素材图片.jpg</div><p>完整上传后原子发布</p></div>'+tag('已完成内容留存')+'</div>'+arrow()+
    '<div class="tech-mirror-node tech-enter"><div class="tech-node-head">'+icon('laptop')+'<h3>访问者</h3></div><p>由主机分发内容</p><div class="tech-divider"></div><p>来源离线时，完成副本仍可使用</p></div></div>'+note('目录句柄与安全上下文条件满足才显示入口；未完成内容仍依赖来源在线。'));

  builders.S29 = () => frame('<div class="tech-cow">'+
    '<div class="tech-cow-edge tech-enter"><div class="tech-node-head">'+icon('laptop')+'<h3>正在读取</h3></div><p>下载持有旧文件句柄</p>'+tag('保持完整旧代','blue')+'</div>'+arrow()+
    '<div class="tech-cow-stack"><div class="tech-generation old tech-enter"><div class="tech-version">V1</div><div><h3>旧版本</h3><p>已有读取继续使用</p></div></div><div class="tech-generation new tech-enter"><div class="tech-version">V2</div><div><h3>新写入</h3><p>暂存 · 刷盘 · 校验 · 发布</p><div class="tech-meter"><i></i></div></div></div></div>'+arrow()+
    '<div class="tech-cow-publish tech-enter"><h3>新请求</h3><p>发布后读取 V2</p><span class="tech-check">✓</span></div></div><div class="tech-cow-conflict tech-enter"><strong>另一个写入者仍提交 V1 条件</strong><span>409 · 重新确认版本</span></div>'+note('应用层 COW 与版本检查；逐 IP 额度、总带宽与单文件预算继续约束写入。'));

  builders.S30 = () => frame('<div class="tech-security"><div class="tech-enter"><div class="tech-lock-hero"></div></div><div class="tech-security-rows"><div class="tech-security-row tech-enter"><strong>登录口令</strong><div class="tech-attempts">'+Array.from({length:5},()=>'<i class="tech-attempt"></i>').join('')+'<span class="tech-delay">120s 起</span></div></div><div class="tech-security-row tech-enter"><strong>严格主机</strong><code>127.0.0.1 / ::1</code></div><div class="tech-security-row tech-enter"><strong>可选 HTTPS</strong><div>'+tag('TLS 1.2 / 1.3','blue')+'</div></div></div></div>'+note('封禁等待逐轮增加，主机可解锁；证书信任由客户端管理，HTTP口令不等于链路加密。'));

  builders.S31 = () => frame('<div class="tech-freeze"><div class="tech-freeze-list">'+['当前配置 · config.json','私有状态目录','活动脚本与应用包','使用中的 TLS 密钥'].map(n=>'<div class="tech-frozen-file tech-enter"><code>'+esc(n)+'</code><span>冻结 · 不可操作</span></div>').join('')+'<div class="tech-normal-file tech-enter"><code>用户资料/config.json</code>'+tag('普通同名文件')+'</div></div><div class="tech-freeze-rule tech-enter"><div class="tech-num">真实依赖</div><h3>按路径和文件身份判断</h3><p>不按文件名前缀一概冻结</p><div class="tech-divider"></div><p>规范路径、安全打开、权限与身份再次检查</p></div></div>'+note('冻结项默认隐藏；显示隐藏后可看到完整斜纹，仍无法读取或修改。'));

  builders.S34 = () => frame('<div class="tech-package"><div class="tech-zip tech-enter"><div class="tech-zip-head"><strong>demo-app.zip</strong><span>ZIP APP</span></div><div class="tech-package-section"><h4>'+tag('公开前端')+'</h4><div class="tech-code-lines"><span>public/index.html</span><span>public/app.js · assets/</span></div></div><div class="tech-package-section private"><h4>后台模块 · 不公开</h4><div class="tech-code-lines"><span>server.qjs</span><span>lib/ · application.wasm</span></div></div></div>'+arrow()+
    '<div class="tech-browser-node tech-enter"><div class="tech-browser-toolbar"><i></i><i></i><i></i></div><div class="tech-browser-content"><h3>局域网应用</h3><code>IP : 应用端口</code><div class="tech-mini-hero"></div><div class="tech-mini-lines"><i></i><i></i><i></i></div><p>请求经过登录与访问策略</p></div></div><div class="tech-control-origin tech-enter"><strong>独立主机控制来源</strong><code>localhost : 管理端口</code><span>设置 / MCP · 仅回环</span></div></div>'+note('默认仅公开 public/；真实文件共享 API 需显式开放，应用不获得主机管理权限。'));

  builders.S35 = () => frame('<div class="tech-components"><div class="tech-code-window tech-enter"><div class="tech-section">真实 API · 简化示例</div><code>import {fs, wasm} from \'hls\';\nconst app = wasm.instantiate(\'/calc.wasm\');\n\nconst result = app.exports.add(19, 23);\nfs.writeText(\'/results/latest.json\',\n  JSON.stringify({result}));</code>'+tag('已授权共享目录 · 受写方法约束')+'</div><div class="tech-capability-bank">'+[['fs','受限文件'],['SQLite','内存与快照'],['ZIP','有界读取'],['gzip','压缩解压'],['Magika','内容推断'],['SHA-256','内容摘要'],['QR','二维码'],['UTF-8','文字与字节']].map(s=>'<div class="tech-capability tech-enter"><strong>'+s[0]+'</strong><span>'+s[1]+'</span></div>').join('')+'</div></div><div class="tech-budget tech-enter"><strong>能力预算</strong><span class="tech-label">大小 · 数量 · 时间</span><div class="tech-range"><i></i></div><span>有界执行</span></div>');

  builders.S36 = () => frame('<div class="tech-wasm"><div class="tech-wasm-main"><div class="tech-wasm-node tech-enter"><h3>QuickJS</h3><code>add(19, 23)</code><p>可信控制逻辑</p></div>'+arrow('control')+'<div class="tech-wasm-node engine tech-enter">'+icon('cpu')+'<h3>WAMR</h3><p>CPU 解释器 · 受限 Wasm 互操作</p></div>'+arrow()+'<div class="tech-wasm-node tech-enter"><span class="tech-result">42</span><p>数值返回</p></div></div><div class="tech-budgets"><div class="tech-enter"><strong>内存上限</strong><span>受控分配</span></div><div class="tech-enter"><strong>执行期限</strong><span>墙钟检查</span></div><div class="tech-enter"><strong>总分派燃料</strong><span>跨调用共享</span></div></div></div>'+note('WebAssembly / WASI 子集；临时实例显式 close，不是 Node 或完整系统宿主。'));

  builders.S37 = () => frame('<div class="tech-mcp"><div class="tech-mcp-client tech-enter">'+icon('network')+'<h3>本机 AI 工具</h3>'+tag('独立 Bearer','blue')+'<p>明确启用 MCP</p></div>'+arrow('control')+'<div class="tech-mcp-tools">'+[['list_files','发现文件'],['file_type','识别类型'],['read_text','受限文本'],['download_url','固定版本']].map(s=>'<div class="tech-tool tech-enter"><strong>'+s[0]+'</strong><span>'+s[1]+'</span></div>').join('')+'</div>'+arrow('control')+'<div class="tech-mcp-result tech-enter"><div class="tech-num">只读</div><h3>结构化结果</h3><p>沿用路径、冻结与权限限制</p></div></div>'+note('不提供写文件或修改主机设置的 MCP 工具；ZIP 应用使用独立回环控制端口。'));

  builders.S38 = () => frame('<div class="tech-cache"><div class="tech-cache-main"><div class="tech-disk tech-enter">'+icon('server')+'<h3>原生分块读取</h3><p>同块冷请求合并</p></div>'+arrow()+'<div class="tech-heat tech-enter">'+['hot','hot','warm','cool','cool','warm','hot','hot','warm','cool','cool','cool'].map((c,i)=>'<div class="tech-heat-cell '+c+'">'+(i<4?'H'+(i+1):'B'+(i+1))+'</div>').join('')+'</div>'+arrow()+'<div class="tech-cache-output tech-enter"><div class="tech-output-line"><span class="tech-check">✓</span>复用热点</div><div class="tech-output-line"><span class="tech-check">✓</span>频率退火</div><div class="tech-output-line"><span class="tech-check">✓</span>空闲回收</div></div></div><div class="tech-workers tech-enter"><strong>工作线程</strong><div class="tech-worker-slots">'+Array.from({length:8},()=>'<i></i>').join('')+'</div><span class="tech-muted">按负载扩展，空闲撤回</span></div><div class="tech-protocols tech-enter"><span><b class="tech-gate-chip">gzip / deflate</b> 可压缩响应按能力协商</span><span><b class="tech-gate-chip">Range → identity</b> 保留原字节偏移</span></div></div>'+note('工作原理示意；连接、正文缓存、传输缓冲和并发均有独立预算。'));

  builders.S39 = () => frame('<div class="tech-reuse"><div class="tech-reuse-lane"><div class="tech-lane-label tech-enter">普通脚本</div><div class="tech-lane-box tech-enter"><h4>源码编译一次</h4><p>可信内存字节码</p></div>'+arrow()+'<div class="tech-lane-box accent tech-enter"><h4>独占外壳复用</h4><p>空闲池 · 逐次新上下文</p><div class="tech-contexts"><i></i><i></i><i></i></div></div>'+arrow('control')+'<div class="tech-lane-box tech-enter"><h4>新调用，新状态</h4><p>异常与待办任务回收</p></div></div><div class="tech-reuse-lane"><div class="tech-lane-label tech-enter">ZIP 静态</div><div class="tech-lane-box tech-enter"><h4>解压并校验 CRC</h4><p>同一资源冷请求合并</p></div>'+arrow()+'<div class="tech-lane-box accent tech-enter"><h4>热点保留缓存</h4><p>默认 16 MiB，可设为 0</p></div>'+arrow('control')+'<div class="tech-lane-box tech-enter"><h4>每次请求检查</h4><p>权限 · 私有模块 · 源版本</p></div></div></div>'+note('减少重复初始化和解压；持久 ZIP 应用脚本仍串行，不是多核后端池。'));

  builders.S40 = () => frame('<div class="tech-peer"><svg class="tech-peer-lines" viewBox="0 0 1500 500" preserveAspectRatio="none" aria-hidden="true"><path class="control" d="M750 115 C600 135 400 190 250 260"/><path d="M750 115 C900 135 1100 190 1250 260"/><path class="peer" d="M300 285 C530 385 970 385 1200 285"/><circle class="tech-peer-dot" cx="300" cy="285" r="9"/></svg><div class="tech-peer-node tech-peer-source tech-enter">'+icon('browser')+'<h3>缓存节点 / 挂载源</h3><p>校验过的内容块</p>'+tag('本页可关闭发送','blue')+'</div><div class="tech-peer-node tech-peer-host tech-enter"><div class="tech-host-badge">'+icon('server')+'</div><h3>主机</h3><p>授权协调 · 缺块回源</p></div><div class="tech-peer-node tech-peer-receiver tech-enter">'+icon('laptop')+'<h3>接收者</h3><p>右键选择 P2P</p>'+tag('SHA-256 分块校验')+'</div><div class="tech-peer-bottom tech-enter"><span><strong>PCDN · 可选开启</strong></span><code>0–64 MiB / 每页共享缓存</code><span>普通下载始终默认</span></div></div>'+note('能力和连接允许时分担部分流量；大文件需要安全本地写入，缺块可能回主机。'));

  builders.S41 = () => frame('<div class="tech-platforms"><div class="tech-platform-table"><div class="tech-platform tech-enter"><strong>Windows x64</strong><span><b class="tech-check">✓</b>发布与针对性回归</span></div><div class="tech-platform tech-enter"><strong>Linux-musl</strong><span><b class="tech-check">✓</b>静态发布与回归</span></div><div class="tech-platform pending tech-enter"><strong>macOS</strong><span><b class="tech-check">—</b>源码适配 · 未实测</span></div></div><div class="tech-fallback"><h3 class="tech-enter">按浏览器能力增强</h3><div class="tech-capability-level tech-enter">现代预览 · 代码编辑增强</div><div class="tech-capability-level secondary tech-enter">基础文档 · 普通文本编辑</div><div class="tech-capability-level tertiary tech-enter">IE11 基本浏览与下载</div></div></div>'+note('Windows 7 为兼容目标，未获得本版实机结论；资源随程序交付，不依赖 CDN。'));

  builders.S42 = () => frame('<div class="tech-bench"><div class="tech-bench-legend tech-enter"><span><i></i>3.5.0 对照</span><span><i></i>3.5.1 优化后</span><span>成功请求 / 秒 · 各组同尺度</span></div>'+[
    ['小脚本',3909.49,4899.58,'+25%'],['2048 函数声明脚本',1158.17,2601.44,'2.25×'],['450 KB ZIP 静态资源',412.27,443.33,'+7.5%']
  ].map(s=>'<div class="tech-bench-row tech-enter"><div class="tech-bench-title">'+s[0]+'</div><div class="tech-bench-bars"><div class="tech-bar-track"><i class="tech-bar before" style="width:'+(s[1]/6000*80).toFixed(3)+'%"></i><strong>'+Math.round(s[1])+'</strong></div><div class="tech-bar-track"><i class="tech-bar after" style="width:'+(s[2]/6000*80).toFixed(3)+'%"></i><strong>'+Math.round(s[2])+'</strong></div></div><div class="tech-bench-gain">'+s[3]+'</div></div>').join('')+'<div class="tech-bench-foot tech-enter">Windows 本机 HTTP 回环 · 8 客户端 · 每组 3 秒 · 固定线程与脚本预算。短时对比，不代表实际局域网或持续容量；两平台本次各 10 组针对性检查通过。</div></div>');

  builders.S43 = () => frame('<div class="tech-usecases"><div class="tech-case-list">'+[['book','课堂资料','分发与提交'],['media','工作室媒体','浏览与流式预览'],['browser','项目演示站','静态 HTTP 托管'],['code','内网轻量应用','可信脚本与组件']].map(s=>'<div class="tech-case tech-enter">'+icon(s[0])+'<strong>'+s[1]+'</strong><span>'+s[2]+'</span></div>').join('')+'</div><div class="tech-unified tech-enter"><img src="assets/logo.svg" alt=""><h3>HttpLocalServer</h3><div class="tech-line"></div><p>文件服务，也是 HTTP 服务</p></div></div>');

  const aliases = {permissions:'S24',lifecycle:'S25',mapping:'S27',mount:'S28',cow:'S29',security:'S30',freeze:'S31',workspace:'S34',components:'S35',wasm:'S36',mcp:'S37',cache:'S38',reuse:'S39',pcdn:'S40',platforms:'S41',bench:'S42',usecases:'S43'};
  function sceneKey(scene) { return builders[scene.id] ? scene.id : aliases[scene.kind] || aliases[scene.type]; }
  function render(scene) { const fn=builders[sceneKey(scene)]; return fn ? fn(scene) : ''; }
  function animate(tl, root, start, duration, scene) {
    const key=sceneKey(scene); if(!builders[key])return;
    const enters=Array.from(root.querySelectorAll('.tech-enter'));
    // Each diagram owns its scoped elements; all numbers/timings are fixed.
    enters.forEach((element,i)=>tl.from(element,{opacity:0,y:i%3===0?24:0,x:i%3===1?-22:0,scale:i%3===2?.975:1,duration:.65,ease:['power3.out','sine.out','expo.out'][i%3]},start+.22+Math.min(i*.085,1.45)));
    const pulseTargets=Array.from(root.querySelectorAll('.tech-check,.tech-tag'));
    pulseTargets.slice(0,6).forEach((e,i)=>tl.from(e,{scale:.9,duration:.45,ease:'back.out(1.1)'},start+1.4+i*.12));
    root.querySelectorAll('.tech-arrow').forEach((e,i)=>{
      tl.from(e,{scaleX:0,transformOrigin:'left center',duration:.6,ease:'sine.out'},start+.5+i*.12);
      const dot=e.querySelector('.tech-flow-dot');if(!dot)return;
      tl.from(dot,{opacity:0,duration:.25,ease:'sine.out'},start+2+i*.12);
      const distance=Math.max(20,e.clientWidth-15);
      tl.to(dot,{x:distance,duration:Math.min(1.25,Math.max(.5,duration*.12)),repeat:Math.max(0,Math.floor((duration-3.7)/2.4)-1),repeatDelay:1.2,ease:'none'},start+2+i*.12);
    });
    if(key==='S25') {
      const sample=root.querySelector('.tech-life-sample');if(sample)tl.from(sample,{scale:.97,x:35,duration:.8,ease:'power3.out'},start+Math.min(4,duration*.4));
    }
    if(key==='S28') {
      const priority=root.querySelector('.tech-priority');if(priority)tl.from(priority,{x:-28,backgroundColor:'#FDFEFF',duration:.8,ease:'power2.out'},start+Math.min(3,duration*.32));
    }
    if(key==='S29') {
      const meter=root.querySelector('.tech-meter i');if(meter)tl.from(meter,{scaleX:0,duration:Math.min(2.1,duration*.22),ease:'power1.inOut'},start+1.7);
      const conflict=root.querySelector('.tech-cow-conflict');if(conflict)tl.from(conflict,{borderColor:'#D8E2EF',backgroundColor:'#F4F7FC',duration:.7,ease:'sine.out'},start+Math.min(5.2,duration*.55));
    }
    if(key==='S30')root.querySelectorAll('.tech-attempt').forEach((e,i)=>tl.from(e,{scale:0,duration:.3,ease:'back.out(1.2)'},start+1.4+i*.3));
    if(key==='S35') {const fill=root.querySelector('.tech-range i');if(fill)tl.from(fill,{scaleX:0,duration:1.5,ease:'power2.out'},start+2);}
    if(key==='S36') {const result=root.querySelector('.tech-result');if(result)tl.from(result,{scale:.7,y:15,duration:.8,ease:'expo.out'},start+Math.min(4,duration*.4));}
    if(key==='S38') {
      root.querySelectorAll('.tech-heat-cell').forEach((e,i)=>tl.from(e,{scale:.8,duration:.48,ease:'power3.out'},start+.8+i*.09));
      root.querySelectorAll('.tech-heat-cell.cool').forEach((e,i)=>tl.to(e,{backgroundColor:'#F4F7FC',color:'#536B84',borderColor:'#D9E3EE',duration:1.3,ease:'sine.inOut'},start+Math.max(3,duration-3.7)+i*.06));
      root.querySelectorAll('.tech-worker-slots i:nth-child(n+5)').forEach((e,i)=>tl.from(e,{scale:.55,opacity:.3,duration:.55,ease:'power2.out'},start+2.8+i*.17));
    }
    if(key==='S39')root.querySelectorAll('.tech-contexts i').forEach((e,i)=>tl.from(e,{y:14,scale:.8,duration:.45,ease:'power3.out'},start+2+i*.3));
    if(key==='S40') {
      tl.from(root.querySelector('.tech-peer-lines'),{opacity:0,duration:.8,ease:'power2.out'},start+.35);
      const paths=root.querySelectorAll('.tech-peer-lines path');paths.forEach((e,i)=>{const length=e.getTotalLength();e.style.strokeDasharray=e.classList.contains('peer')?'9 10':e.classList.contains('control')?'3 9':String(length);if(!e.classList.contains('peer')&&!e.classList.contains('control'))tl.from(e,{strokeDashoffset:length,duration:1.1,ease:'sine.inOut'},start+.6+i*.25);});
      const dot=root.querySelector('.tech-peer-dot');
      if(dot){
        tl.from(dot,{opacity:0,duration:.3,ease:'sine.out'},start+2);
        const route=root.querySelector('.tech-peer-lines .peer');
        const routeLength=route.getTotalLength(),travel=Math.min(3,duration*.35),steps=24;
        for(let step=1;step<=steps;step++){
          const point=route.getPointAtLength(routeLength*step/steps);
          tl.to(dot,{attr:{cx:point.x,cy:point.y},duration:travel/steps,ease:'none'},start+2+(step-1)*travel/steps);
        }
      }
    }
    if(key==='S42')root.querySelectorAll('.tech-bar').forEach((e,i)=>tl.from(e,{scaleX:0,duration:1.25,ease:'power3.out'},start+1+i*.15));
    if(key==='S43'){const logo=root.querySelector('.tech-unified img');if(logo)tl.to(logo,{y:-6,duration:2,ease:'sine.inOut',yoyo:true,repeat:Math.max(0,Math.floor((duration-2)/4)-1)},start+2);}
  }
  window.HLSTechnical={render,animate,supported:Object.keys(builders)};
}());
