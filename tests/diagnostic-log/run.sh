#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
compiler="${CXX:-g++}"
flags=(-std=c++17 -Wall -Wextra -Werror -pthread -I"$root/Core/GameEngine/Include")
"$compiler" "${flags[@]}" "$root/tests/diagnostic-log/test_async_log.cpp" -o "$test_dir/queue"
"$test_dir/queue"
"$compiler" "${flags[@]}" -I"$root/tests/diagnostic-log/stubs" \
    "$root/tests/diagnostic-log/test_release_log.cpp" \
    "$root/Core/GameEngine/Source/Common/ReleaseLog.cpp" -o "$test_dir/files"
"$test_dir/files" "$test_dir/logs"
