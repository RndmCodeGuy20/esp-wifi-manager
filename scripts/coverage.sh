#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
build_dir="$repo/build/coverage"
cmake -S "$repo/tests" -B "$build_dir" -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure
mkdir -p "$repo/coverage/core" "$repo/coverage/adapter"
# Keep core and adapter denominators separate. gcovr must match the compiler's
# gcov executable; CI uses GCC rather than Apple Clang for this command.
gcovr --root "$repo" "$build_dir" --filter 'src/policy.cpp' \
  --fail-under-line 95 --fail-under-branch 90 \
  --json-summary "$repo/coverage/core/summary.json" \
  --xml "$repo/coverage/core/cobertura.xml" \
  --html-details "$repo/coverage/core/index.html" --print-summary
if test -f "$repo/src/wifi_manager.cpp"; then
  gcovr --root "$repo" "$build_dir" --filter 'src/wifi_manager.cpp' \
    --json-summary "$repo/coverage/adapter/summary.json" \
    --xml "$repo/coverage/adapter/cobertura.xml" \
    --html-details "$repo/coverage/adapter/index.html" --print-summary
fi
