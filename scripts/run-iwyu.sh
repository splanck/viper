#!/usr/bin/env bash
# File: scripts/run-iwyu.sh
# Purpose: Run include-what-you-use on a curated set of translation units.
# Key invariants: Requires compile_commands.json generated for the build directory.
# Ownership/Lifetime: Temporary log file removed before exit.
# Links: https://github.com/include-what-you-use/include-what-you-use

set -euo pipefail

build_dir=${1:-build}

cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "${build_dir}" -j2

# Translation units with heavy headers that benefit from IWYU policing.
files=(
  src/vm/VM.cpp
  src/vm/VMInit.cpp
  src/il/transform/ConstFold.cpp
  src/il/transform/DCE.cpp
  src/il/transform/Peephole.cpp
  src/il/transform/PassManager.cpp
  src/il/io/Parser.cpp
  src/il/io/FunctionParser.cpp
  src/il/io/InstrParser.cpp
  src/il/io/ModuleParser.cpp
  src/il/io/Serializer.cpp
  src/il/core/Type.cpp
)

log=$(mktemp)
trap 'rm -f "$log"' EXIT

iwyu_tool.py -p "${build_dir}" "${files[@]}" >"$log" 2>&1 || {
  cat "$log"
  exit 1
}

cat "$log"

if grep -E "should (add|remove)" "$log" >/dev/null; then
  echo "include-what-you-use reported header adjustments" >&2
  exit 1
fi
