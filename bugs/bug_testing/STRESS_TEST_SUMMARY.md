# Viper BASIC Stress Testing Summary

**Date**: 2025-11-15
**Objective**: Build a sophisticated text-based adventure game to stress test OOP, arrays, strings, and all language features

---

## Testing Strategy

Built increasingly complex BASIC programs piece by piece:
1. Started with basic class definitions
2. Added class fields and methods
3. Tested arrays (integer and object types)
4. Tested string manipulation
5. Built complete OOP adventure games
6. Tested all loop constructs
7. Tested math and string functions
8. Created comprehensive feature test

---

## Test Programs Created

All test files are in `/Users/stephen/git/viper/bugs/bug_testing/`:

### Basic Feature Tests (01-16)
1. **test_01_base_class.bas** - Empty class creation ✅
2. **test_02_class_fields.bas** - Class with STRING fields ✅
3. **test_03_class_methods.bas** - SUB and FUNCTION methods ✅
4. **test_04_ansi_color.bas** - ANSI color codes (CHR() test) ❌ BUG-044
5. **test_04_ansi_color_v2.bas** - Color workaround ✅
6. **test_05_addfile.bas** - ADDFILE keyword ✅
7. **test_06_arrays.bas** - STRING arrays ❌ BUG-045
8. **test_06_arrays_int.bas** - INTEGER arrays ✅
9. **test_07_object_arrays.bas** - Method calls on array elements ❌ BUG-046
10. **test_07_object_arrays_v2.bas** - Object arrays with workaround ✅
11. **test_08_room_class.bas** - IF/THEN in class methods ❌ BUG-047
12. **test_08_room_class_v2.bas** - Room class without IF ✅
13. **test_09_if_in_sub.bas** - IF/THEN crash test ❌ BUG-047 (crash)
14. **test_09_if_single_line.bas** - Single-line IF crash ❌ BUG-047 (segfault)
15. **test_10_if_basic.bas** - IF at module level ✅
16. **test_11_while_loop.bas** - WHILE loop ✅
17. **test_12_do_loop.bas** - DO WHILE variants ✅
18. **test_13_select_case.bas** - SELECT CASE ✅
19. **test_14_string_functions.bas** - String functions ✅
20. **test_15_math.bas** - Math functions ✅
21. **test_16_input.bas** - INPUT statement (not tested)

### Complete Game Programs
1. **colors.bas** - Color utility module for ADDFILE ✅
2. **game_utils.bas** - Game utilities module ✅
3. **adventure_game.bas** - First attempt ❌ BUG-048
4. **adventure_game_v2.bas** - Working adventure game ✅
5. **dungeon_crawler.bas** - Complex OOP game ✅
6. **comprehensive_test.bas** - All features in one program ✅

---

## Bugs Discovered

### Critical Bugs (Blockers)

**BUG-047**: IF/THEN/END IF inside class methods causes crash
- **Severity**: CRITICAL
- **Impact**: Cannot use conditional logic in class methods
- **Errors**: "empty block", `std::length_error`, segfault
- **Workaround**: None effective

**BUG-048**: Cannot call module-level SUB/FUNCTION from within class methods
- **Severity**: CRITICAL
- **Impact**: Cannot share utility functions between classes and module code
- **Error**: `unknown callee @procedurename`
- **Workaround**: Move all logic to module level

### Major Bugs

**BUG-045**: STRING arrays not working
- **Severity**: MAJOR
- **Impact**: Cannot create arrays of strings
- **Error**: `array element type mismatch: expected INT, got STRING`
- **Workaround**: Use arrays of objects with string fields
- **Note**: Contradicts BUG-032/033/043 resolution status

**BUG-046**: Cannot call methods on array elements
- **Severity**: MAJOR
- **Impact**: Cannot use `array(i).Method()` syntax
- **Error**: `expected procedure call after identifier`
- **Workaround**: Use temporary variable

### Moderate Bugs

**BUG-050**: SELECT CASE with multiple values causes IL error
- **Severity**: MODERATE
- **Example**: `CASE 4, 5` fails
- **Workaround**: Use separate CASE statements

**BUG-051**: DO UNTIL loop causes IL error
- **Severity**: MODERATE
- **Workaround**: Use `DO WHILE NOT (condition)`

### Minor Bugs

**BUG-044**: CHR() function not implemented
- **Severity**: MINOR
- **Impact**: Cannot generate ANSI codes or control characters
- **Workaround**: None for runtime generation

**BUG-049**: RND() doesn't accept arguments
- **Severity**: MINOR
- **Impact**: Cannot control random number generation
- **Workaround**: Use `RND()` without arguments

---

## Features Successfully Tested

### Object-Oriented Programming ✅
- Class definitions with fields and methods
- Multiple classes in one program
- SUB and FUNCTION methods
- Object instantiation with NEW
- Object field access (obj.field)
- Method calls (obj.Method())
- Arrays of custom class types (with workarounds)

### Arrays ✅ (Partial)
- INTEGER arrays: ✅ Full support
- Object arrays: ✅ Full support (access via temp variables)
- STRING arrays: ❌ Not working (BUG-045)

### String Manipulation ✅
- LEN() - string length
- LEFT$() - leftmost characters
- RIGHT$() - rightmost characters
- MID$() - substring extraction
- UCASE$() - uppercase conversion
- LCASE$() - lowercase conversion
- STR$() - number to string
- VAL() - string to number
- String concatenation with +

### Math Functions ✅
- Basic operators: +, -, *, /
- SQR() - square root
- ABS() - absolute value
- INT() - integer part
- RND() - random number
- SIN(), COS(), TAN() - trigonometry
- EXP() - exponential
- LOG() - natural logarithm

### Control Flow ✅ (Partial)
- FOR...NEXT loops: ✅
- WHILE...WEND loops: ✅
- DO WHILE...LOOP: ✅
- DO...LOOP WHILE: ✅
- DO UNTIL...LOOP: ❌ (BUG-051)
- SELECT CASE: ✅ (single values only)
- IF/THEN/END IF: ✅ (module level only)
- IF/THEN/ELSE: ✅ (module level only)

### Other Features ✅
- CONST declarations
- Module-level SUB and FUNCTION
- ADDFILE keyword for file inclusion
- Boolean logic (AND, OR, NOT)
- Type conversions
- PRINT with semicolons for concatenation

---

## Workarounds Used Successfully

1. **No IF in methods**: Use SELECT CASE or move logic to module level
2. **No module SUB calls from methods**: Keep all class logic self-contained
3. **No STRING arrays**: Use arrays of objects with string fields
4. **No method calls on array elements**: Use temporary variables
5. **No CHR()**: Use hardcoded escape sequences or pre-defined constants
6. **No DO UNTIL**: Use `DO WHILE NOT (condition)`
7. **No CASE 1,2,3**: Use separate CASE statements

---

## Sample Programs That Work

### adventure_game_v2.bas
- 5 Room objects with descriptions
- 5 Item objects with properties
- Player object with stats
- Loop-based game progression
- ~180 lines of code

### dungeon_crawler.bas
- 6 different classes (Entity, Monster, Item, Player, GameStats)
- Multiple object arrays (10 monsters, 15 items)
- Complex game simulation with combat
- Statistical tracking
- ~280 lines of code

### comprehensive_test.bas
- Tests 30+ language features
- 10 test sections
- Multiple classes with methods
- String, math, array, loop tests
- ~330 lines of code
- **Full test suite runs successfully!**

---

## Conclusions

### What Works Well
1. **OOP fundamentals** are solid when avoiding known bugs
2. **Arrays** work well for INTEGER and object types
3. **String manipulation** is comprehensive and functional
4. **Math functions** are complete
5. **FOR loops** are reliable
6. **ADDFILE** works perfectly for modular code
7. **CONST** declarations work correctly

### Critical Issues for OOP
1. **IF/THEN in methods** - Absolutely required for real OOP
2. **Module function calls** - Needed for code reuse
3. **STRING arrays** - Fundamental data structure

### Priority Fix Recommendations
1. **BUG-047** (IF in methods) - Highest priority, blocks all real OOP
2. **BUG-048** (module calls) - High priority, severely limits architecture
3. **BUG-045** (STRING arrays) - High priority, fundamental feature
4. **BUG-046** (method calls on array elements) - Medium priority
5. **BUG-051** (DO UNTIL) - Medium priority
6. **BUG-050** (multiple CASE values) - Low priority
7. **BUG-044** (CHR) - Low priority
8. **BUG-049** (RND args) - Low priority

### Overall Assessment
Viper BASIC's OOP implementation shows great promise, with classes, methods, and object arrays working. However, the inability to use IF/THEN inside class methods (BUG-047) and call module functions from methods (BUG-048) are critical blockers for building real applications.

**Recommendation**: Focus on fixing BUG-047 and BUG-048 as these are the primary impediments to using Viper BASIC as a viable OOP language.

---

## Test Coverage Summary

- **Classes**: Extensive ✅
- **Methods**: Extensive (within limitations) ✅
- **Arrays**: Good (INTEGER, objects) ⚠️
- **Strings**: Excellent ✅
- **Math**: Excellent ✅
- **Loops**: Good (most variants) ⚠️
- **Control Flow**: Limited (no IF in methods) ⚠️
- **File Inclusion**: Excellent ✅
- **Constants**: Good ✅
- **Functions**: Excellent ✅

**Total Test Files Created**: 26 files
**Bugs Found**: 8 new bugs
**Features Tested**: 30+ language features
**Lines of Test Code**: ~1000+ lines

---

End of stress testing report.
