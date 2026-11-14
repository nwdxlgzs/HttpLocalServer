# 内置 Magika CPU 推理

当前模型为 Google Magika `standard_v3_3`，固定源提交 `8575380e2e17b02b40eabdd703d2dc6b23c08af8`。Apache-2.0 原始许可证、来源、权重重排说明和完整声明均保存在仓库及可执行文件 `--licenses` 中。

## 执行与资源

自有 C++17 推理器只执行这个模型的固定图：2048 个字节/填充特征，embedding、GELU、归一化、卷积、池化、分类器和 softmax，输出 214 类概率。它不是通用 ONNX 解释器，不加载用户模型。正常构建和运行不需要 Python、Rust、ONNX Runtime、GPU 或在线下载。

原始 ONNX 为 3,163,737 字节；将卷积权重从 OIHW 重排为内核位置/输入通道/输出通道后的内嵌包为 3,136,960 字节。特征提取仿照官方模型约定，仅取首尾各最多 4096 字节，分别去除开头/结尾 ASCII 空白，截取各 1024 个字节，使用 256 填充。应用同时保留签名识别和低置信度回退，使用模型原始分类阈值及标签重映射。

列目录仅使用扩展名信息，不批量推理。未知文件被选择、文件上传完成、扩展名变更后的名称检查，以及需要推断 MIME 的文件响应会按需识别。按文件版本缓存最多 512 个结果，并发相同版本合并；空闲 60 秒清理。文件头签名明确时直接返回，无需运行模型。

模型页约 3.1 MiB，首次推理时分配并共享；每路推理另用约 4 MiB 临时页，完成后通过 VirtualFree/munmap 释放。默认两路，最多四路，受现有有界工作池限制。模型空闲 60 秒后释放解码后的可写页；嵌入可执行文件的只读数据仍由操作系统管理。上述推理内存不计入文件正文缓存预算。

```sh
HttpLocalServer --magika-threads 1 --cpu-kernel scalar
HttpLocalServer --no-magika
```

`/api/status` 的 `magika` 字段提供启用状态、模型、已选内核、累计推理次数、活跃数和模型页大小。首次使用前内核显示 `pending`。

## 指令集选择

| 平台 | 可选内核 | 运行时门槛 |
|---|---|---|
| 任意受支持 CPU | scalar | 基础 C++ 浮点实现 |
| x86 / x64 | SSE2、AVX、AVX2/FMA、AVX512/FMA | Windows/Linux 使用 CPUID，AVX 同时检查 OSXSAVE/XGETBV；macOS 查询系统 `hw.optional.*` 能力 |
| ARM32 | NEON | Linux HWCAP 或 Windows 处理器特性接口 |
| ARM64 | NEON、SVE | Linux HWCAP；macOS arm64 使用基线 NEON，不编入 SVE；SVE 需要对应系统及编译器 intrinsic |
| RISC-V32 / 64 | RVV | 编译器 ratified V intrinsic、Linux HWCAP + riscv_hwprobe，未显式关闭线程 V 状态 |

所有内核聚合于 `src/cpu_kernels.cpp`，通过函数级 target 属性隔离可选指令；`src/cpu_features.hpp` 按编译器、头文件和目标架构自动决定可编译的内核。普通 `src/*.cpp` / CMake 构建无需手工指定每个指令集开关；不会设置 `-march=native`。调度器与标量代码保持基础目标。编译器能力不足时不编入对应内核。自动模式先检查 CPU 和系统能力，再做含非整齐尺寸/长归约的数值自检与短时矩阵计时，从可用候选中选择。候选未通过自检则跳过，最终可回退标量；显式指定不支持的内核会报错。`--cpu-kernel` 可用名称为 `auto`、`scalar`、`sse2`、`avx`、`avx2-fma`、`avx512-fma`、`neon`、`sve`、`rvv`。

## 可复现验证与边界

`tests/magika_tests.cpp` 对所有当前可用内核比较三组完整 214 类黄金概率，验证并发限制、空闲页回收、重载与非法输入。`tests/write_tests.cpp` 另验证首尾提取、空白、填充方向、二进制字节及窗口边界。`tests/magika_reference.py` 使用开发环境的官方 ONNX Runtime 对照同一原始模型，可输出完整结果；ONNX Runtime 不链接进发布文件。

| 已验证目标 | 环境 | 范围 |
|---|---|---|
| Windows x64 / x86 | MSVC，本机 x64 CPU / x86 进程 | 标量及可用 SSE2、AVX、AVX2/FMA、AVX512/FMA 的完整推理 |
| Linux musl x86_64 | Alpine，静态可执行文件 | 完整服务器、C++ 与 HTTP 测试 |
| Linux ARM32 | GCC 9，QEMU | 标量 / NEON 完整黄金概率、并发与回收测试 |
| Linux ARM64 | GCC 9 + Clang 20 SVE 对象，QEMU 10 | 标量 / NEON / SVE 完整黄金概率、并发与回收测试 |
| Linux RISC-V64 | GCC 9 + Clang 20 RVV 对象，QEMU 10 | 标量 / RVV 完整黄金概率、并发与回收测试；VLEN 128 / 256、禁用 V 后标量回退 |
| Linux RISC-V32 | Clang 20，无运行库测试程序，QEMU 10 | 标量 / RVV 矩阵算术、长归约与尾部，VLEN 128 / 256 |

x64 对 13 组输入、全部 214 类概率与官方 ONNX 的交叉验证最大绝对误差为 `2.86102294921875e-6`，详见 `verification/magika-x64.json`。这说明固定样本下实现与参考模型接近，不表示任意文件都能正确分类。

ARM 和 RISC-V 的验证使用模拟器，不代表真实硬件速度。尚未完成 RISC-V32 的完整 C++ 运行库/推理器与服务器运行测试，也未在这些架构交付完整服务器二进制。用户于 2026-11-10 反馈 2.2.0 的 IE 验收通过，具体系统版本未提供；Win7 系统本身的实机/VM 验证范围仍未确认。

2.2 聚合实现已重新执行 ARM32 NEON、ARM64 NEON/SVE、RV64 RVV 的完整黄金概率、并发与回收测试；RV64 VLEN 128/256 和关闭 V 后的标量回退均通过。RV32 聚合文件以基础 rv32gc 编译，仅 RVV 函数带 target，VLEN 128/256 矩阵算术通过。QEMU 与一次性探针在保存结果后移除。

macOS Intel / Apple Silicon 的平台分支现已补齐，默认部署到 macOS 11.0+，支持分别构建或 Universal 2；本轮仅做源码适配，没有执行 macOS 编译、内核基准或黄金概率测试，不列入以上已验证目标。
