#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
build_dir="$repo/build/llvm-coverage"
if command -v xcrun >/dev/null; then
  cov=(xcrun llvm-cov)
  prof=(xcrun llvm-profdata)
else
  cov=(llvm-cov)
  prof=(llvm-profdata)
fi
cmake -S "$repo/tests" -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fprofile-instr-generate -fcoverage-mapping' \
  -DCMAKE_EXE_LINKER_FLAGS=-fprofile-instr-generate
cmake --build "$build_dir" --parallel
profiles=$(mktemp -d "$build_dir/profiles.XXXXXX")
LLVM_PROFILE_FILE="$profiles/%m-%p.profraw" ctest --test-dir "$build_dir" --output-on-failure
"${prof[@]}" merge -sparse "$profiles"/*.profraw -o "$profiles/merged.profdata"
mkdir -p "$repo/coverage/llvm"
"${cov[@]}" export "$build_dir/test_policy" -object="$build_dir/test_adapter" \
  -instr-profile="$profiles/merged.profdata" "$repo/src/policy.cpp" "$repo/src/wifi_manager.cpp" \
  > "$repo/coverage/llvm/export.json"
"${cov[@]}" show "$build_dir/test_policy" -object="$build_dir/test_adapter" \
  -instr-profile="$profiles/merged.profdata" -format=html \
  -output-dir="$repo/coverage/llvm/html" "$repo/src/policy.cpp" "$repo/src/wifi_manager.cpp"
python3 "$repo/scripts/check-llvm-coverage.py" "$repo/coverage/llvm/export.json"
