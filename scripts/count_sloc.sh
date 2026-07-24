#!/usr/bin/env bash
# =============================================================================
# count_sloc.sh — Consistent LOC/SLOC counter for the Zanna project
# =============================================================================
# Usage: ./scripts/count_sloc.sh [--summary | --json | --subsystem | --all]
#   --summary    One-line totals only (default)
#   --subsystem  Production SLOC broken down by src/ subdirectory
#   --all        Full report: by language, subsystem, non-production, totals
#   --json       Machine-readable JSON output
#
# Definitions:
#   LOC  = total lines (including blanks and comments)
#   SLOC = source lines of code (excluding blank lines and comment-only lines)
#
# Comment detection:
#   C/C++/ObjC/.inc:  blank lines, // line comments, and /* ... */ block
#                comments (including multi-line blocks) are excluded. A small
#                stateful awk pass tracks whether it is inside a block comment
#                and skips string/char literals, so a "/*" or "//" appearing
#                inside a literal is not mistaken for a comment (matching
#                cloc/sloccount). A line keeps a count if any code survives
#                comment stripping.
#   Zia:         same as C/C++ (// and /* ... */ comments). The Zanna Studio
#                application is counted separately from src/zannastudio/src/
#                (not lumped with demos).
#   BASIC:       blank lines, or lines starting with REM or '
#   Shell:       blank lines, or lines starting with #
#   CMake:       blank lines, or lines starting with #
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
cd "$ROOT"

# Colors
CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

# ─── Counting helpers ────────────────────────────────────────────────────────

# SLOC for C-style files (C/C++/ObjC/.inc/Zia): exclude blank lines, // line
# comments, and /* ... */ block comments (including multi-line blocks). String
# and char literals are tracked so a "/*" or "//" inside a literal is not
# mistaken for a comment.
sloc_c() {
    xargs cat 2>/dev/null | awk '
    BEGIN { sq = sprintf("%c", 39); bs = sprintf("%c", 92) }
    {
        line = $0
        n = length(line)
        i = 1
        hascode = 0
        while (i <= n) {
            if (inblock) {
                rest = substr(line, i)
                p = index(rest, "*/")
                if (p == 0) { i = n + 1 }
                else { inblock = 0; i = i + p + 1 }
                continue
            }
            rest = substr(line, i)
            plc = index(rest, "//")
            pbc = index(rest, "/*")
            pdq = index(rest, "\"")
            psq = index(rest, sq)
            m = 0
            if (plc > 0)                        m = plc
            if (pbc > 0 && (m == 0 || pbc < m)) m = pbc
            if (pdq > 0 && (m == 0 || pdq < m)) m = pdq
            if (psq > 0 && (m == 0 || psq < m)) m = psq
            if (m == 0) {
                if (rest ~ /[^ \t\r]/) hascode = 1
                i = n + 1
                continue
            }
            if (m > 1 && substr(rest, 1, m - 1) ~ /[^ \t\r]/) hascode = 1
            tokpos = i + m - 1
            tok = substr(line, tokpos, 2)
            onech = substr(line, tokpos, 1)
            if (tok == "//") {
                i = n + 1
            } else if (tok == "/*") {
                inblock = 1
                i = tokpos + 2
            } else {
                hascode = 1
                j = tokpos + 1
                while (j <= n) {
                    cj = substr(line, j, 1)
                    if (cj == bs) { j = j + 2; continue }
                    if (cj == onech) { j = j + 1; break }
                    j = j + 1
                }
                i = j
            }
        }
        if (hascode) count++
    }
    END { print count + 0 }
    ' 2>/dev/null || echo 0
}

# LOC for any files: total line count
loc_any() {
    xargs cat 2>/dev/null | wc -l | tr -d ' '
}

# SLOC for shell: exclude blank + # comment-only lines
sloc_shell() {
    xargs cat 2>/dev/null | grep -cv '^\s*$\|^\s*#' 2>/dev/null || echo 0
}

# SLOC for BASIC: exclude blank + REM + ' comment lines
sloc_basic() {
    xargs cat 2>/dev/null | grep -civ '^\s*$\|^\s*REM\b\|^\s*'"'"'' 2>/dev/null || echo 0
}

# File finder for C/C++/ObjC and C-style include fragments in a directory
find_c_files() {
    find "$1" -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' -o -name '*.m' -o -name '*.inc' 2>/dev/null
}

# Count files
count_files() {
    find "$1" -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' -o -name '*.m' -o -name '*.inc' 2>/dev/null | wc -l | tr -d ' '
}

# ─── Compute all metrics ─────────────────────────────────────────────────────

# Production by language
SLOC_C=$(find src -name '*.c' | sloc_c)
SLOC_H=$(find src -name '*.h' | sloc_c)
SLOC_CPP=$(find src -name '*.cpp' | sloc_c)
SLOC_HPP=$(find src -name '*.hpp' | sloc_c)
SLOC_OBJC=$(find src -name '*.m' | sloc_c)
SLOC_INC=$(find src -name '*.inc' | sloc_c)
SLOC_SRC=$((SLOC_C + SLOC_H + SLOC_CPP + SLOC_HPP + SLOC_OBJC + SLOC_INC))

# Tests (subset of src/)
SLOC_TESTS=$(find_c_files src/tests | sloc_c)
SLOC_PROD=$((SLOC_SRC - SLOC_TESTS))

# LOC (total lines including blanks/comments)
LOC_SRC=$(find_c_files src | loc_any)
LOC_TESTS=$(find_c_files src/tests | loc_any)
LOC_PROD=$((LOC_SRC - LOC_TESTS))

# Subsystems (no associative arrays — macOS bash 3.x compat)
SUB_RUNTIME=$(find_c_files src/runtime | sloc_c)
SUB_CODEGEN=$(find_c_files src/codegen | sloc_c)
SUB_FRONTENDS=$(find_c_files src/frontends | sloc_c)
SUB_IL=$(find_c_files src/il | sloc_c)
SUB_VM=$(find_c_files src/vm | sloc_c)
SUB_LIB=$(find_c_files src/lib | sloc_c)
SUB_TOOLS=$(find_c_files src/tools | sloc_c)
SUB_TESTS=$(find_c_files src/tests | sloc_c)

# Zanna Studio — the IDE application, written in Zia (src/zannastudio/src/*.zia)
SLOC_ZANNASTUDIO=$(find src/zannastudio/src -name '*.zia' 2>/dev/null | sloc_c)

# Non-production
SLOC_ZIA=$(find examples src/tests/fixtures -name '*.zia' 2>/dev/null | sloc_c)
SLOC_BASIC=$(find examples src/tests/fixtures -name '*.bas' 2>/dev/null | sloc_basic)
SLOC_CMAKE=$(find . -name 'CMakeLists.txt' -o -name '*.cmake' | grep -v build | sloc_shell)
SLOC_SHELL=$(find scripts -name '*.sh' 2>/dev/null | sloc_shell)
SLOC_PS1=$(find scripts -name '*.ps1' 2>/dev/null | sloc_shell)
SLOC_SCRIPTS=$((SLOC_SHELL + SLOC_PS1))
LOC_DOCS=$(find docs -name '*.md' 2>/dev/null | loc_any)
SLOC_IL=$(find src/tests/fixtures examples -name '*.il' 2>/dev/null | sloc_c)

# File counts
FILES_SRC=$(count_files src)
FILES_ZIA=$(find . -name '*.zia' | grep -v build | wc -l | tr -d ' ')
FILES_ZANNASTUDIO=$(find src/zannastudio/src -name '*.zia' 2>/dev/null | wc -l | tr -d ' ')
FILES_IL=$(find . -name '*.il' | grep -v build | wc -l | tr -d ' ')
FILES_TOTAL=$(find src examples scripts docs -type f 2>/dev/null | grep -v build | wc -l | tr -d ' ')

# Overall SLOC
SLOC_ALL=$((SLOC_SRC + SLOC_ZANNASTUDIO + SLOC_ZIA + SLOC_BASIC + SLOC_IL + SLOC_SCRIPTS))

# ─── Output modes ────────────────────────────────────────────────────────────

fmt_num() {
    printf "%'d" "$1"
}

print_summary() {
    echo -e "${BOLD}Zanna Project SLOC Summary${NC}"
    echo "=========================="
    printf "  Production SLOC:    %'10d  (src/ minus tests)\n" "$SLOC_PROD"
    printf "  Test SLOC:          %'10d  (src/tests/)\n" "$SLOC_TESTS"
    printf "  All src/ SLOC:      %'10d\n" "$SLOC_SRC"
    printf "  Zanna Studio SLOC:  %'10d  (src/zannastudio/src, Zia)\n" "$SLOC_ZANNASTUDIO"
    printf "  Demo code SLOC:     %'10d  (Zia + BASIC + IL)\n" "$((SLOC_ZIA + SLOC_BASIC + SLOC_IL))"
    printf "  Overall SLOC:       %'10d  (all code)\n" "$SLOC_ALL"
    echo ""
    printf "  Source files (src/): %'d\n" "$FILES_SRC"
    printf "  Total files:         %'d\n" "$FILES_TOTAL"
}

print_subsystem() {
    echo -e "${BOLD}Production SLOC by Subsystem${NC}"
    echo "============================="
    printf "  %-22s %'10s  %s\n" "Subsystem" "SLOC" "Purpose"
    echo "  ──────────────────────────────────────────────────────────"
    printf "  %-22s %'10d  %s\n" "src/runtime"    "$SUB_RUNTIME"    "C runtime library"
    printf "  %-22s %'10d  %s\n" "src/frontends"  "$SUB_FRONTENDS"  "Zia + BASIC compilers"
    printf "  %-22s %'10d  %s\n" "src/lib"        "$SUB_LIB"        "Graphics/audio/GUI"
    printf "  %-22s %'10d  %s\n" "src/codegen"    "$SUB_CODEGEN"    "x86_64 + AArch64 native"
    printf "  %-22s %'10d  %s\n" "src/il"         "$SUB_IL"         "IL core/optimizer/verifier"
    printf "  %-22s %'10d  %s\n" "src/tools"      "$SUB_TOOLS"      "CLI tools"
    printf "  %-22s %'10d  %s\n" "src/vm"         "$SUB_VM"         "Bytecode VM"
    echo "  ──────────────────────────────────────────────────────────"
    printf "  %-22s %'10d\n" "Production total" "$SLOC_PROD"
    echo ""
    printf "  %-22s %'10d  %s\n" "src/tests"      "$SUB_TESTS"      "Unit/integration/e2e"
    printf "  %-22s %'10d\n" "All src/ total" "$SLOC_SRC"
}

print_all() {
    echo -e "${CYAN}╔══════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║${NC}  ${BOLD}Zanna Project — LOC / SLOC Report${NC}                       ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}  $(date '+%Y-%m-%d')                                          ${CYAN}║${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════════════════════════╝${NC}"
    echo ""

    echo -e "${GREEN}Production Source (src/)${NC}"
    echo "─────────────────────────────────────────"
    printf "  %-24s %'10d SLOC\n" "C (.c)"          "$SLOC_C"
    printf "  %-24s %'10d SLOC\n" "C++ (.cpp)"       "$SLOC_CPP"
    printf "  %-24s %'10d SLOC\n" "C++ headers (.hpp)" "$SLOC_HPP"
    printf "  %-24s %'10d SLOC\n" "C headers (.h)"   "$SLOC_H"
    printf "  %-24s %'10d SLOC\n" "C/C++ includes (.inc)" "$SLOC_INC"
    printf "  %-24s %'10d SLOC\n" "ObjC (.m)"        "$SLOC_OBJC"
    echo "  ─────────────────────────────────────"
    printf "  %-24s %'10d SLOC\n" "All src/"         "$SLOC_SRC"
    printf "  %-24s %'10d SLOC\n" "  minus tests"    "-$SLOC_TESTS"
    printf "  ${BOLD}%-24s %'10d SLOC${NC}\n" "Production SLOC"  "$SLOC_PROD"
    printf "  %-24s %'10d LOC\n"  "Production LOC"   "$LOC_PROD"
    echo ""

    print_subsystem
    echo ""

    echo -e "${GREEN}Applications (Zia)${NC}"
    echo "─────────────────────────────────────────"
    printf "  %-24s %'10d SLOC\n"  "Zanna Studio (src/zannastudio/src)" "$SLOC_ZANNASTUDIO"
    printf "  %-24s %'10d files\n" "  source files"          "$FILES_ZANNASTUDIO"
    echo ""

    echo -e "${YELLOW}Non-Production${NC}"
    echo "─────────────────────────────────────────"
    printf "  %-24s %'10d SLOC\n" "Zia demos (.zia)"  "$SLOC_ZIA"
    printf "  %-24s %'10d SLOC\n" "BASIC demos (.bas)" "$SLOC_BASIC"
    printf "  %-24s %'10d SLOC\n" "Build system (CMake)" "$SLOC_CMAKE"
    printf "  %-24s %'10d LOC\n"  "Documentation (.md)" "$LOC_DOCS"
    printf "  %-24s %'10d SLOC\n" "IL fixtures (.il)"  "$SLOC_IL"
    printf "  %-24s %'10d SLOC\n" "Scripts (sh/ps1)"   "$SLOC_SCRIPTS"
    echo ""

    echo -e "${BOLD}Totals${NC}"
    echo "─────────────────────────────────────────"
    printf "  %-24s %'10d\n" "Production SLOC"     "$SLOC_PROD"
    printf "  %-24s %'10d\n" "Test SLOC"           "$SLOC_TESTS"
    printf "  %-24s %'10d\n" "Zanna Studio SLOC"       "$SLOC_ZANNASTUDIO"
    printf "  %-24s %'10d\n" "Demo SLOC"           "$((SLOC_ZIA + SLOC_BASIC + SLOC_IL))"
    printf "  %-24s %'10d\n" "Overall SLOC"        "$SLOC_ALL"
    echo ""
    printf "  %-24s %'10d\n" "Source files (src/)" "$FILES_SRC"
    printf "  %-24s %'10d\n" "Zia files"           "$FILES_ZIA"
    printf "  %-24s %'10d\n" "IL fixtures"         "$FILES_IL"
    printf "  %-24s %'10d\n" "Total files"         "$FILES_TOTAL"
}

print_json() {
    cat <<ENDJSON
{
  "date": "$(date '+%Y-%m-%d')",
  "production": {
    "sloc": $SLOC_PROD,
    "loc": $LOC_PROD,
    "by_language": {
      "c": $SLOC_C,
      "cpp": $SLOC_CPP,
      "hpp": $SLOC_HPP,
      "h": $SLOC_H,
      "inc": $SLOC_INC,
      "objc": $SLOC_OBJC
    },
    "by_subsystem": {
      "runtime": $SUB_RUNTIME,
      "frontends": $SUB_FRONTENDS,
      "lib": $SUB_LIB,
      "codegen": $SUB_CODEGEN,
      "il": $SUB_IL,
      "tools": $SUB_TOOLS,
      "vm": $SUB_VM
    }
  },
  "tests": {
    "sloc": $SLOC_TESTS,
    "loc": $LOC_TESTS
  },
  "src_total": {
    "sloc": $SLOC_SRC,
    "loc": $LOC_SRC
  },
  "zannastudio": {
    "sloc": $SLOC_ZANNASTUDIO,
    "files": $FILES_ZANNASTUDIO
  },
  "demos": {
    "zia_sloc": $SLOC_ZIA,
    "basic_sloc": $SLOC_BASIC,
    "il_sloc": $SLOC_IL
  },
  "overall_sloc": $SLOC_ALL,
  "files": {
    "src": $FILES_SRC,
    "zia": $FILES_ZIA,
    "zannastudio": $FILES_ZANNASTUDIO,
    "il": $FILES_IL,
    "total": $FILES_TOTAL
  }
}
ENDJSON
}

# ─── Main ────────────────────────────────────────────────────────────────────

MODE="${1:---summary}"

case "$MODE" in
    --summary)   print_summary ;;
    --subsystem) print_subsystem ;;
    --all)       print_all ;;
    --json)      print_json ;;
    -h|--help)
        echo "Usage: $0 [--summary | --subsystem | --all | --json]"
        echo ""
        echo "  --summary    One-line totals (default)"
        echo "  --subsystem  SLOC by src/ subdirectory"
        echo "  --all        Full report"
        echo "  --json       Machine-readable JSON"
        ;;
    *)
        echo "Unknown option: $MODE" >&2
        echo "Usage: $0 [--summary | --subsystem | --all | --json]" >&2
        exit 1
        ;;
esac
