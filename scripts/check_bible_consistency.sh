#!/bin/bash
# check_bible_consistency.sh
# Validates the docs/bible/ documentation hierarchy for consistency:
#   1. Every .md file in bible subdirectories is listed in README.md TOC
#   2. Every chapter referenced in INVENTORY.md exists as a file
#   3. IL opcode references in bible chapters exist in the canonical Opcode.def
#
# Usage: ./scripts/check_bible_consistency.sh

set -euo pipefail

BIBLE_DIR="docs/bible"
INVENTORY="$BIBLE_DIR/INVENTORY.md"
README="$BIBLE_DIR/README.md"
OPCODE_DEF="src/il/core/Opcode.def"

ERRORS=0

if [ ! -d "$BIBLE_DIR" ]; then
    echo "ERROR: $BIBLE_DIR not found. Run from the project root." >&2
    exit 2
fi

for f in "$INVENTORY" "$README" "$OPCODE_DEF"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f not found." >&2
        exit 2
    fi
done

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

echo "=== Bible Consistency Check ==="
echo ""

# ============================================================
# Gather actual bible chapter files
# ============================================================
find "$BIBLE_DIR" -mindepth 2 -name '*.md' -type f \
    | sed "s|^$BIBLE_DIR/||" \
    | sort > "$TMP_DIR/bible_files.txt"

# ============================================================
# Check 1: Every .md file in bible subdirs is linked in README.md
# ============================================================
echo "--- [1] README.md TOC Coverage ---"

# Extract chapter file references from README.md links like (part1-foundations/01-the-machine.md)
grep -oE '[a-z0-9-]+/[a-z0-9_-]+\.md' "$README" \
    | sort -u > "$TMP_DIR/readme_refs.txt"

missing_from_readme=$(comm -23 "$TMP_DIR/bible_files.txt" "$TMP_DIR/readme_refs.txt")
if [ -n "$missing_from_readme" ]; then
    echo "  WARNING: Files in bible/ not linked in README.md TOC:"
    echo "$missing_from_readme" | sed 's/^/    - /'
    ERRORS=$((ERRORS + 1))
else
    echo "  OK — all $( wc -l < "$TMP_DIR/bible_files.txt" | tr -d ' ') bible chapter files are linked in README.md"
fi

echo ""

# ============================================================
# Check 2: INVENTORY chapter numbers match actual files
# ============================================================
echo "--- [2] INVENTORY.md Chapter Coverage ---"

# Extract chapter numbers from INVENTORY (lines like "| 0. Getting Started |" or "| A. Zia Reference |")
# Chapter files use these numbers: 00-getting-started.md, 01-the-machine.md, etc.
# Appendices use: a-zia-reference.md, b-basic-reference.md, etc.

# Extract chapter numbers from bible filenames
sed 's|.*/||; s|-.*||' "$TMP_DIR/bible_files.txt" | sort -u > "$TMP_DIR/file_ids.txt"

# Extract chapter numbers from INVENTORY table rows
# Matches patterns like "| 0. Getting" or "| A. Zia"
grep -E '^\|[[:space:]]+[0-9]+\.' "$INVENTORY" \
    | sed 's/^|[[:space:]]*//' \
    | sed 's/\..*//' \
    | awk '{printf "%02d\n", $1}' \
    | sort -u > "$TMP_DIR/inventory_chapter_nums.txt"

# Also extract appendix letters
grep -E '^\|[[:space:]]+[A-F]\.' "$INVENTORY" \
    | sed 's/^|[[:space:]]*//' \
    | sed 's/\..*//' \
    | tr '[:upper:]' '[:lower:]' \
    | sort -u >> "$TMP_DIR/inventory_chapter_nums.txt"

sort -u -o "$TMP_DIR/inventory_chapter_nums.txt" "$TMP_DIR/inventory_chapter_nums.txt"

missing_from_files=$(comm -23 "$TMP_DIR/inventory_chapter_nums.txt" "$TMP_DIR/file_ids.txt")
missing_from_inventory=$(comm -13 "$TMP_DIR/inventory_chapter_nums.txt" "$TMP_DIR/file_ids.txt")

has_issues=0
if [ -n "$missing_from_files" ]; then
    echo "  WARNING: Chapters in INVENTORY.md with no matching file:"
    echo "$missing_from_files" | sed 's/^/    - chapter /'
    ERRORS=$((ERRORS + 1))
    has_issues=1
fi
if [ -n "$missing_from_inventory" ]; then
    echo "  WARNING: Bible files with no matching INVENTORY.md entry:"
    echo "$missing_from_inventory" | sed 's/^/    - id /'
    ERRORS=$((ERRORS + 1))
    has_issues=1
fi
if [ $has_issues -eq 0 ]; then
    echo "  OK — INVENTORY.md chapters match bible files"
fi

echo ""

# ============================================================
# Check 3: IL opcode references in bible chapters
# ============================================================
echo "--- [3] IL Opcode Reference Validation ---"

# Extract canonical IL opcode mnemonics from Opcode.def
# The mnemonic string is on the line after IL_OPCODE(Name,
grep -A1 'IL_OPCODE(' "$OPCODE_DEF" \
    | grep '"' \
    | sed 's/.*"\([^"]*\)".*/\1/' \
    | sort -u > "$TMP_DIR/canonical_opcodes.txt"

canonical_count=$(wc -l < "$TMP_DIR/canonical_opcodes.txt" | tr -d ' ')
echo "  Canonical IL opcodes: $canonical_count"

# Extract backtick-quoted tokens from bible chapters that exactly match
# known IL opcode mnemonics. We use exact matching — only flag tokens
# that ARE known opcodes but were removed, or tokens that look like
# they intend to be opcodes (containing underscores in opcode-like patterns).
#
# Strategy: collect all backtick-quoted tokens, intersect with canonical list
# to find valid refs, then look for opcode-LIKE tokens that don't match.
for md_file in $(find "$BIBLE_DIR" -mindepth 2 -name '*.md' -type f); do
    grep -oE '`[a-z][a-z_.0-9]*`' "$md_file" 2>/dev/null \
        | sed 's/`//g' \
        || true
done | sort -u | grep -v '^$' > "$TMP_DIR/bible_tokens.txt"

# Find exact matches with canonical opcodes
valid_refs=$(comm -12 "$TMP_DIR/bible_tokens.txt" "$TMP_DIR/canonical_opcodes.txt" || true)
valid_count=0
if [ -n "$valid_refs" ]; then
    valid_count=$(echo "$valid_refs" | wc -l | tr -d ' ')
fi
echo "  Valid IL opcode references in bible: $valid_count"

# Look for tokens that match opcode patterns but are NOT in the canonical list.
# Opcode patterns: contain underscore with opcode-style prefix, or contain dots
# with opcode-style prefix (like iadd.ovf, sdiv.chk0, fcmp_eq).
# Exclude common false positives (filenames, dotted identifiers).
not_canonical=$(comm -23 "$TMP_DIR/bible_tokens.txt" "$TMP_DIR/canonical_opcodes.txt" || true)
if [ -n "$not_canonical" ]; then
    # Only flag tokens that strongly match IL opcode naming:
    # - underscore-separated with known IL prefix (icmp_, scmp_, ucmp_, fcmp_, etc.)
    # - dot-separated with known IL prefix (iadd., isub., sdiv., cast., eh., err., trap., resume.)
    suspect=$(echo "$not_canonical" \
        | grep -E '^(icmp|scmp|ucmp|fcmp|iadd|isub|imul|sdiv|udiv|srem|urem|cast|eh|err|trap|resume|idx)\.' \
        || true)
    suspect2=$(echo "$not_canonical" \
        | grep -E '^(icmp|scmp|ucmp|fcmp)_[a-z]+$' \
        || true)
    all_suspect=""
    if [ -n "$suspect" ]; then all_suspect="$suspect"; fi
    if [ -n "$suspect2" ]; then
        if [ -n "$all_suspect" ]; then
            all_suspect="$all_suspect
$suspect2"
        else
            all_suspect="$suspect2"
        fi
    fi
    if [ -n "$all_suspect" ]; then
        stale=$(echo "$all_suspect" | sort -u)
        echo "  WARNING: Possible stale/invalid IL opcode references in bible:"
        echo "$stale" | sed 's/^/    - /'
        ERRORS=$((ERRORS + 1))
    fi
fi

echo ""

# ============================================================
# Summary
# ============================================================
bible_file_count=$(wc -l < "$TMP_DIR/bible_files.txt" | tr -d ' ')
echo "--- Summary ---"
echo "  Bible chapter files: $bible_file_count"
echo "  README.md links:     $(wc -l < "$TMP_DIR/readme_refs.txt" | tr -d ' ')"
echo "  Canonical IL opcodes: $canonical_count"
echo ""

if [ $ERRORS -eq 0 ]; then
    echo "=== ALL CHECKS PASSED ==="
    exit 0
else
    echo "=== $ERRORS CHECK(S) FAILED ==="
    exit 1
fi
