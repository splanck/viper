# VIPER BASIC Known Bugs and Issues

*Last Updated: 2025-11-12*
*Source: Empirical testing during language audit*

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

### BUG-007: Multi-dimensional
**Difficulty**: 🔴 HARD arrays not supported
**Severity**: High
**Status**: Confirmed
**Test Case**: test027.bas

**Description**:
Multi-dimensional arrays (e.g., `DIM matrix(3, 3)`) are not supported by the parser or runtime.

**Reproduction**:
```basic
DIM matrix(3, 3)
matrix(0, 0) = 1
```

**Error Message**:
```
error[B0001]: expected ), got ,
DIM matrix(3, 3)
            ^
```

**Workaround**:
Use one-dimensional arrays and manually calculate indices:
```basic
DIM matrix(15)  ' For a 4x4 matrix
matrix(0 * 4 + 0) = 1  ' matrix(0, 0)
matrix(0 * 4 + 1) = 2  ' matrix(0, 1)
```

**Analysis**:
The parser does not support comma-separated dimensions in DIM statements or array subscripts. This is a significant limitation as multi-dimensional arrays are fundamental to many BASIC programs. The grammar needs to be extended to support multiple dimensions.

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

### BUG-013: SHARED keyword
**Difficulty**: 🔴 HARD not supported
**Severity**: High
**Status**: Confirmed
**Test Case**: database.bas v0.3 (attempted)

**Description**:
The SHARED keyword for accessing global variables from within SUB/FUNCTION procedures is not implemented. This makes it impossible for procedures to access module-level state without passing everything as parameters.

**Reproduction**:
```basic
DIM count AS INTEGER
count = 0

SUB Increment()
    SHARED count
    count = count + 1
END SUB
```

**Error Message**:
```
error[B0001]: unknown statement 'SHARED'; expected keyword or procedure call
    SHARED count
    ^^^^^^
```

**Workaround**:
Pass all needed variables as parameters, though this doesn't work well for arrays or when you need to modify global state.

**Analysis**:
Without SHARED, procedures are completely isolated from module-level variables. This severely limits the ability to write structured programs with encapsulated state. Traditional BASIC dialects support SHARED for this purpose.

---

### BUG-014: String arrays not supported
**Difficulty**: 🔴 HARD (duplicate of BUG-032)
**Severity**: Critical
**Status**: Confirmed
**Test Case**: test_array_string.bas, database.bas

**Description**:
Arrays of strings cannot be created or used. Both `DIM arr$(5)` and `DIM arr(5) AS STRING` syntax compile, but attempting to assign string values to array elements always produces "array element type mismatch" error.

**Reproduction**:
```basic
DIM arr$(5)
arr$(0) = "Hello"    ' ERROR: array element type mismatch
```

Also fails with explicit AS STRING:
```basic
DIM arr(5) AS STRING
arr(0) = "Hello"     ' ERROR: array element type mismatch
```

**Error Message**:
```
error[B2001]: array element type mismatch
arr$(0) = "Hello"
^
```

**Workaround**:
No direct workaround. Programs requiring collections of strings must use file I/O or other approaches. Arrays only work with numeric types (INTEGER).

**Analysis**:
This is a critical limitation that prevents building many types of practical programs. A contact database, word list, menu system, or any program that needs to store multiple strings cannot use arrays. The runtime array support appears to only handle numeric types. This was not discovered in initial testing because test007.bas only tested integer arrays.

---


### BUG-015: String properties in classes cause runtime error
**Difficulty**: 🔴 HARD (requires OOP implementation)
**Severity**: Critical
**Status**: Confirmed
**Test Case**: db_oop.bas (early versions), test_oop_string_param.bas

**Description**:
String properties can be declared in classes but attempting to assign or access them causes a runtime error about a missing runtime function.

**Reproduction**:
```basic
CLASS Contact
    DIM name$ AS STRING
    DIM phone$ AS STRING
END CLASS

DIM c AS Contact
c = NEW Contact()
c.name$ = "Alice Smith"  ' Runtime error!
```

**Error Message**:
```
error: main:obj_assign_cont: call %t12: unknown callee @rt_str_retain_maybe
```

**Workaround**:
Only use integer properties in classes. Pass strings as method parameters instead of storing them as properties.

**Analysis**:
The code generator is missing a critical runtime function for string reference counting in object contexts. This makes string properties completely unusable. The missing function `@rt_str_retain_maybe` suggests the runtime doesn't support string lifecycle management for class members.

---

### BUG-016: Local string variables in methods cause compilation error
**Difficulty**: 🔴 HARD (requires OOP implementation)
**Severity**: Critical
**Status**: Confirmed
**Test Case**: db_oop.bas (v1.0), test_oop_string_param.bas

**Description**:
Declaring local string variables inside class methods causes a compilation error during code generation.

**Reproduction**:
```basic
CLASS Database
    SUB ListAll()
        DIM line$ AS STRING
        PRINT line$
    END SUB
END CLASS
```

**Error Message**:
```
error: CONTACTDATABASE.INITIALIZE:ret_CONTACTDATABASE.INITIALIZE: empty block
```

**Workaround**:
Do not declare local string variables in methods. Pass all needed strings as parameters.

**Analysis**:
The code generator fails to properly handle local string variable declarations within class methods, resulting in an "empty block" error. This severely limits what can be done inside methods - no file reading with LINE INPUT, no string processing, etc.

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
**Difficulty**: 🔴 HARD (requires OOP implementation)
**Severity**: High
**Status**: Confirmed
**Test Case**: db_oop.bas (attempted FUNCTION GetCount())

**Description**:
Defining a FUNCTION (method with return value) inside a class causes a code generation error.

**Reproduction**:
```basic
CLASS Test
    FUNCTION GetValue() AS INTEGER
        RETURN 42
    END FUNCTION
END CLASS
```

**Error Message**:
```
error: main: unknown label bb_0
```

**Workaround**:
Only use SUB methods (no return value) in classes. To return values, modify object properties and read them afterward.

**Analysis**:
The code generator produces invalid IL with incorrect block labels when generating code for FUNCTION methods within classes. This suggests that class method code generation doesn't properly handle the FUNCTION return mechanism.

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
**Difficulty**: 🔴 HARD (runtime string lifecycle management)
**Severity**: High
**Status**: Confirmed
**Test Case**: test_const.bas
**Discovered**: 2025-11-12 during comprehensive testing

**Description**:
String constants compile successfully but cause a runtime error when the program executes. The code generator is missing a runtime function for string lifecycle management in constant contexts.

**Reproduction**:
```basic
CONST MSG$ = "Hello"
PRINT MSG$
```

**Error Message**:
```
/tmp/test_const.bas:4:1: error: main:entry: call %t11: unknown callee @rt_str_release_maybe
```

**Workaround**:
Don't use string constants; use regular string variables instead:
```basic
DIM MSG$ AS STRING
MSG$ = "Hello"
PRINT MSG$  ' Works fine
```

**Analysis**:
The code generator is missing the runtime function `@rt_str_release_maybe` needed for string lifecycle management in constant contexts. This is similar to BUG-015 (string properties in classes causing missing `@rt_str_retain_maybe`), suggesting incomplete string reference counting support throughout the codebase. The lowering code for CONST likely needs special handling for string types to manage reference counting properly.

**Related Bugs**: BUG-015 (string properties in classes), BUG-016 (local strings in methods)

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

### BUG-025: EXP of large
**Difficulty**: 🔴 HARD values causes overflow trap
**Severity**: Low
**Status**: Confirmed
**Test Case**: test_stress_arrays_loops.bas (first version)
**Discovered**: 2025-11-12 during stress testing

**Description**:
Computing EXP of values greater than approximately 50 causes a floating-point overflow trap that terminates the program.

**Reproduction**:
```basic
x% = 100
result# = EXP(x%)  ' Trap: Overflow (code=0): fp overflow in cast.fp_to_si.rte.chk
```

**Error Message**:
```
Trap @main#7 line X: Overflow (code=0): fp overflow in cast.fp_to_si.rte.chk
```

**Workaround**:
Check the input value before calling EXP:
```basic
IF x% <= 50 THEN
    result# = EXP(x%)
ELSE
    PRINT "Value too large for EXP"
END IF
```

**Analysis**:
The exponential function grows extremely fast. EXP(50) ≈ 5.2e21, and EXP(100) would be approximately 2.7e43, which exceeds the range of double-precision floating point. The overflow occurs when the runtime tries to convert or cast the result. This is mathematically expected behavior, but the error handling could be more graceful (return infinity or NaN instead of trap).

**Impact**:
Low severity because this is expected mathematical behavior. Large exponentials are uncommon in typical programs. Programs that need to handle large exponentials should validate inputs.

---


### BUG-026: DO WHILE loops
**Difficulty**: 🟡 MEDIUM with GOSUB cause "empty block" error
**Severity**: High
**Status**: Confirmed
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
The code generator for DO WHILE loops may be checking if the loop body generated any IL code. GOSUB statements might not count as "content" for this check, or there's an issue with how GOSUB is lowered within DO WHILE loops specifically.

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

### BUG-032: String arrays
**Difficulty**: 🔴 HARD not supported
**Severity**: High
**Status**: Confirmed
**Test Case**: test_strings_comprehensive.bas
**Discovered**: 2025-11-12

**Description**:
Arrays of strings cannot be created or used. DIM names$(5) compiles, but any assignment to array elements fails with "array element type mismatch".

**Reproduction**:
```basic
DIM names$(5)
names$(0) = "Alice"  ' ERROR: array element type mismatch
```

**Error Message**:
```
error[B2001]: array element type mismatch
names$(0) = "Alice"
^
```

**Analysis**:
The runtime only provides integer array functions (@rt_arr_i32_*). There appear to be no string array functions in the runtime, so the BASIC frontend cannot lower string arrays to IL.

**Impact**:
Cannot create collections of strings. This severely limits data structure possibilities. Common patterns like lists of names, menu options, command tables, etc. are impossible.

**Workaround**:
None known. Could potentially use multiple individual string variables, but this doesn't scale.

---

**Investigation Notes (2025-11-12)**:
Attempted fix revealed this is actually TWO separate bugs:

1. **Empty Block Issue**: The DO WHILE/WHILE loop done blocks are empty when only GOSUB is in the body
   - Root cause: GOSUB creates complex control flow that doesn't emit into the current block
   - The loop's done block ends up with no instructions, causing IL verifier error
   - Attempted fix: Add continuation blocks when done block is empty
   - Result: Fixes empty block error BUT breaks golden tests and reveals bug #2

2. **GOSUB Return Continuation Bug**: `gosub_ret_cont` block missing terminator
   - When GOSUB returns from within a loop, the return continuation block is unterminated
   - This appears to be a separate GOSUB lowering issue, not loop-specific
   - Affects both DO WHILE and WHILE loops with GOSUB
   - The workaround mentioned in the bug report does NOT work - it still fails

**Status**: Requires deeper investigation of GOSUB lowering and interaction with loop control flow. Marking as HARD.

