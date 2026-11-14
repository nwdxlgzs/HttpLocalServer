# HttpLocalServer 标志

网页服务窗口中的 `</>` 表示 HTTP 内容服务；下方连线和节点表示本机向局域网提供服务。蓝色底与白色轮廓保证浅深色标签栏都有边界，薄荷绿保留可辨认的内容层。它用于整个 HttpLocalServer 项目，涵盖 Server 和 FileTrans 模式。

- `logo.svg`：彩色矢量母版。
- `logo-mono.svg`：`currentColor` 单色版本。
- `logo.png`：512 px 透明背景导出。
- `favicon.ico`：16、24、32、48、64、128、256 px；小尺寸使用 DIB，兼容 IE11 / Windows 7。
- `favicon-32.png`：32 px 单尺寸图标。
- `apple-touch-icon.png`：180 px 不透明触屏图标。
- `*.gz`、`manifest.json`：编入可执行文件的压缩资源与校验信息。

FileTrans 的 `/favicon.ico`、`/favicon.svg`、`/apple-touch-icon.png` 均从程序内读取；支持 gzip、HEAD、ETag / 304 和浏览器缓存。Server 模式继续使用所托管网站的图标。Windows EXE 也嵌入同一份 ICO。图标不会取代网页左上角的装饰红黄绿灯。

修改母版后执行 `python tools/assets/make_icons.py`；仅该导出步骤需要开发用 Pillow 和 CairoSVG，普通构建不需要图像工具或联网。更新图标形状时，也递增 `web/index.html` 图标链接的版本参数，以避开旧缓存。

标志及生成脚本采用项目 Apache-2.0 许可，作者 github@nwdxlgzs。
