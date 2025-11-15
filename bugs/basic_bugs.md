# VIPER BASIC Known Bugs and Issues

*Last Updated: 2025-11-15*
*Source: Empirical testing during language audit*

---

## RECENT UPDATES (2025-11-13)

**Recent Fixes (2025-11-13)**:

- ✅ **BUG-007 RESOLVED**: Multi-dimensional arrays now work
- ✅ **BUG-013 RESOLVED**: SHARED keyword now accepted (compatibility no-op)
- ✅ **BUG-014 RESOLVED**: String arrays now work (duplicate of BUG-032)
- ✅ **BUG-015 RESOLVED**: String properties in classes now work
- ✅ **BUG-016 RESOLVED**: Local string variables in methods now work
- ✅ **BUG-020 RESOLVED**: String constants (CONST MSG$ = "Hello") now work
- ✅ **BUG-025 RESOLVED**: EXP of large values no longer crashes
- ✅ **BUG-026 RESOLVED**: DO WHILE loops with GOSUB now work
- ⚠️ **BUG-032/033 PARTIAL**: String arrays reported in basic_resolved.md but verification shows issues remain
- ✅ **BUG-034 RESOLVED**: MID$ float argument conversion now works
- ✅ **BUG-036 RESOLVED**: String comparison in OR conditions now works

**Behavior Tweaks (2025-11-13)**:
- IF/ELSEIF conditions now accept INTEGER as truthy (non-zero = true, 0 = false) in addition to BOOLEAN. Prior negative tests expecting an error on `IF 2 THEN` were updated.

**Boolean Type System Changes**: Modified `isBooleanType()` to only accept `Type::Bool` (not `Type::Int`) for logical operators (AND/ANDALSO/OR/ORELSE). This makes the type system stricter and fixes some test cases.

**Bug Statistics**: 51 resolved, 5 outstanding, 0 partially resolved (56 total documented)

**Recent Investigation (2025-11-14)**:
- ✅ **BUG-012 NOW RESOLVED**: BOOLEAN variables can now be compared with TRUE/FALSE constants and with each other; STR$(boolean) now works
- ✅ **BUG-017 NOW RESOLVED**: Class methods can now access global strings without crashing
- ✅ **BUG-019 NOW RESOLVED**: Float CONST preservation - CONST PI = 3.14159 now correctly stores as float
- ✅ **BUG-030 NOW RESOLVED**: Global variables now properly shared across SUB/FUNCTION (fixed as side effect of BUG-019)
- ✅ **BUG-035 NOW RESOLVED**: Duplicate of BUG-030, also fixed
- ✅ **BUG-010 NOW RESOLVED**: STATIC keyword now fully functional - procedure-local persistent variables work correctly
- ❌ **NEW OOP BUGS DISCOVERED (2025-11-13)**: During BasicDB development, discovered 6 OOP limitations (BUG-037 through BUG-042). After re-testing (2025-11-14), only 3 remain outstanding.
- ✅ **BUG-038 NOW RESOLVED**: String concatenation with method results works correctly
- ✅ **BUG-041 NOW RESOLVED**: Arrays of custom class types work perfectly
- ✅ **BUG-042 NOW RESOLVED**: LINE keyword no longer reserved, can be used as variable name
- ✅ **BUG-043 VERIFIED RESOLVED**: String arrays work correctly (duplicate/false report of BUG-032/033)

**Recent Fixes (2025-11-15)**:
- ✅ **BUG-037 NOW RESOLVED**: SUB methods on class instances can now be called (parser heuristic fix)
- ✅ **BUG-039 NOW RESOLVED**: Method call results can now be assigned to variables (OOP expression lowering fix)
- ✅ **BUG-040 NOW RESOLVED**: Functions can now return custom class types (symbol resolution and return statement fix)

**Stress Testing (2025-11-15)**:
Conducted comprehensive OOP stress testing by building text-based adventure games. Created 26 test files (1501 lines of code) exercising classes, arrays, strings, math, loops, and file inclusion. See `/bugs/bug_testing/STRESS_TEST_SUMMARY.md` for full report.

**New Bugs Discovered During Stress Testing (2025-11-15)**:
- ✅ **BUG-044 RESOLVED (2025-11-15)**: CHR() supported via alias to CHR$; ANSI codes work
- ✅ **BUG-045 RESOLVED (2025-11-15)**: STRING arrays honored with AS STRING; assignments validated correctly
- ✅ **BUG-046 RESOLVED (2025-11-15)**: Methods on array elements parse and run (e.g., `array(i).Method()`)
- ✅ **BUG-047 RESOLVED (2025-11-15)**: IF/THEN inside class methods no longer crashes (fixed OOP exit block emission)
- ✅ **BUG-048 RESOLVED (2025-11-15)**: Class methods can call module SUB/FUNCTIONs (signature pre-collection)
- ℹ️ **BUG-049 BY DESIGN**: RND() is zero-argument; propose ADR if argumentized forms are desired
- ✅ **BUG-050 VERIFIED**: SELECT CASE with multiple values works; not reproducible as an error
- ✅ **BUG-051 VERIFIED**: DO UNTIL loop works; not reproducible as an error
- ❌ **BUG-052 OPEN**: ON ERROR GOTO not implemented (no error handling)

**Grep Clone Stress Testing (2025-11-15)**:
Built a complete grep clone (vipergrep) testing file I/O, string searching, pattern matching, multiple file processing, and OOP. Successfully demonstrated OPEN/LINE INPUT/CLOSE, INSTR, LCASE$, and case-insensitive searching. Discovered BUG-052 (ON ERROR GOTO). See `/bugs/bug_testing/vipergrep_simple.bas` for working implementation.

**Priority**: Former CRITICAL items (BUG-047, BUG-048) resolved.

**Recent Fixes (2025-11-15 PM)**:
- Closed BUG-044/045/046/047/048 based on targeted frontend changes
- Verified BUG-050/051 pass in current build
- Left BUG-049 as design note (ADR recommended), BUG-052 open

**Othello Game Stress Testing (2025-11-15)**:
Built an OOP Othello/Reversi game testing arrays, game logic, move validation, and complex control flow. Discovered 4 new bugs including BUG-053 (CRITICAL compiler crash when accessing global arrays in functions). See `/bugs/bug_testing/othello_simple.bas` for working implementation demonstrating board representation, piece flipping, and score tracking.

**New Bugs from Othello Testing**:
- ❌ **BUG-053 CRITICAL**: Cannot access global arrays in SUB/FUNCTION - compiler assertion failure
- ❌ **BUG-054**: STEP reserved word cannot be used as variable name
- ❌ **BUG-055**: Cannot assign to FOR loop variable inside loop body
- ❌ **BUG-056 MAJOR**: Arrays not allowed as class fields

---

## OUTSTANDING BUGS (5 bugs)

The following bugs are currently unresolved and actively affect Viper BASIC development:

---

### BUG-053: Cannot access global arrays inside SUB/FUNCTION - compiler assertion failure
**Status**: ❌ CRITICAL - COMPILER CRASH
**Discovered**: 2025-11-15 during Othello game stress testing
**Test Cases**:
- /Users/stephen/git/viper/bugs/bug_testing/othello_04_array_crash.bas
- /Users/stephen/git/viper/bugs/bug_testing/othello_game.bas

**Description**:
Accessing global arrays from within SUB or FUNCTION procedures causes a compiler assertion failure: `Assertion failed: (info && info->slotId), function lowerArrayAccess, file Emit_Expr.cpp, line 98`.

**Example that crashes**:
```basic
DIM globalArray(10) AS INTEGER

FUNCTION AccessArray(index AS INTEGER) AS INTEGER
    DIM value AS INTEGER
    value = globalArray(index)  ' CRASH!
    RETURN value
END FUNCTION
```

**Error**:
```
Assertion failed: (info && info->slotId), function lowerArrayAccess, file Emit_Expr.cpp, line 98.
```

**Impact**: CRITICAL - Cannot use arrays with modular code. All array logic must be at module level, making code repetitive and unmaintainable. Severely limits ability to build complex programs.

**Workaround**: Keep all array access at module level. Cannot pass arrays or use them in functions.

**Note**: This is a compiler bug, not a language limitation. The compiler crashes rather than generating code or showing a proper error message.

---

### BUG-056: Arrays not allowed as class fields
**Status**: ❌ MAJOR
**Discovered**: 2025-11-15 during Othello game stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/othello_02_classes.bas

**Description**:
Cannot declare arrays as fields within CLASS definitions. This prevents creating classes that encapsulate array data.

**Example that fails**:
```basic
CLASS Board
    DIM cells(64) AS INTEGER  ' ERROR!
    DIM size AS INTEGER
END CLASS
```

**Error**:
```
error[B0001]: expected END, got DIM
    DIM cells(64) AS INTEGER
    ^
```

**Impact**: MAJOR - Cannot create proper OOP encapsulation for data structures that need arrays. Must use global arrays or work around with multiple fields.

**Workaround**: Declare arrays outside class at module level:
```basic
DIM boardCells(64) AS INTEGER  ' Global

CLASS Board
    DIM size AS INTEGER
    ' Cannot have array field
END CLASS
```

**Standard BASIC**: Many modern BASIC implementations support arrays as class members.

**Related**: This is similar to BUG-053 (array access issues) and may be part of a broader limitation with arrays in the compiler.

---

### BUG-052: ON ERROR GOTO not implemented
**Status**: ❌ OUTSTANDING
**Discovered**: 2025-11-15 during grep clone stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/grep_10_on_error.bas

**Description**:
The `ON ERROR GOTO label` statement for error handling is not implemented. Attempting to use it causes an "empty block" error during IL generation.

**Example that fails**:
```basic
ON ERROR GOTO ErrorHandler

OPEN "/nonexistent/file.txt" FOR INPUT AS #1
PRINT "File opened"
END

ErrorHandler:
    PRINT "An error occurred"
    END
```

**Error**:
```
error: main:UL999999995: empty block
```

**Current Behavior**:
File errors and other runtime errors cause immediate program termination with a Trap message. There is no way to catch or handle errors gracefully.

**Impact**: MODERATE - Cannot build robust programs that handle errors gracefully. File operations, division by zero, array bounds, etc. all cause immediate program termination.

**Workaround**: None effective. Must ensure all operations succeed or program will crash.

**Standard BASIC**: ON ERROR GOTO is a fundamental error handling mechanism in most BASIC implementations.

**Related**: May also affect ON ERROR RESUME NEXT, RESUME, ERR, ERL if they exist.

---

### BUG-055: Cannot assign to FOR loop variable inside loop body
**Status**: ❌ OUTSTANDING
**Discovered**: 2025-11-15 during Othello game stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/othello_game.bas (original version)

**Description**:
Cannot assign to a FOR loop counter variable inside the loop body, even when trying to break out of the loop early.

**Example that fails**:
```basic
FOR i = 1 TO 100
    PRINT i
    IF i = 50 THEN
        i = 999  ' Try to break loop - ERROR!
    END IF
NEXT i
```

**Error**:
```
error[B1010]: cannot assign to loop variable 'I' inside FOR
        i = 999
        ^
```

**Impact**: MODERATE - Cannot use common pattern of setting loop variable to break early. Must use flags or restructure code.

**Workaround**: Use a separate flag variable or EXIT FOR (if available):
```basic
DIM shouldBreak AS INTEGER
shouldBreak = 0

FOR i = 1 TO 100
    IF shouldBreak = 1 THEN
        ' Do nothing, loop will end
    ELSE
        PRINT i
        IF i = 50 THEN
            shouldBreak = 1
        END IF
    END IF
NEXT i
```

**Note**: This is actually good language design (prevents common bugs), but differs from some BASIC implementations that allow loop variable modification.

---

### BUG-054: STEP is a reserved word and cannot be used as variable name
**Status**: ❌ OUTSTANDING
**Discovered**: 2025-11-15 during Othello game stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/othello_game.bas (original version)

**Description**:
The identifier `STEP` cannot be used as a variable name because it is reserved for the FOR loop STEP clause. However, STEP is commonly used as a variable name in loops.

**Example that fails**:
```basic
DIM step AS INTEGER
FOR step = 1 TO 10
    PRINT step
NEXT step
```

**Error**:
```
error[B0001]: expected ident, got STEP
DIM step AS INTEGER
    ^^^^
```

**Impact**: MINOR - Can easily work around by using different variable names (e.g., stepNum, stepCount, etc.).

**Workaround**: Use alternative names: `stepNum`, `stepIndex`, `stepCount`, `i`, etc.

**Note**: Other BASIC implementations often allow STEP as a variable name when not in a FOR...TO...STEP context.

---

## RESOLVED BUGS (51 bugs)

**Note**: See `basic_resolved.md` for full details on all resolved bugs.

- ✅ **BUG-001**: String concatenation requires $ suffix for type inference - RESOLVED 2025-11-12
- ✅ **BUG-002**: & operator for string concatenation not supported - RESOLVED 2025-11-12
- ✅ **BUG-003**: FUNCTION name assignment syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-004**: Procedure calls require parentheses even with no arguments - RESOLVED 2025-11-12
- ✅ **BUG-005**: SGN function not implemented - RESOLVED 2025-11-12
- ✅ **BUG-006**: Limited trigonometric/math functions - RESOLVED 2025-11-12
- ✅ **BUG-007**: Multi-dimensional arrays not supported - RESOLVED 2025-11-13
- ✅ **BUG-008**: REDIM PRESERVE syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-009**: CONST keyword not implemented - RESOLVED 2025-11-12
- ✅ **BUG-010**: STATIC keyword not implemented - RESOLVED 2025-11-14
- ✅ **BUG-011**: SWAP statement not implemented - RESOLVED 2025-11-12
- ✅ **BUG-012**: BOOLEAN type incompatibility with TRUE/FALSE constants - RESOLVED 2025-11-14
- ✅ **BUG-013**: SHARED keyword not supported - RESOLVED 2025-11-13
- ✅ **BUG-014**: String arrays not supported (duplicate of BUG-032) - RESOLVED 2025-11-13
- ✅ **BUG-015**: String properties in classes cause runtime error - RESOLVED 2025-11-13
- ✅ **BUG-016**: Local string variables in methods cause compilation error - RESOLVED 2025-11-13
- ✅ **BUG-017**: Accessing global strings from methods causes segfault - RESOLVED 2025-11-14
- ✅ **BUG-018**: FUNCTION methods in classes cause code generation error - RESOLVED 2025-11-12
- ✅ **BUG-019**: Float literals assigned to CONST are truncated to integers - RESOLVED 2025-11-14
- ✅ **BUG-020**: String constants cause runtime error - RESOLVED 2025-11-13
- ✅ **BUG-021**: SELECT CASE doesn't support negative integer literals - RESOLVED 2025-11-12
- ✅ **BUG-022**: Float literals without explicit type default to INTEGER - RESOLVED 2025-11-12
- ✅ **BUG-023**: DIM with initializer not supported - RESOLVED 2025-11-12
- ✅ **BUG-024**: CONST with type suffix causes assertion failure - RESOLVED 2025-11-12
- ✅ **BUG-025**: EXP of large values causes overflow trap - RESOLVED 2025-11-13
- ✅ **BUG-026**: DO WHILE loops with GOSUB cause "empty block" error - RESOLVED 2025-11-13
- ✅ **BUG-027**: MOD operator doesn't work with INTEGER type (%) - RESOLVED 2025-11-12
- ✅ **BUG-028**: Integer division operator (\\) doesn't work with INTEGER type (%) - RESOLVED 2025-11-12
- ✅ **BUG-029**: EXIT FUNCTION and EXIT SUB not supported - RESOLVED 2025-11-12
- ✅ **BUG-030**: SUBs and FUNCTIONs cannot access global variables - RESOLVED 2025-11-14
- ✅ **BUG-031**: String comparison operators (<, >, <=, >=) not supported - RESOLVED 2025-11-12
- ✅ **BUG-032**: String arrays not supported - RESOLVED 2025-11-13
- ✅ **BUG-033**: String array assignment causes type mismatch error (duplicate of BUG-032) - RESOLVED 2025-11-13
- ✅ **BUG-034**: MID$ does not convert float arguments to integer - RESOLVED 2025-11-13
- ✅ **BUG-035**: Global variables not accessible in SUB/FUNCTION (duplicate of BUG-030) - RESOLVED 2025-11-14
- ✅ **BUG-036**: String comparison in OR condition causes IL error - RESOLVED 2025-11-13
- ✅ **BUG-037**: SUB methods on class instances cannot be called - RESOLVED 2025-11-15
- ✅ **BUG-038**: String concatenation with method results fails in certain contexts - RESOLVED 2025-11-14
- ✅ **BUG-039**: Cannot assign method call results to variables - RESOLVED 2025-11-15
- ✅ **BUG-040**: Cannot use custom class types as function return types - RESOLVED 2025-11-15
- ✅ **BUG-041**: Cannot create arrays of custom class types - RESOLVED 2025-11-14
- ✅ **BUG-042**: Reserved keyword 'LINE' cannot be used as variable name - RESOLVED 2025-11-14
- ✅ **BUG-043**: String arrays reported not working (duplicate of BUG-032/033) - RESOLVED 2025-11-13
- ✅ **BUG-044**: CHR() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-045**: STRING arrays not working with AS STRING syntax - RESOLVED 2025-11-15
- ✅ **BUG-046**: Cannot call methods on array elements - RESOLVED 2025-11-15
- ✅ **BUG-047**: IF/THEN/END IF inside class methods causes crash - RESOLVED 2025-11-15
- ✅ **BUG-048**: Cannot call module-level SUB/FUNCTION from within class methods - RESOLVED 2025-11-15
- ✅ **BUG-049**: RND() function signature incompatible with standard BASIC - BY DESIGN
- ✅ **BUG-050**: SELECT CASE with multiple values causes IL generation error - VERIFIED NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-051**: DO UNTIL loop causes IL generation error - VERIFIED NOT REPRODUCIBLE 2025-11-15

---

