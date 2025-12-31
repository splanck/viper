# Graphics Show Demo - Bug Report

## Summary
While developing the graphics demo suite (`demos/viperlang/graphics_show/`), several bugs were discovered in the Viperlang frontend. This document catalogs each issue with reproduction steps.

---

## BUG-001: Runtime Classes Not Registered as Types (FIXED)

**Status:** Fixed in this session
**File:** `src/frontends/viperlang/Sema.cpp`
**Severity:** Critical

### Description
Runtime classes defined in `runtime.def` (e.g., `Viper.Graphics.Canvas`, `Viper.Graphics.Pixels`) were not registered in the Viperlang type system's `typeRegistry_`, causing "Unknown type" errors.

### Error Message
```
error[V3000]: Unknown type: Viper.Graphics.Canvas
```

### Fix Applied
Added runtime class types to `typeRegistry_` in `initRuntimeFunctions()`:
```cpp
typeRegistry_["Viper.Graphics.Canvas"] = types::ptr();
typeRegistry_["Viper.Graphics.Color"] = types::ptr();
typeRegistry_["Viper.Graphics.Pixels"] = types::ptr();
```

---

## BUG-002: Module Import System Not Implemented (FIXED)

**Status:** Fixed in this session
**Files:** `src/frontends/viperlang/Sema_Decl.cpp`, `src/frontends/viperlang/Sema_Expr.cpp`, `src/frontends/viperlang/Lowerer_Expr.cpp`, `src/frontends/viperlang/Types.hpp`, `src/frontends/viperlang/Types.cpp`, `src/frontends/viperlang/Sema.hpp`
**Severity:** Critical

### Description
Module-qualified access (e.g., `colors.initColors()`) was not implemented. The import resolver was already functional, but the semantic analyzer and lowerer didn't support accessing imported symbols via module prefix.

### Root Cause
1. The `analyzeImport()` function only stored import paths but didn't register module names as symbols
2. The semantic analyzer didn't handle field access on Module types
3. The lowerer didn't handle module-qualified function calls

### Fix Applied
1. Added `TypeKindSem::Module` type kind for imported module namespaces
2. Added `Symbol::Kind::Module` for module symbols
3. Modified `analyzeImport()` to extract module name from import path and register it as a Module symbol
4. Modified `analyzeField()` to handle field access on Module types (looking up symbols in global scope)
5. Modified `lowerCall()` to handle module-qualified function calls
6. Modified `lowerField()` to handle module-qualified variable/constant access

### Now Works
```viper
// colors.viper
module colors;
func initColors() { /* ... */ }

// main.viper
import "./colors";
func main() {
    colors.initColors();  // Works: module-qualified call
    initColors();         // Also works: direct access (merged into scope)
}
```

### Features Supported
- Module-qualified function calls: `utils.myFunc()`
- Module-qualified constant access: `utils.MY_CONST`
- Module-qualified variable access: `utils.myVar`
- Direct (unqualified) access: `myFunc()`, `MY_CONST`, `myVar`
- Circular import detection (handled by ImportResolver)
- Relative import paths: `import "./subdir/module"`

---

## BUG-003: Cannot Use `new` on Runtime Classes (FIXED)

**Status:** Fixed in this session
**Files:** `src/frontends/viperlang/Sema_Expr.cpp`, `src/frontends/viperlang/Lowerer_Expr.cpp`
**Severity:** Critical

### Description
The `new` keyword could not be used with runtime classes like `Viper.Graphics.Canvas` or `Viper.Graphics.Pixels`, even though they have `.New` constructors defined in `runtime.def`.

### Error Message (before fix)
```
error[V3000]: 'new' can only be used with value, entity, or collection types
```

### Fix Applied
1. Modified `analyzeNew()` in Sema_Expr.cpp to check if the type is a Ptr with a `.New` constructor registered
2. Modified `lowerNew()` in Lowerer_Expr.cpp to call the `.New` constructor for runtime class types

### Now Works
```viper
var canvas = new Viper.Graphics.Canvas("Title", 800, 600);
var pixels = new Viper.Graphics.Pixels(100, 100);
```

---

## BUG-004: Type Mismatch in Return Value Contexts (FIXED)

**Status:** Fixed in this session
**Files:** `src/frontends/viperlang/Sema_Stmt.cpp`, `src/frontends/viperlang/Lowerer_Stmt.cpp`
**Severity:** Medium

### Description
When a function returns `Integer` but the return value is `Number` (e.g., from `Viper.Math.Floor`), type mismatches occurred.

### Error Message (before fix)
```
error[V3000]: Type mismatch: expected Integer, got Number
```

### Fix Applied
1. Modified `analyzeReturnStmt()` in Sema_Stmt.cpp to allow implicit Number→Integer narrowing in return statements
2. Modified `lowerReturnStmt()` in Lowerer_Stmt.cpp to emit `cast.fp_to_si.rte.chk` for the actual f64→i64 conversion

### Now Works
```viper
func toInt(x: Number) -> Integer {
    return Viper.Math.Floor(x);  // Implicitly converts Number to Integer
}
```

---

## BUG-005: Invalid Operands for Arithmetic with Mixed Entity Fields (FIXED)

**Status:** Fixed in this session
**File:** `src/frontends/viperlang/Lowerer_Expr.cpp`
**Severity:** Medium

### Description
Arithmetic operations between Integer and Number operands failed at IL verification because integer operations were used with float operands.

### Error Message (before fix)
```
error: testBug005:entry_0: %3 = imul.ovf %t2 0.4: operand type mismatch: operand 1 must be i64
```

### Root Cause
The `lowerBinary()` function only checked the left operand's type to determine if float operations should be used. When the left operand was Integer and right was Number (e.g., `i * 0.4`), integer multiply was incorrectly used.

### Fix Applied
Modified `lowerBinary()` in Lowerer_Expr.cpp to:
1. Check both operand types for Number
2. If either is Number, use float operations (fmul, fdiv, fsub, fadd)
3. Emit `sitofp` to convert Integer operand to f64 before the operation

### Now Works
```viper
var i = 5;
var count = 10;
var fade = 1.0 - (i * 0.4) / count;  // Properly uses float arithmetic
```

---

## BUG-006: Floor Function Returns Number Instead of Integer (BY DESIGN)

**Status:** By Design (with workaround via BUG-004 fix)
**Severity:** Low
**File:** `src/il/runtime/runtime.def`

### Description
`Viper.Math.Floor()` returns `Number` (f64) rather than `Integer` (i64). This is by design because the underlying IL runtime function is typed as `f64(f64)`.

### Why Not Changed
Changing Floor to return Integer would cause IL type mismatches since the actual runtime implementation returns f64. The proper solution was implemented via BUG-004: implicit Number→Integer conversion in return statements.

### Current Behavior
```viper
var floored = Viper.Math.Floor(3.7);  // floored is Number (3.0)
```

### Workaround (now automatic for returns)
Thanks to BUG-004 fix, returning Floor from Integer-returning functions now works:
```viper
func toInt(x: Number) -> Integer {
    return Viper.Math.Floor(x);  // Automatic conversion
}
```

For explicit conversions, use `Viper.Convert.NumToInt`:
```viper
var intVal = Viper.Convert.NumToInt(Viper.Math.Floor(3.7));
```

---

## BUG-007: Runtime Class Method Calls Not Resolved (FIXED)

**Status:** Fixed in this session
**File:** `src/frontends/viperlang/Sema_Expr.cpp`, `src/frontends/viperlang/Lowerer_Expr.cpp`, `src/frontends/viperlang/Sema.cpp`
**Severity:** Critical

### Description
Method calls on runtime class instances (e.g., `canvas.Poll()`, `canvas.Clear(color)`) were not being resolved correctly. The semantic analyzer wasn't recognizing these as runtime method calls, causing the lowerer to generate broken `call.indirect 0` instructions.

### Root Cause
Three issues:
1. `Viper.Graphics.Canvas.New` returned generic `types::ptr()` instead of a named type that carries the class name
2. The semantic analyzer didn't check for runtime class methods when analyzing FieldExpr call expressions
3. The lowerer didn't prepend the object pointer as the first argument for runtime class method calls

### Fix Applied
1. Added `types::runtimeClass(name)` to create pointer types that carry the class name
2. Added runtime class method resolution in `analyzeCall()` in Sema_Expr.cpp
3. Added object pointer prepending in `lowerCall()` in Lowerer_Expr.cpp
4. Registered all Canvas and Pixels methods in Sema.cpp

---

## BUG-008: Mutable Global Variables Not Working (FIXED)

**Status:** Fixed
**Files:** `src/frontends/viperlang/Lowerer_Decl.cpp`, `src/frontends/viperlang/Lowerer.hpp`
**Severity:** Critical

### Description
Mutable global variables with literal initializers (e.g., `var counter = 0;`) were being treated as compile-time constants. Functions couldn't read the updated value or write to the variable.

### Root Cause
Two issues:
1. Variables with literal initializers were added to BOTH `globalConstants_` and `globalVariables_`
2. During identifier resolution, `globalConstants_` was checked first, so the constant value was always returned instead of loading from runtime storage
3. Initial values were not being stored to runtime storage at module initialization

### Fix Applied
1. Modified `lowerGlobalVarDecl` to only add `final` declarations to `globalConstants_`
2. Added `globalInitializers_` map to track literal initializer values for mutable variables
3. Added initialization code at start of `main()` to store initial values to runtime storage
4. Special handling for string literals (use `emitConstStr` to get actual address)

### Now Works
```viper
var counter = 0;  // Mutable global with literal initializer

func increment() {
    counter = counter + 1;  // Properly reads and writes runtime storage
}

func main() {
    increment();
    increment();
    Viper.Terminal.Say("Counter: " + counter);  // Correctly prints "Counter: 2"
}
```

---

## Recommendations

### All Critical Bugs Fixed!
All bugs discovered during graphics demo development have been fixed.

### Completed Fixes (This Session)
- ✅ BUG-001: Runtime classes registered as types
- ✅ BUG-002: Module imports with qualified access (`colors.func()`)
- ✅ BUG-003: `new` keyword works on runtime classes
- ✅ BUG-004: Implicit Number→Integer conversion in return statements
- ✅ BUG-005: Mixed-type arithmetic with Integer/Number operands
- ✅ BUG-006: Clarified as by-design with workaround via BUG-004
- ✅ BUG-007: Runtime class method calls resolved
- ✅ BUG-008: Mutable global variables with literal initializers

### Known Limitations
None - all discovered bugs have been fixed.

---

## Test Files
The following demo files can be used to reproduce these bugs:
- `demos/viperlang/graphics_show/main.viper`
- `demos/viperlang/graphics_show/colors.viper`
- `demos/viperlang/graphics_show/starfield.viper`
- `demos/viperlang/graphics_show/plasma.viper`

## Working Demo
The following standalone demo works with all fixes applied:
- `demos/viperlang/graphics_show/simple_starfield.viper`

To run:
```bash
./build/src/tools/viper/viper demos/viperlang/graphics_show/simple_starfield.viper
```

Controls:
- UP/DOWN: Adjust speed
- SPACE: Toggle warp mode
- ESC/Q: Exit

---

## Environment
- Compiler: Viper toolchain built from `master` branch
- Platform: macOS (Darwin)
- Date: 2025-12-31
