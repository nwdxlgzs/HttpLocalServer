# MCP（3.5）

2026-11-14 核对当前正式规范为 **2026-07-28**；兼容 **2025-11-25 / 2025-06-18** 旧握手客户端。

`HttpLocalServer --mcp` 启用 `/mcp`，采用 Streamable HTTP 的 POST JSON 响应。只接受主机设备并核验启动生成的独立 Bearer 令牌；`--strict-host` 将主机收紧到 127.0.0.1 / ::1。GET 返回 405。不声明订阅、任务、采样或日志能力。HTTP/HTTPS 共用协议处理逻辑。

现代协议不需要 initialize，每次请求带版本和客户端能力，以及匹配的 HTTP 头：

```http
POST /mcp HTTP/1.1
Content-Type: application/json
Authorization: Bearer <启动显示的令牌>
MCP-Protocol-Version: 2026-07-28
Mcp-Method: tools/call
Mcp-Name: runtime_components

{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"runtime_components","arguments":{},"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}}
```

`server/discover` 返回版本、能力、身份、缓存范围与 TTL；`tools/list` 返回只读工具集合。结果带 `resultType: "complete"` 和 `_meta.io.modelcontextprotocol/serverInfo`；工具 JSON 同时提供 text 与 structuredContent。头/正文不符返回 HTTP 400 / -32020；未知版本返回 400 / -32022 并列出支持版本；未知方法返回 404 / -32601。支持 Mcp-Name 的 Base64 sentinel 编码。

| 工具 | 功能 |
|---|---|
| runtime_components | QuickJS/WAMR/可选 mbedTLS 版本及脚本预算，不返回敏感路径/参数 |
| server_status | 运行计数和资源指标 |
| share_info | 公告及关注文件 |
| list_files / search_files | 每页最多 100 项，显式 showHidden |
| file_type | 类型推断 |
| download_url | 固定文件版本的下载地址 |
| read_text | 最多 64 KiB UTF-8 TXT、Markdown、JSON |

文件工具沿用冻结、路径、挂载和脚本访问限制。公告、文件名、内容视为数据。工具不提供写文件或主机设置权限。

旧客户端继续 initialize，随后以对应旧版本头调用；initialize 请求现代版本时回报最新握手版本 2025-11-25。现代客户端直接 server/discover。initialized/cancelled 通知返回 202。

验证：`python tests/mcp_current_integration.py <程序> [--https]`。

官方来源：[规范](https://modelcontextprotocol.io/specification/2026-07-28)、[HTTP 绑定](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http)、[Schema](https://modelcontextprotocol.io/specification/2026-07-28/schema)。

使用 ZIP 应用时，MCP 位于 CLI 打印的独立 loopback 管理端口，不能从应用端口调用。需要固定 AI 客户端 URL 时设置 `--workspace-control-port 9001`；管理端口不提供应用前端代码。
