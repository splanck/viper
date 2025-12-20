# ViperLang Frontend Bugs Found During Development

This document tracks bugs discovered while developing and testing the ViperLang frontend.

---

## Parser Bugs

### 1. Generic Type Fields in Entities Not Parsing
**Status**: FIXED (December 2025)
**Severity**: CRITICAL - Blocks OOP with collections
**Description**: Entity fields with generic types like `List[Vehicle]` fail to parse with "expected field name" error.

```viper
entity Game {
    List[Vehicle] vehicles;  // Previously ERROR: expected field name
    List[Platform] platforms;  // Previously ERROR: expected field name
}
```

**Resolution**: Changed `parseFieldDecl()` to call `parseType()` instead of manually creating a `NamedType`, allowing generic types to be properly parsed.

---

### 2. Module-Level Variable Declarations with Complex Types Not Supported
**Status**: FIXED (December 2025)
**Severity**: HIGH
**Description**: Module-level `var` declarations with generic types or optional types fail to parse.

```viper
module Test;

var items: List[Integer]?;  // Previously ERROR: expected declaration

func start() { }
```

**Resolution**: Updated `parseDeclaration()` to detect and handle generic types (identified by `[`) and optional types (identified by `?`) following an identifier.

---

### 3. Lambda with Typed Parameters Not Supported
**Status**: FIXED (December 2025)
**Severity**: MEDIUM
**Description**: Lambda expressions with typed parameters fail to parse. Only parameterless lambdas `() => expr` work.

```viper
var double = (Integer x) => x * 2;  // Previously ERROR: doesn't parse
var greet = (x: Integer) => x * 2;  // Swift-style also works now
var greet = () => Viper.Terminal.Say("Hi");  // OK
```

**Resolution**: Added lambda parameter detection and parsing in `parsePrimary()`. Supports both Java-style `(Type name)` and Swift-style `(name: Type)` typed parameters, as well as single untyped parameter lambdas like `(x) => ...`.

---

### 4. Null Coalesce Operator (??) Not Parsing with Method Calls
**Status**: FIXED (December 2025)
**Severity**: MEDIUM
**Description**: The `??` operator fails to parse when combined with method calls.

```viper
Integer count = items?.count() ?? 0;  // Previously ERROR: expected ), got ??
```

**Resolution**: Investigated and verified the parser handles this correctly. The issue was related to Bug #5 (List count() not callable), which has been fixed.

---

### 5. List count() Method Not Available
**Status**: FIXED (December 2025)
**Severity**: MEDIUM
**Description**: The `count()` method is not callable on List objects.

```viper
List[Integer] mylist = [];
mylist.add(1);
Integer count = mylist.count();  // Previously ERROR: Expression is not callable
```

**Resolution**: Added special case handling in `Sema::analyzeCall()` for method calls on List types. Now `count()`, `size()`, `length()`, and `isEmpty()` are recognized as valid methods on Lists and Strings.

---

## Semantic Analysis Bugs

### 6. Import Path Resolution
**Status**: VERIFIED - Working correctly
**Description**: Import paths like `import "./entities"` may not resolve correctly depending on working directory.

**Resolution**: Code review verified the implementation handles:
- Relative paths (`./`, `../`) relative to importing file
- Simple paths with automatic `.viper` extension
- Circular import detection via normalized paths
- Maximum depth (50) and file count (100) guards

---

## Fixed Bugs

### 7. Lambda Block Body Parsed as Map/Set Literal
**Status**: FIXED (December 2025)
**Description**: When parsing `() => { statements }`, the `{...}` was being parsed as a Map/Set literal instead of a block expression.

```viper
var greet = () => {
    Viper.Terminal.Say("Hello");  // Was hanging during parse
};
```

**Resolution**: Added special handling in `parsePrimary` for lambda block bodies.

---

### 8. Match Expression Not Parsing as Value
**Status**: FIXED (December 2025)
**Description**: Match expressions could only be used as statements, not as values in expressions.

```viper
// This would hang
Integer result = match (x) {
    1 => 10,
    2 => 20,
    _ => 0
};
```

**Resolution**: Added match expression parsing in `parsePrimary` to allow match as a value expression.

---

### 9. BlockExpr and MatchExpr Not Handled in lowerExpr
**Status**: FIXED (December 2025)
**Description**: The `lowerExpr` switch statement was missing cases for `ExprKind::Block` and `ExprKind::Match`, causing infinite loops.

**Resolution**: Added `lowerBlockExpr` and `lowerMatchExpr` implementations.

---

### 10. Visibility Enforcement Breaking Existing Code
**Status**: FIXED (December 2025)
**Description**: After adding visibility enforcement, all entity methods became private by default, breaking cross-module access.

**Resolution**: Added `expose` keyword to mark public methods. Demo files updated to use `expose`.

---

### 11. Slots Not Cleared Between Methods
**Status**: FIXED (December 2025)
**Severity**: CRITICAL
**Description**: The `slots_` map in the Lowerer wasn't cleared between method lowering. Parameter slots from one method (e.g., `speed` parameter in Frog.setOnPlatform) would persist and incorrectly shadow entity field assignments in later methods (e.g., `speed = spd` in Vehicle.init).

**Symptoms**: Field assignments would store to wrong locations (block parameters instead of entity fields). IL verification would fail with "pointer type mismatch" errors.

**Resolution**: Added `slots_.clear();` after `locals_.clear();` in both `lowerFunctionDecl` and `lowerMethodDecl`.

---

### 12. lookupLocal("self") Fails for Slot-Based Self
**Status**: FIXED (December 2025)
**Severity**: CRITICAL
**Description**: Entity method parameters are stored in slots (for cross-block SSA correctness), but the code used `lookupLocal("self")` which only checks `locals_`, not `slots_`. This caused field access/assignment to fail silently in entity methods.

**Resolution**: Added `getSelfPtr()` helper function that checks both `slots_` and `locals_`, and updated all 7 locations that looked up "self".

---

### 13. Wrong Runtime Function Name for String Equality
**Status**: FIXED (December 2025)
**Severity**: HIGH
**Description**: `kStringEquals` was defined as `"Viper.String.Equals"` but the actual runtime function is `"Viper.Strings.Equals"`.

**Resolution**: Fixed the constant in `RuntimeNames.hpp`.

---

### 14. Escape Sequences Cause Compiler to Hang
**Status**: FIXED (December 2025)
**Severity**: CRITICAL
**Description**: Using escape sequences like `\r` (carriage return) or `\n` (newline) in string literals causes the ViperLang compiler to hang indefinitely during parsing/lowering.

**Reproduction**:
```viper
module Test;

func start() {
    String key = "\r";  // Previously compiler hung here
    String msg = "Line 1\nLine 2";  // Now works correctly
}
```

**Resolution**: Fixed bug in `Lexer::processEscape()` where the escape character was being consumed twice - once before calling `processEscape()` and again inside it. Changed `processEscape()` to take the escape character as a parameter instead of consuming it.

---

### 15. Module-Level Entity Instantiation Causes Compiler to Hang
**Status**: VERIFIED - Working correctly
**Severity**: HIGH
**Description**: Declaring entity instances at module level with `new` causes the compiler to hang.

**Reproduction**:
```viper
module Test;

entity Frog {
    Integer age;
}

Frog frog = new Frog();  // Now works

func start() { }
```

**Resolution**: Testing confirmed this works correctly. The issue may have been related to other parser bugs (Bug #2) that have been fixed.

---

## Language Limitations (Not Bugs)

### L1. No Negative Integer Literals
**Description**: The lexer doesn't support negative integer literals directly.

**Workaround**: Use `0 - value` instead of `-value`:
```viper
Integer negOne = 0 - 1;  // Instead of -1
```

### L2. No For-In Loops with Index
**Description**: For-in loops don't provide access to the current index.

**Workaround**: Use while loops with manual index tracking.

### L3. Break/Continue in Loops
**Status**: IMPLEMENTED AND WORKING
**Description**: break and continue statements for loop control are fully supported.

```viper
// Test - works correctly
var i = 0;
while (i < 10) {
    if (i == 3) {
        break;  // Exits loop when i == 3
    }
    i = i + 1;
}

i = 0;
while (i < 5) {
    i = i + 1;
    if (i == 3) {
        continue;  // Skips rest of iteration when i == 3
    }
    // This runs for i = 1, 2, 4, 5 (not 3)
}
```

---

## Summary

### All Parser/Semantic Bugs Fixed
- Bug #1: Generic type fields in entities - FIXED
- Bug #2: Module-level complex type declarations - FIXED
- Bug #3: Lambda with typed parameters - FIXED
- Bug #4: Null coalesce with method calls - FIXED
- Bug #5: List count() method - FIXED
- Bug #6: Import path resolution - Verified working
- Bug #14: Escape sequences - FIXED
- Bug #15: Module-level entity instantiation - Verified working

### Previously Fixed Bugs
- Bug #7: Lambda block body parsing
- Bug #8: Match expression as value
- Bug #9: BlockExpr/MatchExpr lowering
- Bug #10: Visibility enforcement
- Bug #11: Slots not cleared between methods (CRITICAL)
- Bug #12: lookupLocal("self") fails for slot-based self (CRITICAL)
- Bug #13: Wrong string equality function name

---

## Bugs Fixed During Frogger Development (December 2025)

### 16. Entity Type as Function/Method Parameter Causes Compiler Hang
**Status**: FIXED (December 2025)
**Severity**: CRITICAL - Blocks OOP patterns
**Description**: When a function or entity method has a parameter of an entity type, the compiler hung indefinitely during compilation.

**Root Cause**: The `parseParameters()` function only supported Swift-style parameter syntax (`name: Type`) but the demos used Java-style syntax (`Type name`). This caused parsing to fail and the module-level loop to spin without advancing.

**Resolution**: Updated `parseParameters()` in `Parser.cpp` to support both Java-style (`Type name`, `List[T] name`) and Swift-style (`name: Type`) parameter syntax, matching the lambda parameter parser's dual-style support.

**Test**: Added `test_viperlang_types.cpp::EntityAsParameter` to verify fix.

---

### 17. List[Entity] Add/Access Causes Runtime Assertion Failure
**Status**: FIXED (December 2025)
**Severity**: HIGH - Blocks collection-based game designs
**Description**: Adding entity instances to a `List[Entity]` and later accessing them caused a runtime assertion failure in the heap validator.

**Error**: `Assertion failed: (hdr->magic == RT_MAGIC), function rt_heap_validate_header, file rt_heap.c, line 64.`

**Root Cause**: Entity allocation in `lowerNew()` used `rt_alloc` which doesn't set up the heap header with magic value, refcount, etc. When entities were added to lists, `rt_obj_retain_maybe` was called which validates the heap header - failing because no header existed.

**Resolution**: Changed entity allocation in `Lowerer.cpp` from `rt_alloc` to `rt_obj_new_i64`, which properly initializes the heap header with magic, refcount, and class_id. This allows entities to participate in reference counting and be stored in collections.

**Test**: Added `test_viperlang_collections.cpp::ListOfEntities` to verify fix.

---

## Summary Update

### All Bugs Fixed
- **Bug #16**: Entity parameters - FIXED (Parser dual-style parameters)
- **Bug #17**: List[Entity] runtime failure - FIXED (Entity allocation with heap header)

Both fixes are verified with ctests and all 919 tests pass.

---

## Bugs Found During VTris Development (December 2025)

### 18. Entity Methods Cannot Call Other Methods Without self. Prefix
**Status**: DOCUMENTED - Language Behavior
**Severity**: MEDIUM - Confusing for developers
**Description**: Within an entity method, calling another method of the same entity requires explicit `self.` prefix. Without it, the method name is treated as an undefined identifier.

**Reproduction**:
```viper
entity Piece {
    expose func getBlock(Integer i) -> Integer {
        return i * 10;
    }

    expose func getBlockRow(Integer i) -> Integer {
        // ERROR: Undefined identifier: getBlock
        Integer offset = getBlock(i);
        return offset / 10;
    }
}
```

**Workaround**: Always use `self.` prefix when calling methods from within the same entity:
```viper
expose func getBlockRow(Integer i) -> Integer {
    Integer offset = self.getBlock(i);  // Works
    return offset / 10;
}
```

**Notes**: This may be intentional language design (explicit self like Python/Rust) rather than a bug. Documenting as potential UX improvement.

---

### 19. List.set() Method Not Implemented
**Status**: FIXED (December 2025)
**Severity**: CRITICAL
**Description**: The `List.set(index, value)` method was not implemented in the Lowerer - it fell through to default handling which caused various issues.

**Resolution**: Added List.set() handling in Lowerer.cpp to call `Viper.Collections.List.set_Item`.

---

### 20. Parameter Name 'value' Causes Compiler Hang
**Status**: OPEN - Compiler Bug
**Severity**: HIGH - Blocks common naming patterns
**Description**: Using `value` as a parameter name in entity methods causes the compiler to hang indefinitely.

**Reproduction**:
```viper
entity Board {
    List[Integer] items;

    expose func doSet(Integer idx, Integer value) {  // 'value' param causes hang
        items.set(idx, value);
    }
}
```

**Working Case** (use different param name):
```viper
entity Board {
    List[Integer] items;

    expose func doSet(Integer idx, Integer val) {  // 'val' works fine
        items.set(idx, val);
    }
}
```

**Likely Cause**: The name `value` may conflict with an internal keyword or reserved name in the parser/lowerer.

**Workaround**: Use alternative parameter names like `val`, `v`, `newValue`, etc.

---

### 21. Cross-Module Type Resolution Fails for Boolean/Arithmetic
**Status**: OPEN - Compiler Bug
**Severity**: HIGH - Blocks multi-module OOP patterns
**Description**: When importing modules, method return types and field types from imported entities are not properly resolved, causing:
- Boolean return values from imported entity methods fail "Logical not requires Boolean operand"
- Arithmetic operations on imported entity fields fail "Invalid operands for arithmetic operation"

**Reproduction**:
```viper
// board.viper
entity Board {
    expose func canPlacePiece(Piece p) -> Boolean {
        return true;
    }
}

// game.viper
import "./board";

entity Game {
    expose Board board;

    expose func test() {
        if (!board.canPlacePiece(currentPiece)) {  // ERROR: Logical not requires Boolean operand
            gameOver = true;
        }
    }
}
```

**Workaround**: Extract values to local variables with explicit types, or avoid cross-module method calls that return Boolean.

---

### 22. Viper.Terminal Namespace Not Recognized
**Status**: OPEN - Compiler Bug
**Severity**: CRITICAL - Blocks TUI applications
**Description**: The `Viper.Terminal` namespace for terminal operations is not recognized when used in multi-file projects.

**Error**: `Undefined identifier: Viper`

**Workaround**: Use single-file demos or avoid Viper.Terminal calls.

---

## Notes

This file will be updated as more bugs are discovered during game development.
