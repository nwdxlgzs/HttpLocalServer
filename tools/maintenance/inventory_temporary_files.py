"""Read-only inventory: candidate paths only; never removes project files."""
from pathlib import Path
from datetime import datetime
import json,os,re

ROOT=Path(__file__).resolve().parents[2];VIDEO=ROOT/'AnimationScriptWorkSpace'
records=[];seen=set()

def size_of(path):
    if path.is_file():return path.stat().st_size,1
    total=count=0
    for current,dirs,files in os.walk(path,followlinks=False):
        dirs[:]=[d for d in dirs if not (Path(current)/d).is_symlink()]
        for name in files:
            try:p=Path(current)/name;total+=p.stat().st_size;count+=1
            except OSError:pass
    return total,count

def add(path,category,reason,impact,condition='停止占用它的进程后'):
    path=Path(path).resolve()
    if not path.exists() or str(path).lower() in seen:return
    # Credentials are never read or inventoried, even when nested.
    if '.privatespace' in {part.lower() for part in path.parts}:return
    seen.add(str(path).lower());size,count=size_of(path)
    records.append({'path':str(path),'relativePath':str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else None,
      'kind':'directory' if path.is_dir() else 'file','category':category,'bytes':size,'files':count,
      'reason':reason,'deletionImpact':impact,'deleteAfter':condition,'manuallyDeleteOnly':True})

for path in ROOT.iterdir():
    if path.is_dir() and re.fullmatch(r'build(?:[-_].*)?',path.name,re.I):
        add(path,'可重建依赖与构建','CMake 构建输出','下次构建需重新配置和编译')
add(ROOT/'.deps','可重建依赖与构建','依赖展开、开发工具、测试数据、实验脚本与日志的集中目录',
    '应用已发布 EXE 不依赖此目录；CMake 可从 third_party 离线重建固定依赖；浏览器测试/视频审查的 npm 工具可能需另行恢复')
for path in [VIDEO/'node_modules',ROOT/'tools/dependencies/preview-tooling']:
    add(path,'可重建依赖与构建','npm 开发依赖与预览制作工具','需要时按锁文件重新安装；停止 Studio 后再清理')
for name in ['.hyperframes','.thumbnails','.waveform-cache','.scaffold','capture']:
    add(VIDEO/name,'缓存与临时输出','视频制作缓存、分析中间文件、采集日志或原始操作录屏副本',
        '成片与实际使用的 assets/capture 素材保留；Studio 索引/波形/缩略图会重建')
for path in (VIDEO/'demo').glob('state-*'):
    add(path,'缓存与临时输出','隔离采集服务运行状态','只丢弃制作演示状态；不影响主项目 config.json 或真实共享目录')
for path in (VIDEO/'demo').glob('runtime*'):
    add(path,'缓存与临时输出','隔离演示运行目录','重新采集时重建')
for path in (VIDEO/'exports').glob('.*.hf-transaction-*'):
    add(path,'缓存与临时输出','HyperFrames 原子导出事务中间目录','渲染过程中禁止删除；成功导出后通常自动移除','确认全部导出结束且进程退出后')
for path in (VIDEO/'tools').glob('audio-delivery-*'):
    add(path,'缓存与临时输出','音频处理暂存','混音结束后可删')
for path in (VIDEO/'tools').glob('*.tgz'):
    add(path,'可重建依赖与构建','制作工具下载归档','本地 tools/bin 保留时不影响使用；重装工具可能重新下载')
for path in [VIDEO/'assets/capture-retina',VIDEO/'assets/capture-before-retina',
             VIDEO/'assets/audio/before-4k-mix',VIDEO/'assets/audio/before-music-rebalance',
             VIDEO/'assets/audio/before-pronunciation',VIDEO/'assets/audio/local-sapi',VIDEO/'assets/audio/auditions']:
    impact='历史或重复素材；不影响当前视频成片'
    if path.name=='before-4k-mix':impact+='；audio_delivery.py 依赖此基准，删后需从当前正确旁白重设基准'
    if path.name=='before-music-rebalance':impact+='；audio_rebalance.py 依赖此基准，删除后不可直接重跑，否则会重复增益；须先改配置和重设基准'
    add(path,'历史素材与核验记录（可选）','历史回退、比较或采集暂存',impact)
for path in (VIDEO/'assets/audio/brand').glob('asr-cache'):
    add(path,'缓存与临时输出','辅助发音识别缓存','保留实际发音修正音频与报告；重做辅助识别时需重建缓存')
add(VIDEO/'review','历史素材与核验记录（可选）','抽帧、联系表、旧剪辑和检查报告',
    '不影响 exports/ 成片；会失去核验依据与旧稿，建议先保留最新 JSON 报告；当前任务进行时不可删')

# Discover Python bytecode and genuine local cache directories; do not descend
# into dependencies, source vendors or private inputs just to duplicate entries.
skip={'.PrivateSpace','.git','.deps','node_modules','third_party','.hyperframes'}
for current,dirs,files in os.walk(ROOT,followlinks=False):
    dirs[:]=[d for d in dirs if d not in skip and not (Path(current)/d).is_symlink()]
    for name in list(dirs):
        if name in {'__pycache__','.pytest_cache','.mypy_cache','.ruff_cache','.hls-cache'}:
            add(Path(current)/name,'缓存与临时输出','解释器、静态工具或可再生成缓存','需要时自动重建');dirs.remove(name)
    p=Path(current)
    if p in [ROOT,VIDEO,VIDEO/'scripts',VIDEO/'tools']:
        for name in files:
            if name.endswith(('.log','.tmp','.temp','.bak')):
                add(p/name,'缓存与临时输出','日志或暂存文件','不影响源码，丢弃调试记录')

# List only external temporary folders actually referenced by this project's
# rendering logs, rather than sweeping other projects' Windows TEMP entries.
external=set()
for log in (VIDEO/'review').glob('*render*.log'):
    content=log.read_text(encoding='utf-8-sig',errors='replace').replace('\\\\','\\')
    for value in re.findall(r'[A-Za-z]:\\[^\r\n"<>]*?\\hf-render-[A-Za-z0-9]+',content):
        if Path(value).is_dir():external.add(value)
for path in external:
    add(path,'当前工程引用的系统临时目录','本项目渲染日志引用的 Windows 临时工作目录',
        '只在确认导出结束后删除；若进程仍占用，保留至其退出','全部 HyperFrames 渲染结束后')

records.sort(key=lambda r:(r['category'],r['path'].lower()))
deps=[]
if (ROOT/'.deps').exists():
    for path in sorted((ROOT/'.deps').iterdir()):
        if path.is_dir():
            size,count=size_of(path);deps.append({'path':str(path.resolve()),'bytes':size,'files':count})
retained=[
 ('src/、web/、cmake/、CMakeLists.txt','源码和构建逻辑'),
 ('third_party/、licenses/、NOTICE、LICENSE','固定离线构建输入、模型与许可'),
 ('tests/','测试源码；只有 __pycache__ 属于缓存'),
 ('docs/、docs/verification/','开发文档与发布核验材料，非临时目录'),
 ('dist/','已发布程序，非临时构建缓存；需要运行/分发时保留'),
 ('config.json 及真实共享目录中的运行状态','用户设置、上传文件和持久化数据，不能按名称当垃圾删除'),
 ('.PrivateSpace/','私有接口和凭据，不纳入扫描和清理'),
 ('AnimationScriptWorkSpace/exports/','正式视频、字幕、封面，保留其正常文件'),
 ('AnimationScriptWorkSpace/PromotionalPosters/','九张正式宣传海报、整版图和可编辑源稿'),
 ('AnimationScriptWorkSpace/data/、scripts/、styles/、js/、edits/、index.html','可编辑视频工程'),
 ('AnimationScriptWorkSpace/assets/capture/、logo.svg、fonts/、vendor/','当前视频引用的素材；缺失将影响预览和重导'),
 ('AnimationScriptWorkSpace/assets/audio/cloud/、brand/、S*.wav 等镜头音频、voice-*、music-*、master-*、字幕','已付费语音缓存、正确读音与当前成片音轨；不要当作普通临时文件'),
 ('AnimationScriptWorkSpace/demo/（除 state-* / runtime*）','重新采集界面需要的原创示例资料和站点'),
 ('AnimationScriptWorkSpace/tools/bin/','当前工作站编码与探测工具，删后需恢复工具才能重导'),
]
categories=sorted({r['category'] for r in records})
def human(value):return f'{value/1024**3:.2f} GiB' if value>=1024**3 else f'{value/1024**2:.1f} MiB'
stamp=datetime.now().isoformat(timespec='seconds')
payload={'generatedAt':stamp,'root':str(ROOT),'readOnly':True,'deletedFiles':0,
 'note':'候选清单，不是删除授权。含可重建依赖和可选历史材料，不能把所有项都当无代价临时文件。父子路径不要重复统计。',
 'candidates':records,'depsDirectoryBreakdown':deps,'keep':[{'path':p,'reason':s} for p,s in retained]}
(ROOT/'CleanupInventory.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
lines=['# 项目清理清单','',f'扫描时间：{stamp}。根目录：`{ROOT}`。本次只统计，没有删除文件。','',
'先结束渲染、采集、测试和 Studio 预览，再手动清理。下列条目区分缓存、可重建依赖和可选历史材料；不要一键全删。当前占用中的 `.hf-transaction-*` / `hf-render-*` 尤其不能提前删除。',
'','完整机器可读路径和每项影响见 `CleanupInventory.json`。所有路径均来自本次实际存在的目录/文件，没有用通配符代替实际路径。','']
for category in categories:
    rows=[r for r in records if r['category']==category]
    lines+=['## '+category,'',f'共 {len(rows)} 项；按条目直接相加约 {human(sum(r["bytes"] for r in rows))}。如果删除父目录，不要再次叠加其子路径大小。','', '| 实际路径 | 体积 | 用途及删除影响 |','|---|---:|---|']
    for row in rows:lines.append(f'| `{row["path"]}` | {human(row["bytes"])} | {row["reason"]}。{row["deletionImpact"]}。{row["deleteAfter"]}。 |')
    lines.append('')
lines+=['## .deps 内部目录明细','','`.deps` 是开发暂存集中区，可整目录清理；以下仅帮助选择性保留，不应与父目录重复计算。固定构建原料留在 `third_party/`。','', '| 实际路径 | 体积 |','|---|---:|']
for row in deps:lines.append(f'| `{row["path"]}` | {human(row["bytes"])} |')
lines+=['','## 请保留','','| 路径 | 原因 |','|---|---|']
for path,reason in retained:lines.append(f'| `{path}` | {reason} |')
lines+=['','## 清理后的恢复','','- C/C++ 构建：保留固定 `third_party/` 输入后，重新运行原 CMake 配置和构建命令。',
'- 视频工具：在 `AnimationScriptWorkSpace` 按 `package-lock.json` 执行 `npm ci --ignore-scripts --omit=optional`，本机工具按 `scripts/setup_tools.py` 恢复。',
'- Python `__pycache__`、Studio 波形和缩略图会自动重建。',
'- 历史配音/采集备份一旦删除，只保留当前状态；特别是 `before-music-rebalance/` 是调音的固定输入，删后不要直接再跑增益脚本。',
'- 没有把系统全局 npm、Chrome、Python 或其他项目临时目录算进本项目；系统临时目录仅列本项目日志引用且仍存在的路径。','']
(ROOT/'CleanupInventory.md').write_text('\n'.join(lines),encoding='utf-8')
print(json.dumps({'candidates':len(records),'categories':{c:len([r for r in records if r['category']==c]) for c in categories},'bytes':sum(r['bytes'] for r in records),'deleted':0},ensure_ascii=False),flush=True)
