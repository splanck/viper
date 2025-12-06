# Viper BASIC Stress Testing

This directory contains comprehensive stress tests for Viper BASIC's OOP implementation, created on 2025-11-15.

## Quick Links

- **[STRESS_TEST_SUMMARY.md](STRESS_TEST_SUMMARY.md)** - Complete testing report with findings
- **[BUGS_QUICK_REFERENCE.md](BUGS_QUICK_REFERENCE.md)** - Quick reference for all outstanding bugs
- **[../basic_bugs.md](../basic_bugs.md)** - Main bug tracking document

## Purpose

Build increasingly complex OOP programs to discover bugs and limitations in Viper BASIC, with focus on:
- Classes and methods
- Arrays (integer, object, string)
- String manipulation
- Control flow (loops, conditionals, SELECT CASE)
- Math operations
- File inclusion (ADDFILE)

## Test Files (26 files, 1501 lines)

### Basic Feature Tests (test_01 through test_16)
Progressive tests of individual language features.

### Complete Applications
- **adventure_game_v2.bas** - Text adventure with 5 rooms, 5 items, player stats (180 lines)
- **dungeon_crawler.bas** - RPG simulation with 6 classes, combat system (280 lines)
- **comprehensive_test.bas** - All features in one program (330 lines)

### Support Files
- **colors.bas** - Color utility module (for ADDFILE testing)
- **game_utils.bas** - Game helper functions

## Bugs Found

**8 new bugs discovered** (BUG-044 through BUG-051):
- 2 CRITICAL: IF in methods (BUG-047), module function calls (BUG-048)
- 2 MAJOR: STRING arrays (BUG-045), array method calls (BUG-046)
- 2 MODERATE: Multiple CASE values (BUG-050), DO UNTIL (BUG-051)
- 2 MINOR: CHR() (BUG-044), RND() signature (BUG-049)

## Running Tests

From the viper root directory:

```bash
# Run individual test
./build/src/tools/ilc/ilc front basic -run bugs/bug_testing/test_01_base_class.bas

# Run without heap debug output
./build/src/tools/ilc/ilc front basic -run bugs/bug_testing/comprehensive_test.bas 2>&1 | grep -v "rt_heap"

# Run adventure game
./build/src/tools/ilc/ilc front basic -run bugs/bug_testing/adventure_game_v2.bas 2>&1 | grep -v "rt_heap"

# Run dungeon crawler
./build/src/tools/ilc/ilc front basic -run bugs/bug_testing/dungeon_crawler.bas 2>&1 | grep -v "rt_heap"
```

## Key Findings

### What Works ✅
- Classes with fields and methods
- Object instantiation and field access
- Integer arrays and object arrays
- All string functions (LEN, LEFT$, RIGHT$, MID$, UCASE$, LCASE$, STR$, VAL)
- All math functions (SQR, ABS, INT, SIN, COS, TAN, EXP, LOG, RND)
- FOR loops, WHILE loops, DO WHILE loops
- SELECT CASE (single values)
- ADDFILE keyword
- CONST declarations
- Boolean operations

### Critical Issues ❌
- IF/THEN crashes inside class methods (BUG-047)
- Cannot call module SUBs from class methods (BUG-048)
- STRING arrays don't work (BUG-045)
- Cannot call methods on array elements (BUG-046)

### Workarounds Used
See STRESS_TEST_SUMMARY.md for detailed workarounds.

## Files Overview

```
bugs/bug_testing/
├── README.md                      (this file)
├── STRESS_TEST_SUMMARY.md         (full report)
├── BUGS_QUICK_REFERENCE.md        (quick bug reference)
│
├── test_01_base_class.bas         (empty class)
├── test_02_class_fields.bas       (class with fields)
├── test_03_class_methods.bas      (SUB and FUNCTION methods)
├── test_04_ansi_color.bas         (CHR test - fails)
├── test_04_ansi_color_v2.bas      (workaround)
├── test_05_addfile.bas            (ADDFILE keyword)
├── test_06_arrays.bas             (STRING arrays - fails)
├── test_06_arrays_int.bas         (INTEGER arrays - works)
├── test_07_object_arrays.bas      (method calls on array elements - fails)
├── test_07_object_arrays_v2.bas   (workaround)
├── test_08_room_class.bas         (IF in methods - crashes)
├── test_08_room_class_v2.bas      (workaround)
├── test_09_if_in_sub.bas          (IF crash test)
├── test_09_if_single_line.bas     (single-line IF crash)
├── test_10_if_basic.bas           (IF at module level - works)
├── test_11_while_loop.bas         (WHILE loop)
├── test_12_do_loop.bas            (DO loops)
├── test_13_select_case.bas        (SELECT CASE)
├── test_14_string_functions.bas   (string functions)
├── test_15_math.bas               (math functions)
├── test_16_input.bas              (INPUT - not tested)
│
├── colors.bas                     (utility module)
├── game_utils.bas                 (utility module)
├── adventure_game.bas             (first attempt - fails)
├── adventure_game_v2.bas          (working version)
├── dungeon_crawler.bas            (complex game)
└── comprehensive_test.bas         (all features)
```

## Statistics

- **Total test files**: 26 (.bas files)
- **Total lines of code**: 1,501
- **Bugs discovered**: 8
- **Features tested**: 30+
- **Classes created**: 15+
- **Test programs**: 3 complete games

## Recommendations

1. **Fix BUG-047** (IF in methods) - Highest priority
2. **Fix BUG-048** (module calls) - Highest priority
3. **Fix BUG-045** (STRING arrays) - High priority
4. Fix remaining bugs in priority order

These two critical bugs (047, 048) block all serious OOP development in Viper BASIC.

---

Created: 2025-11-15
Testing Duration: ~2 hours
Tester: AI stress testing session
