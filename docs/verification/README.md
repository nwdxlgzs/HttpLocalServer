# 3.5.0 验证

最终产物和准确校验和见 [release-v350.json](release-v350.json)、dist/SHA256SUMS.txt。Windows 36 项通过，8000 被用户服务占用而跳过默认端口检查；musl 36 项通过。两个平台另通过 Wasm/MCP 的 HTTPS 检查、3 组 CTest，以及 96 客户端 / 768 次请求的本地混合下载（完整性失败 0）。Windows Chrome 对 HTTP/HTTPS 做了应用与 localhost 管理来源的实际隔离验证。

主报告 acceptance-v350-*.json 逐项保留命令结果和日志；补充测试日志在对应目录。播放器测试替身缺少 CSSStyleDeclaration.setProperty 已修复，加入音量填充断言；失败项和后续项在相同二进制 SHA-256 上续跑通过。没有复用前版的浏览器场景数量。

WAMR/WebAssembly/WASI 的支持子集和执行边界见 ../quickjs-wasm.md；MCP 版本、头/元数据、旧握手见 ../mcp.md。Windows 导入表仅系统 DLL，目标 subsystem 6.01；Win7 未实机测试。Linux-musl 无动态节与 interpreter。macOS 仅源码/CMake 适配，按要求未测试。IE11 本次只做静态兼容检查，沿用用户既有实机验收结论。

---

# 验证记录

2026-11-14，3.2.0 增加默认关闭的 ZIP 预览三态、按需展开与压缩炸弹预算、管理器内预览标签、Silurus Office 预编译 Wasm、旧浏览器基础 Office 预览，以及 PDF.js/ES5 PDF 渲染。包内网页同时覆盖 HTTP 和 HTTPS 的主机/客户端模式，真实浏览器核对了复制、JPEG2000、12 标签上限和暗色移动端。详细预算见 [../previews.md](../previews.md)，IE11 人工清单见 [ie-v320.md](ie-v320.md)，最终产物与验收范围见 [release-v320.json](release-v320.json)。

2026-11-13，3.1.0 完成复制文件内续传、目录直存的本地分块暂存、磁盘/私有暂存 ZIP 压缩器检查点、可恢复浏览器挂载上传，以及跨挂载复制/版本核对/条件删除。ZIP 压缩保持在客户端执行；主机复用已有暂存字节发布挂载副本，不要求提供方重复上传。两端真实进程、分离页面上下文、标准 ZIP 解压、前缀变化检测、丢失确认和跨挂载链路均有测试。最终产物与准确范围见 [release-v310.json](release-v310.json)，IE11 增量清单见 [ie-v310.md](ie-v310.md)。复现入口为 `tools/maintenance/verify_release.py`。

2026-11-13，3.0.1 补齐公共任务调度、按需挂载优先级与依赖让位、批量移动的真实同名询问和目标版本检查、浏览器提供方移动，以及复制/目录保存/磁盘 ZIP 的检查点恢复。清单与游标分开持久化，避免碎文件重复写入整份元数据；P2P 磁盘任务也可重新授权恢复，输出变化会拒绝覆盖。两平台原生回归、浏览器脚本与 ZIP 内容验证的准确范围见 [release-v301.json](release-v301.json)。IE11 仅做本轮 ES5/缺少现代 API 的静态和启动检查，手工增量清单见 [ie-v301.md](ie-v301.md)。macOS 按用户要求只保留源码适配。

2026-11-13，2.8.0 将默认普通下载与专用 P2P 明确分开，发送端默认静默参与且不询问共享。所有文件共用整页0–64MiB缓存，默认64MiB；主机动态设置空闲有效期，默认一小时，核实节点协商或实际传输时续期，纯轮询不续期。下载完成后保留缓存供后续任务复用。普通 HTTP 的96MiB文件未提供P2P入口，16MiB专用P2P直接启动并完成，没有共享确认；大文件仅在具备安全本地写入API时提供专用入口。Windows/Linux-musl的CTest3/3、P2P及HTTP/HTTPS控制回归通过；Windows原有HTTP/HTTPS/访问回归通过。Windows8000已被用户另一个程序占用，因此本轮不宣称零参数8000验收通过；用户服务保留，Linux零参数通过。最终产物、测试范围及限制见 [release-v280.json](release-v280.json)，设置截图见 [v280-cache-settings.png](v280-cache-settings.png)。

2026-11-13，2.7.1 将发送者与下载者选择分离：主机 PCDN 总开关默认关闭，开启后有能力的页面默认按需发送；页面关闭发送后，后续所有事件均保持关闭，不持久化，刷新/重开恢复默认。HTTP 使用内置 SHA-256 / Worker 回退，HTTPS 优先 WebCrypto；本地写盘仍遵循浏览器 API 条件。真实浏览器通过非回环局域网 HTTP 地址验证：未主动下载的页面自动分发 16 MiB，关闭自身发送的接收页仍从节点取得全部数据；缓存复用时，16 MiB 节点接收对应主机响应正文增量约 15 KiB。Windows/Linux-musl CTest 与 HTTP/HTTPS 中继控制测试、ES5 静态及相关回归通过。范围与限制见 [release-v271.json](release-v271.json)，截图见 [v271-http-pcdn.png](v271-http-pcdn.png) 和 [v271-http-sender.png](v271-http-sender.png)。

2026-11-13，2.7.0 增加默认关闭的浏览器协作下载（P2P / PCDN）：WebRTC 节点传输、按需 SHA-256 分片校验、错误/缺块回源、有界缓存与信令、磁盘写入/续传及暂停/取消。Windows、静态 Linux-musl 均通过 CTest 3/3、P2P 控制与哈希测试，以及原有 HTTP / HTTPS / 登录 / 防盗链回归；另有不含 TLS 的独立构建和 P2P 测试。真实浏览器两个页面完成 8 MiB 传输，第二页全部来自节点、主机文件正文 0 B，两个保存文件与源文件哈希一致。磁盘 API 使用句柄替身验证，真实保存选择器、超大文件长时负载及跨物理机器尚未验收。准确范围及最终产物见 [release-v270.json](release-v270.json)，截图见 [v270-peer-download.png](v270-peer-download.png)，说明见 [../p2p.md](../p2p.md)。

2026-11-11，2.4.0 增加常驻配色按钮、主机根/分区/目录映射、浏览器持续挂载与逐步复制到主机、按需优先复制、直接保存到本地文件夹，以及统一/指定 IP 上传额度。Windows 与静态 musl 完成 CTest、挂载提供方、并发 Range、离线副本、额度预留和已有 COW/协议回归；浏览器实际确认盘符视图、主题、主机挂载表单及额度保存。目录句柄操作和直接保存使用 API 测试替身验证，原生授权弹窗尚未完成实机端到端验收。准确范围见 [release-v240.json](release-v240.json)，行为说明见 [../mounts.md](../mounts.md)。

2026-11-11，2.3.0 本轮完成文件夹上传、按需 IP 目录与空目录回收、姓名主机审批、矮屏侧栏和目录更新提示/个人自动刷新。Windows 与 Linux musl 均通过最终 CTest 3/3、核心 HTTP、大文件 Range、COW 与新协作接口回归；新测试包括 48 路并发检测、多浏览者登记以及 45 秒异常离线超时回收。浏览器实测完成真实目录上传、审批后胶囊直接更新、默认提示与自动刷新，并检查 400px 矮屏不覆盖导航。当前产物哈希、准确验收范围和限制见 [collaboration-v230.json](collaboration-v230.json)，效果见 [collaboration-v230.png](collaboration-v230.png)。macOS 本轮未构建或测试，IE 增量手工步骤见 [../ie11-verification.md](../ie11-verification.md)。

以下为历次验证背景。
2.2.1（2026-11-10）：记录用户反馈，2.2.0 的 IE 验收无问题，具体系统与 IE 版本未提供。新版仅当前标签带底部外扩，标签布局重叠 18px，非活动标签底边收平；主机通过文件右键维护多个关注标记，管理面板移除单文件推荐项。关注上限 128，逐项操作使用同一个写锁发布，重命名/删除同步更新。`v221_integration.py` 验证越界、无权限、只读主机标记、并发无丢失、幂等、容量释放、重命名和目录前缀边界；C++ 写入测试额外验证远端即便持有页面令牌并伪造转发头也不能修改标记。具体发布产物和新版浏览器验证见 `release-v221.json`。


2026-11-10，2.2 完整服务器在 Windows x64 与静态 musl x86_64 上通过 CTest、integration / v2 / v21 / v22 HTTP 套件、零参数默认值及混合下载测试。两者均完成 96 客户端、768 次请求（96 个 8 MiB 文件 + 672 个碎文件），无内容错误，退火后正文缓存驻留与实际分配均归零。吞吐仅代表本机功能负载，不代表物理局域网极限。结果见 `windows-load-v22.json`、`musl-load-v22.json`。

GCC9 Linux ASAN + UBSAN 构建通过 CTest 和全部 HTTP 套件，最终网页资源更新后再次通过 CTest / v22。Windows x86 推理核心也通过全部当前可用内核黄金概率、并发与回收测试。聚合 ARM / RISC-V 内核的验证范围见 `architectures-v22.json`，不把模拟器运行当作硬件性能数据。

2.2 浏览器实测包括 Chrome 形标签、拖动保留活动目录、12 个标签上限、溢出箭头、连续关闭宽度保持、浅/深色、390px 窄屏无横向溢出、JSON 每批 100 项展开、Markdown HTML 转义、关联站内图片，以及网页相对 CSS / 根路径脚本 / JSON 的实际加载。新窗口 WebM 完整播放器可解码、播放、定位。主机启用缩略图后，第二客户端从自己的 PNG 预览生成图标；主机关闭后第二客户端缓存清空。目录 ZIP 浏览器执行完成；同一发货脚本另经标准 ZIP 解压、CRC、中文名、空目录及文件逐字节校验。

已保存 `grid-v22.png`、`json-v22.png`、`mobile-v22.png`。IE11 由用户按 `../ie11-verification.md` 进行实机验收；Win7 / IE11 尚无实机通过结论。QEMU 软件包已卸载，一次性源码迁移与裸机探针源文件已删除；批量删除 `.deps` 缓存被自动审批拒绝，因此保留这些非发布缓存。

以下保留此前版本的验证背景。

日期：2026-11-10。所有自动化测试使用生成的临时数据；网页验证使用独立测试目录。

最终发布文件大小、SHA-256、内嵌网页哈希和截图尺寸见 `release-v21.json`；发布文件校验清单为 `dist/SHA256SUMS.txt`。

| 产物 | 环境 | 结果 |
|---|---|---|
| Windows x64 | MSVC 19.44，C++17，/MT，subsystem 6.01 | Magika/核心/写入、三组 HTTP 集成、零参数、线程扩缩、混合负载通过；仅导入系统 DLL |
| Linux musl x86_64 | Alpine 3.22.2，GCC 14.2，musl 1.2.5，静态链接 | 同上；无 ELF interpreter，无动态 NEEDED 项 |
| Linux 内存检查 | Ubuntu GCC 9.4，AddressSanitizer + UBSan，非 PIE | 三组 C++ 测试与三组真实 HTTP 测试通过，包含 Magika、上传、下载、权限、媒体 Range 和版本隔离 |

Windows 7 与 IE11 尚未在真实机器或 VM 中运行验收。Windows 已设置 Win7 API 与 PE subsystem 下限；当前可执行文件由本机 MSVC 14.44 构建，未使用尚未安装的 v142。直接导入检查不替代目标系统运行测试。

## 协议、权限与资源

- 完整/HEAD/304/206/416、UTF-8、Keep-Alive、版本固定续传、5 GiB 稀疏文件的 4 GiB 以上偏移。
- 只读、共享写入、独立 IP 目录及本机管理员；独立模式的源/目标重命名权限、前缀相似的其他 IP、管理根保护。
- CSRF 令牌、Origin/Host、父目录穿越、保留设备名、ADS、路径/组件长度、内部临时文件不可访问。
- 8 MiB chunked 流式上传与内容哈希、64 个并发上传后下载校验、无覆盖、上传中禁止修改祖先、中断清理、上传大小上限与 100-continue。
- 默认 100 项分页、目录变更失效、类型签名及后缀建议；预览需显式开启，HTML/SVG 直接地址带 CSP sandbox，IE 主动文档强制下载降级。
- TXT 接口仅接受 `.txt`、最多 256 KiB；媒体预览的 206、HEAD、正确 MIME、inline 响应与旧版本 409。
- 不同 IP 名额的占用/拒绝/断开释放；自动网络线程增长到上限 4，再撤回 2，存储/元数据池撤回 2/1；持续状态轮询也能撤回。
- 同块缺失合并、热点保持、一次性扫描抗污染、缓存硬上限与空闲 OS 页面释放。

`tests/core_tests.cpp`、`tests/write_tests.cpp`、`tests/magika_tests.cpp`、`tests/integration.py`、`tests/v2_integration.py`、`tests/v21_integration.py`、`tests/defaults.py` 记录可重复运行的测试。

## Magika

固定 `standard_v3_3` 模型，实际运行自有 C++ 推理器。x64 对 13 组输入和全部 214 类输出与官方 ONNX Runtime 交叉验证，所有内核分类一致，最大绝对概率误差 `2.86102294921875e-6`，结果见 `magika-x64.json`。另验证首尾窗口、空白、填充方向、二进制字节、并发推理、512 项结果缓存、同版本并发合并、版本失效、模型页空闲回收与重新加载。

Windows x86 完整推理通过；QEMU 中的 ARM32 NEON、ARM64 NEON/SVE、RISC-V64 RVV 完整黄金概率/并发/回收通过。RV64 同时验证 VLEN 128/256 及关闭 V 后标量回退；RV32 仅完成 VLEN 128/256 的独立矩阵算术测试，尚未完成完整推理器与服务器运行。详细范围见 [../magika.md](../magika.md)。

## 混合下载负载

两平台分别 96 个客户端，共 768 次请求（96 次 8 MiB、672 次碎文件），各传输 810,915,840 字节。零内容校验失败、零传输错误、零过载拒绝，冷却后内容缓存的 residentBytes / allocatedBytes 均为 0。

原始结果：`windows-load-v21.json`、`musl-load-v21.json`。这是本机 loopback 的功能负载，包含 Python 客户端和校验开销，不代表物理局域网、磁盘冷读或 NIC 的极限吞吐。v1/v2 的报告与截图保留作历史记录。

## 网页

实际浏览器验证：首屏 100 项，滚动加载后 DOM 只保留视口及缓冲行；列表/多列网格、地址输入、前进/后退禁用边界、标签新建/关闭并保留至少一个；原生下载、记录清空、文件上传和 PNG 推荐后缀补齐、网页侧栏脚本执行与相对资源加载。

v2.1 验证非活动标签边框/底色、随标签排列的“+”、最多 12 标签、104px 下限后箭头切换且无覆盖标签的滚动条、鼠标拖动重排后仍保留活动标签与目录。验证 TXT 内容、SVG/PNG 图片、WAV 音频与 WebM 视频真实解码、播放/暂停、进度跳转、倍速、静音、全屏进出和键盘控制；关闭预览释放媒体源。右键菜单仅支持的类型显示预览及新窗口，侧栏无新窗口按钮。390px 窄屏检查媒体控件、标签箭头与工具栏。

JavaScript 经 Acorn 8.15.0 的 ecma5 模式解析通过，不将 Acorn、Node 或 npm 作为运行/构建依赖。IE11 实机行为仍需最终验收，不能仅凭 ES5 语法通过宣称完成。

当前截图：`list-v21.png`、`grid-v21.png`、`media-v21.png`、`image-v21.png`、`mobile-v21.png`。
