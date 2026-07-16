#!/usr/bin/env bash
# Audit C/C++ files for file headers and runtime prototype doc comments.
# Replaces the old Python helper to keep tooling dependency-free.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

FILES=()
while IFS= read -r f; do
  FILES+=("$f")
done < <(cd "$ROOT" && git ls-files | grep -E '\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$')

missing_headers=()
missing_proto_docs="$(mktemp)"
trap 'rm -f "$missing_proto_docs"' EXIT

has_header() {
  head -n 12 "$1" | grep -Eq 'Part of the Viper project|^// File:|^/// @file'
}

audit_runtime_header() {
  local path="$1"
  awk -v OUT="$missing_proto_docs" '
  {
    lines[NR]=$0
    # Skip comment-only lines from consideration.
    if ($0 ~ /^[[:space:]]*\/\// || $0 ~ /^[[:space:]]*\/\*/) next
    if ($0 ~ /^[^\/].*\)[[:space:]]*;[[:space:]]*$/) {
      # Heuristic guard: ignore obvious statements.
      if ($0 ~ /return / || $0 ~ /=/ || $0 ~ /^[[:space:]]*,/) next
      candidate=$0
      sub(/^[[:space:]]*/, "", candidate)
      # A bare identifier followed by an argument list is a call, not a C
      # declaration (which has a return type before the function name).
      if (candidate ~ /^[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(/) next
      # Function-pointer struct members (and callback typedefs) are documented
      # at the struct/typedef level; their lines are not prototypes.
      is_fnptr = ($0 ~ /\(\*/) ? 1 : 0
      for (i=NR-1; i>0 && !is_fnptr; --i) {
        if (lines[i] ~ /\(\*/) { is_fnptr=1; break }
        if (lines[i] ~ /;[[:space:]]*$/ || lines[i] ~ /[{}][[:space:]]*$/ ||
            lines[i] ~ /^[[:space:]]*$/ || lines[i] ~ /^[[:space:]]*#/) break
      }
      if (is_fnptr) next
      has_doc=0
      # Walk back across a multiline declaration, but never borrow a comment
      # from the preceding declaration or section.
      for (i=NR-1; i>0; --i) {
        if (lines[i] ~ /\/\/\// || lines[i] ~ /\/\*/) { has_doc=1; break }
        if (lines[i] ~ /^[[:space:]]*$/ ||
            lines[i] ~ /;[[:space:]]*$/ ||
            lines[i] ~ /[{}][[:space:]]*$/ ||
            lines[i] ~ /^[[:space:]]*#/) break
      }
      if (!has_doc) {
        printf("%s:%d:%s\n", FILENAME, NR, $0) >> OUT
      }
    }
  }
  ' "$path"
}

for rel in "${FILES[@]}"; do
  path="$ROOT/$rel"
  # Skip generated headers
  if head -n 3 "$path" | grep -q "Generated file"; then
    continue
  fi
  if ! has_header "$path"; then
    missing_headers+=("$rel")
  fi
  if [[ "$rel" =~ ^src/runtime/.*\.(h|hh|hpp|hxx)$ ]]; then
    audit_runtime_header "$path"
  fi
done

echo "Files missing file-level header: ${#missing_headers[@]}"
for p in "${missing_headers[@]:0:50}"; do
  echo "  - $p"
done

if [[ -s "$missing_proto_docs" ]]; then
  count=$(wc -l < "$missing_proto_docs")
else
  count=0
fi
echo "Runtime headers prototypes lacking doc comments: $count"
if [[ $count -gt 0 ]]; then
  head -n 50 "$missing_proto_docs"
fi
