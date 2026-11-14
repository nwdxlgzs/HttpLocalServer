# 素材与制作依赖

| 内容 | 来源与使用方式 |
|---|---|
| HttpLocalServer Logo | 项目已有 `resources/branding` SVG，复制为 `assets/logo.svg`；作者 github@nwdxlgzs。 |
| 产品界面与录屏 | 当前 `dist/HttpLocalServer-windows-x64.exe` 3.5.1，隔离演示资料；SHA-256、端口、实际设置、分辨率、录屏时长记录在 `capture-manifest.json`。 |
| 演示文档、图片与站点 | `demo/` 下为制作本片创建的示例，采集不使用私人目录。 |
| 手机扫码演示 | 自行生成编码 `https://example.com` 的占位二维码，PNG / SVG 均经实际解码校验。模拟扫码后展示真实的 HttpLocalServer 手机端管理器截图 `mobile.png`，并非展示 example.com 网站；证据见 `qr-demo.json`。 |
| 技术图形与动画 | 本工程原创 HTML/CSS/SVG 与 GSAP 编排；事实与基准出处见 `assets/diagrams/README.md`。 |
| 当前配音 | 用户授权的 DeepSeek TTS，stella 音色；请求凭据只由脚本从工程外私有目录读取，未进入素材、日志或页面。提供商条款适用于其服务输出。 |
| 配乐 | `scripts/audio_delivery.py` 原创合成 108 BPM 配乐，柔和键盘、铺底、贝斯和克制节拍；固定随机种子，不引用外部歌曲或采样。48 kHz 立体声，旁白期间自动降低 6 dB。 |
| 字体 | 本工作站合法安装的 Microsoft YaHei / Segoe UI / Consolas 的本地制作子集。仅保留在此工作站，已加入忽略规则；分享工程前应删除或替换为可再分发字体。导出视频中的文字已栅格化。 |
| HyperFrames | npm `hyperframes@0.9.44`，独立视频制作 devDependency；许可证以安装包为准。 |
| GSAP | 使用 HyperFrames 随附 GSAP 3.15.0 并复制其 min.js；源文件保留自身版权头和许可地址。 |
| FFmpeg / FFprobe | 当前工作站视频编码工具，不链接进 HttpLocalServer、不随产品发布。具体来源、版本与 SHA-256 在 `tools/manifest.json`；对应安装包的通知随工具保留。 |

HttpLocalServer 使用 Apache 2.0 并不使所有第三方软件或字体自动获得 Apache 2.0 许可。本工程制作工具和本机字体不属于产品发行依赖。对外分发建议使用 `exports/` 中的 MP4、SRT、封面，源工程分享需保留各依赖许可通知并处理字体资产。
