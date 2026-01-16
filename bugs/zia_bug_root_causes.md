# Zia Bug Root Cause Analysis

**Date:** 2026-01-16
**Analyzed by:** Claude (Automated)

This document provides detailed root cause analysis for all bugs identified during the comprehensive Zia language audit.

---

## Summary Table

| Bug ID | Severity | Root Cause Type | Fix Complexity |
|--------|----------|-----------------|----------------|
| BUG-001 | ~~Medium~~ | **NOT A BUG** - Works correctly | N/A |
| BUG-002 | High | Parser bug - StringMid token not triggered | Medium |
| BUG-003 | Medium | Lexer design - two-stage parsing issue | Low |
| BUG-004 | Medium | Lexer design - same as BUG-003 | Low |
| BUG-005 | ~~High~~ | **NOT A BUG** - Misunderstanding of Flip vs Not | N/A |
| BUG-006 | Critical | Compiler bug - SimplifyCFG parameter sync | High |
| BUG-007 | Medium | Missing feature - for-in collection support | Medium |
| BUG-008 | Critical | Missing feature - auto-boxing in runtime calls | Medium |
| BUG-009 | ~~High~~ | **NOT A BUG** - Wrong API usage | N/A |
| BUG-010 | Critical | Compiler bug - value type init not called | Medium |
| BUG-011 | Critical | Missing feature - generics not implemented | High |
| BUG-012 | Medium | Sema bug - runtime class property access not resolved | Medium |

---

## Detailed Analysis

---

### BUG-001: `\$` Escape Sequence - **NOT A BUG**

**Original Report:** `\$` escape sequence not supported in strings

**Finding:** This was a false positive. The `\$` escape sequence **works correctly**.

**Evidence:**
```zia
var s: String = "Price: \$5";  // Works - outputs "Price: $5"
var s: String = "Price: \$${x}";  // Works - outputs "Price: $5" with x=5
```

**Code Location:** `/Users/stephen/git/viper/src/frontends/zia/Lexer.cpp:654`
```cpp
case '$':
    return '$';  // Dollar sign escape IS implemented
```

**Conclusion:** No fix needed.

---

### BUG-002: Multiple String Interpolations Fail

**Symptom:** `"${a} + ${b}"` produces "expected end of interpolated string" error

**Root Cause:** The lexer's `lexInterpolatedStringContinuation()` function fails to properly transition from one interpolation to the next when there's text between them.

**Code Location:** `/Users/stephen/git/viper/src/frontends/zia/Lexer.cpp:857-924`

**The Problem:**
1. When the first `}` closes the first interpolation, `lexInterpolatedStringContinuation()` is called
2. This function should produce a `StringMid` token when it encounters another `${`
3. However, the logic at lines 875-883 is not being reached correctly when there's intervening text

**Expected Flow for `"${a} + ${b}"`:
1. `StringStart` with text "" (empty before first `${`)
2. Expression `a`
3. `StringMid` with text " + " ← **This is where it fails**
4. Expression `b`
5. `StringEnd` with text "" (empty after `}`)

**Actual Flow:**
1. `StringStart` with text ""
2. Expression `a`
3. **Error:** "expected end of interpolated string"

**Potential Fix:**
In `lexInterpolatedStringContinuation()`, ensure the check for `${` (lines 875-883) is evaluated correctly after processing the closing `}` and any subsequent characters.

**Fix Complexity:** Medium - requires careful lexer state management

---

### BUG-003/004: Min i64 Literal Overflow

**Symptom:** `-9223372036854775808` and `0x8000000000000000` cause "out of range" errors

**Root Cause:** Two-stage parsing design issue

**The Problem:**
1. The lexer parses numeric literals as positive values first
2. The parser then applies unary negation as a separate operation
3. `9223372036854775808` exceeds `INT64_MAX` (9223372036854775807)
4. The lexer rejects it before the parser can apply negation

**Code Locations:**
- `/Users/stephen/git/viper/src/frontends/common/NumberParsing.hpp:74-84` - Overflow check
- `/Users/stephen/git/viper/src/frontends/zia/Lexer.cpp:605-612` - Error reporting

**Affected Code:**
```cpp
// NumberParsing.hpp:76-79
if (parseResult.ec == std::errc::result_out_of_range)
{
    result.overflow = true;
    result.valid = false;
}
```

**Potential Fixes:**
1. **Quick fix:** Parse unsigned then cast: Use `uint64_t` for parsing, allowing `9223372036854775808` to be stored, then cast to `int64_t` after negation
2. **Better fix:** Special-case min i64 in the parser by recognizing `-` followed by `9223372036854775808` as a single token
3. **Best fix:** Parse the literal as part of the unary expression, not separately

**Fix Complexity:** Low

---

### BUG-005: Bits.Flip Returns Wrong Values - **NOT A BUG**

**Original Report:** `Bits.Flip(0)` returns 0 instead of -1

**Finding:** This is **correct behavior**. `Bits.Flip` is **bit reversal**, not bitwise NOT.

**The Difference:**
- `Bits.Not(x)` = Bitwise complement (flip 0s to 1s and vice versa)
- `Bits.Flip(x)` = Bit reversal (reverse bit order: bit 0 → bit 63, bit 1 → bit 62, etc.)

**Correct Results:**
| Function | Input | Output | Explanation |
|----------|-------|--------|-------------|
| `Bits.Not(0)` | 0 | -1 | All bits flipped: 0→1 |
| `Bits.Flip(0)` | 0 | 0 | Reversing zeros = zeros |
| `Bits.Flip(1)` | 1 | -9223372036854775808 | Bit 0 moves to bit 63 |

**Code Location:** `/Users/stephen/git/viper/src/runtime/rt_bits.c:681-698`

**Conclusion:** No fix needed. Documentation could clarify the difference between Flip and Not.

---

### BUG-006: Match Binding Patterns Crash Compiler

**Symptom:** `match x { n => { result = n + 1; } }` crashes with "SimplifyCFG verification failed"

**Root Cause:** Bug in SimplifyCFG parameter canonicalization

**The Problem:**
The `shrinkParamsEqualAcrossPreds()` function in `ParamCanonicalization.cpp` removes block parameters but fails to synchronize all predecessor branch arguments correctly. This creates a mismatch where:
- Block has N parameters
- Some predecessor branches have N+1 arguments

**Code Locations:**
- `/Users/stephen/git/viper/src/il/transforms/SimplifyCFG.cpp:74` - Assertion failure
- `/Users/stephen/git/viper/src/il/transforms/ParamCanonicalization.cpp:195-199` - Incomplete argument erasure
- `/Users/stephen/git/viper/src/il/verify/BranchVerifier.cpp:69` - Verification that catches the bug

**Why Match Bindings Trigger This:**
1. Match arms with bindings generate blocks with parameters (e.g., `%n`)
2. SimplifyCFG runs 7 transformations in a loop, including parameter canonicalization
3. When parameters are pruned, some predecessor branches still have the old argument count
4. Verification fails: "branch arg count mismatch"

**The Vulnerable Code:**
```cpp
// ParamCanonicalization.cpp:195-199
auto &args = term->brArgs[edgeIdx];
if (paramIdx < args.size())
{
    args.erase(args.begin() + static_cast<std::ptrdiff_t>(paramIdx));
}
```
This erases arguments but doesn't handle all edge cases (multiple edges to same block, etc.)

**Potential Fix:**
Ensure atomic synchronization of parameter removal across ALL predecessors, possibly by:
1. Building a complete list of all predecessor edges first
2. Removing parameters and arguments in a single atomic operation
3. Adding validation before committing changes

**Fix Complexity:** High - core compiler infrastructure

---

### BUG-007: For-In Only Works with Ranges

**Symptom:** `for item in list` fails with "Expression is not iterable" when list is a runtime collection

**Root Cause:** Early return in lowerer bypasses collection handling

**The Problem:**
The `lowerForInStmt()` function has an early `dynamic_cast<RangeExpr*>` check that returns immediately for ranges. The code for List and Map iteration (lines 436-588) exists but is never reached for runtime collections because the semantic analyzer marks them differently.

**Code Locations:**
- `/Users/stephen/git/viper/src/frontends/zia/Lowerer_Stmt.cpp:303-373` - Range-only path with early return
- `/Users/stephen/git/viper/src/frontends/zia/Lowerer_Stmt.cpp:436-588` - List/Map handling (unreachable for runtime collections)
- `/Users/stephen/git/viper/src/frontends/zia/Sema_Stmt.cpp:180-270` - Semantic analysis recognizes List/Map but lowerer doesn't use it

**The Issue:**
```cpp
// Lowerer_Stmt.cpp:304 - This early return bypasses everything else
auto *rangeExpr = dynamic_cast<RangeExpr *>(stmt->iterable.get());
if (rangeExpr) {
    // Handle range iteration
    return;  // <-- EARLY RETURN
}
```

**Potential Fix:**
1. Remove the early return after range handling
2. Add a fallthrough case that uses type information from semantic analysis
3. When iterable type is `List`, use the existing List iteration code at lines 436-499

**Fix Complexity:** Medium

---

### BUG-008: Adding Unboxed Primitives to Collections Crashes

**Symptom:** `List.Add(list, 100)` crashes compiler; must use `List.Add(list, Viper.Box.I64(100))`

**Root Cause:** Missing auto-boxing in direct runtime function calls

**The Problem:**
Two different code paths handle collection method calls:

1. **Method call syntax** (`list.Add(100)`) → Goes through `lowerListMethodCall()` which **auto-boxes** arguments
2. **Direct call syntax** (`Viper.Collections.List.Add(list, 100)`) → Goes through generic runtime call path which does **NOT** auto-box

**Code Locations:**
- `/Users/stephen/git/viper/src/frontends/zia/Lowerer_Expr_Call.cpp:124-127` - Auto-boxing in method calls
- `/Users/stephen/git/viper/src/frontends/zia/Lowerer_Expr_Call.cpp:725-735` - NO boxing in direct calls

**The Difference:**
```cpp
// Path 1 (method call) - BOXES arguments
args.push_back(emitBox(result.value, result.type));

// Path 2 (direct call) - Does NOT box
args.push_back(argValue);  // Raw value passed
```

**Why It Crashes:**
Runtime function `Viper.Collections.List.Add` expects `void(ptr, ptr)` - two pointers. When passed an unboxed `i64`, the runtime tries to dereference it as a pointer → crash.

**Potential Fixes:**
1. **Auto-box in direct calls:** Check expected parameter types from runtime.def and box primitives automatically
2. **Better error message:** Type check arguments against runtime function signatures and report mismatches
3. **Semantic validation:** Reject calls with wrong argument types at semantic analysis phase

**Fix Complexity:** Medium

---

### BUG-009: Seq.New(size) Crashes - **NOT A BUG**

**Original Report:** `Seq.New(5)` crashes compiler

**Finding:** This is **API misuse**, not a bug.

**The Correct API:**
- `Seq.New()` - Creates empty sequence (0 arguments)
- `Seq.WithCapacity(n)` - Creates sequence with initial capacity (1 argument)

**Code Location:** `/Users/stephen/git/viper/src/il/runtime/runtime.def:256,266`
```
RT_FUNC(SeqNew,          rt_seq_new,           "Viper.Collections.Seq.New",          "obj()")
RT_FUNC(SeqWithCapacity, rt_seq_with_capacity, "Viper.Collections.Seq.WithCapacity", "obj(i64)")
```

**Why It Crashes:**
The method resolver uses arity-based lookup. `Seq.New(5)` looks for `Seq.New#1` (arity 1), which doesn't exist. Only `Seq.New#0` is defined.

**Conclusion:** No fix needed. Users should use `Seq.WithCapacity(5)` for sized allocation.

---

### BUG-010: Value Types Crash at Runtime

**Symptom:** `value Point2D { ... } var p = new Point2D(1.0, 2.0)` crashes with segfault

**Root Cause:** Value type constructor doesn't call `init` method

**The Problem:**
Entity types correctly call the `init` method after allocation. Value types do NOT - they only store arguments directly to field offsets, bypassing any custom initialization logic.

**Code Locations:**
- `/Users/stephen/git/viper/src/frontends/zia/Lowerer_Expr_Call.cpp:377-428` - Value type construction (BROKEN)
- `/Users/stephen/git/viper/src/frontends/zia/Lowerer_Expr_Call.cpp:430-500` - Entity type construction (CORRECT)

**The Missing Code:**
```cpp
// Entity construction (correct) - line 467-479
auto initIt = info.methodMap.find("init");
if (initIt != info.methodMap.end())
{
    std::string initName = typeName + ".init";
    emitCall(initName, initArgs);  // Calls init method
}

// Value construction (broken) - line 377-428
// NO init method lookup or call!
// Just stores arguments directly to fields
```

**Why It Crashes:**
1. Stack space allocated via `Alloca`
2. Arguments stored directly to field offsets
3. `init` method body never executes
4. If `init` had important initialization logic, object is in invalid state
5. When stack frame ends, pointer becomes invalid → segfault on later access

**Potential Fix:**
Mirror entity construction logic in `lowerValueTypeConstruction()`:
1. Check for `init` method in `info.methodMap`
2. If found, call it with self pointer and arguments
3. If not found, fall back to direct field initialization

**Fix Complexity:** Medium

---

### BUG-011: Generics Not Implemented

**Symptom:** `entity Box[T] { ... }` produces "Unknown type: T" error

**Root Cause:** Partial implementation - parsing exists, resolution doesn't

**What Exists:**
- Generic parameter parsing in declarations (`FunctionDecl::genericParams`)
- `TypeParam` type kind in the type system
- Generic type annotation parsing (`GenericType` AST node)
- Built-in generic handling for `List[T]`, `Map[K,V]`

**What's Missing:**
1. **Generic parameter scope:** When analyzing `func identity[T](x: T)`, no scope tracks that `T` is a valid type
2. **Type parameter substitution:** No mechanism to replace `T` with concrete types during instantiation
3. **Generic call syntax:** `CallExpr` has no field for type arguments like `identity[Integer](42)`
4. **Type resolution for params:** `resolveNamedType("T")` searches type registry, where `T` isn't registered

**Code Locations:**
- `/Users/stephen/git/viper/src/frontends/zia/Sema.cpp:309-311` - Type registry lookup (fails for T)
- `/Users/stephen/git/viper/src/frontends/zia/Sema.cpp:329-427` - Type resolution (no generic param handling)

**The Failure Point:**
```cpp
// Sema.cpp:309-311
auto it = typeRegistry_.find(name);  // name = "T"
if (it != typeRegistry_.end())
    return it->second;
// T is not in registry → returns nullptr → "Unknown type: T"
```

**Potential Fix (High Level):**
1. Add `genericParameterScope_` to Sema class
2. Push type parameters when entering generic function/entity
3. Check generic scope in `resolveNamedType()` before registry lookup
4. Add `typeArgs` field to `CallExpr` for call-site type arguments
5. Implement type substitution when instantiating generics

**Fix Complexity:** High - significant new infrastructure required

---

### BUG-012: Runtime Class Property Access Not Resolved

**Symptom:** `Viper.Math.Pi` fails with "Undefined identifier: Viper" but `Viper.Math.get_Pi()` works

**Root Cause:** Semantic analyzer doesn't resolve property access syntax to getter functions for runtime classes

**The Problem:**
Runtime class properties are registered as getter functions with `get_` prefix (e.g., `Viper.Math.get_Pi`), but when code uses property access syntax (`Viper.Math.Pi`), the semantic analyzer doesn't synthesize the getter call.

**Evidence:**
```zia
// This FAILS:
var pi: Number = Viper.Math.Pi;  // Error: Undefined identifier: Viper

// This WORKS:
var pi: Number = Viper.Math.get_Pi();  // OK - explicit getter call

// Function calls work fine:
var x: Number = Viper.Math.Abs(-5.0);  // OK
```

**Code Locations:**
- `/Users/stephen/git/viper/src/frontends/zia/Sema_Expr.cpp` - Field expression analysis
- `/Users/stephen/git/viper/build/generated/il/runtime/ZiaRuntimeExterns.inc` - Property getters registered as `get_*`

**Runtime Registration (from ZiaRuntimeExterns.inc):**
```cpp
defineExternFunction("Viper.Math.get_Pi", types::number());
defineExternFunction("Viper.Math.get_E", types::number());
defineExternFunction("Viper.Math.get_Tau", types::number());
```

**The Missing Logic:**
When analyzing a field expression like `Viper.Math.Pi`:
1. The base `Viper.Math` is resolved as a runtime class type
2. The field `Pi` is looked up directly on the class
3. Since there's no `Pi` field/method, it fails
4. **Missing:** Check for a `get_Pi` getter function and synthesize the call

**Affected Runtime Classes:**
Any runtime class with property getters, including:
- `Viper.Math` (Pi, E, Tau)
- `Viper.Diagnostics.Stopwatch` (ElapsedMs, ElapsedNs, ElapsedUs)
- `Viper.IO.BinFile` (Eof)
- `Viper.IO.LineReader` (Eof)
- `Viper.IO.Watcher` (EVENT_* constants)
- `Viper.Log` (ERROR, etc.)
- `Viper.Machine` (Endian)
- And others with `get_*` functions

**Potential Fix:**
In the field expression analysis for runtime classes:
1. If direct field lookup fails, check for `get_<fieldName>` function
2. If found, synthesize a call expression to the getter
3. Return the getter's return type as the expression type

**Workaround:**
Use explicit getter syntax: `Viper.Math.get_Pi()` instead of `Viper.Math.Pi`

**Fix Complexity:** Medium - requires changes to field expression analysis for runtime classes

---

## Priority Recommendations

### Critical (Must Fix for End-User Testing)
1. **BUG-006:** Match binding crash - Core language feature broken
2. **BUG-010:** Value types crash - Core language feature broken
3. **BUG-011:** Generics not implemented - Core language feature missing

### High (Should Fix)
4. **BUG-002:** Multiple interpolations - Common use case
5. **BUG-008:** Auto-boxing - Easy to hit, confusing crash

### Medium (Nice to Fix)
6. **BUG-007:** For-in collections - Ergonomic improvement
7. **BUG-003/004:** Min i64 literal - Edge case
8. **BUG-012:** Runtime property access - Ergonomic improvement (workaround exists)

### Not Bugs (Documentation Only)
- BUG-001: `\$` escape works correctly
- BUG-005: Bits.Flip is bit reversal, not NOT
- BUG-009: Use Seq.WithCapacity(n), not Seq.New(n)

---

## Files to Modify

| Bug | Primary File | Secondary Files |
|-----|--------------|-----------------|
| BUG-002 | `Lexer.cpp:857-924` | - |
| BUG-003/004 | `NumberParsing.hpp:74-84` | `Lexer.cpp:605` |
| BUG-006 | `ParamCanonicalization.cpp:195-199` | `SimplifyCFG.cpp` |
| BUG-007 | `Lowerer_Stmt.cpp:303-373` | - |
| BUG-008 | `Lowerer_Expr_Call.cpp:725-735` | `Sema_Expr.cpp:376-386` |
| BUG-010 | `Lowerer_Expr_Call.cpp:377-428` | - |
| BUG-011 | `Sema.cpp:285-427` | `Parser_Expr.cpp`, `AST_Expr.hpp` |
| BUG-012 | `Sema_Expr.cpp` (field analysis) | - |

---

*Generated: 2026-01-16*
*Updated: 2026-01-16 - Added BUG-012*
