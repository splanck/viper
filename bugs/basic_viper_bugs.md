# Bugs Found During BASIC vs ViperLang Comparison

## ViperLang Bugs

### BUG-VL-001: Byte type doesn't accept integer literals
- **Status**: ✅ FIXED
- **Test**: `tests/comparison/viper/01_primitives.viper`
- **Code**: `var b: Byte = 255;`
- **Expected**: Should accept integer literal for Byte type
- **Actual**: `Type mismatch: expected Byte, got Integer`
- **Severity**: Medium
- **Fix**: Two-part fix: (1) Added compile-time check in `Sema_Stmt.cpp:analyzeVarStmt` to treat integer literals in range 0-255 as Byte type when assigning to a Byte variable. (2) Added `widenByteToInteger()` helper in `Lowerer_Emit.cpp` to zero-extend i32 to i64 when passing Byte arguments to runtime functions expecting Integer.

#### Root Cause Analysis

The type system only supports **widening** numeric conversions, not narrowing:

In `Types.cpp:108-114`, the `isAssignableFrom` method defines:
```cpp
// Numeric promotions
if (kind == TypeKindSem::Number && source.kind == TypeKindSem::Integer)
    return true; // Integer -> Number (widening)
if (kind == TypeKindSem::Integer && source.kind == TypeKindSem::Byte)
    return true; // Byte -> Integer (widening)
if (kind == TypeKindSem::Number && source.kind == TypeKindSem::Byte)
    return true; // Byte -> Number (widening)
```

**Missing**: There is no rule for `Integer -> Byte` (narrowing conversion).

When you write `var b: Byte = 255;`:
1. The literal `255` is typed as `Integer` by the parser
2. `Byte.isAssignableFrom(Integer)` returns `false`
3. Type mismatch error is raised

#### Fix Suggestions

**Option A**: Add compile-time constant folding for integer literals that fit in Byte range (0-255):
```cpp
// In Sema_Stmt.cpp analyzeVarStmt, before type checking:
if (declaredType->kind == TypeKindSem::Byte && initType->kind == TypeKindSem::Integer) {
    if (auto* lit = dynamic_cast<IntLitExpr*>(stmt->initializer.get())) {
        if (lit->value >= 0 && lit->value <= 255) {
            initType = types::byte(); // Treat as Byte literal
        }
    }
}
```

**Option B**: Require explicit cast: `var b: Byte = 255 as Byte;`

**Option C**: Add narrowing rule with runtime range check (not recommended for Byte)

#### Files Involved
- `src/frontends/viperlang/Types.cpp` (lines 108-114) - `isAssignableFrom` numeric promotions
- `src/frontends/viperlang/Sema_Stmt.cpp` (lines 87-93) - variable declaration type checking

---

### BUG-VL-002: Bitwise operators not supported
- **Status**: ✅ FIXED
- **Test**: `tests/comparison/viper/03_operators.viper`
- **Code**: `var and = a & b;`
- **Expected**: Bitwise AND/OR/XOR/NOT should work
- **Actual**: Parse error - operators not recognized
- **Severity**: Medium
- **Fix**: Added `parseBitwiseOr()`, `parseBitwiseXor()`, `parseBitwiseAnd()` functions to `Parser_Expr.cpp` and updated `parseLogicalAnd()` to call `parseBitwiseOr()` instead of `parseEquality()`. Verified: `12 & 10 = 8`, `12 | 10 = 14`, `12 ^ 10 = 6`.

#### Root Cause Analysis

The parser is **incomplete** - bitwise operators are lexed but not parsed:

1. **Lexer**: Correctly produces tokens (`Ampersand`, `Pipe`, `Caret`, `Tilde`) - `Lexer.cpp:1063-1099`
2. **AST**: Has `BinaryOp::BitAnd`, `BitOr`, `BitXor` enums - `AST_Expr.hpp:399-401`
3. **Semantic analysis**: Handles bitwise ops - `Sema_Expr.cpp:219-221`
4. **Lowerer**: Generates IL for bitwise ops - `Lowerer_Expr.cpp:794-800`

**Missing**: The parser has no `parseBitwiseOr()`, `parseBitwiseXor()`, or `parseBitwiseAnd()` functions!

Current parsing hierarchy in `Parser_Expr.cpp`:
```
parseLogicalOr (||)
  → parseLogicalAnd (&&)
    → parseEquality (==, !=)  ← bitwise ops should be HERE
      → parseComparison (<, <=, >, >=)
        → parseAdditive (+, -)
          → parseMultiplicative (*, /, %)
            → parseUnary (-, !, ~)
```

Standard precedence requires:
```
parseLogicalOr (||)
  → parseLogicalAnd (&&)
    → parseBitwiseOr (|)      ← MISSING
      → parseBitwiseXor (^)   ← MISSING
        → parseBitwiseAnd (&) ← MISSING
          → parseEquality (==, !=)
            → ...
```

#### Fix Suggestion

Add three new parsing functions in `Parser_Expr.cpp`:

```cpp
ExprPtr Parser::parseBitwiseOr() {
    ExprPtr expr = parseBitwiseXor();
    while (match(TokenKind::Pipe)) {
        ExprPtr right = parseBitwiseXor();
        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::BitOr, std::move(expr), std::move(right));
    }
    return expr;
}

ExprPtr Parser::parseBitwiseXor() {
    ExprPtr expr = parseBitwiseAnd();
    while (match(TokenKind::Caret)) {
        ExprPtr right = parseBitwiseAnd();
        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::BitXor, std::move(expr), std::move(right));
    }
    return expr;
}

ExprPtr Parser::parseBitwiseAnd() {
    ExprPtr expr = parseEquality();
    while (match(TokenKind::Ampersand)) {
        ExprPtr right = parseEquality();
        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::BitAnd, std::move(expr), std::move(right));
    }
    return expr;
}
```

And update `parseLogicalAnd()` to call `parseBitwiseOr()` instead of `parseEquality()`.

#### Files Involved
- `src/frontends/viperlang/Parser_Expr.cpp` - missing parsing functions
- `src/frontends/viperlang/Parser.hpp` - add function declarations

---

### BUG-VL-003: String concatenation with Viper.Fmt.Int() in loops causes crash
- **Status**: ✅ FIXED
- **Test**: `tests/comparison/viper/04_control_flow.viper`
- **Code**: `Viper.Terminal.Say("i = " + Viper.Fmt.Int(i));` inside for loop
- **Expected**: Should print formatted string
- **Actual**: Exit code 134 (SIGABRT), corrupted output
- **Workaround**: Use `Viper.Terminal.SayInt()` instead
- **Severity**: High
- **Fix**: Made literal strings immortal by setting `literal_refs = SIZE_MAX` in `rt_const_cstr()` (`rt_string_encode.c:114`). This prevents the use-after-free when `rt_concat` unrefs the cached literal string.

#### Root Cause Analysis

**Use-After-Free in String Literal Cache**

The bug is a **use-after-free** caused by a semantic mismatch between the VM's string literal caching and `rt_concat`'s consuming ownership model.

**The Execution Flow:**

1. **String literal caching**: During VM initialization, all string literals are cached in `inlineLiteralCache`:
   ```cpp
   // VMInit.cpp:377
   inlineLiteralCache[operand.str] = ViperStringHandle(rt_const_cstr(operand.str.c_str()));
   ```
   The `ViperStringHandle` owns **ONE reference** to the literal string (via `literal_refs = 1`).

2. **Evaluating `const_str`**: When the VM evaluates a constant string operand, it returns the cached pointer **without incrementing the reference**:
   ```cpp
   // VMContext.cpp:233-237
   auto it = vmInstance->inlineLiteralCache.find(value.str);
   if (it != vmInstance->inlineLiteralCache.end()) {
       s.str = it->second.get();  // Returns raw pointer, no ref increment!
       return s;
   }
   ```

3. **`rt_concat` consumes inputs**: The `+` operator for strings calls `Viper.String.Concat` → `rt_concat`:
   ```c
   // rt_string_ops.c:332 (documented as "consuming the inputs")
   rt_string rt_concat(rt_string a, rt_string b) {
       // ... create new string ...
       if (a) rt_string_unref(a);  // Consumes (releases) first operand
       if (b) rt_string_unref(b);  // Consumes (releases) second operand
       return out;
   }
   ```

4. **Freeing the cached wrapper**: When `rt_string_unref` is called on a literal string:
   ```c
   // rt_string_ops.c:248-258
   void rt_string_unref(rt_string s) {
       rt_heap_hdr_t *hdr = rt_string_header(s);
       if (!hdr) {  // Literal strings have heap=NULL
           if (s->literal_refs > 0 && --s->literal_refs == 0)
               free(s);  // FREES THE WRAPPER STRUCT!
           return;
       }
   }
   ```
   The `literal_refs` drops from 1 to 0, and the wrapper struct is **freed**.

5. **Dangling pointer**: The `inlineLiteralCache` still holds a **pointer to freed memory**.

6. **Use-after-free on next iteration**: The next loop iteration accesses the cached string, reading freed memory → **crash**.

**Why it only crashes in loops:**
- First iteration: Works (cached string is valid)
- `rt_concat` frees the cached string wrapper
- Second iteration: Accesses freed memory → SIGABRT

#### Fix Suggestions

**Option A** (Recommended): Retain literal strings before passing to consuming functions

In `VMContext.cpp` when evaluating `ConstStr`, increment the reference count before returning:
```cpp
if (it != vmInstance->inlineLiteralCache.end()) {
    rt_str_retain_maybe(it->second.get());  // ADD: Increment refcount
    s.str = it->second.get();
    return s;
}
```
This requires corresponding release logic elsewhere to avoid leaks.

**Option B**: Make literal strings immortal

Modify `rt_const_cstr` to set a high initial reference count that never reaches zero:
```c
s->literal_refs = UINT_MAX;  // "Immortal" - never freed
```

**Option C**: Use copy semantics for literal strings

Have the VM create a fresh copy of the literal each time instead of returning the cached pointer. This is less efficient but safer.

**Option D**: Change `rt_concat` to not consume when operand is literal

Add a check in `rt_concat` to skip `rt_string_unref` for literal strings:
```c
if (a && a->heap != NULL)  // Only unref heap-backed strings
    rt_string_unref(a);
```

#### Files Involved
- `src/vm/VMContext.cpp` (lines 233-247) - string literal evaluation returns borrowed reference
- `src/vm/VMInit.cpp` (lines 374-381) - string literal cache initialization
- `src/runtime/rt_string_ops.c` (lines 248-264, 332-365) - `rt_string_unref` and `rt_concat`
- `src/runtime/rt_string_encode.c` (lines 98-114) - `rt_const_cstr` wrapper creation

---

### BUG-VL-004: Lambda/higher-order functions cause runtime errors
- **Status**: ✅ FIXED
- **Test**: `tests/comparison/viper/05_functions.viper`
- **Code**: `var add = (a: Integer, b: Integer) => a + b;`
- **Expected**: Lambda should be callable
- **Actual**: `call arg count mismatch: @rt_alloc expects 1 argument but got 2`
- **Severity**: High
- **Fix**: Two changes in `Lowerer_Expr.cpp`: (1) Removed extra `classId` argument from `rt_alloc` calls (lines 2578, 2593). (2) Changed `Value::constInt(0)` to `Value::null()` for null environment pointer to fix pointer type mismatch. Verified: `3 + 5 = 8` output works.

#### Root Cause Analysis

**Incorrect Call Signature for `rt_alloc`**

The lambda lowerer in `Lowerer_Expr.cpp` calls `rt_alloc` with **two arguments** (classId, size), but the runtime function only accepts **one argument** (bytes).

**The Problematic Code (Lowerer_Expr.cpp:2591-2595):**
```cpp
// Allocate closure struct: { ptr funcPtr, ptr envPtr } = 16 bytes
Value closureClassId = Value::constInt(0);
Value closureSizeVal = Value::constInt(16);
Value closurePtr =
    emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {closureClassId, closureSizeVal});
```

This emits IL:
```il
%t0 = call @rt_alloc(0, 16)
```

**But `rt_alloc` is declared as:**
```c
// rt_memory.c:64
void *rt_alloc(int64_t bytes)  // Only ONE parameter!
```

**Same issue occurs for environment allocation (line 2579):**
```cpp
envPtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {classIdVal, envSizeVal});
```

The lowerer appears to have been designed for a two-argument allocation API (perhaps `rt_alloc_object(classId, size)`) that was never implemented or was removed.

#### Fix Suggestion

Remove the unnecessary `classId` argument from both allocation calls in `Lowerer_Expr.cpp`:

**Line 2579:**
```cpp
// Change from:
envPtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {classIdVal, envSizeVal});
// To:
envPtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {envSizeVal});
```

**Lines 2591-2595:**
```cpp
// Change from:
Value closureClassId = Value::constInt(0);
Value closureSizeVal = Value::constInt(16);
Value closurePtr =
    emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {closureClassId, closureSizeVal});
// To:
Value closureSizeVal = Value::constInt(16);
Value closurePtr =
    emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {closureSizeVal});
```

#### Files Involved
- `src/frontends/viperlang/Lowerer_Expr.cpp` (lines 2576-2579, 2591-2595) - lambda closure allocation
- `src/runtime/rt_memory.c` (line 64) - `rt_alloc` only takes one argument

---

### BUG-VL-005: `override` keyword causes parse error
- **Status**: ✅ FIXED
- **Test**: `tests/comparison/viper/08_oop_inheritance.viper`
- **Code**: `override expose func speak() -> String`
- **Expected**: Should allow explicit override declaration
- **Actual**: `error[V2000]: expected field or method declaration`
- **Workaround**: Omit override keyword (methods override implicitly)
- **Severity**: Low
- **Fix**: Modified entity member parsing in `Parser_Decl.cpp` (lines 464-477) to handle `KwOverride` token alongside `KwExpose`/`KwHide`. Modifiers can now appear in any order. The `isOverride` flag is properly set on `MethodDecl`.

#### Root Cause Analysis

**Parser Doesn't Handle `KwOverride` Token**

The infrastructure for `override` exists but isn't wired up in the parser:
- **Lexer**: `KwOverride` token is defined (`Lexer.cpp:274`)
- **AST**: `MethodDecl::isOverride` field exists (`AST_Decl.hpp:226`)
- **Parser**: Does NOT check for `KwOverride` in entity member parsing

**The Parsing Flow (Parser_Decl.cpp:465-501):**
```cpp
// Check for visibility modifiers ONLY
if (check(TokenKind::KwExpose)) {
    visibility = Visibility::Public;
    advance();
}
else if (check(TokenKind::KwHide)) {
    visibility = Visibility::Private;
    advance();
}

if (check(TokenKind::KwFunc)) {
    // Method declaration
    auto method = parseMethodDecl();
    // ...
}
else if (check(TokenKind::Identifier)) {
    // Field declaration
    // ...
}
else {
    error("expected field or method declaration");  // <-- Falls here for 'override'
    advance();
}
```

When the parser sees `override expose func speak()`:
1. Checks for `KwExpose` - NO (current token is `override`)
2. Checks for `KwHide` - NO
3. Checks for `KwFunc` - NO (current token is still `override`)
4. Falls to `else` branch → "expected field or method declaration"

#### Fix Suggestion

Add `KwOverride` handling in the entity member parsing loop (around line 465):

```cpp
bool isOverride = false;
Visibility visibility = Visibility::Implicit;

// Handle modifiers in any order
while (check(TokenKind::KwExpose) || check(TokenKind::KwHide) || check(TokenKind::KwOverride))
{
    if (match(TokenKind::KwExpose))
        visibility = Visibility::Public;
    else if (match(TokenKind::KwHide))
        visibility = Visibility::Private;
    else if (match(TokenKind::KwOverride))
        isOverride = true;
}

if (check(TokenKind::KwFunc)) {
    auto method = parseMethodDecl();
    if (method) {
        auto* m = static_cast<MethodDecl*>(method.get());
        m->visibility = visibility;
        m->isOverride = isOverride;  // ADD: Set the override flag
        entity->members.push_back(std::move(method));
    }
}
```

#### Files Involved
- `src/frontends/viperlang/Parser_Decl.cpp` (lines 465-486) - entity member parsing missing `KwOverride` check
- `src/frontends/viperlang/Token.hpp` (line 206) - `KwOverride` token exists
- `src/frontends/viperlang/AST_Decl.hpp` (line 226) - `MethodDecl::isOverride` field exists

---

### BUG-VL-006: Inherited fields not accessible in child entities
- **Status**: ✅ FIXED
- **Test**: `tests/comparison/viper/08_oop_inheritance.viper`
- **Code**: Child entity trying to access parent's `name` field
- **Expected**: `name = n;` should work in child's init
- **Actual**: `error[V3000]: Undefined identifier: name`
- **Severity**: Critical - inheritance is broken
- **Fix**: Two-part fix: (1) Added inheritance handling in `Sema_Decl.cpp:analyzeEntityDecl` to add parent's fields and methods to child entity's scope. (2) Added inherited field copying in `Lowerer_Decl.cpp:lowerEntityDecl` to copy parent's fields (with correct offsets) to child entity's field list. (3) Added parent method lookup in `Lowerer_Expr.cpp:lowerCall` to walk inheritance chain when resolving method calls.

#### Root Cause Analysis

**Semantic Analysis Ignores Inheritance Completely**

The semantic analyzer (`Sema_Decl.cpp`) does not process the `baseClass` field of `EntityDecl`. Parent entity fields and methods are never added to the child entity's scope.

**The Problem in `analyzeEntityDecl` (Sema_Decl.cpp:276-326):**
```cpp
void Sema::analyzeEntityDecl(EntityDecl &decl)
{
    auto selfType = types::entity(decl.name);
    currentSelfType_ = selfType;

    pushScope();

    // Analyze fields first (adds them to scope)
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Field)
        {
            analyzeFieldDecl(*static_cast<FieldDecl *>(member.get()), selfType);
        }
    }
    // ... analyzes ONLY this entity's members, never touches baseClass!
}
```

The `decl.baseClass` field is **never read** in semantic analysis:
- `baseClass` is stored in the AST by the parser
- Only the lowerer reads `baseClass` (at `Lowerer_Decl.cpp:345`)
- Semantic analysis skips inheritance entirely

**What's Missing:**
1. Lookup parent entity when `decl.baseClass` is not empty
2. Add parent's fields to child entity scope
3. Add parent's methods to child entity scope
4. Validate parent entity exists

#### Fix Suggestion

Add inheritance handling at the start of `analyzeEntityDecl` (around line 281):

```cpp
void Sema::analyzeEntityDecl(EntityDecl &decl)
{
    auto selfType = types::entity(decl.name);
    currentSelfType_ = selfType;

    pushScope();

    // ADD: Handle inheritance - add parent's members to scope
    if (!decl.baseClass.empty())
    {
        auto parentIt = entityDecls_.find(decl.baseClass);
        if (parentIt == entityDecls_.end())
        {
            error(decl.loc, "Unknown base class: " + decl.baseClass);
        }
        else
        {
            EntityDecl *parent = parentIt->second;
            // Add parent's fields to this entity's scope
            for (auto &member : parent->members)
            {
                if (member->kind == DeclKind::Field)
                {
                    auto *field = static_cast<FieldDecl *>(member.get());
                    std::string fieldKey = parent->name + "." + field->name;
                    auto typeIt = fieldTypes_.find(fieldKey);
                    if (typeIt != fieldTypes_.end())
                    {
                        Symbol sym;
                        sym.kind = Symbol::Kind::Field;
                        sym.name = field->name;
                        sym.type = typeIt->second;
                        defineSymbol(field->name, sym);
                    }
                }
            }
            // Similarly add parent's methods...
        }
    }

    // Existing field analysis...
    for (auto &member : decl.members) { ... }
}
```

#### Files Involved
- `src/frontends/viperlang/Sema_Decl.cpp` (lines 276-326) - `analyzeEntityDecl` ignores `baseClass`
- `src/frontends/viperlang/AST_Decl.hpp` (line 349) - `EntityDecl::baseClass` field exists but unused in sema

---

### BUG-VL-007: Polymorphism not working (child to parent assignment)
- **Status**: ✅ FIXED
- **Test**: `tests/comparison/viper/08_oop_inheritance.viper`
- **Code**: `var animal: Animal = dog;`
- **Expected**: Should allow assigning Dog to Animal variable
- **Actual**: `error[V3000]: Type mismatch: expected Animal, got Dog`
- **Severity**: Critical - polymorphism is broken
- **Fix**: Two-part fix: (1) Added entity inheritance tracking in `Types.cpp` with `g_entity_parents` map and helper functions `registerEntityInheritance()`, `isSubclassOf()`, `clearEntityInheritance()`. (2) Added entity inheritance check in `ViperType::isAssignableFrom()` to allow derived entities to be assigned to base type variables. Registration happens in `Sema_Decl.cpp:analyzeEntityDecl`.
- **Note**: Virtual dispatch (calling overridden methods based on runtime type) is not yet implemented - methods are resolved based on declared type. This would require vtable generation.

#### Root Cause Analysis

**Type System Doesn't Check Entity Inheritance**

The `isAssignableFrom` function in `Types.cpp` has no handling for entity subtype relationships. It only checks for exact type matches for entities.

**The Missing Logic (Types.cpp:74-141):**
```cpp
bool ViperType::isAssignableFrom(const ViperType &source) const
{
    // Exact match
    if (equals(source))
        return true;

    // ... handles Any, Never, Unknown, Optional, Numeric, Interface ...

    // MISSING: Entity inheritance check!
    // Should have something like:
    // if (kind == TypeKindSem::Entity && source.kind == TypeKindSem::Entity)
    //     return types::isSubtypeOf(source.name, name);

    return false;  // Dog→Animal falls through to here
}
```

The function checks interface implementation (line 117-119):
```cpp
if (kind == TypeKindSem::Interface &&
    (source.kind == TypeKindSem::Entity || source.kind == TypeKindSem::Value))
    return types::implementsInterface(source.name, name);
```

But there's **no equivalent check** for entity inheritance.

**Related Issue**: Even if we added the check, `types::isSubtypeOf` doesn't exist because inheritance info isn't tracked in semantic analysis (see BUG-VL-006).

#### Fix Suggestion

1. **First, fix BUG-VL-006** to track parent-child relationships in semantic analysis

2. **Then add a helper function** in `Types.cpp`:
```cpp
bool types::extendsEntity(const std::string &child, const std::string &parent)
{
    // Walk the inheritance chain
    std::string current = child;
    while (!current.empty())
    {
        if (current == parent)
            return true;
        // Look up current entity's baseClass
        auto it = entityDecls_.find(current);
        if (it == entityDecls_.end())
            break;
        current = it->second->baseClass;
    }
    return false;
}
```

3. **Add entity inheritance check** in `isAssignableFrom`:
```cpp
// Entity subtype polymorphism
if (kind == TypeKindSem::Entity && source.kind == TypeKindSem::Entity)
    return types::extendsEntity(source.name, name);
```

#### Files Involved
- `src/frontends/viperlang/Types.cpp` (lines 74-141) - `isAssignableFrom` missing entity inheritance check
- `src/frontends/viperlang/Sema.cpp` - needs to track inheritance relationships

---

### BUG-VL-008: Entity field ordering bug
- **Status**: ✅ FIXED
- **Test**: `tests/comparison/viper/08_oop_inheritance.viper`
- **Observed**: When entity has multiple fields, values appear swapped
- **Output**: `Dog name: Golden Retriever` and `Dog breed: Buddy` (swapped)
- **Severity**: High
- **Fix**: Modified `lowerNew` in `Lowerer_Expr.cpp` to call the entity's `init` method instead of doing inline field initialization. This ensures fields are assigned in the order specified by `init()`, not field declaration order.

#### Root Cause Analysis

**`new` Expression Uses Wrong Field Assignment Order**

The `lowerNew` function in `Lowerer_Expr.cpp` does inline field initialization by matching constructor arguments to field declaration order, instead of calling the `init` method.

**The Problematic Code (Lowerer_Expr.cpp:1760-1781):**
```cpp
// Store each argument into the corresponding field
for (size_t i = 0; i < argValues.size() && i < info.fields.size(); ++i)
{
    const FieldLayout &field = info.fields[i];
    // GEP to field offset and store argValues[i]
}
```

**Example:**
```viper
entity Dog {
    expose String breed;      // field[0] at offset 8
    expose String dogName;    // field[1] at offset 16

    expose func init(n: String, b: String) {
        dogName = n;  // param 0 should go to field[1]
        breed = b;    // param 1 should go to field[0]
    }
}

// Calling: new Dog("Buddy", "Golden Retriever")
```

**What happens:**
- `argValues[0]` ("Buddy") → `info.fields[0]` (breed @ offset 8) ❌
- `argValues[1]` ("Golden Retriever") → `info.fields[1]` (dogName @ offset 16) ❌

**What should happen (if init was called):**
- param `n` ("Buddy") → dogName (field[1] @ offset 16) ✓
- param `b` ("Golden Retriever") → breed (field[0] @ offset 8) ✓

The inline initialization assumes parameters map positionally to fields, but the `init` body can assign parameters to fields in any order.

#### Fix Suggestion

Change `lowerNew` to actually call the `init` method instead of doing inline initialization:

```cpp
LowerResult Lowerer::lowerNew(NewExpr *expr)
{
    // ... type resolution and allocation ...

    // Allocate the object
    Value ptr = emitCallRet(Type(Type::Kind::Ptr),
                            "rt_obj_new_i64",
                            {Value::constInt(info.classId),
                             Value::constInt(info.totalSize)});

    // CHANGED: Call the init method instead of inline initialization
    std::string initName = typeName + ".init";
    std::vector<Value> initArgs;
    initArgs.push_back(ptr);  // self
    for (auto &arg : expr->args)
    {
        auto result = lowerExpr(arg.value.get());
        initArgs.push_back(result.value);
    }
    emitCall(initName, initArgs);

    return {ptr, Type(Type::Kind::Ptr)};
}
```

#### Files Involved
- `src/frontends/viperlang/Lowerer_Expr.cpp` (lines 1760-1781) - `lowerNew` uses inline field init
- The generated IL shows direct field stores instead of calling the entity's `init` method

---

### BUG-VL-009: Generics not implemented
- **Test**: `tests/comparison/viper/10_oop_generics.viper`
- **Code**: `entity Box[T] { expose T value; }`
- **Expected**: Generic type parameter T should be recognized
- **Actual**: `error[V3000]: Unknown type: T` for all uses of type parameter
- **Severity**: High - generics completely non-functional

#### Root Cause Analysis

**Generic Type Parameters Not Registered in Type System**

The semantic analyzer (`Sema.cpp`) parses generic parameter names from `EntityDecl::genericParams` but never registers them as valid types within the entity's scope.

**Type Resolution Flow (Sema.cpp:268-297):**
```cpp
TypeRef Sema::resolveNamedType(const std::string &name) const
{
    // Built-in types
    if (name == "Integer" ...) return types::integer();
    if (name == "String" ...) return types::string();
    // ... other built-ins ...

    // Look up in registry
    auto it = typeRegistry_.find(name);
    if (it != typeRegistry_.end())
        return it->second;

    return nullptr;  // <-- "T" reaches here → "Unknown type: T"
}
```

**What's Missing:**

1. When analyzing `entity Box[T]`, the type parameter `T` should be added to a scope-local type registry or symbol table
2. The type registry only contains concrete types (entities, interfaces), not generic type parameters
3. No mechanism to substitute `T` with actual types during instantiation (e.g., `Box[Integer]`)

**The EntityDecl AST stores generic params but they're unused:**
```cpp
// AST_Decl.hpp:346
std::vector<std::string> genericParams;  // Contains ["T"] for Box[T]
```

**analyzeEntityDecl ignores genericParams:**
```cpp
void Sema::analyzeEntityDecl(EntityDecl &decl)
{
    // decl.genericParams is never read!
    // No typeRegistry_ entry for "T"
}
```

#### Fix Suggestion (Conceptual)

Implementing generics requires significant work:

1. **Register type parameters in scope** when analyzing generic entities:
```cpp
void Sema::analyzeEntityDecl(EntityDecl &decl)
{
    pushScope();

    // Register generic type parameters as placeholder types
    for (const auto &param : decl.genericParams)
    {
        typeRegistry_[param] = types::typeParam(param);
    }
    // ... rest of analysis ...
}
```

2. **Create a TypeParam type kind** for placeholders:
```cpp
TypeRef types::typeParam(const std::string &name)
{
    return std::make_shared<ViperType>(TypeKindSem::TypeParam, name);
}
```

3. **Implement type substitution** during generic instantiation:
   - When `Box[Integer]` is used, substitute all occurrences of `T` with `Integer`
   - This affects field types, method signatures, and type checking

4. **Generate monomorphized code** (one version per instantiation) or implement runtime generics

#### Files Involved
- `src/frontends/viperlang/Sema.cpp` (lines 268-297) - `resolveNamedType` doesn't handle type parameters
- `src/frontends/viperlang/Sema_Decl.cpp` - `analyzeEntityDecl` ignores `genericParams`
- `src/frontends/viperlang/Types.hpp` - no `TypeKindSem::TypeParam` for generic parameters

---

### BUG-VL-010: Interface method calls return wrong type
- **Test**: `/tmp/test_iface_poly.viper`
- **Code**: `var shape: IShape = c; shape.getName();`
- **Expected**: Method call through interface should return correct type
- **Actual**: `error: store void: instruction type must be non-void` (generates broken IL)
- **Note**: Assignment to interface variable works, calling methods doesn't
- **Severity**: High - interface polymorphism broken

#### Root Cause Analysis

**Lowerer Doesn't Handle Interface Type Method Calls**

The `lowerCall` function in `Lowerer_Expr.cpp` handles method calls for value types, entity types, and collection types, but has **no handling for interface types**.

**The Call Dispatch Logic (Lowerer_Expr.cpp:888-912):**
```cpp
TypeRef baseType = sema_.typeOf(fieldExpr->base.get());
if (baseType)
{
    std::string typeName = baseType->name;

    // Check value type methods
    auto it = valueTypes_.find(typeName);
    if (it != valueTypes_.end()) { /* ... */ }

    // Check entity type methods
    auto entityIt = entityTypes_.find(typeName);
    if (entityIt != entityTypes_.end()) { /* ... */ }

    // Handle module-qualified calls
    if (baseType->kind == TypeKindSem::Module) { /* ... */ }

    // Check for List/Set/Map methods
    if (baseType->kind == TypeKindSem::List) { /* ... */ }
    // ...

    // MISSING: No handling for TypeKindSem::Interface!
}
```

When `shape.getName()` is called where `shape: IShape`:
1. `baseType->kind == TypeKindSem::Interface`
2. `typeName = "IShape"` (not in `entityTypes_` or `valueTypes_`)
3. Falls through all checks
4. Eventually generates broken IL: `call.indirect 0` with no target

**The Generated IL:**
```il
%t7 = load ptr, %t6
call.indirect 0               ; Wrong! No vtable lookup, no method target
%t8 = alloca 8
store void, %t8, 0            ; Error: can't store void!
```

**What's Needed (Virtual Dispatch):**
1. Load the vtable pointer from the object (usually at offset 0)
2. Find the method's slot index in the interface vtable
3. Load the method pointer from the vtable
4. Call through the method pointer with the object as first argument

#### Fix Suggestion

Add interface method dispatch handling in `lowerCall`:

```cpp
// After entity type check
if (baseType->kind == TypeKindSem::Interface)
{
    auto ifaceIt = interfaceTypes_.find(typeName);
    if (ifaceIt != interfaceTypes_.end())
    {
        // Find the method index in the interface
        size_t methodIdx = 0;
        TypeRef methodType = nullptr;
        for (const auto &m : ifaceIt->second.methods)
        {
            if (m.name == fieldExpr->field)
            {
                methodType = m.type;
                break;
            }
            methodIdx++;
        }

        auto baseResult = lowerExpr(fieldExpr->base.get());

        // Load vtable pointer from object (offset 0)
        Value vtablePtr = emitLoad(emitGEP(baseResult.value, 0), Type::Ptr);

        // Load method pointer from vtable
        Value methodPtr = emitLoad(
            emitGEP(vtablePtr, methodIdx * 8), Type::Ptr);

        // Build args: [self, ...args]
        std::vector<Value> args = {baseResult.value};
        for (auto &arg : expr->args)
        {
            auto r = lowerExpr(arg.value.get());
            args.push_back(r.value);
        }

        // Emit indirect call
        Type retType = mapType(methodType->returnType());
        Value result = emitIndirectCall(methodPtr, retType, args);
        return {result, retType};
    }
}
```

**Note**: This also requires implementing vtable generation for entities that implement interfaces, which is a larger architectural change.

#### Files Involved
- `src/frontends/viperlang/Lowerer_Expr.cpp` (lines 888-1200) - `lowerCall` missing interface dispatch
- `src/frontends/viperlang/Lowerer_Decl.cpp` (lines 390-410) - `lowerInterfaceDecl` stores interface info but no vtable gen
- `src/frontends/viperlang/Lowerer.hpp` - `InterfaceTypeInfo` exists but not used for dispatch

---

## BASIC Bugs

### BUG-BAS-001: Namespace function calls fail at codegen
- **Status**: ✅ FIXED
- **Test**: `tests/comparison/basic/15_modules.bas`
- **Code**: `PRINT MyModule.Helper$()`
- **Expected**: Should call function in namespace
- **Actual**: `error: main:entry: call: unknown callee @MYMODULE.HELPER$`
- **Severity**: High - namespace functions not callable
- **Fix**: Added `CanonicalizeQualifiedName()` function in `Lowerer_Procedure_Signatures.cpp` and `StripTypeSuffix()` in `IdentifierUtil.hpp` to properly handle BASIC type suffixes (`$`, `%`, `#`, `!`, `&`) during qualified name canonicalization.

#### Root Cause Analysis

The bug is in how qualified namespace function calls with type suffixes (like `$`) are resolved:

1. **Function declaration**: `Helper$` in namespace `MyModule` gets `qualifiedName = "mymodule.helper"`
   - The `$` suffix is stripped via `stripSuffix()` in `CollectProcs.cpp`
   - Name is canonicalized to lowercase

2. **Alias registration fails**: In `registerSig` (`Lowerer_Procedure_Signatures.cpp:91-96`):
   - `CanonicalizeIdent("Helper$")` returns **empty string** because `$` is not alphanumeric
   - Since the canonical form is empty, no alias is registered in `procNameAliases`

3. **Call site fallback**: When calling `MyModule.Helper$()`:
   - Parser creates `calleeQualified = ["MyModule", "Helper$"]`
   - `CanonicalizeQualified(...)` returns empty (because `$` fails in `CanonicalizeIdent`)
   - Code falls back to using raw `expr.callee = "MyModule.Helper$"`

4. **Alias lookup fails**: `resolveCalleeName("MyModule.Helper$")` finds no alias, returns unchanged

5. **Name mismatch**: IL emits call to `@MyModule.Helper$` but function was declared as `@mymodule.helper`

#### Fix Suggestions

Option A: In `registerSig`, strip suffixes before canonicalizing for alias registration:
```cpp
std::string canon = CanonicalizeIdent(stripSuffix(unqual));
```

Option B: In `CanonicalizeIdent`, handle BASIC type suffixes (`$`, `%`, `#`, `!`, `&`) by stripping them before validation

#### Files Involved
- `src/frontends/basic/Lowerer_Procedure_Signatures.cpp` (lines 87-97) - alias registration
- `src/frontends/basic/IdentifierUtil.hpp` - `CanonicalizeIdent`, `CanonicalizeQualified`
- `src/frontends/basic/passes/CollectProcs.cpp` - qualified name generation

---

### BUG-BAS-002: Interface with constructor parameters causes codegen error
- **Status**: ✅ FIXED
- **Test**: `tests/comparison/basic/09_oop_interfaces.bas`
- **Code**: `CLASS Circle IMPLEMENTS IShape` with `PUBLIC SUB NEW(r AS DOUBLE)`
- **Expected**: Constructor with parameters should work
- **Actual**: `error: CIRCLE.__ctor: store %t9 %t7: operand type mismatch: operand 1 must be f64`
- **Severity**: High - prevents using interfaces with parameterized constructors
- **Fix**: Multi-part fix: (1) Added `currentProcParamNames_` tracking in `Lowerer.hpp`. (2) Register parameters with types BEFORE `collectVars` in `Lower_OOP_Emit.cpp` and `Lowerer_Procedure_Emit.cpp`. (3) Skip `moduleObjectClass_` cache for procedure parameters in `getSlotType()`. (4) Use `sym->type` for parameters when `isProcParam(name)` is true.

#### Root Cause Analysis

The bug is in parameter type tracking for class constructors when the class implements an interface:

1. **Parameter storage is correct**: In `emitParamInit`, parameter `R` is correctly stored as `f64`:
   ```il
   store f64, %t5, %R    ; Correct
   ```

2. **Parameter load uses wrong type**: When the parameter is loaded via `lowerVarExpr`:
   ```il
   %t7 = load ptr, %t5   ; BUG: Should be "load f64, %t5"
   ```

3. **Type lookup returns ptr**: `getSlotType("R")` returns `Type::Ptr` instead of `Type::F64`

4. **Root cause**: In `getSlotType` (`Lowerer_Procedure_Variables.cpp:299-307`):
   ```cpp
   if (sym && sym->isObject && !sym->objectClass.empty() && !isGenericObject(sym->objectClass))
   {
       info.type = Type(Type::Kind::Ptr);  // Returns ptr!
       return info;
   }
   ```
   The symbol for parameter `R` has `isObject = true` when it shouldn't.

5. **Flag not cleared**: `setSymbolType` (called for non-object parameters) doesn't clear `isObject`:
   ```cpp
   void Lowerer::setSymbolType(std::string_view name, AstType type)
   {
       auto &info = ensureSymbol(name);
       info.type = type;
       info.hasType = true;
       info.isBoolean = !info.isArray && type == AstType::Bool;
       // MISSING: info.isObject = false;
       // MISSING: info.objectClass.clear();
   }
   ```

#### Fix Suggestion

In `setSymbolType` (`Lowerer_Procedure_Context.cpp:102-108`), explicitly clear object flags:
```cpp
void Lowerer::setSymbolType(std::string_view name, AstType type)
{
    auto &info = ensureSymbol(name);
    info.type = type;
    info.hasType = true;
    info.isBoolean = !info.isArray && type == AstType::Bool;
    info.isObject = false;       // ADD: Clear object flag
    info.objectClass.clear();    // ADD: Clear object class
}
```

#### Files Involved
- `src/frontends/basic/Lowerer_Procedure_Context.cpp` (lines 102-108) - `setSymbolType`
- `src/frontends/basic/Lowerer_Procedure_Variables.cpp` (lines 290-370) - `getSlotType`
- `src/frontends/basic/lower/oop/Lower_OOP_RuntimeHelpers.cpp` - `emitParamInit`

---

## Notes

### ViperLang Syntax Discoveries
- Module declaration required: `module Test;`
- Entity field syntax: `expose Type name;` (not `expose name: Type`)
- Constructor: `expose func init()` method (not explicit `new()`)
- Methods override implicitly (no override keyword needed)
- Self reference: fields accessed directly by name, not `self.name`

### BASIC Syntax Discoveries
- Inheritance uses colon: `CLASS Child : Parent`
- Interface implementation: `CLASS MyClass IMPLEMENTS IInterface`
- Members require PUBLIC/PRIVATE visibility
- Constructor: `PUBLIC SUB NEW()`
