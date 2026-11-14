// SPDX-License-Identifier: Apache-2.0
#include "script_isolate.hpp"
#include "script_bytecode.hpp"
#include "script_components.hpp"
#include "platform.hpp"
#include <algorithm>
#include <chrono>
#include <mutex>

namespace hls {
namespace {
struct Value {
    JSContext *ctx;
    JSValue value;
    Value(JSContext *c, JSValue v) : ctx(c), value(v) {}
    ~Value() { JS_FreeValue(ctx, value); }
    Value(const Value &) = delete;
    Value &operator=(const Value &) = delete;
    operator JSValue() const { return value; }
    JSValue take() { auto result = value; value = JS_UNDEFINED; return result; }
};
} // namespace

struct IsolatedScript::Impl {
    using Clock = std::chrono::steady_clock;
    struct Runtime {
        JSRuntime *rt;
        Clock::time_point deadline, used = Clock::now();
        unsigned calls = 0;
        explicit Runtime(const Config &config) : rt(JS_NewRuntime()) {
            if (!rt)
                throw Failure(503, "Cannot allocate script runtime");
            JS_SetMemoryLimit(rt, static_cast<std::size_t>(config.script_memory_mb) << 20);
            // musl worker stacks can be only 128 KiB.
            JS_SetMaxStackSize(rt, 32 * 1024);
            JS_SetCanBlock(rt, false);
            JS_SetInterruptHandler(rt, [](JSContext *, void *p) {
                return Clock::now() > static_cast<Runtime *>(p)->deadline ? 1 : 0;
            }, this);
        }
        ~Runtime() { JS_UpdateStackTop(rt); JS_FreeRuntime(rt); }
    };
    Config config;
    std::vector<std::uint8_t> bytecode;
    std::mutex mutex;
    std::vector<std::unique_ptr<Runtime>> idle;
    ScriptStats counters;
    Impl(const Config &c, const std::string &source, const std::string &name) : config(c) {
        Runtime compiler(config);
        compiler.deadline = Clock::now() + std::chrono::milliseconds(config.script_timeout_ms);
        std::unique_ptr<JSContext, decltype(&JS_FreeContext)> ctx(JS_NewContext(compiler.rt), JS_FreeContext);
        if (!ctx)
            throw Failure(503, "Cannot allocate script compiler context");
        bytecode = compile_script(ctx.get(), source.data(), source.size(), name.c_str(), 8u << 20);
        counters.compilations = 1;
        counters.bytecode = bytecode.size();
        idle.reserve(std::min<unsigned>(8, config.script_concurrency));
    }
    std::unique_ptr<Runtime> acquire() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (counters.active >= config.script_concurrency)
                throw Failure(503, "Script concurrency budget reached");
            ++counters.active;
            ++counters.calls;
            if (!idle.empty()) {
                auto runtime = std::move(idle.back());
                idle.pop_back();
                ++counters.reused;
                return runtime;
            }
        }
        try {
            auto runtime = std::make_unique<Runtime>(config);
            std::lock_guard<std::mutex> lock(mutex);
            ++counters.created;
            return runtime;
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            --counters.active;
            throw;
        }
    }
    void release(std::unique_ptr<Runtime> runtime, bool reusable) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        --counters.active;
        if (reusable && ++runtime->calls < 256 && idle.size() < std::min<unsigned>(8, config.script_concurrency)) {
            runtime->used = Clock::now();
            idle.push_back(std::move(runtime)); // Capacity reserved at construction.
        } else
            ++counters.retired;
    }
    std::string invoke(const std::string &hook, const std::string &input) {
        auto runtime = acquire();
        struct Lease {
            Impl &owner;
            std::unique_ptr<Runtime> &runtime;
            bool reusable = false;
            ~Lease() { owner.release(std::move(runtime), reusable); }
        } lease{*this, runtime};
        JS_UpdateStackTop(runtime->rt);
        runtime->deadline = Clock::now() + std::chrono::milliseconds(config.script_timeout_ms);
        std::string output;
        {
            // A fresh realm and fresh native components preserve the original
            // per-call globals, prototypes, SQLite and Wasm isolation.
            std::unique_ptr<JSContext, decltype(&JS_FreeContext)> ctx(JS_NewContext(runtime->rt), JS_FreeContext);
            if (!ctx)
                throw Failure(503, "Script context memory exhausted");
            ScriptComponents components(ctx.get(), config);
            components.deadline(runtime->deadline);
            Value global(ctx.get(), JS_GetGlobalObject(ctx.get())), api(ctx.get(), JS_NewObject(ctx.get()));
            components.install(api);
            JS_SetPropertyStr(ctx.get(), global, "hls", api.take());
            Value evaluated(ctx.get(), evaluate_script(ctx.get(), bytecode));
            if (JS_IsException(evaluated))
                throw Failure(500, "Script initialization failed or exceeded its resource limit");
            Value fn(ctx.get(), JS_GetPropertyStr(ctx.get(), global, hook.c_str()));
            if (JS_IsUndefined(fn))
                output = "{}";
            else {
                if (!JS_IsFunction(ctx.get(), fn))
                    throw Failure(500, "Script hook must be a function");
                Value arg(ctx.get(), JS_ParseJSON(ctx.get(), input.data(), input.size(), "request.json"));
                Value result(ctx.get(), JS_IsException(arg) ? JS_EXCEPTION : JS_Call(ctx.get(), fn, JS_UNDEFINED, 1, &arg.value));
                if (JS_IsException(result))
                    throw Failure(500, "Script hook failed or exceeded its resource limit");
                if (JS_PromiseState(ctx.get(), result) >= 0)
                    throw Failure(500, "Script hooks must return synchronously");
                if (JS_IsUndefined(result) || JS_IsNull(result))
                    output = "{}";
                else {
                    Value json(ctx.get(), JS_JSONStringify(ctx.get(), result, JS_UNDEFINED, JS_UNDEFINED));
                    std::size_t length = 0;
                    auto raw = JS_IsException(json) ? nullptr : JS_ToCStringLen(ctx.get(), &length, json);
                    auto free_text = [&ctx](const char *p) { JS_FreeCString(ctx.get(), p); };
                    std::unique_ptr<const char, decltype(free_text)> text(raw, free_text);
                    if (raw && length <= 65536)
                        output.assign(raw, length);
                    if (output.empty())
                        throw Failure(500, "Script result must be JSON and at most 64 KiB");
                }
            }
        }
        // Collect cycles before reusing the runtime. Never execute jobs from
        // an old realm after its native components have been destroyed.
        JS_RunGC(runtime->rt);
        JSMemoryUsage memory{};
        JS_ComputeMemoryUsage(runtime->rt, &memory);
        lease.reusable = !JS_IsJobPending(runtime->rt) && memory.malloc_size <= (1 << 20);
        return output;
    }
    void maintain() {
        // Destroy retired VMs outside the pool lock.
        std::vector<std::unique_ptr<Runtime>> retired;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto now = Clock::now();
            for (auto it = idle.begin(); it != idle.end();)
                if (now - (*it)->used >= std::chrono::seconds(config.thread_idle)) {
                    retired.push_back(std::move(*it));
                    it = idle.erase(it);
                    ++counters.retired;
                } else
                    ++it;
        }
    }
};
IsolatedScript::IsolatedScript(const Config &c, const std::string &source, const std::string &name)
    : impl_(std::make_unique<Impl>(c, source, name)) {}
IsolatedScript::~IsolatedScript() = default;
std::string IsolatedScript::invoke(const std::string &hook, const std::string &input) { return impl_->invoke(hook, input); }
void IsolatedScript::maintain() { impl_->maintain(); }
ScriptStats IsolatedScript::stats() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto result = impl_->counters;
    result.idle = impl_->idle.size();
    return result;
}
}
