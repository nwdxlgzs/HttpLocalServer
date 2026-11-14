# JSON 配置与冻结项

默认读取**当前工作目录**的 `config.json`，缺省来源缺少时，首次修改主机设置才保存；显式指定的新来源会初始化。[默认配置模板](../examples/config-filetrans.json)可复制到工作目录使用；本机配置可能包含登录口令，已从版本管理中排除。

```powershell
.\HttpLocalServer.exe --config D:\HLS\config.json
.\HttpLocalServer.exe --config D:\HLS\config.json --port 9000 --write-mode shared
.\HttpLocalServer.exe --no-config --root D:\Share
```

优先级：**内置默认值 → JSON → CLI**。JSON 的相对磁盘路径以配置文件所在目录为基准，CLI 相对路径以当前工作目录为基准。`--config` 只接受一个来源；`--no-config` 禁用配置值加载和保存，未作为本次依赖加载的同名文件不冻结。

```json
{
  "version": 1,
  "options": {
    "root": "./share",
    "port": 8000,
    "write-mode": "isolated",
    "preview": true,
    "lazy-thumbnails": false,
    "p2p": false,
    "ip-limit": 256,
    "ip-upload-mb": 0,
    "password": ""
  },
  "host": {
    "title": "在线文件下载",
    "announcement": "",
    "attentionPaths": []
  }
}
```

`options` 的数值和文本键对应去掉 `--` 的 CLI 长参数，使用同一套范围校验。开关使用布尔键：`preview`、`lazy-thumbnails`、`magika`、`mcp`、`index`、`open`、`system-root`、`show-cli-qr`、`compression`、`https`、`tls-self-signed`、`p2p`、`recycle-bin`、`wasm`、`workspace-share`。不要使用 `no-preview` 一类负开关。`mount`、`mount-rw`、`mount-root`、`mount-volumes`、`ip-upload-limit`、`response-header`、`error-page`、`workspace-arg`、`workspace-allow-ip` 接受字符串数组。同一虚拟挂载路径在 CLI 重新指定时，以 CLI 定义为准。

`--strict-host` 继续只接受 CLI。JSON 的未知键、非法类型、范围错误、超过 256 KiB、超过 8 层或格式错误均拒绝启动，保留原文件。`--help`、`--version`、`--licenses` 无需加载配置。

主机面板设置、公告、关注文件及面板新增的原生挂载保存到配置，重启恢复。保存写出当前有效的启动选项及主机设置；下次启动明确指定的 CLI 参数仍优先。监听、线程预算等结构参数在启动时生效。口令以文本保存，磁盘访问由系统权限控制。

配置路径有进程锁。保存使用独占新建的 `.hls-*` 事务文件、刷新与同目录原子替换；保存失败时接口报错，不发布新的面板设置。Unix 以 `0600` 创建配置，Windows 继承目录 ACL。`.hls-config-lock-*` 锁文件仅显示冻结占位，内容不共享；其他内部事务文件不列出。

## 冻结保护

目录列表与搜索中的配置有雪花标识、整行/卡片斜纹遮罩和“冻结”胶囊；隐藏项仍遵循访问者的开关。只公开名称和位置，不公开大小、修改时间。不能读取、下载、预览、识别、复制、写入、覆盖、重命名、移动、删除、标记或加入 P2P；主机也不能通过网页操作它。

所有冻结文件和目录均默认隐藏，不依赖系统隐藏属性。个人勾选“显示隐藏文件和文件夹”后才会出现在列表和搜索结果中，仍然禁止读写、下载及进入；分页与项目总数按这一规则计算。

保护覆盖 FileTrans、Server、网页关联资源、MCP、Range、P2P、上传、浏览器副本与原生挂载。当前启动实际依赖的配置文件、配置锁、运行状态目录、脚本和 TLS 材料冻结；普通文件不会因 basename 或 `.hls-` 前缀重叠而冻结。服务端实际文件身份检查覆盖改名硬链接和其他原生挂载映射；路径规范化、禁止符号链接及打开后身份核验阻止穿越、ADS 和路径别名绕过。

ZIP、本地目录保存、复制、文件夹上传和挂载后台复制跳过冻结项。直接删除或重命名含冻结项的目录会在修改前拒绝整个操作；网页跨挂载移动跳过冻结项，仅移除处理完的空目录，含冻结项的源目录保留。检查具有数量、深度上限。浏览器提供方也排除保留名称并检查目录子项。

配置原子替换前仍有硬链接时，旧身份存入 `protectedFileIds`，使旧链接在保存后、重启后继续冻结。内部身份最多 1024 项，达到上限拒绝扩大；不要手工删除该字段。任意改名另存形成的独立副本不具有原文件身份，需要主机自行管理。浏览器提供方当前按保留名称排除文件；服务端不能从浏览器句柄获知来源的 OS 文件身份，来源中另名硬链接和另名独立副本不在这项身份识别范围内。

验证：`python tests/config_integration.py <程序路径>`；锁文件冻结占位、索引/挂载搜索及内部目录保护另见 `python tests/frozen_lock_integration.py <程序路径>`。

3.0 的 `state-dir`、`script` 磁盘路径也按配置文件所在目录解析；流控和回收站设置随主机设置持久化。`--no-config` 不关闭 SQLite 运行状态；上传续传、额度和姓名审核的恢复及 Server 参数见 [server-and-transfers.md](server-and-transfers.md)。

ZIP 预览：`options.zip-preview` 可为 `off`（默认）、`host` 或 `client`；CLI 为 `--zip-preview`，主机面板字段为 `zipPreview`。总 `preview=false` 会一并禁用。安全预算见 [previews.md](previews.md)。

## 默认配置延迟生成

CWD/config.json 已存在时启动读取。缺省来源不存在时，启动不创建 config.json，主机首次修改设置时才原子保存；如果运行中有人在该保留位置放入文件，主机保存会替换它，继续冻结。显式 `--config PATH` 指定不存在的新来源仍会初始化。当前进程实际创建或加载的状态目录、配置锁和其他依赖项禁止网页操作；状态与锁的临时文件按依赖路径或文件身份冻结，可在显示隐藏项目时看到冻结条目，不枚举其内部内容。仅名称相同的普通条目仍可正常访问。

## Server 应用配置（3.5）

`options.mode` 可为 `filetrans` 或 `server`；应用设置包括 `workspace-app`（ZIP 磁盘路径）、`workspace-arg`（字符串数组）、`workspace-public`、`workspace-access`、`workspace-allow-ip`、`workspace-share`、`script-fs` 和 Wasm 预算。CLI 指定应用参数或 IP 白名单时替换对应 JSON 数组。静态 Server 不要求 ZIP；指定 ZIP 时必须是 Server 模式。完整示例和默认访问边界见 [workspace-app.md](workspace-app.md)，示例配置见 `examples/config-server.json`。

ZIP 应用的 `workspace-control-port` 默认为 0（自动分配），仅在 127.0.0.1 提供管理器与 MCP。可固定为一个不同于应用端口的值；保存配置仍保留 0，不把某次随机端口持久化。

实际使用的回收站路径也登记为当前运行依赖：启用回收站或从持久化记录恢复时冻结对应卷目录，默认隐藏，不能由网页、脚本或别名读取。没有实际依赖的普通同名路径不按名称一概冻结。

活跃的上传、原子配置事务及 Windows 替换备份按实际物理路径做作用域保护；活动期间可在显示隐藏时看到冻结占位，事务结束后释放登记。Linux 可读取的活跃事务文件另核验文件身份，阻止硬链接绕过；Windows 独占写句柄本身禁止读取。

ZIP 应用的保留静态缓存可用 `workspace-cache-mb` 设置为 0–256 MiB，默认 16，0 关闭；CLI 的 `--workspace-cache-mb` 仍优先。容量独立于普通文件块缓存，空闲时间与热度半衰期沿用 `cache-idle` / `heat-half-life`。普通脚本的空闲运行时回收沿用 `thread-idle`。这些预算在启动时生效。
