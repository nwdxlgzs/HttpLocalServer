// SPDX-License-Identifier: Apache-2.0
#include "script_bytecode.hpp"
#include "platform.hpp"
#include <memory>

namespace hls {
std::vector<std::uint8_t> compile_script(JSContext *ctx, const char *source, std::size_t length,
                                        const char *name, std::size_t maximum) {
    auto compiled = JS_Eval(ctx, source, length, name, JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled))
        throw Failure(500, "Script compilation failed or exceeded its resource limit");
    std::size_t size = 0;
    auto raw = JS_WriteObject(ctx, &size, compiled, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(ctx, compiled);
    auto cleanup = [ctx](std::uint8_t *p) { js_free(ctx, p); };
    std::unique_ptr<std::uint8_t, decltype(cleanup)> bytes(raw, cleanup);
    if (!raw || size > maximum)
        throw Failure(500, "Compiled script exceeds its resource limit");
    return {raw, raw + size};
}
JSValue evaluate_script(JSContext *ctx, const std::vector<std::uint8_t> &bytecode) {
    auto compiled = JS_ReadObject(ctx, bytecode.data(), bytecode.size(), JS_READ_OBJ_BYTECODE);
    return JS_IsException(compiled) ? compiled : JS_EvalFunction(ctx, compiled);
}
}
