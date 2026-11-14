# 许可与商业分发

本项目自有 C++ 源码和网页采用 Apache License 2.0，作者 github@nwdxlgzs。完整条款见根目录 LICENSE，归属声明见 NOTICE。项目许可证与第三方声明均内置到可执行文件 --licenses。

第三方部分保持其原有许可：

- Boost.Asio / Beast：Boost Software License 1.0。
- QuickJS-NG 0.18.0：MIT；仅静态嵌入引擎核心，完整上游原始声明在 `quickjs-ng-MIT.txt`；组件归属在 `quickjs-ng-components.txt`，Unicode 数据表的 Unicode License V3 在 `quickjs-ng-Unicode-3.0.txt`，三者均内嵌。
- marked 4.3.0 / fflate 0.8.2：MIT，分别用于 Markdown 渲染和客户端流式 DEFLATE。
- SQLite：官方交付代码为公有领域。
- 可选 Mbed TLS 3.6.7：选择上游 Apache-2.0 许可选项；完整原始双许可文本在 `mbedtls-Apache-2.0.txt`，TLS 构建内嵌该声明。默认配置不启用其可选 Everest 或 p256-m 驱动。
- Google Magika `standard_v3_3` 模型、标签、阈值和特征约定：Apache License 2.0。重排权重及固定图 C++ 适配已在内置第三方声明中说明，保留版权与完整许可证。该依赖不要求公开应用自有源代码。
- Linux 静态版本的 musl：所附 MIT / BSD 等宽松许可，详见 COPYRIGHT。
- GCC 的 libgcc / libstdc++：GPL 配合 GCC Runtime Library Exception 3.1，符合例外条件的应用可以使用专有许可。例外文本和 GPL 文本一并保存。
- 选择 MinGW 构建时：保留 MinGW-w64 和 winpthreads 的相关声明。
- MSVC `/MT` 构建：Microsoft 运行库按对应 Visual Studio 许可分发。构建者需要具备相应的工具使用与再分发权利。

以上完整声明通过 `--licenses` 内置在可执行文件里，部署时无需另带许可证侧文件。编译器本体、Python、CMake、Ninja 和构建环境不随应用发布。

重新选用或新增依赖、改变静态链接方式时，应同步复核并更新声明。原始头文件中的版权注释应保留。

WAMR 2.4.5 采用 Apache-2.0 WITH LLVM-exception，原始许可在 `wamr-LICENSE.txt`，随程序 `--licenses` 分发。主机预算检查点适配由本项目提供，归档保持原样。
