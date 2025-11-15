# VIPER BASIC Known Bugs and Issues

*Last Updated: 2025-11-14*
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

**Bug Statistics**: 40 resolved, 3 outstanding, 0 partially resolved (43 total documented)

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
- ⚠️ **BUG-039 DIAGNOSIS CORRECTED**: Issue is not about mutation - cannot assign ANY method result to a variable (works inline only)

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
**Difficulty**: 🔴 HARD - OOP method invocation
**Severity**: HIGH
**Status**: Outstanding
**Discovered**: 2025-11-13 during BasicDB development
**Test Case**: /devdocs/basic/basicdb.bas version 0.2

**Description**:
When calling SUB methods on class instances, the compiler reports "unknown procedure" errors. FUNCTION methods work correctly, but SUB methods fail completely.

**Reproduction**:
```basic
CLASS Database
  SUB IncrementCount()
    LET Me.count = Me.count + 1
  END SUB
END CLASS

DIM db AS Database
db = NEW Database()
db.IncrementCount()   ' ERROR
```

**Error Message**:
```
error[B1006]: unknown procedure 'db.incrementcount'
```

**Workaround**:
Convert all SUB methods to FUNCTION methods that return a dummy value (e.g., INTEGER returning 0).

**Impact**:
- Cannot use SUB methods on class instances
- All mutation methods must be FUNCTIONs
- Reduces code clarity (mutation methods shouldn't return values)

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
**Difficulty**: 🔴 HARD - IL lowering type mismatch
**Severity**: CRITICAL
**Status**: Outstanding
**Discovered**: 2025-11-13 during BasicDB development
**Diagnosis Corrected**: 2025-11-14
**Test Cases**: /tmp/test_bug039_readonly_method.bas, /tmp/test_bug039_inline.bas

**Description**:
Method call results cannot be assigned to variables - causes "call arg type mismatch" IL errors. However, method calls work perfectly when used directly inline in expressions (PRINT, concatenation, etc.).

**IMPORTANT**: This has **nothing to do with mutation** - even read-only methods fail when assigned to variables.

**Fails** (variable assignment):
```basic
CLASS Counter
  count AS INTEGER

  FUNCTION GetCount() AS INTEGER
    RETURN Me.count
  END FUNCTION
END CLASS

DIM c AS Counter
c = NEW Counter()
c.count = 5
DIM result AS INTEGER
result = c.GetCount()  ' ❌ ERROR: call arg type mismatch
```

**Works** (inline usage):
```basic
DIM c AS Counter
c = NEW Counter()
c.count = 5
PRINT "Count: "; c.GetCount()  ' ✅ Works perfectly!
```

**Error Message**:
```
error: main:obj_assign_cont1: call %t8: call arg type mismatch
```

**Workaround**:
Use method calls only inline in expressions (PRINT, string concatenation, arithmetic). Cannot store results in variables.

**Impact**:
- **CRITICAL**: Cannot store method results for reuse
- Severely limits method utility
- Forces awkward inline-only patterns
- Makes complex calculations with method results impossible

**Root Cause** (suspected):
IL lowering issue in `/src/frontends/basic/Lower_OOP_Expr.cpp` - method return values not properly bridged to variable assignment context.

---

### BUG-040: Cannot use custom class types as function return types
**Difficulty**: 🔴 HARD - Type system extension
**Severity**: HIGH
**Status**: Outstanding
**Discovered**: 2025-11-13 during BasicDB development
**Test Case**: /devdocs/basic/basicdb.bas version 0.3

**Description**:
Functions can only return built-in types (INTEGER, STRING, etc.). Cannot declare a function that returns a custom CLASS type.

**Reproduction**:
```basic
CLASS Record
  id AS INTEGER
  name AS STRING
END CLASS

FUNCTION DB_GetRecordById(id AS INTEGER) AS Record  ' ERROR
  DIM rec AS Record
  rec = NEW Record(id, "Test", "test@example.com", 25)
  RETURN rec
END FUNCTION
```

**Error Message**:
```
error[B0001]: unknown statement 'RECORD'; expected keyword or procedure call
```

**Workaround**:
Return an index into an array instead of the object itself:
```basic
FUNCTION DB_FindRecordIndexById(id AS INTEGER) AS INTEGER
  ' Return index, then caller uses: DB_RECORDS(index)
  RETURN index
END FUNCTION
```

**Impact**:
- Cannot create factory functions or getters that return objects
- Must use array indexing directly
- Limits abstraction capabilities
- Makes APIs less clean and more error-prone

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
