#!/bin/sh
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
task_build=${1:-"$task_root/build-musl"}
if ! ldd --version 2>&1 | grep -qi musl; then
    echo 'Run this script inside an Alpine/musl GCC environment.' >&2
    exit 1
fi
cmake -S "$task_root" -B "$task_build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DHLS_FULL_STATIC=ON -DHLS_ENABLE_TLS="${HLS_ENABLE_TLS:-OFF}"
cmake --build "$task_build" --parallel "${HLS_JOBS:-4}"
ctest --test-dir "$task_build" --output-on-failure
if readelf -l "$task_build/HttpLocalServer" | grep -q INTERP; then
    echo 'Unexpected dynamic ELF interpreter.' >&2
    exit 1
fi
if readelf -d "$task_build/HttpLocalServer" | grep -q NEEDED; then
    echo 'Unexpected shared-library dependency.' >&2
    exit 1
fi
echo "Static binary: $task_build/HttpLocalServer"
