# VIPER BASIC Known Bugs and Issues

*Last Updated: 2025-11-13*
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

**Boolean Type System Changes**: Modified `isBooleanType()` to only accept `Type::Bool` (not `Type::Int`) for logical operators (AND/ANDALSO/OR/ORELSE). This makes the type system stricter and fixes some test cases.

**Bug Statistics**: 27 resolved, 9 outstanding, 2 partially resolved (36 total documented)

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
**Difficulty**: 🔴 HARD not implemented
**Severity**: Low
**Status**: Confirmed
**Test Case**: test034.bas

**Description**:
The STATIC keyword for declaring persistent local variables in procedures is not implemented.

**Reproduction**:
```basic
SUB Counter()
    STATIC count
    count = count + 1
    PRINT count
END SUB
```

**Error Message**:
```
error[B0001]: unknown statement 'STATIC'; expected keyword or procedure call
    STATIC count
    ^^^^^^
```

**Workaround**:
Use global variables or pass state as parameters.

**Analysis**:
The STATIC keyword is not recognized in the parser. Static local variables maintain their values between procedure calls, which is useful for maintaining state. This would require changes to the parser, semantic analyzer, and code generator to allocate static storage for these variables.

---

### BUG-011: SWAP statement not implemented
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-012: BOOLEAN type
**Difficulty**: 🔴 HARD incompatible with TRUE/FALSE and integer comparisons
**Severity**: Medium
**Status**: Confirmed
**Test Case**: test042.bas (extended version), test037.bas

**Description**:
Variables declared with `DIM x AS BOOLEAN` cannot be compared with TRUE, FALSE constants or integer values. This makes the BOOLEAN type impractical to use. Additionally, functions like EOF() that logically return boolean values actually return INT, requiring comparisons like `EOF(#1) = 0` instead of the more natural `NOT EOF(#1)`.

**Reproduction**:
```basic
DIM flag AS BOOLEAN
flag = TRUE
IF flag = FALSE THEN    ' ERROR: operand type mismatch
    PRINT "False"
END IF
```

**Error Message**:
```
error[B2001]: operand type mismatch
IF flag = FALSE THEN
        ^
```

**Additional Issue - EOF returns INT not BOOLEAN**:
```basic
DO WHILE NOT EOF(#1)  ' ERROR: NOT requires BOOLEAN operand, got INT
    LINE INPUT #1, line$
LOOP
```

**Error Message**:
```
error[E1003]: NOT requires a BOOLEAN operand, got INT.
DO WHILE NOT EOF(#1)
         ^^^
```

**Workaround**:
1. Don't use BOOLEAN type - use INTEGER and TRUE/FALSE constants:
```basic
flag = TRUE          ' flag is INTEGER
IF flag THEN
    PRINT "True"
END IF
```

2. For EOF, use integer comparison:
```basic
DO WHILE EOF(#1) = 0
    LINE INPUT #1, line$
LOOP
```

**Analysis**:
There's a type system inconsistency where:
1. BOOLEAN is a distinct type incompatible with INT
2. TRUE/FALSE are INT constants (-1 and 0)
3. Logical functions (EOF, etc.) return INT rather than BOOLEAN
4. IF statements accept both INT and BOOLEAN in conditions

This makes BOOLEAN type unusable in practice. The type system needs to either:
- Allow implicit conversion between BOOLEAN and INT
- Make TRUE/FALSE actual BOOLEAN constants
- Make EOF() and similar functions return BOOLEAN
- Or eliminate BOOLEAN type entirely and use INT for all boolean operations (traditional BASIC approach)

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
**Difficulty**: 🔴 HARD (requires OOP implementation)
**Severity**: Critical
**Status**: Confirmed
**Test Case**: db_oop.bas (v2.0)

**Description**:
Attempting to access global string variables from within class methods causes a segmentation fault crash.

**Reproduction**:
```basic
DIM globalString$ AS STRING
globalString$ = "Hello"

CLASS Test
    SUB UseGlobal()
        PRINT globalString$  ' Segfault!
    END SUB
END CLASS

DIM t AS Test
t = NEW Test()
t.UseGlobal()
```

**Error Message**:
```
Exit code 139
```

**Workaround**:
Do not access global strings from methods. Pass them as parameters instead.

**Analysis**:
Exit code 139 indicates a segmentation fault. The generated code for accessing global strings from within class methods has an invalid memory access, likely due to incorrect scope resolution or missing initialization in the object context.

---

### BUG-018: FUNCTION methods in classes cause code generation error
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

---

### BUG-019: Float literals assigned to CONST are truncated to integers
**Difficulty**: 🔴 HARD (module-level type inference)
**Severity**: Medium
**Status**: Partial - BUG-022 (procedure-level) fixed, module-level remains
**Test Case**: test_const_simple.bas, test_scientific_calc.bas
**Discovered**: 2025-11-12 during comprehensive testing

**Description**:
When a float literal is assigned to a CONST declaration, the value is truncated to an integer rather than preserved as a float. This makes CONST unusable for mathematical constants like PI or E.

**Reproduction**:
```basic
CONST PI = 3.14159
PRINT PI  ' Output: 3 (expected 3.14159)

CONST E = 2.71828
PRINT E   ' Output: 3 (expected 2.71828)

CONST HALF = 0.5
PRINT HALF  ' Output: 0 (expected 0.5)
```

**Error Message**:
None (compiles and runs, but with wrong value)

**Workaround**:
Use regular variables with type suffixes instead of CONST:
```basic
REM Instead of CONST PI = 3.14159 (which truncates to 3)
PI! = 3.14159   ' Works correctly - type suffix ! for FLOAT
PRINT PI!       ' Output: 3.14159

E# = 2.71828    ' Works correctly - type suffix # for DOUBLE
PRINT E#        ' Output: 2.71828
```

Note: Cannot use type suffixes directly on CONST due to BUG-024.

**Analysis**:
BUG-022 fixed float literal type inference for regular LET assignments within procedures. However, CONST declarations are evaluated at module level before the semantic analyzer's type inference runs. The lowering phase queries the semantic analyzer but module-level symbols haven't been analyzed yet. Need to either: (1) run semantic analysis on module-level declarations before lowering, or (2) add special handling for CONST in the lowering phase to infer from initializer type.

This is a deeper architectural issue than initially assessed - upgrading difficulty to HARD.

**Impact**:
Cannot define accurate mathematical constants at module level. Regular variables with float literals work fine (BUG-022 resolved). This limits the usefulness of CONST for scientific computing, but workaround exists using regular variables.

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

### BUG-030: SUBs and FUNCTIONs
**Difficulty**: 🔴 HARD cannot access global variables
**Severity**: Critical
**Status**: Confirmed
**Test Case**: dungeon_quest_v4.bas (multiple locations)
**Discovered**: 2025-11-12

**Description**:
Variables declared at module level (global scope) are not accessible inside SUB or FUNCTION procedures. Each SUB/FUNCTION has its own isolated scope and cannot read or write global variables. This makes it impossible to create properly modular programs that share state.

**Reproduction**:
```basic
DIM globalVar% AS INTEGER
globalVar% = 42

SUB TestSub()
    PRINT globalVar%  ' ERROR: unknown variable 'GLOBALVAR%'
END SUB

TestSub()
```

**Error Message**:
```
error[B1001]: unknown variable 'GLOBALVAR%'
```

**Analysis**:
This appears to be a fundamental scoping issue in the BASIC frontend. Traditional BASIC allows procedures to access module-level variables. The current implementation treats each SUB/FUNCTION as completely isolated, which severely limits their usefulness.

**Impact**:
**CRITICAL**: Cannot write modular programs with SUBs and FUNCTIONs that share state. This essentially makes SUB/FUNCTION unusable for any non-trivial program. The only workaround is to pass every variable as a parameter (not possible for arrays) or use GOSUB instead (which has its own limitations per BUG-026).

**Workaround**:
Use GOSUB/RETURN instead of SUB/FUNCTION, which uses the same scope as the main program. However, this conflicts with BUG-026 (DO WHILE + GOSUB).

**CRITICAL NOTE**: The combination of BUG-026 and BUG-030 creates a "modularity crisis":
- Cannot use DO WHILE + GOSUB (BUG-026)
- Cannot use SUB/FUNCTION to access globals (BUG-030)
- Large inline FOR loops trigger IL verifier issues (IL-BUG-001)
- Result: NO viable way to write modular, complex programs in current VIPER BASIC

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
**Status**: CRITICAL BLOCKER
**Severity**: CRITICAL
**Found**: During tic-tac-toe board state implementation
**Test Case**: tictactoe.bas (attempted v0.2), dungeon_quest_v4.bas

**Description**:
Global variables declared with DIM at module level cannot be accessed or modified from within SUB or FUNCTION:
```basic
DIM board$

SUB InitBoard()
    board$ = "ABCDEFGHI"  ' Assignment has no effect
END SUB

InitBoard()
PRINT board$  ' Prints empty string
```

**Impact**: Cannot use global state for game board or any shared data between procedures. Makes any non-trivial program structure impossible.

**Workaround**: None - this blocks all structured programming requiring shared state. Would need to pass all state as parameters, but BASIC doesn't support passing user-defined types or arrays properly.

**Analysis**: This is a duplicate of BUG-030, discovered independently during tic-tac-toe development. The workaround used was to write the entire game as a monolithic program using GOTO for control flow instead of SUBs/FUNCTIONs.

---

### BUG-036: String comparison in OR condition causes IL error
**Status**: ✅ RESOLVED 2025-11-13 - See basic_resolved.md for details

---
