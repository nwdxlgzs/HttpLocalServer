// SPDX-License-Identifier: Apache-2.0
#include "script_wasm.hpp"
#include "platform.hpp"
#include "script_components.hpp"
#include "script_bytecode.hpp"
#include "script_wasm_bootstrap.hpp"
#define WASM_RUNTIME_API_EXTERN
#include <algorithm>
#include <atomic>
#include <boost/json.hpp>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <vector>
#include <wasm_export.h>
namespace {
struct Budget {
    std::atomic<size_t> used{0};
    size_t maximum = 0;
};
struct Execution {
    std::shared_ptr<Budget> budget;
    std::chrono::steady_clock::time_point deadline;
    unsigned fuel = 0, ticks = 0;
    std::function<void()> detach;
};
thread_local Execution *execution = nullptr;
struct Scope {
    Execution *previous;
    explicit Scope(Execution &e) : previous(execution) { execution = &e; }
    ~Scope() { execution = previous; }
};
struct alignas(std::max_align_t) Allocation {
    std::shared_ptr<Budget> budget;
    size_t bytes;
};
void *allocate_for(const std::shared_ptr<Budget> &budget, size_t bytes) {
    if (bytes > std::numeric_limits<size_t>::max() - sizeof(Allocation))
        return nullptr;
    const auto charge = bytes + sizeof(Allocation);
    if (budget) {
        auto used = budget->used.load();
        do {
            if (charge > budget->maximum || used > budget->maximum - charge)
                return nullptr;
        } while (!budget->used.compare_exchange_weak(used, used + charge));
    }
    auto raw = std::malloc(sizeof(Allocation) + bytes);
    if (!raw) {
        if (budget)
            budget->used.fetch_sub(charge);
        return nullptr;
    }
    auto header = new (raw) Allocation{budget, bytes};
    return header + 1;
}
void release(mem_alloc_usage_t, void *p) {
    if (!p)
        return;
    auto h = static_cast<Allocation *>(p) - 1;
    if (h->budget)
        h->budget->used.fetch_sub(h->bytes + sizeof(Allocation));
    h->~Allocation();
    std::free(h);
}
void *allocate(mem_alloc_usage_t usage, unsigned bytes) {
    auto p = allocate_for(execution ? execution->budget : nullptr, bytes);
    if (p && usage == Alloc_For_LinearMemory)
        std::memset(p, 0, bytes);
    return p;
}
void *resize(mem_alloc_usage_t usage, bool, void *old, unsigned bytes) {
    if (!old)
        return allocate(usage, bytes);
    auto h = static_cast<Allocation *>(old) - 1;
    // WAMR's software-checked linear memory can move on memory.grow. Detach
    // every outstanding JS view before the old allocation may be released.
    if (usage == Alloc_For_LinearMemory && execution && execution->detach)
        execution->detach();
    auto p = allocate_for(h->budget, bytes);
    if (!p)
        return nullptr;
    std::memcpy(p, old, std::min<size_t>(bytes, h->bytes));
    if (usage == Alloc_For_LinearMemory && bytes > h->bytes)
        std::memset(static_cast<char *>(p) + h->bytes, 0, bytes - h->bytes);
    release(usage, old);
    return p;
}
std::mutex &registry_mutex() {
    static std::mutex value;
    return value;
}
void initialize_runtime() {
    static const bool initialized = []() {
        RuntimeInitArgs args{};
        args.mem_alloc_type = Alloc_With_Allocator;
        args.mem_alloc_option.allocator.malloc_func = reinterpret_cast<void *>(allocate);
        args.mem_alloc_option.allocator.realloc_func = reinterpret_cast<void *>(resize);
        args.mem_alloc_option.allocator.free_func = reinterpret_cast<void *>(release);
        if (!wasm_runtime_full_init(&args))
            throw hls::Failure(503, "Cannot initialize WAMR");
        wasm_runtime_set_log_level(WASM_LOG_LEVEL_FATAL);
        return true;
    }();
    (void)initialized;
}
struct ThreadEnvironment {
    bool own = false;
    ThreadEnvironment() {
        if (!wasm_runtime_thread_env_inited()) {
            if (!wasm_runtime_init_thread_env())
                throw hls::Failure(503, "Cannot initialize Wasm thread");
            own = true;
        }
    }
    ~ThreadEnvironment() {
        if (own)
            wasm_runtime_destroy_thread_env();
    }
};
} // namespace
// Called by our generated interpreter copy, including module start functions.
// Fuel is shared by all Wasm calls in one hook, rather than refreshed on re-entry.
extern "C" int hls_wamr_consume(void) {
    if (!execution || execution->fuel == 0)
        return 1;
    --execution->fuel;
    if ((++execution->ticks & 255) == 0 && std::chrono::steady_clock::now() > execution->deadline)
        return 1;
    return 0;
}
namespace hls {
struct WasmBridge::Impl {
    JSContext *ctx;
    Config config;
    Asset asset;
    Execution limits;
    unsigned next = 0;
    bool busy = false;
    struct Instance;
    struct Import {
        Impl *owner;
        Instance *instance;
        std::string module, name, signature;
        JSValue function = JS_UNDEFINED;
        std::vector<wasm_valkind_t> params, results;
        NativeSymbol symbol{};
        ~Import() {
            if (owner)
                JS_FreeValue(owner->ctx, function);
        }
    };
    struct Instance {
        Impl *owner;
        std::string bytes;
        wasm_module_t module = nullptr;
        wasm_module_inst_t instance = nullptr;
        wasm_exec_env_t env = nullptr;
        std::vector<std::unique_ptr<Import>> imports;
        JSValue memory = JS_UNDEFINED;
        uint8_t *address = nullptr;
        size_t size = 0;
        explicit Instance(Impl *p) : owner(p) {}
        void detach() {
            if (!JS_IsUndefined(memory)) {
                JS_DetachArrayBuffer(owner->ctx, memory);
                JS_FreeValue(owner->ctx, memory);
                memory = JS_UNDEFINED;
            }
            address = nullptr;
            size = 0;
        }
        ~Instance() {
            detach();
            if (env)
                wasm_runtime_destroy_exec_env(env);
            if (instance)
                wasm_runtime_deinstantiate(instance);
            if (module)
                wasm_runtime_unload(module);
        }
    };
    std::map<unsigned, std::unique_ptr<Instance>> instances;
    Impl(JSContext *c, const Config &cfg, Asset a) : ctx(c), config(cfg), asset(std::move(a)) {
        limits.budget = std::make_shared<Budget>();
        limits.budget->maximum = static_cast<size_t>(config.wasm_memory_mb) << 20;
        limits.detach = [this] {
            for (auto &item : instances)
                item.second->detach();
        };
    }
    ~Impl() {
        Scope scope(limits);
        instances.clear();
    }
    static wasm_val_t from_js(JSContext *c, JSValueConst v, wasm_valkind_t kind) {
        wasm_val_t out{};
        out.kind = kind;
        int rc = -1;
        double d = 0;
        switch (kind) {
        case WASM_I32:
            rc = JS_ToInt32(c, &out.of.i32, v);
            break;
        case WASM_I64:
            if (!JS_IsBigInt(v))
                throw Failure(400, "Wasm i64 requires BigInt");
            rc = JS_ToBigInt64(c, &out.of.i64, v);
            break;
        case WASM_F32:
            rc = JS_ToFloat64(c, &d, v);
            out.of.f32 = static_cast<float>(d);
            break;
        case WASM_F64:
            rc = JS_ToFloat64(c, &out.of.f64, v);
            break;
        default:
            throw Failure(415, "Only numeric Wasm values cross this bridge");
        }
        if (rc)
            throw Failure(400, "Invalid Wasm numeric argument");
        return out;
    }
    static JSValue to_js(JSContext *c, const wasm_val_t &v) {
        switch (v.kind) {
        case WASM_I32:
            return JS_NewInt32(c, v.of.i32);
        case WASM_I64:
            return JS_NewBigInt64(c, v.of.i64);
        case WASM_F32:
            return JS_NewFloat64(c, v.of.f32);
        case WASM_F64:
            return JS_NewFloat64(c, v.of.f64);
        default:
            throw Failure(415, "Unsupported Wasm result type");
        }
    }
    static char signature(wasm_valkind_t kind) {
        switch (kind) {
        case WASM_I32:
            return 'i';
        case WASM_I64:
            return 'I';
        case WASM_F32:
            return 'f';
        case WASM_F64:
            return 'F';
        default:
            throw Failure(415, "Unsupported Wasm import type");
        }
    }
    static void imported(wasm_exec_env_t env, uint64_t *values) {
        auto imp = static_cast<Import *>(wasm_runtime_get_function_attachment(env));
        auto c = imp->owner->ctx;
        std::vector<JSValue> args;
        JSValue result = JS_UNDEFINED;
        try {
            if (hls_wamr_consume())
                throw Failure(503, "Wasm hook execution budget exceeded");
            // The start function is called before instantiate_ex returns its instance.
            auto inst = wasm_runtime_get_module_inst(env);
            (void)inst;
            for (size_t i = 0; i < imp->params.size(); ++i) {
                wasm_val_t v{};
                v.kind = imp->params[i];
                std::memcpy(&v.of, &values[i], v.kind == WASM_I32 || v.kind == WASM_F32 ? 4 : 8);
                auto argument = to_js(c, v);
                if (JS_IsException(argument))
                    throw Failure(503, "Wasm callback argument allocation failed");
                args.push_back(argument);
            }
            result = JS_Call(c, imp->function, JS_UNDEFINED, static_cast<int>(args.size()), args.data());
            if (JS_IsException(result)) {
                auto e = JS_GetException(c);
                JS_FreeValue(c, e);
                throw Failure(500, "Wasm import callback threw an exception");
            }
            if (!imp->results.empty()) {
                auto value = from_js(c, result, imp->results[0]);
                values[0] = 0;
                std::memcpy(&values[0], &value.of, value.kind == WASM_I32 || value.kind == WASM_F32 ? 4 : 8);
            }
        } catch (const std::exception &e) {
            wasm_runtime_set_exception(wasm_runtime_get_module_inst(env), e.what());
        }
        JS_FreeValue(c, result);
        for (auto v : args)
            JS_FreeValue(c, v);
    }
    JSValue fn(int op, unsigned id = 0, const char *name = "") {
        JSValue data[] = {JS_NewBigInt64(ctx, reinterpret_cast<intptr_t>(this)), JS_NewUint32(ctx, id),
                          JS_NewString(ctx, name)};
        auto f = JS_NewCFunctionData(ctx, dispatch, 0, op, 3, data);
        for (auto v : data)
            JS_FreeValue(ctx, v);
        return f;
    }
    static void getter(JSContext *c, JSValueConst object, const char *name, JSValue get, JSValue set = JS_UNDEFINED) {
        auto atom = JS_NewAtom(c, name);
        JS_DefinePropertyGetSet(c, object, atom, get, set, JS_PROP_ENUMERABLE);
        JS_FreeAtom(c, atom);
    }
    JSValue memory(Instance &i) {
        auto mem = wasm_runtime_get_default_memory(i.instance);
        if (!mem)
            throw Failure(400, "Module has no memory");
        auto base = static_cast<uint8_t *>(wasm_memory_get_base_address(mem));
        auto bytes = static_cast<size_t>(wasm_memory_get_cur_page_count(mem)) * wasm_memory_get_bytes_per_page(mem);
        if (base != i.address || bytes != i.size || JS_IsUndefined(i.memory)) {
            i.detach();
            i.address = base;
            i.size = bytes;
            i.memory = JS_NewArrayBuffer(ctx, base, bytes, 0, nullptr, nullptr, false);
        }
        return JS_DupValue(ctx, i.memory);
    }
    std::unique_ptr<Instance> load(const std::string &bytes) {
        if (bytes.size() < 8 || bytes.compare(0, 8, "\0asm\x01\0\0\0", 8))
            throw Failure(415, "Expected WebAssembly bytecode version 1");
        auto i = std::make_unique<Instance>(this);
        i->bytes = bytes;
        char error[256]{};
        LoadArgs options{};
        options.no_resolve = true;
        i->module = wasm_runtime_load_ex(reinterpret_cast<uint8_t *>(i->bytes.data()),
                                         static_cast<uint32_t>(bytes.size()), &options, error, sizeof(error));
        if (!i->module)
            throw Failure(400, std::string("Wasm module rejected: ") + error);
        if (wasm_runtime_get_import_count(i->module) > 128 || wasm_runtime_get_export_count(i->module) > 512)
            throw Failure(413, "Wasm import/export budget exceeded");
        return i;
    }
    JSValue instantiate(const std::string &bytes, JSValueConst imports) {
        if (instances.size() >= config.wasm_instances)
            throw Failure(503, "Wasm instance budget reached; close unused instances");
        auto item = load(bytes);
        char error[256]{};
        std::unique_lock<std::mutex> registry(registry_mutex());
        for (int n = 0; n < wasm_runtime_get_import_count(item->module); ++n) {
            wasm_import_t type{};
            wasm_runtime_get_import_type(item->module, n, &type);
            if (type.kind != WASM_IMPORT_EXPORT_KIND_FUNC)
                throw Failure(415,
                              "Imported Wasm memories, tables and globals are unsupported; export module-owned memory");
            auto group = JS_GetPropertyStr(ctx, imports, type.module_name);
            auto fnvalue = JS_IsObject(group) ? JS_GetPropertyStr(ctx, group, type.name) : JS_UNDEFINED;
            JS_FreeValue(ctx, group);
            if (!JS_IsFunction(ctx, fnvalue)) {
                JS_FreeValue(ctx, fnvalue);
                throw Failure(400, std::string("Missing explicit Wasm import: ") + type.module_name + "." + type.name);
            }
            auto i = std::make_unique<Import>();
            i->owner = this;
            i->instance = item.get();
            i->function = fnvalue;
            i->module = type.module_name;
            i->name = type.name;
            i->signature = "(";
            auto count = wasm_func_type_get_param_count(type.u.func_type);
            if (count > 32)
                throw Failure(413, "Wasm import parameter budget exceeded");
            for (unsigned p = 0; p < count; ++p) {
                auto kind = wasm_func_type_get_param_valkind(type.u.func_type, p);
                i->params.push_back(kind);
                i->signature += signature(kind);
            }
            i->signature += ")";
            count = wasm_func_type_get_result_count(type.u.func_type);
            if (count > 1)
                throw Failure(415, "Imported functions support a single numeric result");
            if (count) {
                auto kind = wasm_func_type_get_result_valkind(type.u.func_type, 0);
                i->results.push_back(kind);
                i->signature += signature(kind);
            }
            i->symbol = {i->name.c_str(), reinterpret_cast<void *>(imported), i->signature.c_str(), i.get()};
            item->imports.push_back(std::move(i));
        }
        // Registrations exist only while resolving this module. Resolved function
        // pointers and attachments are instance-owned; other applications cannot borrow them.
        struct Registrations {
            std::vector<Import *> entries;
            ~Registrations() {
                for (auto i : entries)
                    wasm_runtime_unregister_natives(i->module.c_str(), &i->symbol);
            }
        } registered;
        for (auto &i : item->imports) {
            if (!wasm_runtime_register_natives_raw(i->module.c_str(), &i->symbol, 1))
                throw Failure(503, "Cannot register Wasm import");
            registered.entries.push_back(i.get());
        }
        if (!wasm_runtime_resolve_symbols(item->module))
            throw Failure(400, "Cannot resolve Wasm imports");
        // Unregister before running JS callbacks, which can load no new instance
        // while busy but may use other bounded native components.
        for (auto i : registered.entries)
            wasm_runtime_unregister_natives(i->module.c_str(), &i->symbol);
        registered.entries.clear();
        registry.unlock();
        InstantiationArgs options{};
        options.default_stack_size = 32u << 10;
        options.max_memory_pages = config.wasm_memory_mb * 16;
        item->instance = wasm_runtime_instantiate_ex(item->module, &options, error, sizeof(error));
        if (!item->instance)
            throw Failure(400, std::string("Wasm instance rejected: ") + error);
        item->env = wasm_runtime_create_exec_env(item->instance, 32u << 10);
        if (!item->env)
            throw Failure(503, "Cannot allocate Wasm execution stack");
        auto id = ++next;
        auto *i = item.get();
        instances[id] = std::move(item);
        struct Publication {
            Impl *self;
            unsigned id;
            bool complete = false;
            ~Publication() {
                if (!complete)
                    self->instances.erase(id);
            }
        } publication{this, id};
        auto object = JS_NewObjectProto(ctx, JS_NULL), exports = JS_NewObjectProto(ctx, JS_NULL);
        if (JS_IsException(object) || JS_IsException(exports)) {
            JS_FreeValue(ctx, object);
            JS_FreeValue(ctx, exports);
            throw Failure(503, "Cannot allocate Wasm exports");
        }
        for (int n = 0; n < wasm_runtime_get_export_count(i->module); ++n) {
            wasm_export_t type{};
            wasm_runtime_get_export_type(i->module, n, &type);
            if (type.kind == WASM_IMPORT_EXPORT_KIND_FUNC)
                JS_SetPropertyStr(ctx, exports, type.name, fn(1, id, type.name));
            else if (type.kind == WASM_IMPORT_EXPORT_KIND_MEMORY) {
                auto m = JS_NewObjectProto(ctx, JS_NULL);
                getter(ctx, m, "buffer", fn(2, id));
                JS_SetPropertyStr(ctx, m, "grow", fn(3, id));
                JS_SetPropertyStr(ctx, exports, type.name, m);
            } else if (type.kind == WASM_IMPORT_EXPORT_KIND_GLOBAL) {
                auto g = JS_NewObjectProto(ctx, JS_NULL);
                getter(ctx, g, "value", fn(5, id, type.name),
                       wasm_global_type_get_mutable(type.u.global_type) ? fn(6, id, type.name) : JS_UNDEFINED);
                JS_SetPropertyStr(ctx, exports, type.name, g);
            } else if (type.kind == WASM_IMPORT_EXPORT_KIND_TABLE) {
                auto table = JS_NewObjectProto(ctx, JS_NULL);
                getter(ctx, table, "length", fn(8, id, type.name));
                JS_SetPropertyStr(ctx, table, "get", fn(9, id, type.name));
                JS_SetPropertyStr(ctx, exports, type.name, table);
            }
        }
        JS_SetPropertyStr(ctx, object, "exports", exports);
        JS_SetPropertyStr(ctx, object, "close", fn(4, id));
        if (JS_HasException(ctx)) {
            JS_FreeValue(ctx, object);
            throw Failure(503, "Cannot allocate Wasm bindings");
        }
        publication.complete = true;
        return object;
    }
    JSValue invoke(Instance &i, wasm_function_inst_t function, int argc, JSValueConst *argv) {
        auto params = wasm_func_get_param_count(function, i.instance),
             results = wasm_func_get_result_count(function, i.instance);
        if (params > 32 || results > 16 || static_cast<unsigned>(argc) != params)
            throw Failure(400, "Wasm function argument/result budget or argument count mismatch");
        std::vector<wasm_valkind_t> kinds(params);
        wasm_func_get_param_types(function, i.instance, kinds.data());
        std::vector<wasm_val_t> values;
        for (unsigned p = 0; p < params; ++p)
            values.push_back(from_js(ctx, argv[p], kinds[p]));
        std::vector<wasm_val_t> output(results);
        wasm_runtime_set_exception(i.instance, nullptr);
        wasm_runtime_set_instruction_count_limit(i.env, static_cast<int>(limits.fuel));
        if (!wasm_runtime_call_wasm_a(i.env, function, results, output.data(), params, values.data()))
            throw Failure(400, std::string("Wasm trap: ") + (wasm_runtime_get_exception(i.instance)
                                                                 ? wasm_runtime_get_exception(i.instance)
                                                                 : "execution failed"));
        if (!results)
            return JS_UNDEFINED;
        if (results == 1)
            return to_js(ctx, output[0]);
        auto array = JS_NewArray(ctx);
        for (unsigned n = 0; n < results; ++n)
            JS_SetPropertyUint32(ctx, array, n, to_js(ctx, output[n]));
        return array;
    }
    static JSValue dispatch(JSContext *c, JSValueConst, int argc, JSValueConst *argv, int op, JSValue *data) {
        try {
            int64_t pointer = 0;
            uint32_t id = 0;
            if (JS_ToBigInt64(c, &pointer, data[0]) < 0 || !pointer)
                return JS_ThrowInternalError(c, "Native binding is unavailable");
            JS_ToUint32(c, &id, data[1]);
            auto &self = *reinterpret_cast<Impl *>(static_cast<intptr_t>(pointer));
            if (!self.config.wasm)
                throw Failure(403, "Wasm is disabled");
            if (std::chrono::steady_clock::now() > self.limits.deadline)
                throw Failure(503, "Wasm deadline reached");
            if (self.busy && op != 2 && op != 5 && op != 6 && op != 8 && op != 9)
                throw Failure(409, "Re-entering Wasm from a host import is prohibited");
            initialize_runtime();
            ThreadEnvironment thread;
            Scope scope(self.limits);
            struct Busy {
                bool &flag;
                bool previous;
                explicit Busy(bool &f) : flag(f), previous(f) { flag = true; }
                ~Busy() { flag = previous; }
            } busy(self.busy);
            if (op == 0 || op == 7) {
                if (!argc)
                    throw Failure(400, "Wasm bytes or package path required");
                std::string bytes;
                if (JS_IsString(argv[0])) {
                    if (!self.asset)
                        throw Failure(403, "Package asset loader unavailable");
                    bytes = self.asset(js_text(c, argv[0]));
                } else
                    bytes = js_bytes(c, argv[0], 4u << 20);
                if (bytes.size() > (4u << 20))
                    throw Failure(413, "Wasm bytecode exceeds 4 MiB");
                if (op == 7) {
                    auto i = self.load(bytes);
                    boost::json::array imports, exports;
                    for (int n = 0; n < wasm_runtime_get_import_count(i->module); ++n) {
                        wasm_import_t t{};
                        wasm_runtime_get_import_type(i->module, n, &t);
                        imports.emplace_back(
                            boost::json::object{{"module", t.module_name},
                                                {"name", t.name},
                                                {"kind", t.kind == WASM_IMPORT_EXPORT_KIND_FUNC     ? "function"
                                                         : t.kind == WASM_IMPORT_EXPORT_KIND_MEMORY ? "memory"
                                                         : t.kind == WASM_IMPORT_EXPORT_KIND_GLOBAL ? "global"
                                                                                                    : "table"}});
                    }
                    for (int n = 0; n < wasm_runtime_get_export_count(i->module); ++n) {
                        wasm_export_t t{};
                        wasm_runtime_get_export_type(i->module, n, &t);
                        exports.emplace_back(
                            boost::json::object{{"name", t.name},
                                                {"kind", t.kind == WASM_IMPORT_EXPORT_KIND_FUNC     ? "function"
                                                         : t.kind == WASM_IMPORT_EXPORT_KIND_MEMORY ? "memory"
                                                         : t.kind == WASM_IMPORT_EXPORT_KIND_GLOBAL ? "global"
                                                                                                    : "table"}});
                    }
                    return js_json(c, boost::json::serialize(boost::json::object{{"imports", std::move(imports)},
                                                                                 {"exports", std::move(exports)}}));
                }
                auto imports = argc > 1 && JS_IsObject(argv[1]) ? JS_DupValue(c, argv[1]) : JS_NewObject(c);
                JSValue result;
                try {
                    result = self.instantiate(bytes, imports);
                } catch (...) {
                    JS_FreeValue(c, imports);
                    throw;
                }
                JS_FreeValue(c, imports);
                return result;
            }
            auto found = self.instances.find(id);
            if (found == self.instances.end())
                throw Failure(400, "Wasm instance is closed");
            auto &i = *found->second;
            auto name = js_text(c, data[2]);
            if (op == 1) {
                auto function = wasm_runtime_lookup_function(i.instance, name.c_str());
                if (!function)
                    throw Failure(400, "Wasm function not found");
                return self.invoke(i, function, argc, argv);
            }
            if (op == 2)
                return self.memory(i);
            if (op == 3) {
                uint32_t pages = 0;
                if (!argc || JS_ToUint32(c, &pages, argv[0]) || pages > self.config.wasm_memory_mb * 16)
                    throw Failure(400, "Invalid memory growth");
                auto mem = wasm_runtime_get_default_memory(i.instance);
                if (!mem)
                    throw Failure(400, "No module memory");
                auto before = wasm_memory_get_cur_page_count(mem);
                i.detach();
                if (!wasm_runtime_enlarge_memory(i.instance, pages))
                    throw Failure(413, "Wasm memory growth exceeds budget");
                return JS_NewUint32(c, before);
            }
            if (op == 4) {
                self.instances.erase(found);
                return JS_UNDEFINED;
            }
            if (op == 5 || op == 6) {
                wasm_global_inst_t global{};
                if (!wasm_runtime_get_export_global_inst(i.instance, name.c_str(), &global))
                    throw Failure(400, "Wasm global not found");
                wasm_val_t v{};
                v.kind = global.kind;
                if (v.kind != WASM_I32 && v.kind != WASM_I64 && v.kind != WASM_F32 && v.kind != WASM_F64)
                    throw Failure(415, "Only numeric Wasm globals cross this bridge");
                auto size = v.kind == WASM_I32 || v.kind == WASM_F32 ? 4 : 8;
                if (op == 6) {
                    if (!argc || !global.is_mutable)
                        throw Failure(403, "Wasm global is immutable");
                    v = from_js(c, argv[0], global.kind);
                    std::memcpy(global.global_data, &v.of, size);
                    return JS_UNDEFINED;
                }
                std::memcpy(&v.of, global.global_data, size);
                return to_js(c, v);
            }
            if (op == 8 || op == 9) {
                wasm_table_inst_t table{};
                if (!wasm_runtime_get_export_table_inst(i.instance, name.c_str(), &table))
                    throw Failure(400, "Wasm table not found");
                if (op == 8)
                    return JS_NewUint32(c, table.cur_size);
                uint32_t index = 0;
                if (!argc || JS_ToUint32(c, &index, argv[0]) || index >= table.cur_size)
                    throw Failure(400, "Table index out of bounds");
                auto f = wasm_table_get_func_inst(i.instance, &table, index);
                if (!f)
                    return JS_NULL;
                return self.fn(10, id, (name + "\n" + std::to_string(index)).c_str());
            }
            if (op == 10) {
                auto split = name.rfind('\n');
                auto table_name = name.substr(0, split);
                auto index = static_cast<uint32_t>(std::stoul(name.substr(split + 1)));
                wasm_table_inst_t table{};
                if (!wasm_runtime_get_export_table_inst(i.instance, table_name.c_str(), &table))
                    throw Failure(400, "Wasm table not found");
                auto f = wasm_table_get_func_inst(i.instance, &table, index);
                if (!f)
                    throw Failure(400, "Null table function");
                return self.invoke(i, f, argc, argv);
            }
            throw Failure(400, "Unsupported Wasm operation");
        } catch (const std::exception &e) {
            return JS_ThrowTypeError(c, "%s", e.what());
        }
    }
    void install(JSValueConst api) {
        auto wasm = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, wasm, "instantiate", fn(0));
        JS_SetPropertyStr(ctx, wasm, "inspect", fn(7));
        JS_SetPropertyStr(ctx, api, "wasm", wasm);
    }
};
WasmBridge::WasmBridge(JSContext *c, const Config &cfg, Asset a)
    : impl_(std::make_unique<Impl>(c, cfg, std::move(a))) {}
WasmBridge::~WasmBridge() = default;
void WasmBridge::deadline(std::chrono::steady_clock::time_point deadline) {
    impl_->limits.deadline = deadline;
    impl_->limits.fuel = impl_->config.wasm_instructions;
    impl_->limits.ticks = 0;
}
void WasmBridge::install(JSValueConst api) {
    impl_->install(api);
    auto global = JS_GetGlobalObject(impl_->ctx);
    JS_SetPropertyStr(impl_->ctx, global, "hls", JS_DupValue(impl_->ctx, api));
    JS_FreeValue(impl_->ctx, global);
    // This bytecode is generated only from our embedded trusted source. Every
    // context receives its own functions/prototypes when deserializing it.
    static std::once_flag compiled;
    static std::vector<std::uint8_t> bootstrap;
    std::call_once(compiled, [&] {
        bootstrap = compile_script(impl_->ctx, wasm_bootstrap, sizeof(wasm_bootstrap) - 1, "hls-wasm.js", 1u << 20);
    });
    auto result = evaluate_script(impl_->ctx, bootstrap);
    if (JS_IsException(result)) {
        JS_FreeValue(impl_->ctx, result);
        throw Failure(500, "Cannot initialize Wasm bindings");
    }
    JS_FreeValue(impl_->ctx, result);
}
} // namespace hls
