# Server 工作区应用（3.5）

```text
HttpLocalServer server --root ./data --workspace-app ./application.zip --script-fs readwrite
```

压缩包根部必须有 `server.qjs`，入口采用 ES 模块。支持包内相对导入 `.js` / `.qjs`，以及 `import { fs } from 'hls'`。后端模块不会作为静态文件公开；默认仅 `public/` 中的静态资源可访问，`/` 对应 `public/index.html`；未处理路径返回 404。应用不能覆盖内置管理接口；自定义 HTTP 处理器可使用普通路径和 `/api/app/` 前缀。

```js
import { fs } from 'hls';
import { reply } from './lib/reply.js';

let visits = 0;
export function onStart() {
    fs.mkdir('/records');
}
export function onRequest(request) {
    if (request.url === '/api/app/count')
        return reply(String(++visits));
    if (request.url === '/api/app/save' && request.method === 'POST') {
        fs.writeText('/records/result.json', request.body);
        return reply('已保存');
    }
    return {}; // 查找 public/ 静态资源，不回退到真实共享目录
}
```

`lib/reply.js`：

```js
export function reply(body) {
    return { body, contentType: 'text/plain; charset=utf-8' };
}
```

请求字段和返回结构沿用 `--script` 的 `onRequest` / `onFileOperation` 协议。工作区入口只初始化一次；模块内变量在当前进程中保留，重启清空。钩子必须同步返回 JSON；没有 Node.js、进程执行、网络模块、任意磁盘模块导入或后台事件循环。`--script` 原有的逐次独立执行语义保持不变。

文件 API 的 `/` 指向 `--root` 的真实目录，不是操作系统根，也不经过浏览器挂载。仅显式通过 CLI 加载的工作区脚本享有这个应用能力；它默认只读；显式 --script-fs readwrite 后，启动阶段和 POST/PUT/PATCH/DELETE 钩子可写文件，独立于 FileTrans 上传开关。网页请求仍通过监听地址、Origin、登录和请求体大小检查；应用负责自己的业务用户权限验证。默认远端不能访问文件管理、预览、下载和共享 API；需要时显式 --workspace-share。Host/Origin、全局登录口令和真实 IP 检查仍在应用之前执行。

| API | 行为 |
|---|---|
| `fs.readText(path)` | 读取完整 UTF-8 文本，最多 2 MiB |
| `fs.readBytes(path)` | 读取 ArrayBuffer，最多 2 MiB |
| `fs.writeText(path, text)` | 以 UTF-8 原子写入，最多 2 MiB |
| `fs.writeBytes(path, buffer)` | 原子写入 ArrayBuffer，最多 2 MiB |
| `fs.mkdir(path)` | 创建单层目录，已存在时成功 |
| `fs.remove(path)` | 删除文件或空目录，不递归删除 |
| `fs.stat(path)` | 返回 directory，文件另有 size 和 version |
| `fs.list(path)` | 列出可公开子项名称，最多检查 1000 项 |

父目录需要先存在。API 拒绝 `..`、符号链接、重解析点、设备名、配置及其受保护物理别名、私有状态、TLS 密钥和工作区包。写入使用临时文件和版本检查，与服务端文件变更共用互斥锁；繁忙时抛出异常，调用方可选择重试。脚本同步文件调用的系统磁盘等待不属于可中断 JavaScript 指令，勿用慢速网络文件系统运行后端工作目录。

工作区包上限 32 MiB；模块单个 1 MiB、合计 8 MiB；静态单个响应上限 2 MiB。使用与 ZIP 预览相同的路径、条目、压缩比、CRC 和实际解压预算检查。JS 内存和执行期限沿用 `--script-memory-mb`、`--script-timeout-ms`；默认 16 MiB / 50 ms，可由主机 CLI 调整。单个工作区的执行串行，排队超过执行期限返回繁忙，不无限占用 HTTP 工作线程。共享大文件请通过正常文件下载接口提供。

3.5.1 起，ZIP 静态资源在首次完整解压并通过 CRC 后保留在进程内，默认上限 16 MiB。`--workspace-cache-mb N` / config 的 `workspace-cache-mb` 可设为 0–256；0 关闭保留缓存。同一资源的并发冷请求合并解压，不同资源可独立解压；解压不占用 QuickJS 执行锁。缓存键使用 ZIP 内完整规范路径，查询参数不会产生重复副本。每次请求仍运行 `onRequest`、登录/来源/网络访问检查以及后端模块私有性检查；缓存不保存业务接口返回值。

容量包含保留正文和每项元数据预留，最多 512 项；活跃 HTTP 响应的正文不计入保留缓存。解压队列另外限制为 32 个不同资源、合计 8 MiB 声明输出，并继续执行原有 ZIP 实际输出/压缩比/处理时间预算。超过解压预算或等待期限返回 503，失败的内容不会入缓存。两级频率计数定期衰减，按近期频率及内容体积选择保留项；`--heat-half-life` 控制热度半衰期，`--cache-idle` 控制空闲回收。源包被原地修改时拒绝缓存命中并清空缓存，更新应用包仍需重启；打开的旧文件代在原子替换后保持一致，不自动切换代码和静态资源。

主机控制入口的 `/api/status` 中，`script.assets` 提供命中、合并、实际解压次数、保留容量、释放量和当前加载项数量。该缓存减少解压和读盘，不改变持久应用脚本串行执行的语义。

## 配置与访问边界

```json
{"version":1,"options":{"mode":"server","root":"./data","workspace-app":"./application.zip","workspace-arg":["production","--name=LAN"],"workspace-public":"public","workspace-access":"lan","workspace-allow-ip":["192.168.1.0/24"],"workspace-share":false,"script-fs":"readonly","wasm":true,"wasm-memory-mb":64,"wasm-instances":8,"wasm-instructions":1000000}}
```

mode 可选 filetrans/server；静态 Server 可以不带 ZIP，ZIP 必须配合 Server。JSON 磁盘路径相对于配置文件，公共目录是 ZIP 内路径。CLI 标量覆盖 JSON；CLI 出现 --workspace-arg 或 --workspace-allow-ip 时整个对应 JSON 数组由 CLI 重复参数替代。--workspace-args-clear 清空参数，脚本通过 hls.args 读取，不经过 shell。切回 FileTrans 时删除 workspace-app 或同时设 --workspace-app=。

workspace-access 为 lan（默认）或 host。非空 workspace-allow-ip 进一步限制远端实际 TCP IPv4/IPv6 地址，支持 CIDR，不信任转发头。主机不受该白名单限制；--strict-host 仍仅可从 CLI 设置。workspace-share 默认 false；即使开了，也不能绕过原有登录、冻结、额度及写模式。

script-fs 为 off / readonly（默认）/ readwrite。off 同时禁止 ZIP、类型识别接口的真实文件读取；readwrite 只允许启动与有写方法的请求修改根目录。GET/HEAD 和文件操作钩子禁止直接写磁盘。

应用写请求必须具有本站 Origin，或原生客户端显式设置 X-HLS-App: 1；跨站 Origin 和跨站简单表单被拒绝。显式返回 {file:'/reports/a.csv'} 可流式提供允许读取的主机文件；ZIP 应用禁止 rewrite 回退到宿主静态目录。应用不能覆盖内置控制路由。

旧包可用 --workspace-public / 迁移；根部后端 JS/Wasm 仍不公开，浏览器代码请放在 public/。应用只有在所有实际配置/状态/ZIP/TLS 依赖登记冻结后才初始化。public/ 中的普通同名文件不因宿主依赖的名称重叠而冻结。

内置组件、WAMR 预算与 WebAssembly/WASI 的明确兼容范围见 [QuickJS/Wasm](quickjs-wasm.md)。MCP 最新协议及旧客户端兼容见 [MCP](mcp.md)。

## 可运行示例

```text
python tools/apps/pack_workspace.py examples/workspace-app examples/workspace-demo.zip
mkdir examples/workspace-data
HttpLocalServer --config examples/config-server.json
```

浏览器打开启动 URL 即可调用 ZIP 中的 Wasm 加法模块；后端在 results/latest.json 原子保存结果。示例前端保留 ES5/XHR。默认根目录和应用包均相对于示例配置文件。

## 管理来源隔离

ZIP 应用使用配置的监听端口（默认 8000）。它与主机管理器、MCP 使用不同的 HTTP origin：启用 ZIP 时额外绑定 127.0.0.1 的独立管理端口，CLI 打印 `Host control` 和正确的 MCP URL。默认随机分配，可用 CLI / config 的 `workspace-control-port` 固定端口（0 为自动，不能与应用端口相同）。该端口不监听 LAN。

应用端口上的请求不会因访问者是本机而得到管理权限；默认不能读取 session、主机设置或共享 API。--workspace-share 可额外开放普通共享权限，但不会授予主机权限。管理端口的根路径跳到 /_hls/，不提供 ZIP 前端或自定义应用路由，避免应用页面借同源脚本读取管理令牌。全局登录、来源核验、写令牌、MCP Bearer 仍然有效。原有 FileTrans 和没有 ZIP 的静态 Server 仍使用原来的单端口。

管理 URL 固定使用 localhost，应用 URL 固定使用数值 IP。两端同时核验 Host，应用端口拒绝 localhost，管理端口拒绝数值 Host，兼顾 IE 忽略端口的同源行为。HTTPS 自备证书用于工作区管理时需包含 localhost 的 SAN。来源：[Microsoft IE 同源说明](https://learn.microsoft.com/en-us/aspnet/web-api/overview/security/enabling-cross-origin-requests-in-web-api)。
