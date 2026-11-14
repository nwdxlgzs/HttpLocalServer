# 开发与构建输入

服务器为 C++17 / C，运行时不依赖 Python、Node、Rust、Zig 或外部模型服务。

2.7 的可选 P2P 控制面位于 `src/p2p.cpp/.hpp`，分片哈希使用 `src/sha256.cpp/.hpp`，独立于可选 mbedTLS。浏览器节点、信令、帧与背压在 `web/scripts/p2p.js`，校验、下载生命周期和本地写入在 `web/scripts/p2p_download.js`，均保留 ES5 语法及能力门控。资源仍通过统一 gzip 流程嵌入；未新增第三方依赖。配置与边界见 [p2p.md](p2p.md)。

`node tests/p2p_test.cjs` 验证分片校验、坏块回源、帧重建、资源界限、暂停/取消异步竞争、已有文件续传和 COW 写入句柄替身。`python tests/p2p_integration.py <程序>` 验证真实 HTTP 控制权限、版本/节点隔离、信令确认、并发哈希、动态开关及防盗链空闲过期；测试程序会等待空闲超时。C++ core 测试包含独立的 SHA-256 黄金向量。浏览器节点之间的数据传输需要实际浏览器验收，HTTP 测试不能替代 WebRTC；真实保存选择器和超大文件长时间测试仍需另验。

可选 HTTPS 的构建开关、配置与测试见 [https.md](https.md)。`src/tls.cpp` 管理 mbedTLS 配置、证书和有界异步 BIO；`src/tls.hpp` 给现有 HTTP 会话提供统一异步传输接口；`src/tls` 是 C 兼容配置和 std::mutex 不透明适配。TLS 默认不编译，`HLS_ENABLE_TLS=ON` 静态链接固定源包。普通 HTTP 和 TLS 共享请求解析、存储、缓存与权限逻辑。

| 目录 | 内容 |
|---|---|
| `src` | CLI、HTTP 会话、平台文件操作、缓存、索引、权限、设置、MCP、固定图推理 |
| `src/cpu_kernels.cpp` | 聚合 SIMD 内核，函数级目标属性隔离可选指令 |
| `src/cpu_features.hpp` | 按编译器和架构自动选择可编译内核 |
| `web/scripts` | ES5 网页模块：导航、列表、传输、预览、播放器、拖拽、文档、ZIP、设置、懒缩略图 |
| `web/styles` | 基础、动效/布局修饰、深色主题、播放器、拖拽反馈 |
| `web/vendor` | 已校验的 marked / fflate 离线资源 |
| `resources` | 确定性 gzip 网页和逐文件 SHA-256 清单 |
| `cmake/templates` | 网页、许可证、模型嵌入模板 |
| `third_party` | 固定源压缩包、模型和来源清单 |
| `licenses` | 第三方原始声明和运行库例外文本 |
| `tests` | 可复用的 C++、HTTP、模型、负载和 ZIP 校验工具 |

维护脚本：

```sh
# 离线展开 Boost / SQLite（CMake 也会自动执行）
python tools/dependencies/unpack.py

# 校验并获取固定版本的网页依赖
python tools/dependencies/fetch_web.py

# 修改网页之后重新生成压缩资源和源码清单
python tools/assets/pack_web.py

# 仅在更新固定模型时重新导出，需要开发用 numpy / onnx
python tools/models/export_magika.py
```

CMake 在有 Python 时自动打包网页；没有 Python 时，校验仓库中已生成资源的哈希与源码清单，发现过期就中止，防止静默嵌入旧页面。gzip 在编译时转为字节数组；客户端接受 gzip 时直接发送，否则仅在首次需要时展开并共享。界面与依赖不使用 CDN。

`web_data.hpp` / `icon_data.hpp` 是资源编译单元的显式依赖，避免中文 MSVC 依赖输出未被识别时，增量构建继续携带旧网页。修改网页后应检查运行中程序的页面正文与 `resources/web-manifest.json` 的展开字节数一致。

IE 静态回归执行 `node --expose-internals tests/ie_compat_test.cjs`。它对全部应用和第三方脚本、内嵌登录脚本做 ES5 语法检查，移除现代全局 API 后验证启动、Markdown、gzip/UTF-8 回退、主题和能力门控，并检查 CSS 间距与 sticky 回退；它不是 IE 渲染引擎。主题滑块、额度输入和 IE 样式集中在 `web/styles/compat.css`。

固定的 marked 4.3.0 通过 `tools/dependencies/web_compat.py` 消除初始化时的 Set 迭代构造和未保护的 Symbol / Array.from 访问；预览显式使用同步解析。获取脚本先验证上游包完整性，再应用精确匹配的兼容补丁，`web-lock.json` 同时保存上游哈希、补丁标识和补丁后哈希，保留原始 MIT 声明。

Windows 使用 `tools/build-msvc.cmd`。Alpine 使用 `sh tools/build-musl.sh`。两者执行 CTest；macOS 使用 `sh tools/build-macos.sh`，默认关闭测试构建且不执行测试，可用 `HLS_MACOS_ARCHS` 选择 `arm64`、`x86_64` 或两者。HTTP 测试与负载测试需要另外运行。标准 CMake 构建命令见根目录 README。

一次性源码迁移脚本、QEMU 探针及模拟器不属于构建依赖。异构结果保存到 `docs/verification/release-v22.json`，无需保留这些一次性可执行文件。`tools/LaunchIE.exe` 为用户提供的本机验收辅助程序，不打包进服务器，不纳入应用许可声明。

修改原始网页依赖或模型时，同步更新锁定信息、来源与许可证；不要将个人环境路径、下载缓存或测试服务器目录放入发布包。

2.3 的文件写入回归：`python tests/v230_integration.py <程序>`。浏览器事件/缩略图单元验证：`node tests/drag_thumbnail_test.cjs`。`player.js` 负责媒体交互，`previews.js` 负责预览路由和生命周期，`dragdrop.js` 负责落点与可写移动，`transfers.js` 负责流式上传和同名确认。

文件夹上传、IP 目录生命周期、姓名审核和目录更新：后端集中在 `src/collaboration.cpp`，前端在 `web/scripts/collaboration.js`，样式在 `web/styles/collaboration.css`。`node tests/collaboration_test.cjs` 验证多批次目录遍历、层级限制、单个在途轮询、随机间隔、个人自动刷新和姓名元数据更新；`python tests/collaboration_integration.py <程序>` 验证真实流式目录上传、审批、48 路并发检测、多个浏览者保护及 45 秒崩溃登记超时，完整执行约一分钟。C++ writes 测试还覆盖远端权限、虚拟空目录、越界递归路径、同大小外部改写和中断上传清理。

`POST /api/browse` 是同源、令牌保护的有界浏览登记接口；`id` 绑定实际 TCP IP，`paths` 最多 12 项、总路径 12 KiB，32 KiB 正文上限，每 IP 最多 16 页面、全局最多 2048 页面。正常页面每 12–15 秒请求一次，隐藏页面每 20–23 秒一次；同目录高精度元数据指纹共享 5 秒缓存，最多 128 项。`owners` 最多 64 项，只返回对应已审核姓名；待审队列仅 `GET /api/admin/aliases` 返回。`POST /api/alias` 仅给实际上传过文件的 IP 提交申请；`POST /api/admin/aliases` 要求本机 TCP 对端和申请版本匹配。姓名上限 128 UTF-8 字节、最多 4096 个 IP 的进程内记录，禁止控制字符和方向控制符。清理路径只处理合法 IP 目录，每次最多检查 32 个根、512 个树节点；活动上传和任意标签里的后代路径均保护对应目录，最后通过安全的非递归删除确认仍为空。

隐藏属性、过滤后分页、不同访问者缓存隔离及隐藏目录后代的搜索：`python tests/visibility_integration.py <程序>`。播放器连续缓冲段、卡顿恢复滞回、倍速/片尾边界、暂停/定位取消及计时器清理：`node tests/player_state_test.cjs`。API 的 `showHidden=0|1` 默认 0；MCP 的 `list_files` / `search_files` 接受布尔 `showHidden`。搜索快照记录最近隐藏祖先的深度，以便显式进入隐藏目录时仍能搜索其中未隐藏的子项。

macOS 平台边界：`platform.cpp` 处理 Darwin 的纳秒时间戳、Mach 内存统计、`UF_HIDDEN` 与系统 `open`；`storage.cpp` 使用 Darwin 排他重命名，Linux syscall 头仅在 Linux 包含。CPU 能力通过 macOS sysctl 获取，Apple Silicon 不编入 SVE，Intel 与 ARM 代码按每个架构独立编译，可生成 Universal 2。默认部署下限 11.0，可通过 `MACOSX_DEPLOYMENT_TARGET` / CMake 配置调整；降低下限需自行核对 SDK 与 libc++ 可用性。本次仅静态检查和源码适配，未执行构建或测试；Windows/Linux 已有验收记录不代表对这些新修改的重新验收。

标志母版和导出资产在 `resources/branding`。修改后执行 `python tools/assets/make_icons.py`（仅导出需要 Pillow / CairoSVG）；CMake 校验已提交图标与压缩包并生成 `icon_data.hpp`，普通构建无需图像工具。Windows RC 嵌入同一份 ICO。`python tests/branding_integration.py <程序>` 核验身份/压缩响应、HEAD、304、小尺寸 DIB / 大尺寸 PNG 与 Server 自有图标优先级。

2.4 挂载和额度实现在 `src/mounts.cpp`（主机路径映射）、`src/browser_mounts.cpp`（有界异步提供方队列、复制发布、上传额度）、`web/scripts/mounts.js`（浏览器句柄和后台复制）、`web/scripts/directory_save.js`（256 KiB 块直接保存）。`node tests/mounts_test.cjs` 使用文件系统句柄测试替身，验证浏览器能力门控、扫描/复制合并、读写版本检查、私有写流和直接目录保存；不能替代真实浏览器授权弹窗的实机验收。`python tests/mounts_integration.py <程序>` 用真实 HTTP 提供方验证原生挂载、系统根、只读/可写、主机先落盘、并发 Range、MCP 异步列表和统一上传额度；C++ writes 测试额外覆盖远端只读/独立命名空间、提供方 IP 绑定和单独 IP 额度。

等待浏览器的请求使用 Response.deferred，不阻塞元数据线程。2.9 起有界 JSON 等待保留在控制预算中，返回文件正文时才转入传输预算；提供方心跳/回复保留独立 32 个控制额度，副本上传另有 8 个额度，防止消费者等待反向堵死来源。一次最多全局 128 个等待、每挂载 32 个，列表正文上限 256 KiB（100 项默认分页）、文件副本上传复用普通流式 Upload/COW 管道，主机分发走 HotCache。复制版本表每挂载 8 MiB、总计 32 MiB；目录指纹和列表保留原有有界缓存。配色按钮、API 能力分支和按 IP 限额均保留 ES5 语法；Promise 仅在检测通过的现代文件系统 API 分支运行。

2.7.1 的默认被动发送器在 `web/scripts/p2p_relay.js`，HTTP 的 SHA-256 / Worker 回退在 `web/scripts/p2p_hash.js`。发送偏好为闭包内页面变量，禁止持久化；总开关与个人开关独立，个人关闭不影响接收。`node tests/p2p_test.cjs` 增加 10 次连续事件、主机策略更新、重开页面默认值、合并拉取与缓存复用；`python tests/p2p_relay_integration.py <程序>` 分别验证 HTTP / HTTPS 的页面登记、按需分配、权限、单文件被动节点数、退出接管和总开关。

2.8.0 的正文块缓存集中于 `web/scripts/p2p_cache.js`：整页共享0–64MiB，不随单个任务结束释放；到期淘汰与主机核实活动续期独立于登录/防盗链。主机动态策略在 `LiveSettings` 中发布，节点轮询返回当前额度、TTL与真实活动年龄，纯轮询不刷新活动。默认 `download()` 不进入 P2P；`canP2PDownload()` 根据文件大小、内存提示、安全上下文和本地写入能力控制专用入口，超大文件不会误入 Blob。没有 Service Worker 下载路径。新增回归覆盖跨文件总额、到期、续期/旧活动、零额度、低内存门控以及默认普通下载。

2.9.0 的配置层在 `src/config_store.cpp`：JSON 参数先转入原 CLI 验证器，真实 CLI 后追加；`ConfigStore` 负责原子保存、进程锁、冻结名称/实际文件身份及历史硬链接。`config.json` 是可编辑的默认样例。`web/scripts/frozen.js` 提供统一冻结判断与雪花图形，服务端权限仍是最终边界。主机设置和关注文件先成功保存，再发布快照。`python tests/config_integration.py <程序>` 验证优先级、重启、失败回滚、MCP/Server/挂载别名、硬链接与祖先目录。

挂载来源 PCDN 使用独立 `mount:` 版本命名空间：P2P 专用票据不等待物化，提供方通过挂载密钥证明内容来源，主机下发同路径同来源版本的块清单。哈希缓存键包含完整逻辑路径，防止不同文件同大小/mtime 的版本字符串碰撞。源页面最多 4 个任务，共用带宽与发送并发。后台上传与访问优先落盘保留；物化表同时绑定来源版本及主机文件版本。`python tests/p2p_mount_integration.py <程序>` 在 HTTP/HTTPS、`--max-transfers 1` 下验证来源凭据、并发清单合并、4 文件上限、优先发现、回源落盘和卸载保留。浏览器句柄读取、哈希与块发送使用 `mounts_test.cjs`、`p2p_test.cjs` 验证，不代表真实目录授权窗口/WebRTC 联合实机验收。

## 3.0 托管与持久传输

新增 `state_store`（独占 SQLite/WAL）、`durable.cpp`（续传、回收与批量接口）、`traffic`（异步节流调度）、`script_engine`（隔离 QuickJS 调用）和 `server_control`（托管规则及脚本返回动作）。`tasks.js`、`resume.js`、`batch.js`、`operations.js` 管理 ES5 任务、续传、多选与新增主机 UI。依赖固定在 `tools/dependencies/quickjs-lock.json`；CMake 仅编译四个 C 核心源，不带 qjs CLI、libc 或模块加载器。MSVC C11 atomics 单独作用于该依赖，Windows 最低 API 仍为 0x0601。

恢复协议、默认预算、脚本契约和测试命令见 [server-and-transfers.md](server-and-transfers.md)。新增脚本运行时受独立内存、栈、超时和并发预算约束；不能直接执行 OS 操作。修改涉及 Service/Config/LiveSettings 布局的头文件后应全量复编，避免本地化 MSVC include 输出导致 Ninja 漏跟踪 ABI 变化。

新增验收：`node tests/batch_test.cjs` 覆盖 Ctrl/Shift、多选跳过冻结项、同名策略、长路径批量正文分拆、任务槽和并发扩展名弹窗；`node tests/resume_test.cjs <程序>` 使用真实 HTTP 服务校验浏览器传输实现；`python tests/state_integration.py <程序>` 校验重启后的姓名、配额和登录封禁；`python tests/secure_features_integration.py <TLS 程序>` 校验两个模式的新功能走 HTTPS。目录更新并发测试对明确的 503 背压做有界随机退避，仍核对每个成功响应的共享目录指纹。


## 3.0.1 公共调度与浏览器检查点

`web/scripts/tasks.js` 管理公共并行槽、优先级和暂停边界；`jobs.js` 负责有界目录清单、统一冲突询问、IndexedDB 检查点与命名空间绑定。清单与小检查点分开保存。`directory_save.js`、`archive.js`、`clipboard.js` 和 `p2p_download.js` 使用同一调度器，各自保留符合输出格式的提交边界。浏览器挂载写入、老式 PUT 与普通续传都进入任务中心；按需主机复制等待期间释放父任务槽位，避免单槽死锁。

`node tests/jobs_test.cjs` 验证跨页面复制/目录/ZIP 恢复、目标变化拒绝、写入流清理、优先级、安全边界让位与提供方依赖。`python tests/archive_integration.py <程序>` 将发货 ZIP 编码器与调度代码连到真实服务，覆盖 Server/FileTrans 并逐字节校验解包结果。原来的挂载、剪贴板、P2P 与拖拽测试已接入公共调度器。IE 增量手工清单见 `verification/ie-v301.md`。

## 3.5 脚本与 Wasm

script_engine 管理 QJS 隔离/持久上下文，script_components 提供有界组件，script_wasm 管理 WAMR 分配器、导入、实例和实时线性内存；script_wasm_bootstrap 只在服务器 QuickJS 中执行，不进入 IE 网页。script_policy 负责实际 TCP IP/CIDR 匹配，server_control 执行应用写请求和响应动作限制。MCP 现代/旧协议共用 mcp.cpp，协议说明见 mcp.md。

WAMR 只从校验的离线归档构建。cmake/WAMR.cmake 生成经典解释器预算检查点副本，保证 start、连续调用、导入回调不能重置每次钩子的总燃料。低内存分配失败、增长前旧 JS 视图失效和显式实例 close 均需回归。测试用 wasm_fixtures.py 直接构造小字节码，无下载编译器依赖。

执行 tests/wasm_app_integration.py、tests/mcp_current_integration.py，可加 --https。发布验证脚本已纳入两组 HTTP 回归；HTTPS 两组应额外执行。Windows hpp 结构变更后注意本地化 MSVC showIncludes 的缓存问题，必要时全量重编译 src 和 C++ tests。macOS 按用户要求只检查适配，不宣称实机测试。

维护版本时执行 `python tools/dependencies/check_runtime_updates.py`，它从官方 GitHub 最新正式发布与 MCP latest 入口比较版本，先核验已固定归档，不自动改锁或升级。退出 0 表示一致，1 表示有新版本，2 表示校验或联网失败；不可将联网失败当作最新版确认。`--offline` 仅核验已固定归档。

`runtime_files` 对本进程活跃的上传/配置/替换备份做作用域保护；不按前缀拦截用户文件。事务路径不进入配置持久列表，发布或清理后释放。实际回收站路径则由 ConfigStore 作为运行依赖登记，重启从私有状态恢复；工作区启动在这些依赖登记后执行。

macOS 的 WAMR 编译按每个编译切片的 __arm64__/__x86_64__ 选择目标，兼容单架构与 universal 配置；没有把配置主机的 CMAKE_SYSTEM_PROCESSOR 固定套到所有切片。此改动按要求仅源码适配。

## 3.5.1 脚本与 ZIP 缓存

`script_assets` 管理有界 ZIP 解压缓存、并发合并、频率衰减、源文件校验；`script_isolate` 管理独占运行时池与逐次上下文；`script_bytecode` 只序列化本进程编译的可信源码。不得接受外部 QuickJS 字节码，也不得让旧上下文的任务跨调用执行。

`python tests/script_cache_integration.py <程序>` 验证 Server/FileTrans 的状态隔离、运行时回收、待执行任务丢弃、异常恢复，以及 ZIP 权限、合并、容量、退火、CRC、源包失效。`tests/script_performance.py --before <旧程序> --after <新程序> --output <JSON>` 使用相同的普通脚本及 ZIP 静态资源进行回环短压测；报告包括二进制哈希、请求错误及延迟，不代表实际局域网或持续容量。发布验证入口已加入该功能回归。
