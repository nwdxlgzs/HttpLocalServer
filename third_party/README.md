# 固定的构建输入

这些压缩包只用于构建，部署时仅需 HttpLocalServer 可执行文件。CMake 自动校验 SHA-256 并离线解压；构建不需要联网或包管理器。

| 输入 | 版本 | SHA-256 |
|---|---|---|
| boost-headers-1.83.0.tar.xz | Boost 1.83.0 headers | `9e073d635431f223add11bcf52fadf245654c4a8650c3a5e9b74cf941d41db70` |
| sqlite-amalgamation-3510200.zip | SQLite 3.51.2 | `6e2a845a493026bdbad0618b2b5a0cf48584faab47384480ed9f592d912f23ec` |
| mbedtls-3.6.7.tar.bz2 | 可选 HTTPS 后端，Mbed TLS 3.6.7 LTS | `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6` |
| quickjs-ng-0.18.0.tar.gz | QuickJS-NG 0.18.0 | `28bcd89aa3683fd2a946537e5ce3f11cc46b5cbf3383c7561aaef3df5ff9f1dd` |
| wamr-2.4.5.tar.gz | WAMR 2.4.5 | `1ab09d51099f276ca4a1d6629f6b589aab2bd0caa01445e05031a4bed22c199b` |
| magika/standard_v3_3.hlsm | 固定图内嵌权重 | `6aead17218a39a3db9f754e7a77364d7135e9ead26cdd243c64f4997e9fdd61e` |

Boost 头文件来自 Ubuntu 官方 `libboost1.83-dev_1.83.0-2.1ubuntu3.2_amd64.deb`，仅重新打包 `usr/include/boost`。原包版权清单保存在 `BOOST_PACKAGE_COPYRIGHT.txt`。SQLite 来自官方 2026 年 amalgamation 压缩包。

应用实际使用 Boost.Asio / Beast 和 SQLite FTS5；没有链接本目录 Boost 版权清单中提及的其他可选库或构建工具。

Magika 输入固定至 Google 官方仓库提交 `8575380e2e17b02b40eabdd703d2dc6b23c08af8`，完整文件哈希见 `magika/manifest.json`。`standard_v3_3.onnx` 为原始模型；`standard_v3_3.hlsm` 是本项目重排卷积权重后的等价浮点数据，CMake 校验并嵌入后者。`config.min.json`、`content_types_kb.min.json` 用于生成标签和阈值，原始 Apache-2.0 许可证保存在 `magika/LICENSE`。

正常离线构建使用已生成的数据和 C++ 头文件，无 Python、Rust 或 ONNX Runtime 依赖。仅重新导出模型时执行 `python tools/models/export_magika.py`（需要开发用 `numpy` 与 `onnx`）；与官方概率交叉验证另需要开发用 `onnxruntime`。这些工具不随应用分发。自有推理器仅执行这个固定图，不加载外部 ONNX。

网页依赖 marked 4.3.0 和 fflate 0.8.2 放在 `web/vendor`，MIT 许可证在 `licenses`。下载地址、npm integrity 和展开后 SHA-256 固定在 `tools/dependencies/web-lock.json`；通过 `python tools/dependencies/fetch_web.py` 校验或重新获取。依赖脚本不在运行时联网。

Mbed TLS 只在 `HLS_ENABLE_TLS=ON` 时构建，使用官方完整发布包（含生成文件与子模块），不修改上游源码；线程回调及配置在 `src/tls`，来源锁在 `tools/dependencies/mbedtls-lock.json`。上游双许可选择 Apache-2.0，完整原始许可在 `licenses/mbedtls-Apache-2.0.txt`，TLS 构建将其内嵌。

QuickJS-NG 使用官方 v0.18.0 标签压缩包（2026-11-01），MIT；来源与哈希在 `tools/dependencies/quickjs-lock.json`，`fetch_quickjs.py` 可校验或重新获取。仅静态编译引擎核心，不带 libc、OS 绑定或模块加载器。

WAMR 使用官方 WAMR-2.4.5 标签归档，Apache-2.0 WITH LLVM-exception。锁与获取脚本为 `wamr-lock.json` / `fetch_wamr.py`，原始归档不改写；CMake 生成经典解释器的主机预算检查点适配副本，源码头明确标识改动。默认无 JIT/AOT、原生 WASI/libc、线程或 SIMD，使用软件边界检查和有界分配器，适配 Windows7、musl、macOS。许可内嵌 `--licenses`。

QuickJS-NG 另由 CMake 生成宿主 ArrayBuffer 防转移适配副本，拒绝无 realloc 回调的外部缓冲转移；此限制用于跟踪 Wasm 内存生命周期。普通 JS 自有缓冲不变，原始归档和版权头保留。
