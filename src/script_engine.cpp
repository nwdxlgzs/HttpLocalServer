// SPDX-License-Identifier: Apache-2.0
#include "script_engine.hpp"
#include "config_store.hpp"
#include "file_type.hpp"
#include "platform.hpp"
#include "script_components.hpp"
#include "script_assets.hpp"
#include "script_isolate.hpp"
#include "state_store.hpp"
#include "storage.hpp"
#include "util.hpp"
#include "zip_archive.hpp"
#include <chrono>
#include <cstring>
#include <memory>
#include <quickjs.h>
#include <set>
namespace hls {
bool safe_response_header(const std::string &name, const std::string &value) {
    if (name.empty() || name.size() > 128 || value.size() > 8192)
        return false;
    for (unsigned char c : name)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    for (unsigned char c : value)
        if (c < 32 || c == 127)
            return false;
    const auto n = lower_ascii(name);
    for (const auto *reserved :
         {"content-length", "transfer-encoding", "connection", "upgrade", "content-encoding", "set-cookie", "location",
          "access-control-allow-origin", "trailer", "host", "keep-alive", "content-range", "accept-ranges",
          "www-authenticate", "proxy-authenticate", "te"})
        if (n == reserved)
            return false;
    return true;
}

struct ScriptEngine::Workspace {
    Config config;
    std::shared_ptr<ZipArchive> archive;
    std::map<std::string, std::string> modules;
    std::set<std::string> private_modules;
    ScriptAssets assets;
    std::timed_mutex mutex;
    std::mutex *mutation = nullptr;
    std::unique_ptr<ScriptComponents> components;
    bool write_allowed = true;
    JSRuntime *rt = nullptr;
    JSContext *ctx = nullptr;
    JSValue ns = JS_UNDEFINED;
    std::chrono::steady_clock::time_point deadline;
    std::unique_ptr<ConfigStore> startup_guard;
    const ConfigStore *guard = nullptr;
    const StateStore *state = nullptr;
    Workspace(const Config &c, const std::filesystem::path &path) : config(c), assets(c) {
        auto file = File::open(canonical_root(path.parent_path()), "/" + path.filename().u8string());
        if (file->info().size > (32ull << 20))
            throw Failure(413, "Workspace ZIP exceeds 32 MiB");
        archive = std::make_shared<ZipArchive>(file);
        normalize_path(c.workspace_public);
        std::size_t total = 0;
        for (const auto &item : archive->entries()) {
            if (item.second.directory)
                continue;
            auto ext = lower_ascii(std::filesystem::u8path(item.first).extension().u8string());
            if (ext != ".js" && ext != ".qjs")
                continue;
            if (item.second.size > (1ull << 20) || (total += static_cast<std::size_t>(item.second.size)) > (8ull << 20))
                throw Failure(413, "Workspace module budget exceeded");
            auto &source = modules[item.first];
            archive->extract(item.second, [&](const void *data, std::size_t count) {
                source.append(static_cast<const char *>(data), count);
            });
        }
        if (!modules.count("server.qjs"))
            throw Failure(400, "Workspace ZIP requires server.qjs at its root");
        startup_guard = std::make_unique<ConfigStore>(config, false);
        guard = startup_guard.get();
    }
    void initialize() {
        const auto &c = config;
        rt = JS_NewRuntime();
        if (!rt)
            throw Failure(503, "Cannot allocate workspace runtime");
        JS_SetMemoryLimit(rt, static_cast<std::size_t>(c.script_memory_mb) << 20);
        JS_SetMaxStackSize(rt, 32 * 1024);
        JS_SetCanBlock(rt, false);
        JS_SetInterruptHandler(
            rt,
            [](JSContext *, void *opaque) {
                return std::chrono::steady_clock::now() > static_cast<Workspace *>(opaque)->deadline ? 1 : 0;
            },
            this);
        JS_SetModuleLoaderFunc(rt, normalize, load, this);
        ctx = JS_NewContext(rt);
        if (!ctx) {
            JS_FreeRuntime(rt);
            rt = nullptr;
            throw Failure(503, "Cannot allocate workspace context");
        }
        JS_SetContextOpaque(ctx, this);
        auto global = JS_GetGlobalObject(ctx), api = JS_NewObject(ctx), fs = JS_NewObject(ctx);
        const char *names[] = {"readText", "writeText", "mkdir", "remove", "stat", "list", "readBytes", "writeBytes"};
        for (int i = 0; i < 8; ++i)
            JS_SetPropertyStr(ctx, fs, names[i],
                              JS_NewCFunctionMagic(ctx, operation, names[i], 2, JS_CFUNC_generic_magic, i));
        JS_SetPropertyStr(ctx, api, "fs", fs);
        components = std::make_unique<ScriptComponents>(
            ctx, config,
            [this](const std::string &input) {
                if (config.script_fs == "off")
                    throw Failure(403, "Filesystem capability disabled");
                auto file = File::open(config.root, logical(input));
                guard->protect(*file);
                return file;
            },
            [this](const std::string &input) {
                auto path = normalize_path(input).substr(1);
                auto it = archive->entries().find(path);
                if (it == archive->entries().end() || it->second.directory)
                    throw Failure(404, "Package entry not found");
                if (it->second.size > (4u << 20))
                    throw Failure(413, "Package entry exceeds budget");
                std::string bytes;
                archive->extract(it->second,
                                 [&](const void *p, size_t n) { bytes.append(static_cast<const char *>(p), n); });
                return bytes;
            });
        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(c.script_timeout_ms);
        components->deadline(deadline);
        components->install(api);
        JS_SetPropertyStr(ctx, global, "hls", api);
        JS_FreeValue(ctx, global);
        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(c.script_timeout_ms);
        auto mod = load(ctx, "server.qjs", this);
        if (!mod) {
            close();
            throw Failure(500, "Workspace module initialization failed");
        }
        // Loader modules retain one context-owned reference. EvalFunction consumes
        // another; duplicating here also matches QuickJS-NG compiled-module ownership.
        auto value = JS_DupValue(ctx, JS_MKPTR(JS_TAG_MODULE, mod));
        auto result = JS_EvalFunction(ctx, value);
        const bool failed = JS_IsException(result) || JS_PromiseState(ctx, result) == JS_PROMISE_REJECTED ||
                            JS_PromiseState(ctx, result) == JS_PROMISE_PENDING;
        JS_FreeValue(ctx, result);
        if (!failed)
            ns = JS_GetModuleNamespace(ctx, mod);
        if (failed) {
            close();
            throw Failure(500, "Workspace startup failed or exceeded its resource limit");
        }
    }
    void close() {
        components.reset();
        if (ctx) {
            JS_FreeValue(ctx, ns);
            JS_FreeContext(ctx);
            ctx = nullptr;
        }
        if (rt) {
            JS_FreeRuntime(rt);
            rt = nullptr;
        }
    }
    ~Workspace() { close(); }
    static char *normalize(JSContext *ctx, const char *base, const char *name, void *) {
        try {
            std::string input = name;
            if (input == "hls")
                return copy(ctx, "hls");
            if (input.empty() || input.size() > 4096 || input.find_first_of("\\:%?#") != std::string::npos ||
                input[0] == '/')
                throw Failure(403, "Invalid workspace import");
            auto path = (std::filesystem::u8path(base).parent_path() / std::filesystem::u8path(input))
                            .lexically_normal()
                            .generic_u8string();
            if (path == ".." || path.rfind("../", 0) == 0 || path.size() > 4096)
                throw Failure(403, "Workspace import escapes ZIP");
            return copy(ctx, path);
        } catch (const std::exception &e) {
            JS_ThrowReferenceError(ctx, "%s", e.what());
            return nullptr;
        }
    }
    static char *copy(JSContext *ctx, const std::string &s) {
        auto p = static_cast<char *>(js_malloc(ctx, s.size() + 1));
        if (p)
            std::memcpy(p, s.c_str(), s.size() + 1);
        return p;
    }
    static JSModuleDef *load(JSContext *ctx, const char *name, void *opaque) {
        auto &self = *static_cast<Workspace *>(opaque);
        std::string source;
        if (std::string(name) == "hls")
            source = "export const {fs,wasm,crypto,compression,types,zip,sqlite,qr,args,capabilities}=globalThis.hls; "
                     "export default globalThis.hls;";
        else {
            auto it = self.modules.find(name);
            if (it == self.modules.end()) {
                JS_ThrowReferenceError(ctx, "Workspace module is missing: %s", name);
                return nullptr;
            }
            self.private_modules.insert(name);
            source = it->second;
        }
        auto result =
            JS_Eval(ctx, source.c_str(), source.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(result))
            return nullptr;
        auto module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(result));
        JS_FreeValue(ctx, result);
        return module;
    }
    std::string logical(const std::string &input) {
        auto path = normalize_path(input);
        auto real = config.root / std::filesystem::u8path(path.substr(1));
        guard->protect(real);
        if (state && state->protects(real))
            throw Failure(403, "Private operational state");
        for (auto secret : {config.workspace_app, config.script_path, config.tls_key, config.state_dir}) {
            if (secret.empty())
                continue;
            secret = std::filesystem::absolute(secret).lexically_normal();
            std::error_code ec;
            if (path_within(secret, real) || std::filesystem::equivalent(secret, real, ec))
                throw Failure(403, "Private host path");
        }
        try {
            resolve_path(config.root, path);
        } catch (const Failure &e) {
            if (e.status != 404)
                throw;
            resolve_path(config.root, std::filesystem::u8path(path).parent_path().generic_u8string());
        }
        return path;
    }
    static std::string string(JSContext *ctx, JSValueConst value) {
        std::size_t n = 0;
        auto c = JS_ToCStringLen(ctx, &n, value);
        if (!c)
            throw Failure(400, "Expected text");
        std::string result(c, n);
        JS_FreeCString(ctx, c);
        return result;
    }
    static JSValue operation(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int op) {
        try {
            auto &self = *static_cast<Workspace *>(JS_GetContextOpaque(ctx));
            if (std::chrono::steady_clock::now() > self.deadline)
                throw Failure(503, "Script filesystem deadline reached");
            if (!argc)
                throw Failure(400, "Path required");
            if (self.config.script_fs == "off")
                throw Failure(403, "Filesystem capability disabled");
            if ((op == 1 || op == 2 || op == 3 || op == 7) &&
                (self.config.script_fs != "readwrite" || !self.write_allowed))
                throw Failure(403, "Filesystem writes are disabled for this hook/request");
            std::unique_lock<std::mutex> mutation;
            if (self.mutation && (op == 1 || op == 2 || op == 3 || op == 7)) {
                mutation = std::unique_lock<std::mutex>(*self.mutation, std::try_to_lock);
                if (!mutation.owns_lock())
                    throw Failure(409, "Filesystem mutation is busy; retry request");
            }
            auto path = self.logical(string(ctx, argv[0]));
            if (op == 0 || op == 6) {
                auto file = File::open(self.config.root, path);
                self.guard->protect(*file);
                if (file->info().size > (2ull << 20))
                    throw Failure(413, "Workspace read exceeds 2 MiB");
                std::string bytes(static_cast<std::size_t>(file->info().size), '\0');
                file->read(0, bytes.data(), bytes.size());
                if (op == 6)
                    return JS_NewArrayBufferCopy(ctx, reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
                if (!valid_utf8(bytes) && !bytes.empty())
                    throw Failure(415, "readText requires UTF-8; use readBytes");
                return JS_NewStringLen(ctx, bytes.data(), bytes.size());
            }
            if (op == 1 || op == 7) {
                if (argc < 2)
                    throw Failure(400, "Content required");
                std::string bytes;
                if (op == 1)
                    bytes = string(ctx, argv[1]);
                else
                    bytes = js_bytes(ctx, argv[1]);
                if (bytes.size() > (2ull << 20))
                    throw Failure(413, "Workspace write exceeds 2 MiB");
                std::string version;
                try {
                    auto previous = File::open(self.config.root, path);
                    self.guard->protect(*previous);
                    version = previous->info().version;
                } catch (const Failure &e) {
                    if (e.status != 404)
                        throw;
                }
                Upload output(self.config.root, path, 2ull << 20, version);
                output.append(bytes.data(), bytes.size());
                output.commit();
                return JS_UNDEFINED;
            }
            if (op == 2) {
                secure_mkdir(self.config.root, path, true);
                return JS_UNDEFINED;
            }
            if (op == 3) {
                self.guard->protect(self.config.root / std::filesystem::u8path(path.substr(1)), true);
                secure_remove(self.config.root, path, false, 1000);
                return JS_UNDEFINED;
            }
            const auto real = resolve_path(self.config.root, path);
            if (op == 4) {
                auto result = JS_NewObject(ctx);
                const bool dir = std::filesystem::is_directory(real);
                JS_SetPropertyStr(ctx, result, "directory", JS_NewBool(ctx, dir));
                if (!dir) {
                    auto file = File::open(self.config.root, path);
                    self.guard->protect(*file);
                    JS_SetPropertyStr(ctx, result, "size", JS_NewInt64(ctx, file->info().size));
                    JS_SetPropertyStr(ctx, result, "version", JS_NewString(ctx, file->info().version.c_str()));
                }
                return result;
            }
            auto result = JS_NewArray(ctx);
            unsigned count = 0, emitted = 0;
            for (const auto &entry : std::filesystem::directory_iterator(real)) {
                if (++count > 1000) {
                    JS_FreeValue(ctx, result);
                    throw Failure(413, "Workspace directory exceeds 1000 entries");
                }
                if (link_like(entry.path()) || self.guard->frozen(entry.path()))
                    continue;
                auto name = entry.path().filename().u8string();
                JS_SetPropertyUint32(ctx, result, emitted++, JS_NewStringLen(ctx, name.data(), name.size()));
            }
            return result;
        } catch (const std::exception &e) {
            return JS_ThrowTypeError(ctx, "%s", e.what());
        }
    }
    std::string invoke(const std::string &hook, const std::string &input) {
        std::unique_lock<std::timed_mutex> lock(mutex, std::defer_lock);
        if (!lock.try_lock_for(std::chrono::milliseconds(config.script_timeout_ms)))
            throw Failure(503, "Workspace is busy");
        if (!ctx)
            initialize();
        JS_UpdateStackTop(rt);
        write_allowed = hook == "onStart";
        if (hook == "onRequest") {
            auto request = JS_ParseJSON(ctx, input.data(), input.size(), "request.json");
            auto method = JS_GetPropertyStr(ctx, request, "method");
            if (JS_IsString(method)) {
                auto m = string(ctx, method);
                write_allowed = m == "POST" || m == "PUT" || m == "PATCH" || m == "DELETE";
            }
            JS_FreeValue(ctx, method);
            JS_FreeValue(ctx, request);
        }
        JS_UpdateStackTop(rt);
        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(config.script_timeout_ms);
        components->deadline(deadline);
        auto fn = JS_GetPropertyStr(ctx, ns, hook.c_str());
        if (JS_IsUndefined(fn)) {
            JS_FreeValue(ctx, fn);
            auto global = JS_GetGlobalObject(ctx);
            fn = JS_GetPropertyStr(ctx, global, hook.c_str());
            JS_FreeValue(ctx, global);
        }
        if (JS_IsUndefined(fn)) {
            JS_FreeValue(ctx, fn);
            return "{}";
        }
        auto arg = JS_ParseJSON(ctx, input.c_str(), input.size(), "request.json");
        auto result = JS_IsException(arg) ? JS_EXCEPTION : JS_Call(ctx, fn, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(ctx, arg);
        JS_FreeValue(ctx, fn);
        if (JS_IsException(result)) {
            JS_FreeValue(ctx, result);
            auto exception = JS_GetException(ctx);
            JS_FreeValue(ctx, exception);
            throw Failure(500, "Workspace hook failed or exceeded its resource limit");
        }
        if (JS_IsUndefined(result) || JS_IsNull(result)) {
            JS_FreeValue(ctx, result);
            return "{}";
        }
        if (JS_PromiseState(ctx, result) >= 0) {
            JS_FreeValue(ctx, result);
            throw Failure(500, "Workspace hooks must return synchronously");
        }
        auto json = JS_JSONStringify(ctx, result, JS_UNDEFINED, JS_UNDEFINED);
        JS_FreeValue(ctx, result);
        std::string output;
        if (!JS_IsException(json)) {
            try {
                output = string(ctx, json);
            } catch (...) {
            }
        }
        JS_FreeValue(ctx, json);
        if (output.empty() || output.size() > 65536)
            throw Failure(500, "Workspace result must be JSON up to 64 KiB");
        return output;
    }
};
ScriptEngine::~ScriptEngine() = default;
void ScriptEngine::mutation_mutex(std::mutex *mutex, const ConfigStore *config, const StateStore *state) {
    if (workspace_) {
        workspace_->mutation = mutex;
        workspace_->guard = config;
        workspace_->state = state;
        invoke("onStart", "{}");
    }
}
bool ScriptEngine::asset(const std::string &requested, std::string &bytes, std::string &mime) {
    if (!workspace_)
        return false;
    ZipEntry entry;
    {
        // Backend imports can make a module private at runtime. Check the
        // policy under the VM lock on every request, including cache hits.
        std::unique_lock<std::timed_mutex> lock(workspace_->mutex, std::defer_lock);
        if (!lock.try_lock_for(std::chrono::milliseconds(timeout_)))
            throw Failure(503, "Workspace is busy");
        const auto prefix = normalize_path(workspace_->config.workspace_public);
        const auto name = (prefix == "/" ? "" : prefix.substr(1) + "/") +
                          (requested == "/" ? "index.html" : requested.substr(1));
        if (workspace_->private_modules.count(name) || name == "server.qjs" ||
            lower_ascii(std::filesystem::u8path(name).extension().u8string()) == ".qjs")
            throw Failure(403, "Workspace backend modules are private");
        if (prefix == "/" && name.rfind("public/", 0) != 0) {
            const auto ext = lower_ascii(std::filesystem::u8path(name).extension().u8string());
            if (ext == ".js" || ext == ".wasm")
                throw Failure(403, "Backend package modules are private; put browser code under public/");
        }
        const auto it = workspace_->archive->entries().find(name);
        if (it == workspace_->archive->entries().end() || it->second.directory)
            return false;
        if (it->second.size > (2ull << 20))
            throw Failure(413, "Workspace static asset exceeds 2 MiB");
        entry = it->second;
    }
    // Independent files may decompress concurrently; same-file misses merge.
    auto cached = workspace_->assets.get(workspace_->archive, entry);
    bytes.assign(*cached);
    workspace_->assets.validate(workspace_->archive);
    mime = extension_type(entry.path).mime;
    return true;
}
ScriptEngine::ScriptEngine(const Config &c) : timeout_(c.script_timeout_ms), config_(c) {
    for (const auto &h : c.response_headers)
        if (!safe_response_header(h.first, h.second))
            throw Failure(400, "Unsafe custom response header");
    if (!c.workspace_app.empty()) {
        path_ = std::filesystem::absolute(c.workspace_app).lexically_normal();
        workspace_ = std::make_unique<Workspace>(c, path_);
        identity_ = workspace_->archive->source()->info().identity;
        return;
    }
    if (c.script_path.empty())
        return;
    path_ = std::filesystem::absolute(c.script_path).lexically_normal();
    const auto file = File::open(canonical_root(path_.parent_path()), "/" + path_.filename().u8string());
    identity_ = file->info().identity;
    if (file->info().size > (1ull << 20))
        throw Failure(413, "Script source exceeds 1 MiB");
    std::string source(static_cast<std::size_t>(file->info().size), '\0');
    if (file->read(0, source.data(), source.size()) != source.size())
        throw Failure(500, "Cannot read configured script");
    if (source.empty())
        return;
    isolated_ = std::make_unique<IsolatedScript>(c, source, path_.filename().u8string());
    invoke("onStart", "{}"); // Fail startup on syntax errors or resource violations.
}
bool ScriptEngine::protects(const std::filesystem::path &candidate) const {
    return !identity_.empty() && file_identity(candidate) == identity_;
}
std::string ScriptEngine::invoke(const std::string &hook, const std::string &input) {
    if (workspace_)
        return workspace_->invoke(hook, input);
    return isolated_ ? isolated_->invoke(hook, input) : "{}";
}
void ScriptEngine::maintain() {
    if (workspace_)
        workspace_->assets.maintain();
    if (isolated_)
        isolated_->maintain();
}
ScriptStats ScriptEngine::stats() {
    auto result = isolated_ ? isolated_->stats() : ScriptStats{};
    if (workspace_)
        result.assets = workspace_->assets.stats();
    return result;
}
} // namespace hls
