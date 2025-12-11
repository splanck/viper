# Pascal Frontend Bugs Found During Game Development

This document tracks bugs discovered while porting the Frogger, VTris, and Centipede games from Viper BASIC to Viper Pascal.

## Missing Features (Not Bugs, But Blockers)

### 1. No Terminal/Console Builtins
**Status**: FIXED
**Description**: The Pascal frontend lacked terminal control builtins that exist in BASIC.
**Resolution**: Added all terminal builtins to BuiltinRegistry.cpp and RuntimeSignatures.cpp

---

## Semantic Analysis Bugs

### 2. Class Field Access in Methods Not Working
**Status**: ✅ FIXED (December 2025)
**Description**: When a method accesses a field of `Self`, the field is not resolved:
```pascal
type
  TCircle = class
  public
    Radius: Real;
    function Area: Real;
  end;

function TCircle.Area: Real;
begin
  Result := 3.14159 * Radius * Radius;  // ERROR: undefined identifier 'Radius'
end;
```
**Impact**: Cannot use class fields in methods
**Resolution**: Fixed. Implicit field access (without `Self.` prefix) now correctly resolves to class fields.

**Root Cause Analysis**:
- Location: `SemanticAnalyzer.cpp`, function `typeOfName()` (line ~2007)
- The `typeOfName()` function checks variables, constants, types, builtins, and user functions
- It does NOT check if `currentClassName_` is set and whether the identifier might be a field of the current class
- When inside a method body, `currentClassName_` is correctly set (lines 1051, 1113, 1171, 1207)
- However, `typeOfName()` never consults `classes_[currentClassName_].fields` to resolve bare field names
- **Fix Required**: Add a check in `typeOfName()` after checking variables but before reporting "undefined identifier":
  ```cpp
  // Check if we're inside a class method and the name is a field
  if (!currentClassName_.empty()) {
      auto classIt = classes_.find(toLower(currentClassName_));
      if (classIt != classes_.end()) {
          std::string fieldKey = toLower(expr.name);
          auto fieldIt = classIt->second.fields.find(fieldKey);
          if (fieldIt != classIt->second.fields.end()) {
              return fieldIt->second.type;
          }
      }
  }
  ```
- The Lowerer would also need corresponding changes to emit `Self.fieldName` access IL code

### 3. Constructor Calls Not Recognized
**Status**: ✅ FIXED (December 2025)
**Description**: `TClassName.Create` calls fail with "undefined method 'Create'"
```pascal
var c: TCircle;
begin
  c := TCircle.Create(5.0);  // ERROR: undefined method 'Create'
end.
```
**Impact**: Cannot instantiate classes
**Resolution**: Fixed. `TClassName.Create` syntax now generates proper allocation + constructor call IL.

**Root Cause Analysis**:
- Location: `SemanticAnalyzer.cpp`, function `typeOfCall()` (line ~2150)
- The call `TClassName.Create(...)` is parsed as a `CallExpr` with callee being a `FieldExpr`
- `FieldExpr.base` is a `NameExpr("TClassName")` which resolves to a *type*, not an instance
- In `typeOfField()` (line 2381), when base type is checked, `TClassName` resolves to a type (PasTypeKind::Class with name "TClassName")
- But `typeOfField()` expects the base to be an *instance* of a record/class, not a type reference
- When `TClassName` is looked up as a variable, it's not found (it's a type, not a variable)
- The type lookup returns the class type, but then field access on a type (not instance) fails
- **Issue 1**: Pascal convention `TClassName.Create` means "call constructor on the class type itself"
- **Issue 2**: Constructors are stored in `ClassInfo.methods` but are not registered as callable functions
- **Fix Required**:
  1. In `typeOfCall()`, detect when callee is `FieldExpr` with base being a type name (class)
  2. Special-case constructor calls: look up "Create" in `classes_[className].methods`
  3. Return the class type as the result type (constructors return a new instance)
  4. In Lowerer, emit IL to allocate object and call constructor

---

## Parser Bugs

### 5. Array Dimensions Cannot Use Constants
**Status**: ✅ FIXED (December 2025)
**Description**: Array dimension declarations only accept literal integers, not const values.
```pascal
const
  MAX_SIZE = 10;
var
  Items: array[MAX_SIZE] of Integer;  // ERROR: array dimension must be constant integer
```
**Workaround**: Use literal integers in array declarations

**Root Cause Analysis**:
- Location: `SemanticAnalyzer.cpp`, function `resolveType()` (line ~2519)
- The code explicitly checks for `ExprKind::IntLiteral`:
  ```cpp
  if (dim.size->kind != ExprKind::IntLiteral)
  {
      error(*dim.size, "array dimension must be a constant integer");
      continue;
  }
  ```
- This only accepts literal integers like `10`, rejecting constant identifiers like `MAX_SIZE`
- The `isConstantExpr()` function exists (line 2803) and correctly handles `NameExpr` referring to constants
- **Fix Required**: Replace the `IntLiteral` check with `isConstantExpr()` and evaluate the constant:
  ```cpp
  if (!isConstantExpr(*dim.size))
  {
      error(*dim.size, "array dimension must be a compile-time constant");
      continue;
  }
  // Evaluate the constant expression to get the integer value
  int64_t dimValue = evaluateConstantInt(*dim.size);
  if (dimValue <= 0)
  {
      error(*dim.size, "array dimension must be positive");
  }
  ```
- Would also need a `evaluateConstantInt()` helper to fold constant expressions

### 6. Exit Statement Not Implemented
**Status**: ✅ FIXED (December 2025)
**Description**: The `Exit` procedure/function is not implemented to exit from procedures/functions early.
```pascal
function CheckSomething: Boolean;
begin
  if condition then
  begin
    Result := True;
    Exit;  // ERROR: undefined procedure or function 'Exit'
  end;
end;
```
**Workaround**: Restructure code to avoid early returns

**Root Cause Analysis**:
- Location: `BuiltinRegistry.cpp` and `Lexer.cpp`
- `Exit` is not registered as a builtin in `BuiltinRegistry.cpp`
- `Exit` is not recognized as a keyword in `Lexer.cpp`
- There is no `StmtKind::Exit` in the AST to represent early function return
- **Fix Required** (Multiple components):
  1. **Option A - Builtin approach**: Add `Exit` to `BuiltinRegistry.cpp` as a special builtin
     - Would need special handling since it affects control flow (like `Break`/`Continue`)
  2. **Option B - Keyword/Statement approach**:
     - Add `KwExit` token to Lexer
     - Add `ExitStmt` to AST (similar to `BreakStmt`/`ContinueStmt`)
     - Add parsing in `Parser::parseStatement()`
     - Add semantic analysis to validate it's inside a function/procedure
     - Add lowering in `Lowerer` to emit `ret` instruction (for functions, load Result first)
  3. **Pascal semantics**: `Exit` can optionally take a value `Exit(x)` which sets Result
     - `Exit` in procedure = `ret void`
     - `Exit` in function = `ret (load %result_slot)`
     - `Exit(x)` in function = `store x, %result_slot; ret x`

### 7. Procedure Calls Lowered Incorrectly
**Status**: FIXED
**Description**: Calling a procedure (void function) was generating IL that expects a return value.
```pascal
procedure DoSomething;
begin
  // ...
end;

begin
  DoSomething;  // ERROR: call to void @DoSomething must not have a result
end.
```
**Resolution**: Added `PasTypeKind::Void` case to `mapType()` in Lowerer.cpp

### 8. Empty Else Block Generated for If Without Else
**Status**: FIXED
**Description**: If statements without an else clause were generating empty else blocks.
```pascal
if x > 5 then
  y := 1
else if x < 0 then  // Creates empty else block for inner if
  y := 2;
```
**Resolution**: Only create else block when `stmt.elseBranch` exists

### 9. Record/Type Field Access Not Implemented
**Status**: ✅ FIXED (December 2025)
**Description**: Field access on records was not generating IL code - fell through to default case returning 0.
```pascal
type
  TPoint = record
    X, Y: Integer;
  end;
var p: TPoint;
begin
  p.X := 5;        // Now works - generates GEP + store
  WriteLn(p.X);    // Now works - outputs 5
end.
```
**Resolution**: Fixed by adding global variable lookup for records in both `lowerField()` (Lowerer_Expr.cpp) and `lowerAssign()` (Lowerer_Stmt.cpp). Field access now generates proper GEP + load/store IL.

### 10. Local Variable Types Not Tracked for Procedure Locals
**Status**: FIXED
**Description**: Local variables declared inside procedures were losing their type information during lowering because the semantic analyzer's scope stack was already unwound.
```pascal
procedure Test;
var
  Key: String;  // Type lost during lowering
begin
  Key := InKey;
  Length(Key);  // Error: expects str but got i64
end;
```
**Resolution**: Added `localTypes_` map to Lowerer to store resolved types from VarDecl during allocation

### 11. String Comparison Not Implemented
**Status**: FIXED
**Description**: String equality comparisons used integer comparison opcodes instead of runtime string comparison.
```pascal
if Key = 'w' then  // Error: operand type mismatch (str vs str with icmp_eq)
```
**Resolution**: Added string comparison handling in `lowerBinary` to call `rt_str_eq` runtime function

### 12. Zero-Arg User-Defined Functions Not Recognized
**Status**: FIXED
**Description**: Calling a zero-argument user-defined function without parentheses was treated as an undefined identifier.
```pascal
function GetValue: Integer;
begin
  Result := 42;
end;

var x: Integer;
begin
  x := GetValue;  // ERROR: undefined identifier 'GetValue'
end.
```
**Resolution**: Added check in `typeOfName` and `lowerName` to recognize zero-arg user-defined functions

### 13. Result Variable Not Mapped to Function Return Slot
**Status**: FIXED
**Description**: Assigning to `Result` inside a function was not correctly storing to the function's return slot.
```pascal
function GetValue: Integer;
begin
  Result := 42;  // Not stored - function returns uninitialized value
end;
```
**Resolution**: Added `currentFuncName_` tracking and special handling in `lowerAssign` to map `Result` to the function's return slot

### 14. Function Locals Polluting Main Scope
**Status**: FIXED
**Description**: Local variables from lowered functions (including the function's return slot) were not cleared before lowering `main`, causing name collisions.
**Resolution**: Added `locals_.clear()` and `localTypes_.clear()` before lowering the main function body

### 15. Integer Division/Remainder Using Unchecked Opcodes
**Status**: FIXED
**Description**: The `div` and `mod` operators were using `SDiv` and `SRem` opcodes instead of checked versions.
```pascal
x := a div b;  // ERROR: must use sdiv.chk0
y := a mod b;  // ERROR: must use srem.chk0
```
**Resolution**: Changed to use `Opcode::SDivChk0` and `Opcode::SRemChk0`

### 16. Incorrect Runtime Symbols for Math Builtins
**Status**: FIXED
**Description**: Several builtin functions used non-existent runtime symbols:
- `Trunc` used `rt_fix` instead of `rt_fix_trunc`
- `Round` used `rt_round` instead of `rt_round_even`
- `Random(n)` used `rt_random_int` which doesn't exist
**Resolution**: Updated BuiltinRegistry.cpp with correct runtime symbols

---

## Codegen Bugs

### 4. Signed Integer Arithmetic Uses Wrong Opcodes
**Status**: FIXED
**Description**: The Pascal lowerer was emitting `add`, `sub`, `mul` for signed integer operations, but the IL verifier requires `iadd.ovf`, `isub.ovf`, `imul.ovf` for signed types to trap on overflow.
**Resolution**: Changed Lowerer.cpp to use the overflow-checking opcodes

---

## Runtime/Lowering Bugs

### 17. Global Arrays Do Not Store/Retrieve Values Correctly (CRITICAL)
**Status**: ✅ FIXED (December 2025)
**Severity**: CRITICAL - Blocks any program using global arrays
**Description**: Global array assignments silently fail. Values assigned to global array elements are not stored - reading them back returns 0 (or the default value).

**Minimal Reproduction**:
```pascal
program TestGlobalArray;
var
  Arr: array[5] of Integer;
begin
  Arr[0] := 100;
  Arr[1] := 200;
  Arr[2] := 300;

  WriteLn('Reading:');
  WriteLn(Arr[0]);  // Expected: 100, Actual: 0
  WriteLn(Arr[1]);  // Expected: 200, Actual: 0
  WriteLn(Arr[2]);  // Expected: 300, Actual: 0
end.
```

**Observations**:
- Local arrays inside procedures work correctly
- Global arrays fail silently (no error, just wrong values)
- Object arrays (`array[N] of TClass`) exhibit the same bug
- The bug affects both reading and writing

**Working Example (Local Array)**:
```pascal
program TestLocalArray;

procedure TestProc;
var
  Arr: array[5] of Integer;
begin
  Arr[0] := 100;
  Arr[1] := 200;
  WriteLn('Local array:');
  WriteLn(Arr[0]);  // Outputs: 100 (correct!)
  WriteLn(Arr[1]);  // Outputs: 200 (correct!)
end;

begin
  TestProc;
end.
```

**Root Cause Analysis**:
- Location: Likely in `Lowerer.cpp` or `Lowerer_Stmt.cpp` where global array storage is emitted
- The IL generated for global array access may be incorrect
- Local arrays use stack allocation which works; global arrays use module-level storage which may have different addressing
- Need to compare the IL generated for local vs global array access

**Workaround Used in Frogger**:
- Replaced all arrays with individual named variables (e.g., `V0Row, V0Col, V1Row, V1Col, ...` instead of `Vehicles: array[10] of TVehicle`)
- This dramatically increased code verbosity but allowed the game to function

**Impact**: Cannot use any arrays at global scope. This blocks:
- Game state arrays (enemies, bullets, platforms, etc.)
- Lookup tables
- Any program with global data structures

### 18. For Loop Variables Cannot Be Used As Array Indices Inside Loop Body
**Status**: ✅ FIXED (December 2025) - Fixed as part of Bug #17
**Description**: When using a `for` loop, the loop variable cannot be used as an array index inside the loop body.

**Reproduction**:
```pascal
program TestForLoop;
var
  Arr: array[5] of Integer;
  I: Integer;
begin
  for I := 0 to 4 do
  begin
    Arr[I] := I * 10;  // ERROR: loop variable 'I' is undefined after loop terminates
  end;

  for I := 0 to 4 do
  begin
    WriteLn(Arr[I]);  // ERROR: loop variable 'I' is undefined after loop terminates
  end;
end.
```

**Observations**:
- The error message says "undefined after loop terminates" but occurs inside the loop body
- Using `while` loops with manual index management works as a workaround
- The loop variable appears to have incorrect scoping

**Root Cause Analysis**:
- Location: Likely in `SemanticAnalyzer.cpp` for loop scope handling, or in `Lowerer.cpp` for variable visibility
- The loop variable may be scoped only to the loop control expression, not the loop body
- Or the error message is misleading and the actual issue is elsewhere

**Workaround**:
```pascal
var I: Integer;
begin
  I := 0;
  while I < 5 do
  begin
    Arr[I] := I * 10;  // Works with while loop
    I := I + 1;
  end;
end.
```

**Impact**: Cannot use idiomatic `for` loops for array iteration - must use verbose `while` loops instead

### 19. Copy Function Broken - Argument Count Mismatch
**Status**: ✅ FIXED (December 2025)
**Description**: The `Copy` function (substring extraction) has inconsistent argument handling between the semantic analyzer and runtime.

**Reproduction**:
```pascal
var s, sub: String;
begin
  s := 'Hello World';
  sub := Copy(s, 1, 5);  // ERROR: too many arguments: expected at most 2, got 3
end.
```

When using only 2 arguments:
```pascal
  sub := Copy(s, 1);  // RUNTIME ERROR: rt_substr expects 3 arguments but got 2
```

**Observations**:
- BuiltinRegistry.cpp claims `Copy` accepts 2-3 arguments
- Semantic analyzer rejects 3 arguments as "too many"
- Runtime function `rt_substr` requires exactly 3 arguments
- This makes `Copy` completely unusable in either form

**Root Cause Analysis**:
- Location: `BuiltinRegistry.cpp` and `SemanticAnalyzer_Expr.cpp`
- The builtin registration says 2-3 args, but the semantic analyzer's argument validation doesn't honor the max count properly
- Additionally, the 2-arg variant is not properly handled in the lowerer to provide a default length

**Workaround Used in VTris**:
- Avoided string manipulation entirely
- Used integer-based storage instead of string grids

**Impact**: Cannot extract substrings. Any code requiring string manipulation cannot be ported from BASIC.

### 20. Trunc/Round/Floor/Ceil Return Type Mismatch
**Status**: ✅ FIXED (December 2025)
**Description**: The Trunc, Round, Floor, and Ceil builtins returned f64 from the runtime but Pascal expected i64, causing a type mismatch error.

```pascal
var x: Integer;
begin
  x := Trunc(Random * 7);  // ERROR: operand type mismatch: operand 1 must be i64
end.
```

**Root Cause Analysis**:
- Location: `Lowerer_Expr_Call.cpp`
- The runtime functions (`rt_fix_trunc`, `rt_round_even`, etc.) return `f64`
- Pascal's BuiltinRegistry declared these as returning `Integer`
- The lowerer used the runtime's return type (f64) without converting to the Pascal-expected type (i64)
- The fix adds a `CastFpToSiRteChk` conversion after the call when Pascal expects i64 but runtime returns f64

**Resolution**: Fixed by adding f64→i64 conversion in Lowerer_Expr_Call.cpp (lines 374-380)

### 21. Global Array Memory Allocation Only Allocates 8 Bytes (CRITICAL)
**Status**: ✅ FIXED (December 2025)
**Severity**: CRITICAL - Caused memory corruption in any program with multiple global arrays
**Description**: The runtime function `rt_modvar_addr_*` used for global variable storage only allocated 8 bytes per variable, regardless of the actual type size. Arrays and records need `element_count * element_size` bytes.

**Minimal Reproduction**:
```pascal
program TestOverlap;
var
  A: array[5] of Integer;
  B: array[5] of Integer;
begin
  A[0] := 100;
  A[1] := 200;
  A[2] := 300;
  A[3] := 400;
  A[4] := 500;

  B[0] := 1000;  // This would corrupt A[4]!

  WriteLn(A[4]);  // Expected: 500, Actual: 1000 (corrupted!)
end.
```

**Root Cause Analysis**:
- Location: `rt_modvar.c` and `Lowerer_Decl.cpp`
- The `rt_modvar_addr_i64`, `rt_modvar_addr_f64`, etc. functions all called `mv_addr(name, type, 8)` - always allocating exactly 8 bytes
- Arrays need `count * sizeof(element)` bytes (e.g., `array[5] of Integer` needs 40 bytes)
- With only 8 bytes allocated, consecutive global arrays would overlap in memory, causing corruption
- This explains why Bug #17 appeared to be fixed by lowerer changes but still had issues in complex programs

**Fix Applied**:
1. Added new runtime function `rt_modvar_addr_block(rt_string name, int64_t size)` to `rt_modvar.h` and `rt_modvar.c`
2. Modified `getGlobalVarAddr()` in `Lowerer_Decl.cpp` to detect arrays and records
3. Arrays/records now call `rt_modvar_addr_block(name, totalSize)` instead of `rt_modvar_addr_ptr(name)`
4. Registered `rt_modvar_addr_block` in `RuntimeSignatures.cpp`

**Files Changed**:
- `src/runtime/rt_modvar.h` - Added function declaration
- `src/runtime/rt_modvar.c` - Added function implementation
- `src/frontends/pascal/Lowerer_Decl.cpp` - Modified `getGlobalVarAddr()` to use sized allocation
- `src/il/runtime/RuntimeSignatures.cpp` - Registered new runtime function

**Impact**: This bug affected ALL Pascal programs using global arrays. The memory corruption was silent and unpredictable, making debugging extremely difficult.

---

## Summary

All Pascal frontend bugs have been resolved! 🎉

### Fixed Bugs (December 2025)
- ✅ Bug #2 (Class field access in methods) - FIXED
- ✅ Bug #3 (Constructor calls) - FIXED
- ✅ Bug #5 (Array dimensions with constants) - FIXED
- ✅ Bug #6 (Exit statement) - FIXED
- ✅ Bug #9 (Record field access) - FIXED
- ✅ Bug #17 (Global arrays lowering) - FIXED (Lowerer_Expr_Access.cpp, Lowerer_Stmt.cpp)
- ✅ Bug #18 (For loop variables as array indices) - FIXED (same as #17)
- ✅ Bug #19 (Copy function) - FIXED (SemanticAnalyzer_Util.cpp, Lowerer_Expr_Call.cpp)
- ✅ Bug #20 (Trunc/Round/Floor/Ceil return type) - FIXED
- ✅ Bug #21 (Global array memory allocation) - FIXED (rt_modvar.c, Lowerer_Decl.cpp, RuntimeSignatures.cpp)

### Key Fixes Made Today
1. **Bug #17/18**: Added global array support in lowerIndex() and lowerAssign() - previously only local arrays were handled
2. **Bug #19**: Fixed builtin registration to include optional parameters in FuncSignature, added 1-based to 0-based index conversion for Copy
3. **Bug #21**: Added `rt_modvar_addr_block()` runtime function for properly-sized global array/record allocation

---

## Notes

This file will be updated as more bugs are discovered during game development.
