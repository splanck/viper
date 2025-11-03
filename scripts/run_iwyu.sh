#!/usr/bin/env bash
set -euo pipefail

if [[ $# -gt 1 ]]; then
  echo "usage: $0 [build-dir]" >&2
  exit 1
fi

build_dir=${1:-build}
if [[ ! -f "${build_dir}/compile_commands.json" ]]; then
  echo "compile_commands.json not found in ${build_dir}" >&2
  exit 2
fi

declare -a units=(
  "src/il/verify/Verifier.cpp"
  "src/il/verify/InstructionChecker.cpp"
  "src/il/verify/ControlFlowChecker.cpp"
  "src/il/verify/TypeInference.cpp"
  "src/il/transform/DCE.cpp"
  "src/il/transform/ConstFold.cpp"
  "src/il/transform/Peephole.cpp"
  "src/il/io/Serializer.cpp"
  "src/il/io/Parser.cpp"
  "src/il/io/FunctionParser.cpp"
  "src/il/io/InstrParser.cpp"
  "src/il/io/ModuleParser.cpp"
  "src/vm/debug/Debug.cpp"
)

for unit in "${units[@]}"; do
  python3 /usr/bin/iwyu_tool -p "${build_dir}" "${unit}"
done
