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

**Bug Statistics**: 43 resolved, 9 outstanding, 0 partially resolved (52 total documented)

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

## BUG REPORTS

**Note**: See `basic_resolved.md` for details on resolved bugs.

### BUG-001: String concatenation requires $ suffix for type inference
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-002: & operator for string concatenation not supported
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-003: FUNCTION name assignment syntax not supported
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-004: Procedure calls require parentheses even with no arguments
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-005: SGN function not implemented
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-006: Limited trigonometric/math functions
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-007: Multi-dimensional arrays not supported
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details

---

### BUG-008: REDIM PRESERVE syntax not supported
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-009: CONST keyword not implemented
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-010: STATIC keyword
**Status**: ✅ RESOLVED 2025-11-14
**Test Cases**: devdocs/basic/test_bug010_static.bas, devdocs/basic/test_bug010_static_fixed.bas

**Original Issue**:
The STATIC keyword for declaring persistent local variables in procedures caused an assertion failure during code generation. The parser recognized STATIC, but the lowering phase didn't allocate storage for STATIC variables.

**Example that now works**:
```basic
SUB Counter()
    STATIC count AS INTEGER
    count = count + 1
    PRINT count
END SUB

Counter()  ' Prints: 1
Counter()  ' Prints: 2
Counter()  ' Prints: 3
```

**Fix Details** (2025-11-14):
1. **Storage Mechanism**: STATIC variables now use the `rt_modvar_*` runtime infrastructure (same as module-level globals)
2. **Scoped Naming**: Each STATIC variable is stored with procedure-qualified name (e.g., "Counter.count") to ensure isolation between procedures
3. **Implementation**: Modified `resolveVariableStorage()` in Lowerer.Procedure.cpp to detect `isStatic` flag and emit appropriate `rt_modvar_addr_*` calls

**File Changes**:
- `/src/frontends/basic/Lowerer.Procedure.cpp` (lines 564-620): Added STATIC variable resolution before module-level global check

**Key Features**:
- ✅ Variables persist across procedure calls (zero-initialized on first access)
- ✅ Isolated between procedures (each procedure has its own namespace)
- ✅ Works with all types (INTEGER, SINGLE, STRING, etc.)
- ✅ Can have multiple STATIC variables in one procedure
- ✅ Works in both SUB and FUNCTION

**Test Coverage**:
- Basic counter increment across calls
- Multiple STATIC variables in one procedure
- Same-named STATIC variables in different procedures (isolation test)
- Type suffix handling (e.g., `STATIC f#`)
- STATIC in FUNCTION with return values
- Mixed local and STATIC variables

---

### BUG-011: SWAP statement not implemented
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-012: BOOLEAN type incompatibility with TRUE/FALSE constants
**Status**: ✅ RESOLVED 2025-11-14
**Test Cases**: /devdocs/basic/test_bug012_boolean_comparisons.bas, /devdocs/basic/test_bug012_str_bool.bas

**Original Issue**:
Variables declared with `DIM x AS BOOLEAN` could not be compared with TRUE/FALSE constants or with each other using = and <> operators. STR$() also rejected BOOLEAN arguments.

**Example that now works**:
```basic
DIM flag AS BOOLEAN
flag = TRUE
IF flag = TRUE THEN PRINT "Works!"     ' Now succeeds
IF flag <> FALSE THEN PRINT "Works!"   ' Now succeeds
PRINT STR$(TRUE)                       ' Prints -1
PRINT STR$(FALSE)                      ' Prints 0
```

**Fix Details** (2025-11-14):
1. **Semantic Analysis**: Modified `validateComparisonOperands()` in Check_Expr_Binary.cpp to allow BOOLEAN-vs-BOOLEAN comparisons for Eq/Ne operators
2. **STR$ Builtin**: Modified `checkArgType()` in SemanticAnalyzer.Builtins.cpp to whitelist BOOLEAN for STR$() conversion
3. **IL Lowering**: Fixed operand type coercion in `normalizeNumericOperands()` in LowerExprNumeric.cpp to promote i16 BOOLEAN variables to i64 when compared with i64 TRUE/FALSE constants

**Resolution**:
BOOLEAN type is now fully usable for equality/inequality comparisons with TRUE/FALSE constants and other BOOLEAN variables. STR$(boolean) correctly converts TRUE→"-1" and FALSE→"0".

**Note**: Functions like EOF() still return INT rather than BOOLEAN, which is a separate design decision about builtin return types (classic BASIC compatibility vs modern type safety).

---

### BUG-013: SHARED keyword not supported
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details

---

### BUG-014: String arrays not supported (duplicate of BUG-032)
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details

---


### BUG-015: String properties in classes cause runtime error
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details

---

### BUG-016: Local string variables in methods cause compilation error
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details

---

### BUG-017: Accessing global strings from methods causes segfault
**Status**: ✅ RESOLVED 2025-11-14
**Test Case**: devdocs/basic/test_bug017_global_string_method.bas

**Resolution**:
Class methods can now successfully access global string variables without crashing. The program compiles, runs, and produces correct output.

**Test Code**:
```basic
DIM globalString AS STRING
globalString = "Hello World"

CLASS Test
    SUB UseGlobal()
        PRINT globalString  ' Now works!
    END SUB
END CLASS

DIM t AS Test
t = NEW Test()
t.UseGlobal()
```

**Output**:
```
Hello World
```

**Analysis**:
Fixed as a side effect of OOP or string handling improvements. Global string variables are now properly accessible from class method scopes without segmentation faults.

---

### BUG-018: FUNCTION methods in classes cause code generation error
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-019: Float literals assigned to CONST are truncated to integers
**Status**: ✅ RESOLVED 2025-11-14
**Test Cases**: devdocs/basic/test_bug019_const_float.bas, devdocs/basic/test_bug019_const_float_fixed.bas

**Original Issue**:
When a float literal was assigned to a CONST declaration without a type suffix, the value was truncated to an integer rather than preserved as a float.

**Example that now works**:
```basic
CONST PI = 3.14159
PRINT PI              ' Now outputs: 3.14159 (was: 3)
PRINT STR$(PI)        ' Now outputs: "3.14159" (was: "3")

CONST E = 2.71828
PRINT E               ' Now outputs: 2.71828 (was: 3)

CONST HALF = 0.5
PRINT HALF            ' Now outputs: 0.5 (was: 0)

CONST A = 2
PRINT A               ' Outputs: 2 (integer CONST still works)
```

**Fix Details** (2025-11-14):
1. **Semantic Analysis** (`SemanticAnalyzer.Stmts.Runtime.cpp`): Already had float type inference for CONST when initializer is float (lines 615-619)
2. **Lowering Storage** (`Lowerer.Procedure.cpp`): Modified `getSlotType()` to respect semantic analysis types and only apply symbol table override when semantic analyzer has no type info
3. **STR$ Classification** (`lower/Lowerer_Expr.cpp`): Modified `classifyNumericType()` VarExpr visitor to consult semantic analyzer first for CONST float types before checking symbol table

**Root Cause**:
- Semantic analyzer correctly inferred Float type for `CONST PI = 3.14159`
- But lowering phase's symbol table had I64 from parser (no suffix = I64 default)
- `getSlotType()` and `classifyNumericType()` used symbol table type instead of semantic analysis type
- This caused CONST to be stored as i64 and STR$() to use integer conversion

**Resolution**:
CONST declarations now correctly preserve float types when initialized with float literals. Both storage (`lowerConst`) and conversion (`STR$`) now respect the inferred float type.

---

### BUG-020: String constants cause runtime error
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details

---

### BUG-021: SELECT CASE doesn't support negative integer literals
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-022: Float literals without explicit type default to INTEGER
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details
**Test Cases**: test_float_literals.bas, test_scientific_calc.bas
**Discovered**: 2025-11-12 during comprehensive testing

**Description**:
Float literals like `3.14159` are converted to integers (truncated) when assigned to variables without explicit type suffixes or AS clauses. The type inference system defaults to INTEGER for variables, causing loss of precision.

**Reproduction**:
```basic
x = 3.14159
PRINT x  ' Output: 3 (expected 3.14159)

radius = 5.5
PRINT radius  ' Output: 6 (expected 5.5)

circumference = 2.0 * 3.14159 * 5.5
PRINT circumference  ' Output: integer result, not 34.5575
```

**Warning Message**:
```
warning[B2002]: narrowing conversion from FLOAT to INT in assignment
x = 3.14159
^
```

**Workaround**:
Use type suffixes or explicit AS clauses. All of the following work:
```basic
REM Method 1: Type suffixes (CONFIRMED WORKING)
x! = 3.14159    ' ! suffix for FLOAT
PRINT x!        ' Output: 3.14159

pi# = 3.14159265359   ' # suffix for DOUBLE
PRINT pi#       ' Output: 3.14159265359

count% = 42     ' % suffix for INTEGER
name$ = "Alice" ' $ suffix for STRING

REM Method 2: Explicit AS clause (CONFIRMED WORKING)
DIM radius AS FLOAT
radius = 5.5
PRINT radius    ' Output: 5.5

DIM e AS DOUBLE
e = 2.71828
PRINT e         ' Output: 2.71828
```

**Analysis**:
The type inference system in SemanticAnalyzer.cpp defaults to INTEGER (I64) for variables without explicit type markers. While the literal is parsed as FLOAT, it gets converted to INT during assignment because the target variable is typed as INT. This is a fundamental design decision in the type system that prioritizes INTEGER as the default type, similar to early BASIC dialects. However, it makes floating-point computation difficult without explicit type annotations.

**Impact**:
Makes floating-point mathematics nearly impossible without explicit type suffixes. Scientific computing, financial calculations, and any program requiring precision beyond integers cannot be written easily. Combined with BUG-019, this makes mathematical programming very limited.

**Related Bugs**: BUG-019 (CONST float truncation)

---

### BUG-023: DIM with initializer not supported
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-024: CONST with type suffix causes assertion failure
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-025: EXP of large values causes overflow trap
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details

---


### BUG-026: DO WHILE loops
**Difficulty**: 🟡 MEDIUM with GOSUB cause "empty block" error
**Severity**: High
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details
**Test Case**: test_do_gosub.bas, dungeon_quest.bas
**Discovered**: 2025-11-12 during text adventure game development

**Description**:
DO WHILE loops that contain only GOSUB statements (or perhaps any control flow that doesn't generate local code) cause a code generation error "empty block". The loop body is not recognized as having content.

**Reproduction**:
```basic
count% = 0
DO WHILE count% < 3
    GOSUB Increment
LOOP
END

Increment:
    count% = count% + 1
RETURN
```

**Error Message**:
```
error: main:do_done: empty block
```

**Workaround**:
Add inline code before/after the GOSUB:
```basic
DO WHILE count% < 3
    x% = count%    REM Add inline statement
    GOSUB Increment
LOOP
```

Or use FOR loops instead of DO WHILE.

**Analysis**:
Fixed by keeping loop done blocks open (unterminated) and letting the statement sequencer emit the fallthrough edge to the next line. Previously, lowering marked done blocks as terminated without emitting a terminator when bodies consisted solely of control transfers (e.g., GOSUB), triggering IL verifier "empty block" errors. A golden test was added (basic/do_gosub_loop.bas).

**Impact**:
Cannot structure programs with DO WHILE loops calling subroutines. This is a major limitation for building modular, complex programs.

---

### BUG-027: MOD operator doesn't work with INTEGER type (%)
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-028: Integer division operator (\\) doesn't work with INTEGER type (%)
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details (same fix as BUG-027)

---

### BUG-029: EXIT FUNCTION and EXIT SUB support — ✅ **RESOLVED** — See [basic_resolved.md](basic_resolved.md#bug-029)

---

### BUG-030: SUBs and FUNCTIONs cannot access global variables
**Status**: ✅ RESOLVED 2025-11-14
**Test Cases**: devdocs/basic/test_bug030_fixed.bas, test_bug030_scenario1-6.bas

**Original Issue**:
Variables declared at module level were not properly shared between main and SUB/FUNCTION procedures. Each SUB/FUNCTION saw zero-initialized copies instead of the actual global values.

**Example that now works**:
```basic
DIM X AS INTEGER
X = 1
PRINT "Main: X = "; X    ' Outputs: 1

SUB IncrementX()
    X = X + 1            ' Now sees X=1, not X=0
    PRINT "SUB: X = "; X ' Outputs: 2
END SUB

IncrementX()
PRINT "Main: X = "; X    ' Outputs: 2 (was 1)
```

**Root Cause**:
The infrastructure for module-level globals via `rt_modvar_addr_*` functions was already in place, but was selecting the wrong helper due to type mismatches. The BUG-019 fix (respecting semantic type inference in `getSlotType()`) ensures the correct `rt_modvar_addr_*` helper is selected based on the variable's actual type.

**Fix Details** (2025-11-14):
- No BUG-030-specific changes needed
- Fixed as a side effect of BUG-019 type precedence fix
- `Lowerer::getSlotType()` now respects semantic analysis types
- This ensures correct `rt_modvar_addr_i64`, `rt_modvar_addr_f64`, `rt_modvar_addr_str`, etc. selection

**Resolution**:
Module-level globals are now properly shared across SUB/FUNCTION boundaries. All scalar types (INTEGER, FLOAT, STRING, BOOLEAN) work correctly. Arrays may have additional issues (not tested).

**Test Results**:
All 6 scenarios pass:
1. INTEGER global shared between Main and SUB ✅
2. FUNCTION can read and use globals ✅
3. SUB modifies, FUNCTION reads ✅
4. STRING global shared ✅
5. SUB-to-SUB communication via globals ✅
6. FUNCTION-to-FUNCTION communication ✅

---

### BUG-031: String comparison operators (<, >, <=, >=) not supported
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-032: String arrays not supported
**Difficulty**: 🔴 HARD
**Severity**: High
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details
**Test Case**: tests/golden/arrays/string_array_store_and_print.bas

**Description**:
String arrays now allocate, store, load, and print elements end-to-end via runtime helpers.

**Reproduction**:
```basic
DIM names$(3)
names$(0) = "Alice"
PRINT names$(0)
```

**Error Message**:
Printed output and IL verification both pass; see the new golden above.

---

### BUG-033: String array assignment causes type mismatch error (duplicate of BUG-032)
**Status**: ✅ RESOLVED 2025-11-13 - Covered by BUG-032 resolution

---

### BUG-034: MID$ does not convert float arguments to integer
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details

---

### BUG-035: Global variables not accessible in SUB/FUNCTION (duplicate of BUG-030)
**Status**: ✅ RESOLVED 2025-11-14 (duplicate of BUG-030)

**Resolution**:
This was a duplicate of BUG-030. Fixed as a side effect of the BUG-019 type precedence fix, which ensures the correct `rt_modvar_addr_*` helper is selected for module-level globals.

**Example that now works**:
```basic
DIM board$ AS STRING

SUB InitBoard()
    board$ = "ABCDEFGHI"  ' Now works!
END SUB

InitBoard()
PRINT board$  ' Prints: ABCDEFGHI
```

---

### BUG-036: String comparison in OR condition causes IL error
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details

---

### BUG-037: SUB methods on class instances cannot be called
**Status**: ✅ RESOLVED 2025-11-15 - See [basic_resolved.md](basic_resolved.md#bug-037-sub-methods-on-class-instances-cannot-be-called) for details

---

### BUG-038: String concatenation with method results fails in certain contexts
**Status**: ✅ RESOLVED 2025-11-14
**Test Case**: /tmp/test_bug038_string_concat.bas

**Original Issue**:
Initially reported that concatenating strings with method call results caused operand type mismatch errors.

**Example that now works**:
```basic
CLASS Record
  name AS STRING

  FUNCTION GetName() AS STRING
    RETURN Me.name
  END FUNCTION
END CLASS

DIM rec AS Record
rec = NEW Record()
rec.name = "Alice"
PRINT "Name: " + rec.GetName()  ' Works!
```

**Output**: `Name: Alice`

**Resolution**:
Testing confirms that string concatenation with method results works correctly. The original error may have been related to BUG-039 (cannot assign method results to variables) or another context-specific issue that has since been resolved.

---

### BUG-039: Cannot assign method call results to variables
**Status**: ✅ RESOLVED 2025-11-15 - See [basic_resolved.md](basic_resolved.md#bug-039-cannot-assign-method-call-results-to-variables) for details

---

### BUG-040: Cannot use custom class types as function return types
**Status**: ✅ RESOLVED 2025-11-15 - See [basic_resolved.md](basic_resolved.md#bug-040-cannot-use-custom-class-types-as-function-return-types) for details

---

### BUG-041: Cannot create arrays of custom class types
**Status**: ✅ RESOLVED 2025-11-14
**Test Cases**: /tmp/test_bug041_class_array.bas, /tmp/test_bug041_complex.bas

**Original Issue**:
Initially reported that arrays of custom CLASS types caused IL generation errors when assigning instances to array elements.

**Example that now works**:
```basic
CLASS Record
  id AS INTEGER
  name AS STRING
END CLASS

DIM records(5) AS Record
DIM i AS INTEGER
FOR i = 0 TO 2
  records(i) = NEW Record()
  records(i).id = i + 1
  records(i).name = "Record" + STR$(i)
NEXT i

FOR i = 0 TO 2
  PRINT "ID: "; records(i).id; " Name: "; records(i).name
NEXT i
```

**Output**:
```
ID: 1 Name: Record0
ID: 2 Name: Record1
ID: 3 Name: Record2
```

**Resolution**:
Arrays of custom class types work perfectly! Can create, assign, access, and iterate over class instance arrays. The original error reported in BasicDB development may have been context-specific or related to other issues.

---

### BUG-042: Reserved keyword 'LINE' cannot be used as variable name
**Status**: ✅ RESOLVED 2025-11-14
**Test Case**: /tmp/test_bug042_line_keyword.bas

**Original Issue**:
Initially reported that the identifier `line` (or `LINE`) could not be used as a variable name because the parser thought it was part of `LINE INPUT` statement.

**Example that now works**:
```basic
DIM line AS STRING
line = "Hello World"
PRINT line
```

**Output**: `Hello World`

**Resolution**:
Testing confirms that `line` can be used as a variable name without any parser errors. The keyword is properly contextualized and doesn't conflict with `LINE INPUT` statement parsing.

---

### BUG-043: String arrays reported not working (duplicate of BUG-032/033)
**Status**: ✅ RESOLVED 2025-11-13 - Duplicate/false report
**Discovered**: 2025-11-13

**Description**:
Initially reported that STRING arrays don't work, but testing confirms they work correctly. This was a duplicate concern about BUG-032/033 which had already been resolved.

**Validation**:
```basic
DIM names$(5)
names$(0) = "Alice"
names$(1) = "Bob"
PRINT names$(0)  ' Outputs: Alice
PRINT names$(1)  ' Outputs: Bob
```

**Resolution**: String arrays work correctly. BUG-032/033 resolution was complete.

---

## NEW BUGS FOUND DURING STRESS TESTING (2025-11-15)

### BUG-044: CHR() function not implemented
**Status**: ✅ RESOLVED 2025-11-15
**Discovered**: 2025-11-15 during text adventure stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/test_04_ansi_color.bas

**Description**:
The `CHR()` function, which converts an ASCII/character code to a string character, is not implemented in Viper BASIC. This is a standard BASIC function needed for generating special characters like ANSI escape sequences.

**Example that fails**:
```basic
DIM ESC AS STRING
ESC = CHR(27)  ' Try to create ESC character
PRINT ESC + "[31mRed text[0m"
```

**Error**:
```
error[B1006]: unknown procedure 'chr' (tried: chr)
ESC = CHR(27)
      ^^^
```

**Impact**: Cannot generate ANSI escape codes, control characters, or arbitrary characters from numeric codes. Limits ability to do colored terminal output, cursor positioning, etc.

**Resolution**: Builtin registry now recognizes `CHR` alias for `CHR$` with case-insensitive lookup. ANSI sequences like `CHR(27)` work.

**Related Missing Functions**: Likely ASC() (char to code) is also missing.

---

### BUG-045: STRING arrays not working (contradicts BUG-032/033/043 resolution)
**Status**: ✅ RESOLVED 2025-11-15
**Discovered**: 2025-11-15 during stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/test_06_arrays.bas

**Description**:
Despite BUG-032/033/043 being marked as resolved, STRING arrays do not work. Attempting to create an array with `AS STRING` type causes type mismatch errors.

**Example that fails**:
```basic
DIM items(5) AS STRING
items(0) = "Sword"
items(1) = "Shield"
```

**Error**:
```
error[B2001]: array element type mismatch: expected INT, got STRING
items(0) = "Sword"
^
```

**Observations**:
- INTEGER arrays work fine
- Object arrays work fine
- Only STRING arrays fail
- Error message suggests array is defaulting to INT type regardless of `AS STRING` declaration

**Impact**: Cannot create arrays of strings, which is a fundamental data structure for text-based applications.

**Resolution**: Semantic analyzer now honors `AS STRING` for arrays (not only `$` suffix). Assignments to string arrays compile and run.

**Note**: This contradicts the resolution status of BUG-032/033/043. Those bugs may have been tested with `DIM names$(5)` syntax (string type suffix) rather than `DIM names(5) AS STRING` syntax.

---

### BUG-046: Cannot call methods on array elements
**Status**: ✅ RESOLVED 2025-11-15
**Discovered**: 2025-11-15 during stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/test_07_object_arrays.bas

**Description**:
Cannot call methods on objects accessed via array indexing. Expression like `array(i).Method()` causes parser error.

**Example that fails**:
```basic
CLASS Item
    DIM name AS STRING
    SUB Init(n AS STRING)
        name = n
    END SUB
END CLASS

DIM inventory(3) AS Item
inventory(0) = NEW Item()
inventory(0).Init("Sword")  ' FAILS
```

**Error**:
```
error[B0001]: expected procedure call after identifier 'INVENTORY'
inventory(0).Init("Sword")
^^^^^^^^^
```

**Resolution**: Parser accepts `array(i).Method(...)` in statement context by parsing full expression before call classification.
```basic
DIM temp AS Item
temp = inventory(0)
temp.Init("Sword")
inventory(0) = temp
```

**Impact**: Makes OOP code with collections very verbose and awkward. Cannot use natural `collection(i).method()` syntax.

---

### BUG-047: IF/THEN/END IF inside class methods causes crash
**Status**: ✅ RESOLVED 2025-11-15
**Discovered**: 2025-11-15 during stress testing
**Test Cases**:
- /Users/stephen/git/viper/bugs/bug_testing/test_08_room_class.bas (empty block error)
- /Users/stephen/git/viper/bugs/bug_testing/test_09_if_in_sub.bas (std::length_error)
- /Users/stephen/git/viper/bugs/bug_testing/test_09_if_single_line.bas (segfault)

**Description**:
Using IF/THEN/END IF (or single-line IF/THEN) inside class methods causes compiler crashes or errors. IF statements work fine at module level but fail inside SUB/FUNCTION methods of classes.

**Example that crashes**:
```basic
CLASS Test
    DIM value AS INTEGER

    SUB CheckValue()
        IF value = 0 THEN
            PRINT "Value is zero"
        END IF
    END SUB
END CLASS
```

**Errors observed (prior to fix)**:
1. "empty block" error (test_08)
2. `libc++abi: terminating due to uncaught exception of type std::length_error: vector` (test_09)
3. Segmentation fault (test_09 single-line)

**Verified working**: IF/THEN/END IF works perfectly at module level (outside classes)

**Example that works**:
```basic
DIM x AS INTEGER
x = 5
IF x = 5 THEN
    PRINT "X is 5"
END IF
```

**Resolution**: Fixed OOP emission to avoid dangling pointers to exit blocks (re-resolve by index before branching/return). IF/THEN in methods now compiles and runs.

**Workaround**: None effective. Could potentially move logic outside class, but defeats purpose of OOP.

**Note**: This appears to be a code generation or IL lowering bug specific to the class method context.

---

### BUG-048: Cannot call module-level SUB/FUNCTION from within class methods
**Status**: ✅ RESOLVED 2025-11-15
**Discovered**: 2025-11-15 during stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/adventure_game.bas

**Description**:
Class methods cannot call module-level (global) SUB or FUNCTION procedures. Attempting to do so results in "unknown callee" error during IL generation.

**Example that fails**:
```basic
SUB PrintHeader(title AS STRING)
    PRINT "=== "; title; " ==="
END SUB

CLASS Room
    DIM name AS STRING

    SUB Describe()
        PrintHeader(name)  ' FAILS - cannot call module-level SUB
        PRINT "Room description here"
    END SUB
END CLASS
```

**Error (prior to fix)**:
```
error: ROOM.DESCRIBE:entry_ROOM.DESCRIBE: call %t6: unknown callee @printheader
```

**Resolution**: Procedure signatures/aliases are collected before OOP body emission so method calls resolve to correct global labels. Module SUB/FUNCTION calls now work from methods.

**Impact**: CRITICAL - Severely limits code organization and reusability. Cannot create utility functions that are shared between classes and module code. Makes OOP very awkward.

**Related**: This may be related to namespace/scope resolution in the IL lowering phase.

---

### BUG-049: RND() function signature incompatible with standard BASIC
**Status**: ⚠️ MINOR
**Discovered**: 2025-11-15 during stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/test_15_math.bas

**Description**:
The `RND()` function in Viper BASIC does not accept an argument, whereas standard BASIC implementations typically accept `RND(n)` where the argument controls random number generation behavior (e.g., reseed, return last value, etc.).

**Example**:
```basic
' Standard BASIC
x = RND(1)    ' FAILS in Viper BASIC

' Viper BASIC
x = RND()     ' Works - no argument
```

**Error**:
```
error[B0001]: expected ), got number
result = RND(1)
             ^
```

**Impact**: Minor - Can still generate random numbers, but cannot control RNG seeding or behavior.

**Workaround**: Use `RND()` without arguments.

---

### BUG-050: SELECT CASE with multiple values (CASE 1, 2, 3) causes IL generation error
**Status**: ✅ VERIFIED 2025-11-15 (not reproducible)
**Discovered**: 2025-11-15 during stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/comprehensive_test.bas

**Description**:
Using multiple comma-separated values in a single CASE statement causes an IL generation error with "expected 2 branch argument bundles, or none".

**Example that fails**:
```basic
SELECT CASE value
    CASE 1
        PRINT "One"
    CASE 4, 5        ' FAILS - multiple values
        PRINT "Four or Five"
END SELECT
```

**Error (originally observed)**:
```
error: main:do_head1: cbr %t363 label UL999999903 label do_body1: expected 2 branch argument bundles, or none
```

**Workaround**: Use separate CASE statements for each value:
```basic
SELECT CASE value
    CASE 4
        PRINT "Four"
    CASE 5
        PRINT "Five"
END SELECT
```

**Validation**: Multiple-case `CASE 1, 2` works in ad‑hoc and stress tests. No IL errors observed in current build.

---

### BUG-051: DO UNTIL loop causes IL generation error
**Status**: ✅ VERIFIED 2025-11-15 (not reproducible)
**Discovered**: 2025-11-15 during stress testing
**Test Case**: /Users/stephen/git/viper/bugs/bug_testing/comprehensive_test.bas

**Description**:
The `DO UNTIL` loop construct causes an IL generation error. Other DO loop variants (DO WHILE, DO...LOOP WHILE) work correctly.

**Example that fails**:
```basic
DIM i AS INTEGER
i = 0
DO UNTIL i > 3
    PRINT i
    i = i + 1
LOOP
```

**Error (originally observed)**:
```
error: main:do_head1: cbr %t363 label UL999999903 label do_body1: expected 2 branch argument bundles, or none
```

**Workaround**: Use `DO WHILE NOT (condition)` or `WHILE` loop instead:
```basic
' Instead of: DO UNTIL i > 3
DO WHILE NOT (i > 3)
    PRINT i
    i = i + 1
LOOP
```

**Validation**: DO UNTIL compiles and runs correctly in `bugs/bug_testing/test_12_do_loop.bas` and ad‑hoc samples.

**Note**: This appears to be the same IL generation error as BUG-050, possibly related to conditional branch handling.

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

### BUG-056: Arrays not allowed as class fields
**Status**: ❌ OUTSTANDING
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
