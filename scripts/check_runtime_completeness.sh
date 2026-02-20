#!/bin/bash
# check_runtime_completeness.sh
# Finds RT_METHOD/RT_PROP handler names referenced in runtime.def that
# have no corresponding RT_FUNC entry. Exits 1 if any gaps are found.
#
# Usage: ./scripts/check_runtime_completeness.sh

set -euo pipefail

DEF="src/il/runtime/runtime.def"

if [[ ! -f "$DEF" ]]; then
    echo "ERROR: $DEF not found. Run from the project root." >&2
    exit 2
fi

# Extract handler names from RT_METHOD("Name", "sig", HandlerName) lines.
method_handlers=$(grep -E '^\s+RT_METHOD\(' "$DEF" \
    | sed -E 's/.*RT_METHOD\("[^"]*",[ ]*"[^"]*",[ ]*([A-Za-z_][A-Za-z0-9_]*)\).*/\1/')

# Extract getter handler names from RT_PROP("Name", "type", Getter, Setter) lines.
prop_getters=$(grep -E '^\s+RT_PROP\(' "$DEF" \
    | sed -E 's/.*RT_PROP\("[^"]*",[ ]*"[^"]*",[ ]*([A-Za-z_][A-Za-z0-9_]*),[ ]*[A-Za-z_][A-Za-z0-9_]*\).*/\1/')

# Extract setter handler names (4th arg), excluding "none".
prop_setters=$(grep -E '^\s+RT_PROP\(' "$DEF" \
    | sed -E 's/.*RT_PROP\("[^"]*",[ ]*"[^"]*",[ ]*[A-Za-z_][A-Za-z0-9_]*,[ ]*([A-Za-z_][A-Za-z0-9_]*)\).*/\1/' \
    | grep -v '^none$')

# Combine all handler names, deduplicate, and sort.
handlers=$(printf '%s\n%s\n%s\n' "$method_handlers" "$prop_getters" "$prop_setters" \
    | grep -v '^$' | grep -v '^none$' | sort -u)

# Extract all handler identifiers defined by RT_FUNC(HandlerName, ...) lines.
funcs=$(grep -E '^RT_FUNC\(' "$DEF" \
    | sed -E 's/RT_FUNC\(([A-Za-z_][A-Za-z0-9_]*).*/\1/' \
    | sort -u)

# Find handler names with no RT_FUNC entry.
missing=$(comm -23 <(echo "$handlers") <(echo "$funcs"))

if [[ -n "$missing" ]]; then
    count=$(echo "$missing" | wc -l | tr -d ' ')
    echo "ERROR: $count handler(s) referenced in RT_METHOD/RT_PROP have no RT_FUNC entry:"
    echo "$missing" | sed 's/^/  /'
    echo ""
    echo "Add RT_FUNC entries for the above handlers in $DEF"
    exit 1
fi

echo "OK: All RT_METHOD/RT_PROP handlers have RT_FUNC entries."
