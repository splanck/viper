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
**Status**: Discovered during Frogger development
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
**Status**: Discovered during Frogger development
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

## Runtime Bugs

(None found yet)

---

## Summary of Unresolved Bugs

| Bug # | Issue | Root Cause Location | Complexity |
|-------|-------|---------------------|------------|
| 5 | Array dimensions cannot use constants | `SemanticAnalyzer::resolveType()` | Low |
| 6 | Exit statement not implemented | Missing from Lexer/Parser/AST/Lowerer | Medium |

### Fixed OOP Bugs (December 2025)
- ✅ Bug #2 (Class field access in methods) - FIXED
- ✅ Bug #3 (Constructor calls) - FIXED
- ✅ Bug #9 (Record field access) - FIXED

### Priority Recommendations

1. **Bug #5 (Constant array dimensions)** - Easy fix, improves code maintainability
2. **Bug #6 (Exit statement)** - Common Pascal idiom, medium complexity

---

## Notes

This file will be updated as more bugs are discovered during game development.
