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
**Severity**: Low
**Status**: Confirmed
**Test Case**: test008.bas (initial version)

**Description**:
Calling a SUB or FUNCTION requires parentheses even when there are no arguments. The traditional BASIC syntax of calling without parentheses is not supported.

**Reproduction**:
```basic
SUB PrintHello
    PRINT "Hello"
END SUB

PrintHello  ' ERROR: expected '(' after procedure name
```

**Error Message**:
```
error[B0001]: expected '(' after procedure name 'PRINTHELLO' in procedure call statement
PrintHello
          ^
```

**Workaround**:
Always use parentheses:
```basic
SUB PrintHello()
    PRINT "Hello"
END SUB

PrintHello()  ' WORKS
```

**Analysis**:
The parser requires explicit parentheses for all procedure calls. This is more similar to modern language conventions but differs from traditional BASIC where parentheses were optional for zero-argument calls.

---

### BUG-005: SGN function not implemented
**Severity**: Low
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

**Description**:
The SGN (sign) function, which returns -1, 0, or 1 depending on whether a number is negative, zero, or positive, is not implemented.

**Reproduction**:
```basic
PRINT SGN(-10)
PRINT SGN(0)
PRINT SGN(10)
```

**Error Message**:
```
error[B1006]: unknown procedure 'SGN'
PRINT SGN(-10)
      ^^^
```

**Workaround**:
Implement SGN manually using IF statements:
```basic
FUNCTION SGN(x)
    IF x < 0 THEN
        RETURN -1
    ELSEIF x = 0 THEN
        RETURN 0
    ELSE
        RETURN 1
    END IF
END FUNCTION
```

**Analysis**:
The SGN intrinsic function is not registered in the builtin registry. This is a standard BASIC function that should be implemented.

---

### BUG-006: Limited trigonometric/math functions
**Severity**: Medium
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

**Description**:
Several standard BASIC math functions are not implemented: TAN, ATN (arctangent), EXP (exponential), LOG (natural logarithm).

**Reproduction**:
```basic
PRINT TAN(0)
PRINT ATN(1)
PRINT EXP(1)
PRINT LOG(2.718281828)
```

**Error Message**:
```
error[B1006]: unknown procedure 'TAN'
error[B1006]: unknown procedure 'ATN'
error[B1006]: unknown procedure 'EXP'
error[B1006]: unknown procedure 'LOG'
```

**Workaround**:
None available without implementing these functions.

**Analysis**:
Only SIN and COS are implemented. The builtin registry is missing TAN, ATN, EXP, and LOG functions which are standard in most BASIC implementations.

---

### BUG-007: Multi-dimensional arrays not supported
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
**Severity**: Low
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

**Description**:
The `REDIM PRESERVE` syntax for resizing arrays while preserving contents is not recognized by the parser.

**Reproduction**:
```basic
DIM arr(5)
arr(0) = 100
REDIM PRESERVE arr(10)
```

**Error Message**:
```
error[B0001]: expected (, got ident
REDIM PRESERVE arr(10)
               ^
```

**Workaround**:
Use REDIM without PRESERVE - testing shows that REDIM already preserves array contents by default:
```basic
DIM arr(5)
arr(0) = 100
REDIM arr(10)  ' Contents are preserved
PRINT arr(0)   ' Outputs: 100
```

**Analysis**:
The parser does not recognize PRESERVE as a keyword after REDIM. Interestingly, the runtime behavior of REDIM already preserves array contents, so PRESERVE appears to be the default behavior. This is actually beneficial but differs from traditional BASIC where REDIM without PRESERVE would clear the array.

---

### BUG-009: CONST keyword not implemented
**Severity**: Medium
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details
**Test Case**: test033.bas

**Description**:
The CONST keyword for declaring named constants is not implemented.

**Reproduction**:
```basic
CONST PI = 3.14159
PRINT PI
```

**Error Message**:
```
error[B0001]: unknown statement 'CONST'; expected keyword or procedure call
CONST PI = 3.14159
^^^^^
```

**Workaround**:
Use regular variables (though they can be modified):
```basic
PI = 3.14159
PRINT PI
```

**Analysis**:
The CONST keyword is not recognized in the parser. This prevents defining true constants that cannot be modified after declaration. The semantic analyzer would need to track constant status and prevent reassignment.

---

### BUG-010: STATIC keyword not implemented
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
**Severity**: Low
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

**Description**:
The SWAP statement for exchanging values of two variables is not implemented.

**Reproduction**:
```basic
x = 10
y = 20
SWAP x, y
```

**Error Message**:
```
error[B0001]: unknown statement 'SWAP'; expected keyword or procedure call
SWAP x, y
^^^^
```

**Workaround**:
Use a temporary variable:
```basic
temp = x
x = y
y = temp
```

**Analysis**:
The SWAP keyword is not recognized in the parser. This is a convenience feature that could be easily lowered to use a temporary variable during code generation.

---

### BUG-012: BOOLEAN type incompatible with TRUE/FALSE and integer comparisons
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
**Severity**: Medium
**Status**: Confirmed
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
The type inference for CONST statements appears to default to INTEGER type when no explicit type suffix or AS clause is provided. Float literals are then converted to integers during assignment. This is related to BUG-022 (general float literal type inference issue). The CONST implementation in Parser.cpp and SemanticAnalyzer.cpp uses `typeFromSuffix()` which returns I64 by default. There's no mechanism to infer float type from the initializer expression.

**Impact**:
Cannot define accurate mathematical constants. All calculations using these constants will be incorrect. This severely limits the usefulness of CONST for scientific computing.

---

### BUG-020: String constants cause runtime error
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
**Severity**: Low
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

**Description**:
SELECT CASE labels cannot use negative integer literals like `-1`. The parser treats the minus sign as a separate token rather than part of the literal, making it impossible to use SELECT CASE with functions like SGN() that return negative values.

**Reproduction**:
```basic
x = -10
sign = SGN(x)

SELECT CASE sign
    CASE -1       ' ERROR: parser doesn't accept this
        PRINT "Negative"
    CASE 0
        PRINT "Zero"
    CASE 1
        PRINT "Positive"
END SELECT
```

**Error Message**:
```
error[B0001]: SELECT CASE labels must be integer literals
        CASE -1
             ^
error[B0001]: expected eol, got -
        CASE -1
             ^
error[ERR_Case_EmptyLabelList]: CASE arm requires at least one label
        CASE -1
        ^^^^
```

**Workaround**:
Use IF/ELSEIF statements instead of SELECT CASE for negative values:
```basic
IF sign < 0 THEN
    PRINT "Negative"
ELSEIF sign = 0 THEN
    PRINT "Zero"
ELSEIF sign > 0 THEN
    PRINT "Positive"
END IF
```

Or use positive values only with SELECT CASE:
```basic
absVal = ABS(value)
SELECT CASE absVal
    CASE 0
        PRINT "Zero"
    CASE 5
        PRINT "Five"
    CASE 100
        PRINT "One hundred"
END SELECT
```

**Analysis**:
The parser expects CASE labels to be positive integer literals only. The minus sign is parsed as a separate operator token rather than being part of the integer literal. This is a limitation in Parser_Stmt_Core.cpp in the SELECT CASE parsing logic. The parser would need to be modified to accept unary minus expressions in CASE labels, or to recognize negative integer literals as a special token type.

**Impact**:
Limits the usefulness of SELECT CASE with functions like SGN() that naturally return negative values. Forces use of IF/ELSEIF chains which are more verbose.

---

### BUG-022: Float literals without explicit type default to INTEGER
**Severity**: Medium
**Status**: Confirmed
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
**Severity**: Low
**Status**: Confirmed
**Test Case**: test_float_literals.bas
**Discovered**: 2025-11-12 during comprehensive testing

**Description**:
The syntax `DIM variable = value` for declaring and initializing a variable in one statement is not supported. DIM only accepts type declarations, not initializers.

**Reproduction**:
```basic
DIM pi = 3.14159  ' ERROR: unknown statement
DIM count = 0     ' ERROR: unknown statement
DIM name$ = "Alice"  ' ERROR: unknown statement
```

**Error Message**:
```
error[B0001]: unknown statement '='; expected keyword or procedure call
DIM pi = 3.14159
       ^
```

**Workaround**:
Declare then assign on separate lines:
```basic
DIM pi
pi = 3.14159

DIM count
count = 0

DIM name$ AS STRING
name$ = "Alice"
```

**Analysis**:
This is a syntax limitation in Parser_Stmt_Core.cpp. The DIM statement parser only expects:
- `DIM name` (implicit type)
- `DIM name AS type` (explicit type)
- `DIM name$` (type suffix)
- `DIM name(size)` (array)

Some BASIC dialects (like Visual Basic) support `DIM x = value` for combined declaration and initialization, but Viper BASIC requires separate declaration and assignment statements. This is a parser limitation, not a semantic issue.

**Impact**:
Requires two lines instead of one for variable initialization. Minor inconvenience but not a blocking issue. More verbose code.

---

### BUG-024: CONST with type suffix causes assertion failure
**Severity**: High
**Status**: ✅ RESOLVED 2025-11-12 - See basic_resolved.md for details

**Description**:
Using type suffixes (%, !, #) with CONST declarations causes an assertion failure in the lowering code. The compiler crashes with an internal assertion error.

**Reproduction**:
```basic
CONST MAX% = 100
CONST PI! = 3.14159
CONST E# = 2.71828
```

**Error Message**:
```
Assertion failed: (storage && "CONST target should have storage"), function lowerConst, file LowerStmt_Runtime.cpp, line 358.
```

**Workaround**:
Don't use type suffixes with CONST, use AS clause or no type annotation:
```basic
CONST MAX = 100  ' Works (defaults to INTEGER)
CONST PI = 3     ' Works but truncates
```

For float constants, use regular variables with type suffixes:
```basic
PI! = 3.14159   ' Use variable instead of CONST
E# = 2.71828
```

**Analysis**:
The lowering code for CONST in `LowerStmt_Runtime.cpp:358` expects every CONST to have allocated storage, but CONST declarations with type suffixes don't get storage allocated properly. This is a code generation bug where the storage allocation phase doesn't handle type suffixes on CONST declarations. The assertion failure suggests the code path for CONST with suffixes is incomplete.

**Impact**:
Cannot use CONST for typed constants. Must use regular variables for any constants that need explicit types. Combined with BUG-019 (float truncation), this makes CONST nearly useless for mathematical constants.

**Related Bugs**: BUG-019 (float truncation in CONST), BUG-020 (string CONST runtime error)

---

### BUG-025: EXP of large values causes overflow trap
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

