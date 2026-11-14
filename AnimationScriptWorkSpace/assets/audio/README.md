# 配音与配乐

当前连续版分别使用 `master-main.wav`（319.966667 秒）、`master-overview.wav`（58.3 秒）、`master-teaser.wav`（24.333333 秒）及 `master-vertical.wav`（34.733333 秒）。当前 master / music 为立体声、48 kHz、16-bit PCM，voice 为单声道同规格；master 已经混合旁白与配乐。不要再同时叠加相应 `voice-*` 和 `music-*`，否则会重复播放。

`voice-*.wav` 是纯旁白，`music-*.wav` 是配乐分轨，方便后期替换或重新调音。`S01.wav` 等逐镜音频只有旁白本体，没有镜头开头的静音；放置位置见根目录 `audio-manifest.json` 的 `voiceOffset`。当前每镜在 0.233333 秒进入旁白，普通镜头基本留 0.5 秒尾部；信息阅读与交互演示另有明确 `visualHold`。所有镜头至少留 0.35 秒尾部，相邻画面交叠 0.35 秒，旁白互不重叠，也没有截断任何旁白来凑时长。扫码镜头留出可见的两轮扫码演示时间，其语音尾部较长。

## 来源

- 当前旁白：按用户授权调用 **DeepSeek TTS / stella**。保留原 65 条文案和音频，只为新增加的“扫一扫，直接打开。”请求一次 9 字短句，在三个短版复用，四版共 68 个镜头。首条先试听；相同文案与音色复用缓存。接口未提供可见价格、余额或本次收费，因此这里不报告具体费用。`cloud/` 保留原始 24 kHz WAV 与只含公开文本和技术信息的元数据；流式 WAV 的长度占位字段已修正为实际长度。
- 本地回退：Windows `System.Speech` / SAPI，**Microsoft Yaoyao** 中文语音，全套成片音轨、逐镜语音、manifest 与事件字幕保存在 `local-sapi/`。`auditions/` 保留 Huihui、Kangkang 与 Yaoyao 的比较样音。
- 当前音乐：`scripts/audio_delivery.py` 原创 108 BPM Fmaj7 / Dm9 / Bbmaj7 / Cadd9 配乐，柔和键盘、铺底、贝斯、克制的鼓与打击乐；波形、包络及固定种子噪声自行合成，无外部音乐或采样。空隙目标 -27 LUFS，旁白期间降低 6 dB，平滑进入与恢复；输出立体声。历史 `audio_build.py` 的轻微环境铺底保留在备份中。
- 原始本地素材：`raw/` 保存原生 16 kHz SAPI WAV 与 `SpeakProgress` 事件。之后再由 Python 转换至 48 kHz，避免由 SAPI 输出重采样导致语音事件时间错误。
- 当前字幕：显示批准的中文文案，以实测音轨长度、短语长度与静音位置估计短语边界，明确标注 **estimate / not ASR**。云接口只返回音频，没有词级时间戳；`*-speech-events.json` 因而为空，绝不借用本地声音的事件冒充云声音。原有 SAPI 事件字幕完整保存在回退目录。
- 时长：由缓存音频的实际测长重新安排镜头，发音修订后仅 4 镜有轻微保持音高变速，最大 1.099995 倍，镜头与字幕时长不变。主片保留 44 镜；旧 480 秒历史计划位于 `data/scenes-long-form.json`，不再限制当前成片长度。每镜准确倍率见根目录 `audio-manifest.json` 的 `timeStretchRate`。

凭据仅在程序内从工作区外的 `.PrivateSpace` 读取，放入请求头。不输出、复制、散列或记录凭据；错误响应原文与完整请求头均不写入日志。`private-data-scan.json` 报告工作区文本的凭据完全匹配检查，不包含凭据值或片段。这是素材制作来源说明，不代替任何第三方语音组件自身的许可条款。

## 重新生成

重混当前 4K 配乐及提高音量，无需联网或付费请求，保留所有已校正读音、分镜和字幕：

```powershell
python -X utf8 AnimationScriptWorkSpace/scripts/audio_rebalance.py
python -X utf8 AnimationScriptWorkSpace/scripts/audio_verify.py
python -X utf8 AnimationScriptWorkSpace/scripts/audio_cloud_verify_private.py
```

`before-4k-mix/` 保存首次 4K 调音前的正确发音旁白，`before-music-rebalance/` 保存首次 4K 混音作为 v3 固定输入。二者不被重复覆盖。当前 `audio_rebalance.py` 再把音乐提高 4.5 dB，经局部前瞻峰值控制后，实际 BGM 提高主片 4.439 LU / 概览 4.405 LU / 短片 4.410 LU / 竖版 4.321 LU。旁白只变化 -0.061 / -0.095 / -0.090 / -0.179 LU。时间长度不变，4 倍过采样真峰值限制在 -1.2 dBTP。详细数值、hash 与字幕不变核验在 `delivery-mix-report.json`。导出 AAC 320 kbps；没有新增 TTS 请求。

`audio_cloud_mix.py` 用来从缓存重建逐镜语音与旧混音，会覆盖当前 master；`audio_delivery.py` 是 v2 构建器。仅调音时使用 `audio_rebalance.py`，不要执行历史重建流程。清理掉固定基准后，脚本会拒绝对已经应用 v3 的音轨再次增益，避免重复加大声音。

重新请求批准的云旁白（存在有效缓存时复用；无缓存的请求会使用付费服务）：

```powershell
python -X utf8 AnimationScriptWorkSpace/scripts/audio_cloud.py --film all --voice stella --max-characters 1800
```

`audio_build.py` 是原长版的历史本地制作脚本，仍带 480/90/30/45 秒片长与原 65 条旁白定义；直接运行会覆盖当前音轨并重建历史长度，不能用来发布当前紧凑版。恢复历史版本可使用 `local-sapi/` 保留的文件。若当前版需要换成本地声，应按最新 `data/scenes.json` 的 68 镜重新安排、生成及混合，不能直接把旧长版 WAV 替换当前 master。

本地生成需要 Python 3.10（含 numpy）及支持 `System.Speech` 的 PowerShell 7。云音频混合另外使用 scipy 和 librosa 的频谱/相位声码器。脚本优先使用 PATH 中的 `pwsh`。当前全部旁白文字和镜头时间以 `data/scenes.json` 为准；原始长剧本位于项目根目录 `AnimationScriptPlan.md`。

字幕分别提供 UTF-8 SRT 和 `*-captions.json`。独立校验结果见 `verification.json`，包含当前配音引擎、时长、峰值、字幕数量、镜尾最小/最大余量及扫码演示例外。校验覆盖 68 段旁白、12 个全长音轨、字幕顺序、字幕时域与分镜文案一致性。

## 技术词发音修订

品牌读作 H T T P Local Server；ZIP 读英文 zip；Wasm 旁白按用户选择读 Web Assembly，画面与字幕保留 Wasm。`brand/pronunciation-audition.wav` 可依次试听这三项。完整标准表、裁点、变速与原画面/字幕 hash 对照见 `brand/pronunciation-report.json`。英文片段的 ASR 仅是辅助，未声称人工听审。

18 个命中镜头使用 `cloud/pronunciation-overrides.json` 的预处理声音，重混脚本会自动保留 119 组字幕的原文及时间。50 个无关镜头 WAV 字节不变。修订前紧凑版保存在 `before-pronunciation/`。
