# HttpLocalServer 九宫格宣传海报

按 `01 → 09` 从左到右、从上到下发布。每张均可独立使用，统一采用项目 Logo、蓝 / 薄荷绿 / 深墨色与实际产品截图。

- `images/`：9 张 **2160×2160** PNG，另附同名高质量 JPG。
- `HttpLocalServer-nine-grid.png` / `.jpg`：**6480×6480** 的完整九宫格。
- `preview.jpg`：1620×1620 轻量预览。
- `posters.html`：可编辑 HTML/CSS 源稿。
- `render.py`：本机 Chrome / Playwright 导出脚本；在视频工程根目录执行 `python -X utf8 PromotionalPosters/render.py`。
- `verification.json`：9 张输出的尺寸、SHA-256、文字边界与图文区域检查。

| 顺序 | 主题 | 文件前缀 |
|---|---|---|
| 01 | 本地 HTTP 服务，FileTrans 与 Server 双模式 | `01-local-http-server` |
| 02 | 网页 / CLI 二维码与手机文件管理器 | `02-scan-and-connect` |
| 03 | 标签、路径、列表、检索与文件协作 | `03-familiar-file-manager` |
| 04 | Office、文档和媒体预览 | `04-preview-before-download` |
| 05 | 写入权限、主机管理与依赖冻结 | `05-sharing-under-your-control` |
| 06 | Range、热点缓存与可选 PCDN | `06-large-files-steady-delivery` |
| 07 | HTTP 静态站点与 ZIP 工作区应用 | `07-real-local-http-server` |
| 08 | QuickJS-NG、WAMR 与受控本机组件 | `08-script-and-wasm-ecosystem` |
| 09 | 三平台源码支持、作者与 Apache 2.0 | `09-one-network-many-possibilities` |

二维码内容为用户指定的 `https://example.com` 占位；手机图为实际文件管理器。预览、权限和 Server 图来自本机隔离演示。机制图是原创说明，不是性能测量。macOS 标明源码适配，未声称实机测试。字体使用本工作站字体，交付图片已经栅格化；分享源稿时遵守字体与第三方组件许可。
