#!/usr/bin/env bash
set -euo pipefail

to_shell_path() {
    local path="$1"
    if [[ "$path" =~ ^[A-Za-z]:[\\/] ]] && command -v wslpath >/dev/null 2>&1; then
        wslpath "$path"
        return
    fi
    if [[ "$path" =~ ^[A-Za-z]:[\\/] ]] && command -v cygpath >/dev/null 2>&1; then
        cygpath -u "$path"
        return
    fi
    printf '%s\n' "$path"
}

zia_bin_arg="${1:?path to zia executable required}"
repo_root_arg="${2:?repo root required}"
zia_bin="$(to_shell_path "${zia_bin_arg}")"
repo_root_for_child="${repo_root_arg//\\//}"
iterations="${3:-25}"
test_file="${repo_root_for_child}/tests/zia_runtime/34_async_functions.zia"

for ((i = 1; i <= iterations; ++i)); do
    output="$("${zia_bin}" "${test_file}" 2>&1)" || {
        printf 'FAIL: async functions iteration %d exited non-zero\n%s\n' "${i}" "${output}"
        exit 1
    }

    if ! printf '%s\n' "${output}" | grep -q 'RESULT: ok'; then
        printf 'FAIL: async functions iteration %d did not report success\n%s\n' "${i}" "${output}"
        exit 1
    fi
done

printf 'PASS\n'
