#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 github@nwdxlgzs
set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo 'Run this script on macOS with Xcode Command Line Tools and CMake.' >&2
    exit 1
fi
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
task_build=${1:-"$task_root/build-macos"}
task_archs=${HLS_MACOS_ARCHS:-$(uname -m)}
task_target=${MACOSX_DEPLOYMENT_TARGET:-11.0}
task_generator=${HLS_CMAKE_GENERATOR:-Unix Makefiles}

# Application dependencies and resources are embedded; libSystem/libc++ are
# supplied by macOS. Apple does not support the Linux full-static link mode.
cmake -S "$task_root" -B "$task_build" -G "$task_generator" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$task_archs" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$task_target" \
    -DHLS_FULL_STATIC=OFF -DHLS_BUILD_TESTS=OFF -DHLS_ENABLE_TLS="${HLS_ENABLE_TLS:-OFF}"
cmake --build "$task_build" --config Release --parallel "${HLS_JOBS:-4}"

if [ -f "$task_build/Release/HttpLocalServer" ]; then
    echo "macOS CLI: $task_build/Release/HttpLocalServer"
else
    echo "macOS CLI: $task_build/HttpLocalServer"
fi
