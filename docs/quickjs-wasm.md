# QuickJS、WAMR 与内置组件（3.5）

QuickJS-NG 0.18.0 与 WAMR 2.4.5 静态嵌入单个 C/C++ 可执行文件，不需要 Rust、Zig、Node.js 或外置 VM。依赖核对日期为 2026-11-14；来源、归档哈希和许可证见 `tools/dependencies/*-lock.json`、`licenses`。

```js
import hls, {fs,wasm,sqlite,crypto,compression,types,zip,qr,args,capabilities} from 'hls';
const calculator = wasm.instantiate('/calc.wasm');
export function onRequest(request) {
    if (request.url === '/api/app/add' && request.method === 'POST') {
        const input = JSON.parse(request.body);
        return {body: JSON.stringify({result: calculator.exports.add(input.a,input.b)}),
                contentType: 'application/json'};
    }
    return {};
}
```

`hls.args` 是配置/CLI 的应用参数，不经过 shell。`hls.capabilities` 提供版本、可用功能和文件系统能力。以下实用组件也提供给 `--script` 隔离钩子；真实文件读取与 ZIP 包路径加载只在工作区应用中可用。

| 接口 | 行为 |
|---|---|
| `hls.text.encode(text)` / `decode(bytes)` | UTF-8 与 ArrayBuffer；decode 要求完整有效的 UTF-8 |
| `crypto.sha256(bytes)` | 十六进制 SHA-256；接受文本、ArrayBuffer、TypedArray |
| `crypto.digest(name,bytes)` | SHA256；TLS 构建另提供 mbedTLS 的 SHA384 / SHA512 |
| `crypto.randomToken()` | 程序已有的随机令牌来源 |
| `compression.gzip/deflate(bytes)` | 压缩 ArrayBuffer |
| `compression.gunzip/inflate(bytes)` | 单流解压与校验和检查，最多 2 MiB、压缩比最多 200（小于 4 KiB 输出除外） |
| `types.extension(name)` / `types.detect(path)` | 扩展名表 / 签名及可选 Magika 模型；不能读取冻结文件 |
| `zip.list(path)` / `zip.read(path,entry)` | ZIP 条目元数据 / 最多 2 MiB 单项 ArrayBuffer；沿用路径、CRC、压缩比和炸弹预算，不写出宿主文件 |
| `qr.svg(text)` | 最多 2048 字节文本的二维码 SVG |
| `sqlite.open(snapshot?)` | 独立内存数据库，最多 4 个；可恢复最多 2 MiB、4096 字节页的快照，不直接打开任意磁盘数据库 |
| `sqlite.query/execute(id,sql,params?)` | 一条 SQL；位置参数支持 null/number/string；返回 columns、rows、changes、lastInsertRowid |
| `sqlite.snapshot(id)` / `close(id)` | 最多 2 MiB 数据库镜像 / 关闭；持久化由受限 fs 写出镜像 |

SQL 禁止 ATTACH、DETACH、PRAGMA、扩展加载，设置防御模式、不信任 schema、2 MiB 主库页数上限、64 KiB SQL、128 列/参数、1000 返回行、执行进度中断。普通字节输入/输出最多 2 MiB。磁盘、ZIP 和模型调用另外受原有预算约束。

## Wasm

```js
const instance = hls.wasm.instantiate('/calc.wasm', {env:{report:value=>value}});
const result = instance.exports.add(1,2);
const memory = instance.exports.memory;
new Uint8Array(memory.buffer)[0] = 42; // 真实线性内存
memory.grow(1);                     // 旧视图失效，重新取得 buffer
instance.close();                  // 短期实例及时释放

const module = new WebAssembly.Module(arrayBuffer);
const second = new WebAssembly.Instance(module, imports);
second.close();
```

提供 `hls.wasm.instantiate(bytesOrPackagePath,imports?)`、`inspect(bytesOrPackagePath)`，以及 `WebAssembly.Module`、`Instance`、`validate`、`Module.imports/exports` 的同步兼容入口。另提供 Promise 形式 compile/instantiate 供库适配，但服务钩子仍必须同步完成，不运行后台 Promise 作业。

这是明确受限的 WebAssembly 接口子集。支持 i32/i64/f32/f64、多个返回值、模块自有内存及增长、导出全局变量、函数表 length/get、Bulk Memory、内部 Reference Types。i64 使用 BigInt。导入仅接受显式提供的 JS 数值函数，最多 32 参数、单个返回值。不支持导入内存/全局变量/表、JS 外部引用、SIMD、线程、GC、异常提案、Memory64、AOT、JIT 或组件模型；需要这些能力的模块应选择兼容的编译目标。兼容错误类型目前使用 TypeError。

默认每个 JS 上下文 WAMR 总分配额度 64 MiB，包括解析器、运行栈和线性内存；最多 8 实例，单个字节码 4 MiB，128 导入 / 512 导出。JS 内存额度独立，源字节副本与组件返回缓冲另受大小上限约束。预算不足时拒绝增长/实例化。实例不会因 JS 包装对象失去引用而立即释放，临时实例请在 finally 中 close；实例关闭或内存增长时旧 ArrayBuffer/视图失效。宿主管理的线性内存禁止 ArrayBuffer.transfer 系列操作，普通 JS ArrayBuffer 的转移不受影响。

每次钩子的全部 Wasm 调用共用 1000000 次解释器分派额度，模块 start 和构造函数同样计入；每 256 次分派核对一次墙钟期限。Wasm→JS→Wasm 的执行重入被拒绝，导入回调仍可访问当前内存和其他受限组件。解析、ZIP 解压、磁盘等待不是可中断 JS 指令，有独立大小、数量与解压时间预算；慢速宿主文件系统等待不能由 VM 中断。

CLI / config：`--no-wasm`、`--wasm-memory-mb 64`、`--wasm-instances 8`、`--wasm-instructions 1000000`；JS 原有 `--script-memory-mb`、`--script-timeout-ms`、`--script-concurrency` 继续有效。持久工作区串行执行，隔离钩子按并发预算执行。

## 显式 WASI 子集

```js
const wasi = hls.wasm.wasi({args:hls.args});
const program = hls.wasm.instantiate('/program.wasm', {
    wasi_snapshot_preview1:wasi.wasiImport
});
wasi.initialize(program); // 需要导出 memory，不能在 start 段提前调用 shim
program.exports._start();
const output = wasi.stdout;
program.close();
```

提供参数/空环境变量、随机数、时钟和最多 64 KiB UTF-8 stdout/stderr。无 stdin、预开放目录、真实 FD、网络、进程或完整 WASI 宿主能力；未实现项返回错误，proc_exit 转为 trap。未显式提供的 import 不会自动链接宿主 libc/WASI。

`cmake/WAMR.cmake` 生成仅替换解释器预算检查点的副本，使 start 和连续调用共享主机预算；不改写上游归档。原始许可 Apache-2.0 WITH LLVM-exception 随 `--licenses` 内嵌。依赖可通过 `python tools/dependencies/fetch_wamr.py` 校验/重取。

验证：`python tests/wasm_app_integration.py <程序> [--https]`。三平台使用相同 C/C++ 解释器，macOS 本次只做源码适配。

QuickJS 使用生成的宿主 ArrayBuffer 防转移适配副本，避免脚本把线性内存转移到未跟踪的缓冲后逃过 grow/close 失效处理；QuickJS 原始归档保持不变，MIT 原始声明随程序保留。
