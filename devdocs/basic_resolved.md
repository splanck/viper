# VIPER BASIC Resolved Bugs

*Last Updated: 2025-11-12*
*Source: Bug fixes during language improvement*

---

## RESOLVED BUGS

### BUG-001: String concatenation requires $ suffix for type inference
**Severity**: Medium
**Status**: ✅ RESOLVED
**Test Case**: test010.bas (initial version), /tmp/test_bug001.bas
**Resolution Date**: 2025-11-12

**Original Issue**:
String variables without the `$` suffix could not be concatenated using the `+` operator. The compiler reported "operand type mismatch" error.

**Reproduction**:
```basic
s1 = "Hello"
s2 = "World"
s3 = s1 + " " + s2  ' ERROR: operand type mismatch
PRINT s3
```

**Error Message**:
```
error[B2001]: operand type mismatch
s3 = s1 + " " + s2
        ^
```

**Root Cause Analysis**:
The type inference system operated in two phases:
1. **Semantic Analysis Phase**: Tracked variable types in `SemanticAnalyzer::varTypes_` map
2. **Lowering Phase**: Re-inferred types from variable name suffixes only

The lowering phase (`Lowerer::markSymbolReferenced` and `Scan_RuntimeNeeds.cpp`) used only suffix-based type inference via `inferAstTypeFromName()`, completely ignoring the semantic analyzer's value-based type determinations. This caused variables without suffixes to always be allocated as `i64` regardless of their assigned values.

**Solution Implemented**:
Enhanced the compiler to pass semantic type information from the analyzer to the lowerer:

1. **Modified `SemanticAnalyzer.Stmts.Runtime.cpp`** (lines 83-113):
   - Pre-evaluate RHS expressions before resolving variables
   - Pre-populate `varTypes_` map for new variables based on RHS type
   - Enables value-based type inference for suffix-free variables

2. **Modified `Lowerer` interface**:
   - Added `setSemanticAnalyzer()` and `semanticAnalyzer()` methods
   - Stores reference to semantic analyzer for type lookups during lowering

3. **Modified `BasicCompiler.cpp`** (line 123):
   - Connects semantic analyzer to lowerer via `lower.setSemanticAnalyzer(&sema)`

4. **Modified `Scan_RuntimeNeeds.cpp`**:
   - Created `inferVariableType()` helper that queries semantic analyzer first
   - Updated all type inference points to use semantic information before falling back to suffix-based inference

5. **Modified `Lowerer.Procedure.cpp`**:
   - Created `inferVariableTypeForLowering()` static helper
   - Updated `markSymbolReferenced()` to use semantic type information

**Files Changed**:
- `src/frontends/basic/SemanticAnalyzer.Stmts.Runtime.cpp`
- `src/frontends/basic/Lowerer.hpp`
- `src/frontends/basic/lower/Lowerer_Errors.cpp`
- `src/frontends/basic/BasicCompiler.cpp`
- `src/frontends/basic/lower/Scan_RuntimeNeeds.cpp`
- `src/frontends/basic/Lowerer.Procedure.cpp`

**Test Results**:
✅ Test case now works correctly:
```basic
s1 = "Hello"
s2 = "World"
s3 = s1 + " " + s2
PRINT s3
```
Output: `Hello World`

✅ Generated IL correctly allocates string variables:
```
%t0 = alloca 8
%t1 = call @rt_str_empty()
store str, %t0, %t1  // Correctly typed as 'str' instead of 'i64'
```

✅ All 228 BASIC/frontend tests pass
✅ All 45 golden IL tests regenerated and passing

**Impact**:
- Variables can now be typed by their assigned values, not just by naming convention
- More intuitive and modern type inference behavior
- Backward compatible: suffix-based typing still works when used
- Improves code readability by reducing need for type suffix clutter

---

### BUG-002: & operator for string concatenation not supported
**Severity**: Low
**Status**: ✅ RESOLVED
**Test Case**: test010.bas (attempted workaround), /tmp/test_bug002.bas
**Resolution Date**: 2025-11-12

**Original Issue**:
The `&` operator commonly used for string concatenation in VB-style BASIC was not recognized by the parser.

**Reproduction**:
```basic
s1$ = "Hello"
s2$ = "World"
s3$ = s1$ & " " & s2$  ' ERROR: unknown statement
PRINT s3$
```

**Error Message**:
```
error[B0001]: unknown statement '&'; expected keyword or procedure call
s3$ = s1$ & " " & s2$
          ^
```

**Root Cause Analysis**:
The `&` operator was not implemented in the lexer or parser. The lexer did not tokenize `&` as an operator, and the parser did not include it in the expression parsing tables.

**Solution Implemented**:
Added `&` as a binary operator for string concatenation:

1. **Modified `TokenKinds.def`** (line 109):
   - Added `TOKEN(Ampersand, "&")` to the operator section

2. **Modified `Lexer.cpp`** (lines 458-459):
   - Added tokenization case for '&' character
   - Returns `TokenKind::Ampersand` when '&' is encountered

3. **Modified `Parser_Expr.cpp`** (lines 58, 66):
   - Updated infixParselets array size from 17 to 18
   - Added `InfixParselet{TokenKind::Ampersand, BinaryExpr::Op::Add, 4, Assoc::Left}`
   - Uses same precedence (4) and operation (Add) as Plus operator
   - Left associative, so `a & b & c` is parsed as `(a & b) & c`

**Files Changed**:
- `src/frontends/basic/TokenKinds.def`
- `src/frontends/basic/Lexer.cpp`
- `src/frontends/basic/Parser_Expr.cpp`

**Test Results**:
✅ Test case now works correctly:
```basic
s1$ = "Hello"
s2$ = "World"
s3$ = s1$ & " " & s2$
PRINT s3$
```
Output: `Hello World`

✅ Generated IL uses @rt_concat for string concatenation:
```
%t10 = load str, %t4
%t11 = const_str @.L2
%t12 = call @rt_concat(%t10, %t11)
%t13 = load str, %t2
%t14 = call @rt_concat(%t12, %t13)
```

✅ All 228 BASIC/frontend tests pass

**Impact**:
- VB-style string concatenation syntax now supported
- Provides alternative to `+` operator for string concatenation
- Improves compatibility with traditional BASIC dialects
- No breaking changes to existing code

---

### BUG-003: FUNCTION name assignment syntax not supported
**Severity**: Low
**Status**: ✅ RESOLVED
**Test Case**: test009.bas (initial version), /tmp/test_bug003.bas
**Resolution Date**: 2025-11-12

**Original Issue**:
The traditional BASIC syntax of assigning a return value to the function name (VB-style) was not supported. An explicit RETURN statement was required.

**Reproduction**:
```basic
FUNCTION Add(a, b)
    Add = a + b  ' This syntax didn't work
END FUNCTION
```

**Error Message**:
```
error[B1007]: missing return in FUNCTION ADD
FUNCTION Add(a, b)
^^^
```

**Root Cause Analysis**:
The semantic analyzer only checked for explicit `ReturnStmt` nodes when validating that functions return values. It did not recognize assignments to the function name as implicit returns, which is the traditional VB-style BASIC behavior.

Additionally, the lowering phase did not generate IL code to return the function name variable's value when no explicit RETURN statement was present.

**Solution Implemented**:
Implemented VB-style implicit return by assignment:

**Semantic Analysis Phase:**
1. **Modified `SemanticAnalyzer.hpp`** (line 463):
   - Added `activeFunctionNameAssigned_` boolean field to track if function name was assigned

2. **Modified `SemanticAnalyzer.Procs.cpp`** (lines 228, 231, 267):
   - Initialize `activeFunctionNameAssigned_` to false when entering a function
   - Save and restore the flag when analyzing nested functions

3. **Modified `SemanticAnalyzer.Procs.cpp`** (lines 284-286):
   - Updated `mustReturn()` to check `activeFunctionNameAssigned_` flag
   - Returns true if function name was assigned, treating it as an implicit return

4. **Modified `SemanticAnalyzer.Stmts.Runtime.cpp`** (lines 28, 88-92):
   - Added include for `StringUtils.hpp`
   - In `analyzeVarAssignment()`, detect when variable name matches active function name (case-insensitive)
   - Set `activeFunctionNameAssigned_` flag when match is detected

**Lowering Phase:**
5. **Modified `Lowerer.Procedure.cpp`** (lines 989-1002):
   - Updated `config.emitFinalReturn` lambda in `lowerFunctionDecl()`
   - Check if function name variable exists using `resolveVariableStorage()`
   - If exists, load and return its value instead of default value
   - Otherwise, return default value (0 for INT, 0.0 for FLOAT, "" for STRING, FALSE for BOOLEAN)

**Files Changed**:
- `src/frontends/basic/SemanticAnalyzer.hpp`
- `src/frontends/basic/SemanticAnalyzer.Procs.cpp`
- `src/frontends/basic/SemanticAnalyzer.Stmts.Runtime.cpp`
- `src/frontends/basic/Lowerer.Procedure.cpp`

**Test Results**:
✅ Test case now works correctly:
```basic
FUNCTION Add(a, b)
    Add = a + b
END FUNCTION

PRINT Add(5, 3)
```
Output: `8`

✅ Generated IL correctly returns function name variable:
```
func @ADD(i64 %A, i64 %B) -> i64 {
  ...
  store i64, %t4, %t7  // Store result in ADD variable
  ...
ret_ADD:
  %t8 = load i64, %t4  // Load ADD variable
  ret %t8              // Return its value
}
```

✅ All 228 BASIC/frontend tests pass

**Impact**:
- VB-style function name assignment now supported
- More intuitive for users familiar with traditional BASIC dialects
- Backward compatible: explicit RETURN statements still work
- No breaking changes to existing code

---
