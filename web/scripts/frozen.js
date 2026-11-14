// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
function isFrozenPath(path){return false;}
function frozenEntry(entry){return !!(entry&&(entry.frozen||isFrozenPath(entry.path)));}
function frozenArt(){return '<svg viewBox="0 0 40 40" fill="none" aria-hidden="true"><path d="M20 4v32M6 12l28 16M6 28l28-16M15 7l5 5 5-5M15 33l5-5 5 5M7 18l7-2-2-7M33 22l-7 2 2 7M7 22l7 2-2 7M33 18l-7-2 2-7" stroke="currentColor" stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round"/></svg>';}
