#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 github@nwdxlgzs
"""Small, reproducible IE fixes applied to pinned upstream browser sources."""


def patch_web_dependency(name, code):
    if name != 'marked':
        return code, []
    replacements = [
        (b"Hooks.passThroughHooks = new Set(['preprocess', 'postprocess']);",
         b"Hooks.passThroughHooks = { has: function (name) { return name === 'preprocess' || name === 'postprocess'; } };"),
        (b'var prim = input[Symbol.toPrimitive];',
         b'var prim = typeof Symbol !== "undefined" && Symbol.toPrimitive ? input[Symbol.toPrimitive] : undefined;'),
        (b'if (n === "Map" || n === "Set") return Array.from(o);',
         b'if (n === "Map" || n === "Set") { if (Array.from) return Array.from(o); var entries = []; o.forEach(function (value, key) { entries.push(n === "Map" ? [key, value] : value); }); return entries; }'),
    ]
    for original, replacement in replacements:
        if code.count(original) != 1:
            raise ValueError('Pinned marked source changed; review its IE compatibility patch')
        code = code.replace(original, replacement, 1)
    return code, ['marked-ie-initialization-and-iterables-v1']
