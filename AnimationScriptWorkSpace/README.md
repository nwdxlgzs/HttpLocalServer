# HttpLocalServer 产品宣传视频

基于 HyperFrames 0.9.44 / GSAP 3.15.0 的可编辑工程。产品版本为 3.5.1，分镜依据项目根目录的 `AnimationScriptPlan.md`。网页文件、脚本、音频和素材均保留，后续可以修改文案、换镜头、换声音再导出。

## 成片

| 版本 | 时长 | 画面 | 文件 |
|---|---:|---|---|
| 详细主片 | 约 5 分 20 秒 | 3840×2160 / 30fps | `exports/HttpLocalServer-main-4K.mp4` |
| 产品概览 | 58.3 秒 | 3840×2160 / 30fps | `exports/HttpLocalServer-overview-4K.mp4` |
| 短片 | 24.33 秒 | 3840×2160 / 30fps | `exports/HttpLocalServer-teaser-4K.mp4` |
| 竖版 | 34.73 秒 | 2160×3840 / 30fps | `exports/HttpLocalServer-vertical-4K.mp4` |

新版提高旁白音量，加入原创 108 BPM 立体声配乐，说话时配乐自动避让。最新 v3 混音相对首次 4K 混音再把 BGM 提高约 4.3～4.4 LU，旁白变化控制在 0.2 LU 以内。音轨为 48 kHz 双声道 AAC 320 kbps。4K 直接从 HTML 动画以 DPR 2 渲染，文字和矢量图按目标像素绘制；21 张产品截图已按双倍像素重采。4 段既有操作录屏仍是 1600×900 源素材，不声称所有素材原生 4K。旧 1080p 文件保留并同步新混音。

节奏按实际语音长度安排，44 镜主片保留全部主题，相邻镜头交叠 0.35 秒。原 480 秒规划备份为 `data/scenes-long-form.json`，当前时间以 `data/scenes.json` 为准。二维码分镜展示网页 / CLI 两种入口和手机扫码打开；二维码内容以 `https://example.com` 占位，手机打开过程为动画模拟，打开后的内容使用真实的 HttpLocalServer 手机端文件管理界面。

旁白使用用户授权的付费 TTS 接口生成，音色为 stella。配乐为本地原创合成。字幕既烧录在画面中，也提供独立 SRT。音轨、生成方式和字幕对齐的说明见 `assets/audio/README.md`。云接口没有提供词级时间戳，字幕采用实测音轨与静音辅助的短语估计，不声称 ASR 精确对齐。

读音修正：`HttpLocalServer` 统一读作 **H T T P Local Server**，`ZIP` 使用英文 **zip**，`Wasm` 读作 **Web Assembly**。另对几处字母缩写明确拼读。三个主要术语的合辑试听为 `assets/audio/brand/pronunciation-audition.wav`；18 镜修正的裁点、时间适配与字幕不变校验在 `assets/audio/brand/pronunciation-report.json`。

## 打开与修改

在本目录运行：

```powershell
npm run dev
npm run status
```

以终端返回的 **Studio 地址**打开编辑预览；当前工作站为 `http://127.0.0.1:3002/#project/AnimationScriptWorkSpace`。预览服务保持后台运行，页面随文件变化重新加载。Studio 播放的主片入口是 `index.html`。其他剪辑入口在 `edits/`，可用相应渲染命令单独导出。

主要编辑位置：

- `data/scenes.json`：四版分镜、标题、旁白与时间。调整后执行 `npm run build`。
- `scripts/build.mjs`：镜头构图、真实界面与机制示意的编排。
- `styles/film.css`、`styles/fonts.css`：横竖版样式与本机字体子集。
- `js/film.js`：暂停的 GSAP 总时间线、入场与转场。
- `js/technical.js`、`styles/technical.css`：17 个机制示意分镜。
- `assets/capture/`：21 张真实界面截图和 4 段真实操作录屏。
- `assets/audio/`：四版混音、旁白、配乐、逐镜语音与字幕；原 SAPI 音轨在 `local-sapi/`。
- `DESIGN.md`：配色、版式、动效和事实表述规则。
- `STORYBOARD.md`、`SCRIPT.md`：镜头索引与配音稿。

生成的 HTML 是可直接编辑的标准 HyperFrames 组成，但再次 `npm run build` 会按脚本和分镜数据重建。因此长期修改应落在数据、样式和构图脚本中。

## 重新导出

```powershell
npm run build
node scripts/render-all.mjs
```

按短片、概览、竖版、主片依次导出。只做某一版：

```powershell
node scripts/render-all.mjs overview
node scripts/render-all.mjs main
node scripts/render-all.mjs vertical teaser
```

默认 4 个渲染工作进程、GPU 编码、35 Mbps 视频目标码率。内存紧张时在 PowerShell 设置 `$env:HLS_RENDER_WORKERS='2'`。四版分辨率和文件名集中在 `data/delivery.json`。详细日志位于 `review/render-*-4k.log`。`scripts/hf.mjs` 明确传递本地 FFmpeg、FFprobe 与 Chrome 路径。渲染不需要访问 TTS 服务，已生成声音可离线复用。

只改音频时：`python scripts/audio_rebalance.py` 会从 `assets/audio/before-music-rebalance/` 固定基准重混当前 v3，不请求 TTS；随后 `python scripts/refresh_audio.py` 拷贝原视频流并编码当前音轨，再运行 `python scripts/verify_exports.py`。刷新旧 1080p 使用 `python scripts/refresh_audio.py --legacy-1080p`。`audio_delivery.py` 是历史 v2 混音构建器，不应覆盖当前版本。字幕显示时间有变化时仍应重新构建与渲染。

首次在当前 Windows 环境恢复工具：

```powershell
npm ci --ignore-scripts --omit=optional
python scripts/setup_tools.py
python scripts/fonts.py
```

需要 Node、Chrome、Python 及 imageio-ffmpeg、fontTools[woff]、numpy。新版混音另需 scipy、soundfile、pyloudnorm；视频抽帧拼图使用 Pillow。字体脚本针对当前工作站已安装的微软雅黑、Segoe UI、Consolas；其他系统应提供合法可用字体并调整声明。Playwright 仅用于制作检查与界面采集，不是成片运行依赖。

## 核验

```powershell
node scripts/hf.mjs check --samples 9 --budget 90s --no-craft --json
node scripts/review.mjs
python -X utf8 scripts/audio_verify.py
python -X utf8 scripts/audio_cloud_verify_private.py
python scripts/verify_exports.py
```

核验报告在 `review/`。`review.mjs` 逐镜保存画面，检查文字边界、资源加载、倒序跳转后画面一致性；录屏实际帧以 HyperFrames 快照及导出视频再抽帧检查。`verify_exports.py` 检查流编码、尺寸、帧率、音轨、时长，并保存代表帧。依赖环境和实测结果在 `PRODUCTION.md`。

## 素材与事实范围

演示界面来自当前原生程序在隔离目录和临时回环端口运行的真实采集，未改造产品 UI，采集参数记录在 `capture-manifest.json`。演示使用共享写入、主机 ZIP 预览等显式设置；不代表默认全部开启。目录中没有用户的私人文件。

网络、缓存、权限、PCDN 等是机制示意，终端也标注为启动示意。性能数字只对应已有短时本机回环报告，保留条件说明；macOS 源码适配、Windows 7 兼容目标没有被包装成实机验证结果。正文同时介绍 FileTrans、HTTP 静态托管、ZIP 应用和受控脚本能力。

素材许可及本机依赖边界见 `ASSETS.md`。本工程没有把 TTS 凭据、产品源码或产品依赖打包到视频运行页面中。

九宫格宣传海报位于 `PromotionalPosters/`，包含九张独立 2160×2160 PNG/JPG、6480×6480 整版图、预览和 HTML/CSS 源稿。手动清理清单在项目根目录 `CleanupInventory.md` / `.json`，列出实际路径、体积、删除影响及应保留文件；生成脚本只读取和统计，没有删除操作。
