# HttpLocalServer

<img src="resources/branding/logo.png" width="96" height="96" alt="HttpLocalServer 标志">

C++17 编写的局域网 HTTP 服务，当前版本 3.5.1，作者 github@nwdxlgzs，使用 Apache-2.0 许可。默认启动文件传输模式，监听 `0.0.0.0:8000`，只读共享当前工作目录，默认允许预览。压缩网页、图标、SQLite、Magika 模型和第三方声明均内置，运行时只部署一个可执行文件；配置与持久化运行状态在运行时独立保存。

项目标志由网页服务窗口、`</>` 与局域网节点构成，覆盖 Server / FileTrans 两种用途。彩色/单色 SVG、PNG、IE11 兼容 ICO 和触屏图标位于 [resources/branding](resources/branding/README.md)。FileTrans 内置 favicon，Windows EXE 使用同一图标；Server 模式保留托管网站自己的图标。

默认使用工作目录的 `config.json`，缺省路径不存在时，首次修改主机设置才生成；显式指定的新配置来源会初始化；可用 `--config PATH` 指定来源，CLI 参数优先。仓库提供[默认配置模板](examples/config-filetrans.json)，本机运行配置不提交。主机面板设置自动保存，配置自身在任何目录或挂载中默认隐藏；勾选“显示隐藏”后以冻结项展示，禁止网页读取与修改。格式和保护边界见 [JSON 配置](docs/configuration.md)。

3.0 新增上传断点续传与内容核验、多选/批量操作、传输任务中心、主机/每 IP 流控、持久化配额与姓名审核、可选回收站，以及 QuickJS-NG 扩展。Server 在 `/_hls/` 提供同一文件管理器，并增加默认文档、SPA 回退、响应头和自定义错误页。配置冻结项改用整行斜纹。详细参数、脚本 API 与恢复边界见 [Server 与文件传输](docs/server-and-transfers.md)。

3.1.0 完成文件内的可恢复复制、本地目录分块暂存、ZIP 压缩器检查点及跨挂载移动。缺少本地写盘 API 时，ZIP 仍在浏览器压缩，压缩块暂存主机后由浏览器以 Range 下载；挂载上传使用可恢复暂存，主机已有字节直接发布副本，避免提供方重复上传。移动逐文件核对目标与源版本，再删除源，保留新增或跳过的内容。

3.3.0 让内容推断参与预览与菜单分派，支持 ZIP 内 Markdown 关联图片，增加连续 Word/PDF 阅读、PPT 放映、表格文字选取与图片复制、触屏布局和可写文本编辑。代码编辑器按能力加载内置汉化 Monaco，配色跟随 Modern Dark/Light，旧浏览器保留普通文本编辑。Server 的 ZIP 应用入口见 [工作区应用](docs/workspace-app.md)。压缩响应按 Accept-Encoding 协商 gzip / deflate，Range 保留原字节语义。`.hls-state-*` 已存在条目显示冻结斜纹，不能读写。3.3.1 补齐 `.hls-config-lock-*` 文件和目录的冻结占位显示，目录列表、索引搜索与原生挂载搜索一致，禁止读取内容和枚举内部条目。

3.4.3 支持可写目录中新建文件，冻结项随“显示隐藏”选项展示，统一设置页审核区域的布局与按钮样式。 依赖冻结改为按本次启动实际加载的配置、锁、状态、脚本和 TLS 文件路径/身份判定，同名普通文件不再误冻结；代码文件保留纯文本编辑回退，播放器音量轨道显示实际音量。

3.4.1 补齐文件与目录向标签页拖放移动、剪切粘贴和目标文件夹选择，收拢更多操作并修正同级目录下拉宽度；修复 PDF 缩放选区，PPT 按视觉行选择、复制图文，极窄屏工具栏保持分组对齐。

3.4.0 重做 PPT 缩略图、完整页面适配、顶部播放工具和触摸导航；统一 Office/PDF 的响应式缩放，补齐合并单元格的选择、复制及旧浏览器跨页显示。阅读提示改为短暂消息，所有预览统一使用标签页或独立窗口。

3.5.0 静态嵌入 WAMR 2.4.5，为 QuickJS 开放 Wasm、SQLite、ZIP、压缩、类型推断、哈希和二维码接口。配置可选择模式、应用 ZIP、传参及访问策略；Server 应用默认只公开 public/，共享数据保持主机私有。MCP 支持 2026-07-28 并兼容旧握手。用法见 [工作区应用](docs/workspace-app.md)、[QuickJS/Wasm](docs/quickjs-wasm.md)、[MCP](docs/mcp.md)。

3.5.1 增加有界 ZIP 静态资源热点缓存、并发解压合并和空闲回收；普通脚本复用可信编译结果与空闲运行时，每次调用仍使用独立上下文和原生组件。参数与状态统计见 [工作区应用](docs/workspace-app.md) 和 [Server 与文件传输](docs/server-and-transfers.md)。

## 使用

```powershell
# 默认 FileTrans，当前目录，8000 端口
.\HttpLocalServer.exe

# 指定目录并打开浏览器
.\HttpLocalServer.exe --root D:\Share --open

# 修改端口与监听地址
.\HttpLocalServer.exe --root D:\Share --bind 192.168.1.10 --port 9000

# 独立权限写入：本机管理整个目录，远端仅管理自己的 IP 目录
.\HttpLocalServer.exe --root D:\Share --write-mode isolated

# 允许客户端预览后生成内存缩略图；默认关闭
.\HttpLocalServer.exe --root D:\Share --lazy-thumbnails

# 关闭文件预览，明确关闭懒缩略图
.\HttpLocalServer.exe --root D:\Share --no-preview --no-lazy-thumbnails

# 共享写入（任意客户端可写），默认仍为 readonly
.\HttpLocalServer.exe --root D:\Share --write-mode shared --ip-limit 256

# 静态网站模式
.\HttpLocalServer.exe server --root D:\website --port 8080

# 系统根：Windows 根下显示 C、D、E 等盘符；Unix 显示 /
.\HttpLocalServer.exe --system-root

# 主机目录或分区默认只读；可写映射仍受全局写入权限约束
.\HttpLocalServer.exe --root D:\Share --mount /资料=E:\Documents
.\HttpLocalServer.exe --write-mode shared --mount-rw /工作=E:\Work

# 系统根/盘分区集合放到指定网页目录
.\HttpLocalServer.exe --mount-root /Computer --mount-volumes /Volumes

# 每 IP 上传额度，可单独覆盖；0 表示不限
.\HttpLocalServer.exe --write-mode shared --ip-upload-gb 10 --ip-upload-limit 192.168.1.8=512MB

# 登录口令、严格主机、防盗链空闲 5 分钟与 CLI 二维码
.\HttpLocalServer.exe --password "你的口令" --strict-host --hotlink-seconds 300 --show-cli-qr

# 关闭 HTTP 压缩协商（默认协商 gzip / deflate；Range 保持原始字节）
.\HttpLocalServer.exe --no-compression

# 可选浏览器协作下载（P2P），默认关闭
.\HttpLocalServer.exe --root D:\Share --p2p

# 参数和内置第三方声明
.\HttpLocalServer.exe --help
.\HttpLocalServer.exe --licenses
```

Linux、macOS 下使用 `./HttpLocalServer`，参数相同。`--bind ::` 提供 IPv6 服务；IPv6 socket 设置为 v6-only。启动输出从 Windows 网卡 API / Linux、macOS `getifaddrs` 获取真实接口地址，并仅列出对应监听地址族的候选 URL。实际可达性仍受网络和系统防火墙配置影响。Ctrl+C 正常停止服务。`--open` 在 macOS 调用系统 `/usr/bin/open`。

可选内网 HTTPS：发布程序内置 mbedTLS，默认仍使用 HTTP。指定 `--https --tls-cert D:\Certs\server.pem --tls-key D:\Certs\private.pem`，原端口转为 HTTPS；`--tls-self-signed` 可生成仅本次运行有效的内存证书。源构建通过 `-DHLS_ENABLE_TLS=ON` 加入后端，默认关闭。证书信任、SAN、握手限制及完整使用说明见 [docs/https.md](docs/https.md)。

登录口令、5 次尝试与封禁解除、严格主机、防盗链、二维码、gzip 和复制粘贴的配置、边界及 IE11 手工核验步骤见 [访问与传输](docs/access-and-transfer.md)。

可选 [PCDN / 专用 P2P 下载](docs/p2p.md)：默认仍是普通下载，P2P 仅作为符合能力条件的右键选项。主机启用总开关后，页面默认静默缓存和发送；整页缓存默认 64 MiB / 空闲 1 小时，主机可动态调整为 0–64 MiB 及有效期，核实协商或实际传输时续期。无需下载者确认共享，可在当前页面关闭发送且不记忆。大文件 P2P 仅在浏览器支持安全本地写入时提供，打开标准保存窗口后分块落盘；其他浏览器只允许小文件有界暂存。HTTP / HTTPS 均支持节点传输，内存紧张或文件特大时优先默认普通下载。

## 模式与功能

- **FileTrans（默认）**：Explorer 风格网页，装饰红黄绿灯、实时跟手并让位的标签页（支持拖动排序、边缘滚动与取消还原）、可编辑地址栏、独立前进/后退、列表/网格布局、每次 100 项的滚动加载、虚拟渲染、排序与文件名搜索。右上角系统/白天/夜间图标按钮。支持文件/文件夹上传、复制粘贴及跨标签复制、拖入本地文件或文件夹、拖动可写文件/目录到目标文件夹或面包屑、建目录、重命名、删除，以及可清除的下载记录；兼容窄屏。网页无 CDN 或外部字体依赖。
- **Server**：只读静态网站托管，目录自动访问 `index.html`，目录补斜杠重定向，MIME、HEAD、条件请求与 Range。
- **下载**：64 位偏移、完整文件、闭区间/开放区间/后缀 Range、206/416、Keep-Alive、UTF-8 文件名。多范围请求采取 RFC 允许的忽略 Range 并返回 200 策略，不构造 multipart。
- **索引**：SQLite 内存快照，每 60 秒后台重建一次，新快照完成后切换。默认最多 250,000 项，另设 64 MiB 的索引元数据估算额度与 128 层遍历上限。三个及以上字符使用 FTS5 trigram；短中文查询使用受索引总量限制的名称扫描。大小写折叠为 ASCII / SQLite 的默认行为，并非完整 Unicode 语言排序。
- **路径**：只共享根目录内的普通文件和目录；拒绝父目录穿越、Windows ADS、符号链接与 reparse points。Linux、macOS 文件下载使用逐级 `openat` / `O_NOFOLLOW`；Windows 打开后再次检查最终句柄路径。

文件浏览和搜索默认不显示隐藏项。Windows 按文件/文件夹的 Hidden、System 属性判断，Linux 按名称开头的 `.` 判断，macOS 同时识别点文件与 `UF_HIDDEN` 标志；Windows 上单纯点开头不等于隐藏。“更多 → 显示隐藏文件和文件夹”由每个浏览器独立保存，同一浏览器的标签同步。搜索也会过滤隐藏目录里的后代，分页和总数在服务端过滤后计算。隐藏只是显示偏好，不是访问权限；已知路径仍可打开。目录 ZIP 保持包含目录内所有可共享文件。

音视频使用原生媒体缓存提前预取，不把整段文件拉入 JavaScript。持续卡顿超过 450 ms 后暂停积攒缓冲，恢复目标从 8 秒逐级增加到 20 秒，按倍速折算，并受剩余时长约束；暂停和拖动进度会取消旧缓冲等待。短暂的就绪状态变化不闪烁缓冲提示。全屏与侧栏跟随浅色、深色或系统配色，带底色的播放按钮固定搭配高对比图标。

网页点击下载或复制链接时，会先获取包含文件版本的下载 URL。此 URL 绑定文件标识、大小、修改时间等元数据；文件改变后返回 409，防止把旧下载与新版本拼接。版本固定 URL 允许匹配日期的 `If-Range`；弱 ETag 永远不满足 `If-Range`。未固定版本的下载 URL 收到 `If-Range` 时保守返回完整 200。

默认版本标识不是内容哈希。它依赖文件系统正确维护修改/变更时间；外部程序刻意伪造元数据或在写入过程中共享文件，不属于不可变内容快照保证。服务内的覆盖采用应用层 COW：完整写入并刷盘临时文件，再校验版本并发布；旧下载持有旧文件句柄，新下载读取新版本，缓存也按版本隔离。刷盘不占用全局写锁。活动读取比较文件对象、大小与修改时间，允许文件移动或旧版本解除目录链接；外部程序就地写入并伪造修改时间仍可能逃过检测。需要严格快照时，应共享已经完成写入的目录或文件系统快照。

普通下载交给浏览器管理，不将大文件读入 JavaScript Blob。暂停、恢复能力由浏览器下载器决定；普通网页记录只表示已交给浏览器，不宣称下载完成。协作下载使用独立进度和输出流程，完成后才记录。原始 HTTP 续传可使用：

```sh
curl -C - -o large.iso 'http://192.168.1.10:8000/download?path=%2Flarge.iso'
```

## 写入权限与客户端限制

`--write-mode readonly|isolated|shared` 默认只读，包括本机。`isolated` 下，TCP 对端是回环地址或本机网卡地址时作为本机管理员；`--strict-host` 启用后仅精确的 `127.0.0.1` 与 `::1` 享有主机权限；远端只可在 `/_clients/<实际 IP>/` 内创建、上传、重命名和删除，不能修改该目录本身或其他人的目录。IPv6 使用可逆十六进制目录名。`X-Forwarded-For` 不参与权限判断。

`--ip-limit 256` 限制同时连接的不同 IP，断开全部连接后释放名额；`0` 不限制。`--owner-limit 256` 另限制已有独立目录数，`0` 不限制。NAT 后的多个人会共用一个 IP 目录，这属于 IP 权限模型的限制。不要通过反向代理公开独立权限服务，因为代理的实际 TCP 地址会改变归属。

独立 IP 目录仅在上传开始时按需建立，打开页面、进入“我的文件夹”或查询姓名均不创建物理目录；尚未建立的专属目录展示为空目录。独立目录上限在上传创建目录时检查，不拒绝单纯访问的用户。最后一个文件删除或上传中断后，仅在没有浏览者和活动上传时回收空目录树；清理只删除空目录，不递归删除文件。每个浏览器页面为最多 12 个标签维护一个浏览登记，正常离开会更新登记；页面崩溃、断网或关闭时无法及时通知服务端，以 45 秒登记超时兜底。

支持拖入文件夹，或从“更多 → 上传文件夹”选择目录，保留层级并逐个流式上传；同名冲突仍逐文件确认。共享写入及独立模式的主机管理员使用落点目录；独立模式的远端在自己的目录内部保留落点，在外部拖入时归入自己的 IP 目录。每批最多 10,000 个文件、20,000 个遍历项目、64 层，空文件夹不会创建占位文件。文件夹接口不可用的旧浏览器保留普通文件上传，不将目录误当成零字节文件。上传目录的路径约束与普通写入相同。

独立模式下，上传成功后可选填姓名并提交审核，也可从更多操作中修改申请。只有主机“设置 → 姓名审核”能够读取待审核姓名并通过或拒绝；公开列表和目录轮询只返回已经通过的姓名。通过后在对应 IP 目录及其文件名后显示姓名胶囊，不改变实际目录名和权限。重新提交采用版本校验，避免批准已被修改的申请；姓名与审批在当前服务进程内有效。

当前目录有更新时默认显示“此目录有更新 · 刷新”，不自动打断浏览；“更多 → 目录更新时自动刷新”由各浏览器自行保存。每页面只有一个请求在途，标签共用浏览登记与设置同步，正常间隔 12–15 秒、隐藏页面 20–23 秒；同目录指纹在服务端合并，最多每 5 秒扫描一次，缓存最多 128 份。检测包括目录直属项目的名称、类型、大小和高精度修改时间；可发现外部同大小改写，不递归监听整个子树。自动刷新避开上传、拖拽、弹窗和路径/搜索编辑，并保留滚动位置；姓名胶囊单独更新，不必刷新目录。矮屏侧栏允许滚动，底部设置和连接信息不覆盖导航。

写操作需要网页获取的随机进程令牌及同源检查；令牌接口校验数字 IP / localhost Host，拦截常见 DNS rebinding。文件权限最终以 TCP 对端判断。上传默认不覆盖已有目标。同名检查在发送正文前完成，可改名、跳过或明确选择“替换此版本”；覆盖携带已观察版本，若另一访问者先修改则返回 409，必须重新选择。应用内的删除/重命名不能碰正在上传的路径及其祖先。实际运行中的状态、配置锁和上传临时项按当前依赖路径或文件身份冻结；普通用户文件即使使用 `.hls-` 前缀也不会仅因重名被冻结。路径穿越、ADS、设备名、无效 UTF-8、超长路径和组件、符号链接及 reparse points 仍被拒绝。网络上传可取消，失败或断开清理临时文件。拒绝上传后的排空受 64 KiB 与绝对 1 秒期限约束，不能用慢速滴流延长。进程被强制终止或断电后，隐藏临时文件可能需管理员清理。Linux 3.15 以前缺少 renameat2 时，仅普通文件提供无覆盖重命名降级，目录重命名返回 501。

## 挂载与上传额度

主机根目录、盘分区、浏览器持续挂载、主机副本、直接保存文件夹，以及全局/单独 IP 上传额度的完整行为见 [docs/mounts.md](docs/mounts.md)。浏览器挂载状态与已复制的主机副本独立；离线仍保留已有内容。

## 预览、类型识别与浏览器

配色位于右上角“系统 / 白天 / 夜间”三个按钮，直接选取，无需打开更多菜单；窄屏保留紧凑按钮组。

预览默认启用；`--no-preview` 关闭，`--allow-preview` 显式启用。文本、Markdown、JSON 树、网页、图片（含 SVG）、音视频、Office、PDF、ZIP 与代码编辑均使用管理器标签页，也可右键在独立窗口预览。文件预览不再占用目录侧栏；进入预览标签后收起文件管理工具和左栏，保留顶部标签与主题切换。文本预览最多读取 2 MiB，树节点分批展开并限制深度和节点总量。Markdown 不执行原始 HTML，拒绝危险链接和远程图片。图片可切换适应窗口与原始尺寸；音视频使用浏览器原生媒体解码和带版本的 Range URL，提供独立视频画布、播放/暂停、前后跳转 10 秒、真实缓冲区进度、进度拖动与时间提示、音量/静音、0.5–2 倍速和重新设计的全屏控制栏，不先读入整个文件，也不转码。播放器获得焦点后，空格播放/暂停、左右键跳转 5 秒、上下键调节音量，M 静音，F 切换视频全屏。全屏播放时控制栏可隐藏，鼠标移动、触摸或键盘焦点使其显示。可播放格式取决于浏览器与系统解码器，不支持时显示下载提示。

“在新窗口预览”出现在支持预览文件的右键菜单中。音视频、图片和简单文档使用独立 `/preview?path=...` 页面，Office/PDF 使用 `/_hls/viewer`，音视频保留完整播放器。网页使用 `/view/<目录/文件>`，目录访问 `index.html`；相对资源和共享根目录下的绝对资源路径均可传输，JSON 等关联文件也支持请求。管理路由保留，所有资源沿用根目录约束、符号链接拒绝和安全打开检查。网页使用 iframe sandbox，直接访问也带 CSP sandbox；仅静态资源向隔离来源提供 CORS，管理令牌和写接口不开放此授权。依赖同源存储、表单提交、外部站点或服务端接口的网站可能无法完整运行。

懒缩略图默认关闭，`--lazy-thumbnails` 开启，`--no-lazy-thumbnails` 关闭。此项由主机统一控制，所有客户端跟随；访问者没有独立开关。客户端仅从自己已预览的图片、SVG、视频帧或文档生成缩略图。视频只从实际播放或拖动后已经解码的画面捕获，跳过近黑帧并限制重试次数，保留原始宽高比；不会为寻找缩略图自动改变播放位置。不预取整个目录，不上传、不写 localStorage 或磁盘。每页最多保留 64 张、2 MiB 编码数据，单张最多 64 KiB；主机关闭后清除。设置与目录检测共用周期请求，回到前台时再次同步；独立预览窗口也按相同间隔同步设置；内嵌预览标签复用管理器的策略同步，不另行轮询或加入 PCDN 发送者。

IE11 作为基本功能兼容目标：内嵌 JavaScript 为 ES5，基本浏览/传输使用 XMLHttpRequest，无 fetch、Promise、Map、CSS Grid 或 CSS 变量依赖；目录挂载和直接保存仅在现代浏览器能力检测通过时进入 Promise 分支。IE9/10 不具备文件上传能力时保留浏览与下载。IE 的 HTML/SVG/XML 等主动文档直接地址强制下载，网页预览给出不支持提示，以弥补其 CSP 支持限制。用户已于 2026-11-10 完成 2.2.0 的 IE 验收并反馈无问题。2.5.1 补充全部脚本/依赖和登录页 ES5 解析、移除现代 API 后的启动与 Markdown 回归、CSS 间距/定位回退检查；未执行新版 IE 实机测试，Win7 系统本身的实机/VM 验证范围仍未确认。

内置广泛扩展名 MIME 表与分类 SVG 图标；无后缀或未知文件选择时按需识别，上传完成和扩展名变更重命名时提示推荐后缀，可保留原名称。先做 C++ 签名检查（文件头最多 16 KiB，ZIP 额外读取最多 64 KiB 尾部），未明确命中且内容足够时运行 Magika。不解压也不执行文件，不在列目录时全盘推理。类型结果按文件版本缓存，最多 512 项，空闲 60 秒清理；同版本的并发识别合并。猜测不保证绝对正确，也不是恶意文件扫描。

Magika `standard_v3_3` 已内置，原始 ONNX 为 3,163,737 字节，重排后的内嵌权重为 3,136,960 字节。推理引擎为自有 C++ 固定图实现，无 Rust、ONNX Runtime、GPU 或运行时下载。CPU 内核覆盖标量、SSE2、AVX、AVX2/FMA、AVX512/FMA、NEON、SVE 与 RVV；编译器不支持的内核不编入，运行时再检查 CPU/OS 能力、数值自检和短时计时，选择可用实现。默认最多两路推理，`--magika-threads 1..4` 调整，`--cpu-kernel` 可强制指定可用内核，`--no-magika` 关闭模型。细节与架构验证范围见 [docs/magika.md](docs/magika.md)。

标签布局重叠 18px，仅当前标签带底部外扩肩部并覆盖相邻轮廓；未选中标签底边收平，圆角半径固定。每个标签页独立维护最多 128 步历史，应用导航只更新当前地址的 fragment，不调用浏览器后退。标签页最多 12 个，支持拖动重排；宽度从 190px 缩至 104px 后显示左右切换按钮，隐藏滚动条，“+”紧随标签区域。每个当前目录保留 8 页，近期目录最多缓存 3 份；只渲染视口与少量预取行，节点复用。返回近期目录可复用短期列表缓存；下载保持原生链接，不把整个文件转换成 JS Blob。

## 主机面板与 MCP

在主机上点击左栏“设置”（仅本机设备显示），可动态调整共享名称、写入权限、IP / 独立目录上限、上传上限、预览、懒缩略图、Magika、MCP、公告和关注胶囊点击动作。面板只接受实际 TCP 对端为本机的访问；远端无法通过伪造转发头获得权限。修改采用并发安全快照，当前进程内生效，重启后以 CLI 参数为准。监听地址、端口和线程预算通过 CLI 设置。

主机可直接右键任意文件，选择“标记为关注文件”或“取消关注”，支持同时标记多个文件，最多 128 个。所有客户端看到相应星标及顶部关注胶囊。胶囊默认进入文件所在目录、选中文件并滚动到可见位置；主机设置可统一切换为“直接下载”，所有客户端跟随。启动参数 `--attention-click locate|download` 设置初始动作，默认 `locate`。跨目录定位会清空搜索，按当前排序直接读取目标所在的 100 项分页，不逐页加载前面的文件；同目录内已缓存的文件直接定位。主机也可右键顶部条目取消关注。此功能与写入权限独立，只读模式下主机也可标记，远端无权修改。通过软件重命名文件/父目录时标记跟随，删除时移除；外部删除后的条目可由主机右键清除。关注列表与其他实时设置一样在当前进程内有效。

`--mcp` 开启 `/mcp`，默认关闭。入口仅供本机使用，要求独立 Bearer Token；启动输出和主机面板提供连接信息。采用 MCP 2026-07-28 的无状态 HTTP POST / JSON 响应，支持 server/discover、tools/list 和 tools/call，并兼容 2025-11-25 / 2025-06-18 的 initialize / ping。八个只读工具提供运行组件与预算、状态、公告、分页目录、搜索、类型识别、下载链接和最多 64 KiB 文本，不提供写入或系统命令。ZIP 应用的 MCP 位于独立 localhost 管理入口；详细请求头与元数据见 [MCP](docs/mcp.md)。

```json
{"mcpServers":{"HttpLocalServer":{"type":"http","url":"http://127.0.0.1:8000/mcp","headers":{"Authorization":"Bearer <启动时的令牌>"}}}}
```

## 目录 ZIP

目录右键选择“下载为 ZIP”，由浏览器按 256 KiB Range 块下载，再在客户端执行流式 DEFLATE 和 ZIP64 写入，服务器不压缩目录。支持中文文件名、空目录、取消和文件版本变化检测。每次最多遍历 100,000 个条目、128 层，ZIP 元数据限制 16 MiB。支持 File System Access 的浏览器在允许的安全上下文中直接流式写入本地文件；其他环境使用有界 Blob 暂存（桌面 256 MiB、窄屏 64 MiB），超限终止并提示。普通单文件下载继续交给浏览器下载器。

## 热点与退火

正文缓存按“文件版本 + 固定块偏移”分片。默认块大小 256 KiB，碎文件自然以一个较小块驻留。

- 16 个独立缓存分片，减少全局竞争。
- 短期访问窗口 + TinyLFU 风格的频率草图，重复访问候选才竞争长期缓存；一次性扫描主要停留在短期窗口。
- 文件块热度按时间指数衰减；候选与淘汰对象按单位内存热度比较。
- 同一文件块的并发缺失合并为一个文件读取。
- 默认 60 秒空闲后淘汰；后台周期维护，无访问时也会回收。
- 正文使用 `VirtualAlloc/VirtualFree` 或 `mmap/munmap`，淘汰后在最后一个在途发送释放引用时归还系统。
- 动态缓存预算受可用内存约束，压力下降立即收缩，恢复时缓慢增长。Linux 读取 cgroup v2 可用余量。
- 正文缓存与在途缓冲区共同使用原子硬上限；默认最大缓存 128 MiB，加 64 MiB 在途预算。发送中的已淘汰块继续计入实际分配。

目录结果另有 32 MiB / 32 个结果的短期缓存，单份目录快照限制 8 MiB，目录构建串行合并，3 秒可见性期限，5 秒后台清理。Magika 首次使用时分配约 3.1 MiB 模型页，每路推理约 4 MiB 临时页；推理结束释放临时页，模型空闲 60 秒释放解码后的模型页，可执行文件中的只读模型数据仍由系统管理。SQLite 索引、推理、请求对象、网络缓冲区及分配器开销不计入正文缓存预算；重建索引时旧新快照可能同时存在，因此不能把 `--cache-mb` 当作整个进程的内存上限。

## 并发与资源控制

网络使用 Asio IOCP / epoll，HTTP 使用 Beast。网络线程、正文读取池、目录/元数据池及索引线程相互独立。每个传输只在当前块发送完成后申请下一块，慢客户端施加背压。

线程默认按 CPU 数设置合理上限，从网络 2、正文 2、元数据 1 个工作线程起步，随负载扩展，空闲默认 15 秒撤回。`--io-max`、`--file-max`、`--metadata-max` 设置自动模式上限，`--thread-idle` 调整撤回延时；`--io-threads`、`--file-threads`、`--metadata-threads` 分别固定该池大小，`--threads fixed` 将未指定的池固定为其上限。`/api/status` 报告当前线程数。

默认最多 4096 个连接、128 个普通活跃请求，API 另保留 8 个请求额度；文件和元数据任务队列有界。超额请求返回 503，连接数超额直接关闭。每次网络读写默认 30 秒超时。文件读取在有界工作池中执行：Windows 使用带偏移的 Overlapped ReadFile，Linux、macOS 使用 pread。上传使用 Beast buffer_body + 有界磁盘队列，逐块写入随机临时文件，成功后无覆盖提交；正文与上传块共同计入内容缓冲硬上限。默认每个上传最大 16 GiB，可用 `--max-upload-mb` 调整。当前版本未启用 sendfile / TransmitFile 或 HTTP/2；HTTPS 通过可选 mbedTLS 后端提供，登录采用统一口令，不提供多用户账号系统。

```sh
HttpLocalServer --cache-mb 256 --buffer-mb 128 --max-transfers 256 \
  --file-threads 8 --metadata-threads 2 --io-threads 8 \
  --cache-idle 90 --heat-half-life 30
```

在途预算必须至少容纳每个活跃传输两个块；命令行会验证。磁盘、NIC、文件描述符限制和客户端速度决定最终吞吐，不能从线程数或连接上限推导带宽保证。

## 构建

平台目标为 Windows 7+、Linux（包括 musl）和 macOS 11.0+（Intel / Apple Silicon）。macOS 支持代码与构建入口已加入，本轮未做编译或测试验收。

依赖源压缩包已固定到 `third_party`，CMake 校验 SHA-256 并自动离线展开。需要 CMake 3.20+、C/C++17 工具链和 Ninja 或相应生成器。无需 Zig、Python 包、Node、npm、vcpkg 或运行时安装包。Python 用于修改网页后的打包、依赖获取和可选开发测试；无 Python 的离线构建会校验已提交资源与源码清单是否一致。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
python tests/integration.py build/HttpLocalServer
```

Windows 建议 MSVC `/MT` 构建：开发者命令提示符执行 `tools\build-msvc.cmd`。工具脚本也可自动进入默认 BuildTools 2022 环境，并执行全量构建，防止部分中文 MSVC/Ninja 环境无法记录头文件依赖时混用旧 ABI。Win7 发布构建应使用支持目标系统的 MSVC 工具集（优先 v142），并在真实 Win7 SP1 或 VM 上验收；项目设置 `_WIN32_WINNT=0x0601`、PE console subsystem 6.01。仅设置这些宏不等价于已完成 Win7 实机测试。MinGW 构建需要对应 Win7 可运行的工具链，普通 UCRT 构建可能要求目标系统的 UCRT 更新。

Linux-musl 在 Alpine / musl GCC 工具链中静态编译：

```sh
apk add build-base cmake ninja linux-headers
cmake -S . -B build-musl -G Ninja -DCMAKE_BUILD_TYPE=Release -DHLS_FULL_STATIC=ON
cmake --build build-musl --parallel 4
ctest --test-dir build-musl --output-on-failure
file build-musl/HttpLocalServer
ldd build-musl/HttpLocalServer
```

`HLS_FULL_STATIC=ON` 不会自动把 glibc 工具链变为 musl 工具链；应在正确环境构建，并核查没有 ELF interpreter / NEEDED 动态库。

也可以在 Alpine 中执行 `sh tools/build-musl.sh`。默认生成 `build-musl/HttpLocalServer`；可用第一个参数指定构建目录。

macOS 默认面向 11.0+，支持 Intel `x86_64` 与 Apple Silicon `arm64`。安装 Xcode Command Line Tools 和 CMake 后执行（脚本只构建，不运行测试）：

```sh
sh tools/build-macos.sh
# 可选：一个同时包含两种架构的 Universal 2 可执行文件
HLS_MACOS_ARCHS='x86_64;arm64' sh tools/build-macos.sh build-macos-universal
```

macOS 同样发布一个 CLI 文件，网页、模型和应用依赖内嵌，使用系统自带 libSystem / libc++；不要启用 Linux 的 `HLS_FULL_STATIC`。内存探测使用 Mach / sysctl，目录移动使用 `renameatx_np(RENAME_EXCL)` 避免覆盖竞争，下载与写入保留逐级 `openat` / `O_NOFOLLOW` 的路径约束。Intel SIMD 通过 macOS 的系统能力查询选择，Apple Silicon 使用 NEON 并保留标量回退。此次 macOS 适配仅做源码检查，未在 macOS 编译或运行验证；现有 `dist` 文件未重新构建。


## 商业分发

项目自有代码与网页采用 **Apache-2.0**，作者 **github@nwdxlgzs**，完整条款见 `LICENSE`，归属见 `NOTICE`。商业分发时保留适用许可和声明。第三方部分包括 Boost BSL-1.0、SQLite 公有领域、Magika Apache-2.0、QuickJS-NG / marked / fflate MIT；静态编译器运行库声明和例外见 `licenses`。项目和第三方许可证均内置到 `--licenses`。参见 [licenses/README.md](licenses/README.md) 和 [third_party/README.md](third_party/README.md)。

## 验证

`tests/write_tests.cpp` 验证独立目录、管理员、共享/只读、CSRF/Origin/Host、上传路径占用、类型识别、Magika 特征提取和线程扩缩；`python tests/v2_integration.py <可执行文件>` 验证流式/并发上传、中断清理、无覆盖、类型建议、IP 名额、预览隔离及 IE 主动文档下载降级。`python tests/v21_integration.py <可执行文件>` 验证真实模型识别、并发合并、缓存失效、TXT 边界以及媒体 Range/版本隔离。模型内核另有黄金概率对照、并发、释放重载和官方 ONNX 参考测试。

`tests/core_tests.cpp` 覆盖范围解析、路径规范化、UTF-8、缓存并发合并、文件版本隔离与实际 OS 页回收。`tests/integration.py` 启动真实进程，验证两种模式、中文根路径及文件名、分页、短中文和 trigram 搜索、Range/HEAD/304/416、5 GiB 稀疏文件偏移、Keep-Alive、144 个混合 Range 请求、文件改变、版本固定续传、冷却释放和过载背压。性能结果仅代表本机功能负载，局域网极限需用目标磁盘和网络压测。

`python tests/load.py <可执行文件> --output report.json` 执行 96 客户端、96 次 8 MiB 文件和 672 次小文件的混合下载并验证内容，同时确认退火后缓存归零。`tests/defaults.py` 验证无参数启动，需确保本机 8000 端口空闲。已有结果见 `docs/verification`。

源码结构与可维护脚本见 [docs/development.md](docs/development.md)。2.2 接口、设置与 MCP 验证执行 `python tests/v22_integration.py <可执行文件>`；客户端 ZIP 写入器执行 `node tests/archive_test.cjs <服务地址> <输出.zip>`，再用标准 ZIP 工具校验。IE11 的逐项验收步骤见 [docs/ie11-verification.md](docs/ie11-verification.md)。

多文件关注接口可运行 `python tests/v221_integration.py <可执行文件>` 验证权限、路径、并发、容量及重命名/删除同步。

2.2.2 关注胶囊动作、定向分页和缓存刷新回归：`python tests/v222_integration.py <可执行文件>`。

## 拖拽与并发写入

拖入本地文件时，共享写入保存到鼠标所指的文件夹；空白区域使用当前目录。独立 IP 模式使用实际 TCP 对端的 `/_clients/<IP>/`，包括主机本地拖入：在该目录内保留目标子目录，在其外则自动放入 IP 根目录。原有本机管理员管理整个共享目录的权限保留，网页“我的文件夹”指向自己的上传目录。只读模式拒绝上传。

可写文件和目录可拖到目录、面包屑或左栏位置进行移动；不可写条目不能拖动。移动校验两端权限和活动上传占用，普通文件携带版本，同名目标不覆盖，目录不能移入自身。上传同名冲突和目录移动由同一写入协调器处理；不同路径的正文写入与读取并行，同路径只有一个活动上传。界面取消、断开连接或版本冲突不发布临时内容。

`python tests/v230_integration.py <可执行文件>` 验证覆盖时旧下载完整继续、多人同版本竞争、取消清理、移动冲突和 IP 落点；`node tests/drag_thumbnail_test.cjs` 验证原生 DataTransfer 风格事件、上传确认和有效帧缩略图。浏览器实测结果见 `docs/verification/release-v230.json`。

### ZIP / Office / PDF 预览（3.2.0）

ZIP 默认关闭，通过 `--zip-preview off|host|client` 或主机设置选择。主机按需解压有独立线程、全局缓存/并发/时间上限；客户端模式只传输原 ZIP，在浏览器处理。两者都会检查压缩炸弹相关结构、实际输出和路径边界，包内网页隔离执行。预览与目录共用最多 12 个内部标签。

DOCX/PPTX/XLSX 使用 Silurus 官方预编译 Wasm；PDF 使用 PDF.js。旧浏览器按需请求纯 JS 基本 Office 预览和 ES5 PDF 引擎，支持复制；服务端保持 C/C++，普通构建不需要 Rust/Node。详细预算、兼容边界与复现入口见 [docs/previews.md](docs/previews.md)。
