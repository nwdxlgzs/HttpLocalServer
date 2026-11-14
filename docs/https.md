# 可选内网 HTTPS

2.6.0 增加静态链接的 mbedTLS 3.6.7。应用默认继续使用 HTTP；启用 `--https` 后，当前 `--bind` / `--port` 改为 HTTPS，默认端口仍是 8000。同一监听不混合 HTTP/TLS，也不额外开启明文重定向端口。FileTrans 和 Server 共用此能力。

## 启动

使用已有证书，证书链按叶证书到中间 CA 排列，私钥必须匹配且不加密，支持 PEM / DER；RSA 至少 2048 位，EC 至少 256 位。

```powershell
.\HttpLocalServer.exe --https --tls-cert D:\HLS-Certs\server.pem --tls-key D:\HLS-Certs\private.pem --root D:\Share --port 8443
```

客户端打开 `https://主机IP:8443/`。使用 IP 访问时，证书的 Subject Alternative Name 必须包含该 IP；使用 localhost 时需包含 DNS localhost。局域网 CA / 自签名证书的信任由客户端设备管理，程序不会安装根证书或修改浏览器安全设置。证书不受信任时，HTTPS 不会自动让浏览器目录挂载等安全上下文 API 可用。

临时使用可直接启动：

```powershell
.\HttpLocalServer.exe --tls-self-signed --root D:\Share
```

此参数同时启用 HTTPS。程序在内存生成 RSA-2048 / SHA-256 证书与私钥，包含 localhost、回环及启动时网卡 IP 的 SAN，有效期 30 天，**每次启动更换**，不写证书/私钥文件。控制台显示证书 SHA-256 指纹。需要稳定信任时使用自己的持久证书；不能将临时模式当作已受信任证书。

证书、私钥只在启动时读取，变更需要重启。私钥不进入网页、MCP 或管理设置。指定私钥的物理路径和已有硬链接禁止通过共享路由读写，原生挂载别名同样检查；包含私钥的目录也禁止由网页删除/改名。仍建议将证书材料保存在共享目录外，并设置仅运行账户可读的系统权限。私钥文件的离线副本与管理员自行另存的备份无法按内容自动识别，不应放入共享空间。

## 兼容与资源

仅启用 TLS 1.2 / 1.3：TLS 1.2 优先使用 ECDHE-RSA / ECDHE-ECDSA + AES-GCM，末位保留 AES-128-CBC-SHA256 的 ECDHE-RSA / ECDHE-ECDSA 套件兼容 Win7 Schannel；TLS 1.3 使用 AES-GCM / ChaCha20-Poly1305。ALPN 只宣告 HTTP/1.1。禁用重协商；不接受 TLS 1.0/1.1 或 RSA 密钥交换。IE11 需要系统启用 TLS 1.2、支持相应套件及证书算法，并已信任服务器证书；本轮未执行 IE / Win7 实机 HTTPS 核验。

传输通过有界内存 BIO 接入原异步 socket；无每连接专属阻塞线程。文件仍按块读写，Range 的字节偏移不变。TLS 增加记录缓冲、加密 CPU 和每连接内存开销，原连接/传输上限继续有效。AES 指令等能力由 mbedTLS 自己检测，不把整个程序强制编译为某一指令集。

`--tls-handshakes N` 默认 32，限制同时在握手的连接；`--tls-handshake-timeout SEC` 默认 10 秒，是整个握手的绝对期限，不因慢速滴流重置。超限或握手失败直接关闭连接。TLS 会话票据只保存在客户端、使用进程内随机密钥加密，生命周期 1 小时，重启失效；TLS 1.3 恢复仍使用临时密钥交换，不启用 0-RTT。

来源校验、防盗链 Referer、预览资源 CSP、MCP 下载 URL、启动地址与二维码均跟随实际 HTTPS 协议；Cookie 附带 Secure。TLS 不改变主机身份、IP 权限、登录口令和写令牌规则，不信任转发头来取得 HTTPS 或主机身份。

## 构建

普通 CMake 默认 `HLS_ENABLE_TLS=OFF`；TLS 关闭构建不编译或链接 mbedTLS。此次 `dist` 发布程序内置 TLS，启动默认仍是 HTTP。

```sh
cmake -S . -B build-tls -DCMAKE_BUILD_TYPE=Release -DHLS_ENABLE_TLS=ON
cmake --build build-tls --parallel 4
```

Windows 构建脚本可以先 `set HLS_ENABLE_TLS=ON` 再运行 `tools\build-msvc.cmd`；Linux-musl / macOS 为 `HLS_ENABLE_TLS=ON sh tools/build-musl.sh` / `HLS_ENABLE_TLS=ON sh tools/build-macos.sh`。macOS 只保留源码适配，本轮按原要求不编译、不测试。

官方源包、SHA-256 与版本固定在 `tools/dependencies/mbedtls-lock.json`，普通构建离线校验、展开并静态链接。重新获取执行 `python tools/dependencies/fetch_mbedtls.py`；无需 OpenSSL、Rust、Zig 或运行时 DLL。选择上游 Apache-2.0 许可选项，原始完整许可保存在 `licenses/mbedtls-Apache-2.0.txt` 并随 TLS 构建内嵌到 `--licenses`，保留闭源商业分发能力。

## 自动核验

`python tests/tls_integration.py <程序>` 使用真实 TLS 客户端验证协议、ALPN、信任与 SAN、RSA / ECDSA、Secure Cookie/来源、防盗链、预览资源、MCP/二维码、流式上传/gzip、超过 4 GiB 的 Range、Keep-Alive、并发、分片握手、私钥别名保护及握手预算。测试证书临时生成，仅测试需要 Python cryptography；应用无此依赖。原有 HTTP 回归也应在带 TLS 的程序上继续执行。
