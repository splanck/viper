# BUG-065: Array Field Assignments Silently Dropped - Deep Dive Root Cause Analysis

## Executive Summary

Array field assignments like `arr(idx) = val` inside class methods are **silently dropped** by the compiler with no
error or warning. The assignment statement is correctly parsed but completely disappears during IL code generation.

**Root Cause**: Missing `isArray` field in `ClassLayout::Field` struct causes array metadata to be lost during OOP
scanning, leading to incorrect symbol table initialization in field scopes.

## Symptom

```basic
CLASS Test
    DIM arr(5) AS STRING

    SUB SetItem(idx AS INTEGER, val AS STRING)
        arr(idx) = val  ← SILENTLY DROPPED!
    END SUB
END CLASS
```

**Generated IL** (incorrect):

```il
func @TEST.SETITEM(ptr %ME, i64 %IDX, str %VAL) -> void {
entry_TEST.SETITEM(%ME:ptr, %IDX:i64, %VAL:str):
  ...
  %t6 = load str, %t5      # Loads VAL parameter
  br ret_TEST.SETITEM       # Then just returns - NO STORE!
ret_TEST.SETITEM:
  ret
}
```

**Expected**: Should emit `call @rt_arr_str_put(base, index, value)` to store the element.

---

## Complete Execution Path Trace

### 1. Parse Stage ✅ CORRECT

**File**: Parser (AST construction)

The assignment is correctly parsed as an `ArrayExpr`:

```
(METHOD SETITEM (IDX VAL) {0:(LET (ARR IDX) VAL)})
```

The AST node `ClassDecl::Field` (StmtDecl.hpp:221-231) includes:

- `bool isArray{false};` (line 228)
- `std::vector<long long> arrayExtents;` (line 230)

For our test case, the field has `isArray = true` and `arrayExtents = {5}`.

---

### 2. OOP Scan Stage ⚠️ **INFORMATION LOSS**

**File**: `/Users/stephen/git/viper/src/frontends/basic/Lower_OOP_Scan.cpp:150-165`

When building `ClassLayout` from AST `ClassDecl`:

```cpp
void after(const ClassDecl &decl)
{
    Lowerer::ClassLayout layout;
    std::size_t offset = 0;
    for (const auto &field : decl.fields)  // AST fields with isArray info
    {
        offset = alignTo(offset, kFieldAlignment);
        Lowerer::ClassLayout::Field info{};
        info.name = field.name;                                    // "ARR"
        info.type = field.type;                                    // Type::Str
        info.offset = offset;                                      // 0
        // Line 158: USES field.isArray but doesn't PRESERVE it!
        info.size = field.isArray ? kPointerSize : fieldSize(field.type);  // 8 (ptr size)
        layout.fields.push_back(std::move(info));
        ...
    }
    layouts.emplace_back(decl.name, std::move(layout));  // Store as "TEST" → layout
}
```

**Problem**: `ClassLayout::Field` struct (Lowerer.hpp:803-809) only contains:

```cpp
struct Field
{
    std::string name;
    AstType type{AstType::I64};
    std::size_t offset{0};
    std::size_t size{0};
    // ❌ NO isArray field!
    // ❌ NO arrayExtents field!
};
```

The array metadata is **used to compute size** but **not preserved** for later use.

---

### 3. Field Scope Setup ❌ **INCORRECT METADATA**

**File**: `/Users/stephen/git/viper/src/frontends/basic/Lowerer.Procedure.cpp:430-449`

When entering a method, `pushFieldScope(className)` creates `SymbolInfo` entries for all fields:

```cpp
void Lowerer::pushFieldScope(const std::string &className)
{
    FieldScope scope;
    if (auto it = classLayouts_.find(className); it != classLayouts_.end())
    {
        scope.layout = &it->second;
        for (const auto &field : it->second.fields)  // ClassLayout::Field - NO isArray!
        {
            SymbolInfo info;
            info.type = field.type;
            info.hasType = true;
            info.isArray = false;  // ❌ LINE 441: HARDCODED TO FALSE!
            info.isBoolean = (field.type == AstType::Bool);
            info.referenced = false;
            info.isObject = false;
            info.objectClass.clear();
            scope.symbols.emplace(field.name, std::move(info));  // "ARR" → {isArray=false}
        }
    }
    fieldScopeStack_.push_back(std::move(scope));
}
```

**Problem**: Line 441 hardcodes `info.isArray = false` for ALL fields because `ClassLayout::Field` doesn't contain the
`isArray` information needed to set it correctly.

**Result**: Field scope symbols map has:

- `"ARR"` → `SymbolInfo{type=Str, isArray=false}` ❌ **WRONG!** Should be `isArray=true`

---

### 4. Assignment Lowering ⚠️ **ATTEMPTED RECOVERY**

**File**: `/Users/stephen/git/viper/src/frontends/basic/LowerStmt_Runtime.cpp:286-368`

When lowering `arr(idx) = val`:

```cpp
void Lowerer::lowerLet(const LetStmt &stmt)
{
    ...
    else if (auto *arr = as<const ArrayExpr>(*stmt.target))
    {
        assignArrayElement(*arr, std::move(value), stmt.loc);  // Line 523
    }
}

void Lowerer::assignArrayElement(const ArrayExpr &target, RVal value, il::support::SourceLoc loc)
{
    // Line 290: Get array base and index
    ArrayAccess access = lowerArrayAccess(target, ArrayAccessKind::Store);

    // Line 296: Check if this is a dotted member array (e.g., obj.arr(i))
    bool isMemberArray = target.name.find('.') != std::string::npos;  // false for "ARR"

    // Line 315: Check if this is an implicit field array (e.g., arr(i) inside method)
    bool isImplicitFieldArray = (!isMemberArray) && isFieldInScope(target.name);

    // ❓ KEY QUESTION: Does isFieldInScope("ARR") return true or false?

    if (isImplicitFieldArray)  // Lines 316-341: RECOVERY CODE
    {
        // This code SHOULD run for our case!
        if (const auto *scope = activeFieldScope(); scope && scope->layout)
        {
            if (const ClassLayout::Field *fld = scope->layout->findField(target.name))
                memberElemAstType = fld->type;  // Get element type from layout

            // Recompute base from ME.<field> since lowerArrayAccess may have failed
            const auto *selfInfo = findSymbol("ME");
            if (selfInfo && selfInfo->slotId)
            {
                Value selfPtr = emitLoad(Type(Type::Kind::Ptr), Value::temp(*selfInfo->slotId));
                long long offset = 0;
                if (const ClassLayout::Field *f2 = scope->layout->findField(target.name))
                    offset = static_cast<long long>(f2->offset);
                Value fieldPtr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), selfPtr, Value::constInt(offset));
                // Line 338: Load array handle from field
                access.base = emitLoad(Type(Type::Kind::Ptr), fieldPtr);
            }
        }
    }

    // Lines 343-352: Emit rt_arr_str_put call
    if ((info && info->type == AstType::Str) ||
        (isMemberArray && memberElemAstType == ::il::frontends::basic::Type::Str) ||
        (isImplicitFieldArray && memberElemAstType == ::il::frontends::basic::Type::Str))
    {
        Value tmp = emitAlloca(8);
        emitStore(Type(Type::Kind::Str), tmp, value.value);
        emitCall("rt_arr_str_put", {access.base, access.index, tmp});
    }
}
```

**Analysis of Recovery Logic**:
The code at lines 312-341 is specifically designed to handle implicit field arrays! It should:

1. Detect that "ARR" is a field using `isFieldInScope("ARR")`
2. Recompute the array base by loading from `ME.<field>`
3. Get the correct element type from the class layout

But the IL output shows NO call to `rt_arr_str_put`, meaning this recovery code is NOT executing.

---

### 5. The Critical Check: `isFieldInScope`

**File**: `/Users/stephen/git/viper/src/frontends/basic/Lowerer.Procedure.cpp:465-476`

```cpp
bool Lowerer::isFieldInScope(std::string_view name) const
{
    if (name.empty())
        return false;
    std::string key(name);
    for (auto it = fieldScopeStack_.rbegin(); it != fieldScopeStack_.rend(); ++it)
    {
        if (it->symbols.find(key) != it->symbols.end())  // Case-sensitive lookup
            return true;
    }
    return false;
}
```

**Expected**: Should find "ARR" in `fieldScopeStack_.back().symbols` and return `true`.

**Hypothesis**: Either:

1. The field scope was not pushed (layout lookup failed in `pushFieldScope`), OR
2. Case sensitivity mismatch ("ARR" vs "arr"), OR
3. The symbols map is empty even though the layout exists

**Evidence from IL**: Since no array store is emitted, either `isFieldInScope` returns false OR there's another failure
point in the recovery code.

---

### 6. Fallback Path ❌ **SILENT DROP**

**File**: `/Users/stephen/git/viper/src/frontends/basic/LowerStmt_Runtime.cpp:519`

If the implicit field array check fails, control would flow to:

```cpp
// Line 519: Fallback for unsupported lvalue forms
// Fallback: not a supported lvalue form; do nothing here (analyzer should have errored).
```

But this is in a DIFFERENT branch (MethodCallExpr, not ArrayExpr). For ArrayExpr, if `assignArrayElement` fails to emit
code, the assignment is simply lost.

Actually, looking more carefully, if `isImplicitFieldArray` is false and there's no local symbol info for "ARR", then:

- Lines 343-345: Condition checks `info && info->type == AstType::Str` → `info` is nullptr (from line 293:
  `findSymbol("ARR")` returns nullptr)
- Line 344: `isMemberArray` is false
- Line 345: `isImplicitFieldArray` is false
- Condition fails, falls through to line 354

Line 354-359: Checks for object arrays - also requires `info` which is nullptr
Line 360-367: Integer array fallback - still uses `access.base` which may be invalid

**Hypothesis**: `access.base` is returning an invalid or unusable value from `lowerArrayAccess`, causing the subsequent
`emitCall` to fail silently or not emit the call.

---

## Root Cause Chain

1. **AST → ClassLayout translation loses array metadata**
    - `ClassDecl::Field` has `isArray` and `arrayExtents`
    - `ClassLayout::Field` does NOT have these fields
    - OOP scan uses `isArray` to compute size but doesn't preserve it

2. **Field scope initialization sets wrong metadata**
    - `pushFieldScope` hardcodes `info.isArray = false` (line 441)
    - Cannot set correctly because `ClassLayout::Field` lacks the information

3. **Symbol resolution treats field as non-array**
    - `findSymbol("ARR")` returns nullptr (it's a field, not a local)
    - `resolveVariableStorage("ARR")` via `resolveImplicitField` returns storage with `slotInfo.isArray = false` (line
      687)

4. **Array access lowering fails or produces wrong base**
    - `lowerArrayAccess` may not handle field arrays correctly when `isMemberArray` is false
    - Returns `ArrayAccess` with invalid or wrong `base` value

5. **Assignment emission fails to detect implicit field array**
    - `isFieldInScope("ARR")` may return false (need to verify)
    - OR recovery code runs but subsequent emit call fails silently
    - Either way, no `rt_arr_str_put` is emitted

---

## Proposed Fixes

### Option 1: Add isArray to ClassLayout::Field (Recommended)

**File**: `/Users/stephen/git/viper/src/frontends/basic/Lowerer.hpp:803-809`

```cpp
struct Field
{
    std::string name;
    AstType type{AstType::I64};
    std::size_t offset{0};
    std::size_t size{0};
    bool isArray{false};  // ← ADD THIS
};
```

**File**: `/Users/stephen/git/viper/src/frontends/basic/Lower_OOP_Scan.cpp:158`

```cpp
info.isArray = field.isArray;  // ← ADD THIS after line 158
```

**File**: `/Users/stephen/git/viper/src/frontends/basic/Lowerer.Procedure.cpp:441`

```cpp
info.isArray = field.isArray;  // ← CHANGE from hardcoded false
```

### Option 2: Fix resolveVariableStorage to preserve array info

**File**: `/Users/stephen/git/viper/src/frontends/basic/Lowerer.Procedure.cpp:687`

Instead of hardcoding `slotInfo.isArray = false`, detect array fields from ClassLayout.

### Option 3: Improve implicit field array detection

Ensure `isFieldInScope` uses case-insensitive lookup (similar to the `findField` fix from earlier).

---

## Testing

**Test Case**: `/Users/stephen/git/viper/bugs/bug_testing/debug_parse_test.bas`

**Verify Fix**:

```bash
./build/src/tools/ilc/ilc front basic -emit-il bugs/bug_testing/debug_parse_test.bas | grep -A 20 "TEST.SETITEM"
```

Should see:

```il
call @rt_arr_str_put(%base, %index, %tmp)
```

---

## Impact Assessment

- **Severity**: CRITICAL - Silently wrong code generation
- **Scope**: All array field assignments in class methods
- **Workaround**: Use explicit `ME.arr(idx)` syntax instead of implicit `arr(idx)`

---

## Related Code

- Parse: `src/frontends/basic/ast/StmtDecl.hpp:221-231` (ClassDecl::Field with isArray)
- OOP Scan: `src/frontends/basic/Lower_OOP_Scan.cpp:150-165` (builds ClassLayout)
- Field Scope: `src/frontends/basic/Lowerer.Procedure.cpp:430-449` (pushFieldScope)
- Symbol Resolution: `src/frontends/basic/Lowerer.Procedure.cpp:558-697` (resolveVariableStorage)
- Assignment: `src/frontends/basic/LowerStmt_Runtime.cpp:286-368` (assignArrayElement)
- Array Access: `src/frontends/basic/lower/Emit_Expr.cpp:103-246` (lowerArrayAccess)
