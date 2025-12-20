# ViperLang Frontend Bugs Found During Development

This document tracks bugs discovered while developing and testing the ViperLang frontend.

---

## Parser Bugs

### 1. Generic Type Fields in Entities Not Parsing
**Status**: OPEN
**Severity**: CRITICAL - Blocks OOP with collections
**Description**: Entity fields with generic types like `List[Vehicle]` fail to parse with "expected field name" error.

```viper
entity Game {
    List[Vehicle] vehicles;  // ERROR: expected field name
    List[Platform] platforms;  // ERROR: expected field name
}
```

**Workaround**: None - cannot use generic collection types as entity fields.

**Impact**: Cannot create game objects that contain lists of other objects.

---

### 2. Module-Level Variable Declarations with Complex Types Not Supported
**Status**: OPEN
**Severity**: HIGH
**Description**: Module-level `var` declarations with generic types or optional types fail to parse.

```viper
module Test;

var items: List[Integer]?;  // ERROR: expected declaration

func start() { }
```

**Workaround**: Declare variables inside functions or use entity fields (but see Bug #1).

---

### 3. Lambda with Typed Parameters Not Supported
**Status**: OPEN
**Severity**: MEDIUM
**Description**: Lambda expressions with typed parameters fail to parse. Only parameterless lambdas `() => expr` work.

```viper
var double = (Integer x) => x * 2;  // ERROR: doesn't parse
var greet = () => Viper.Terminal.Say("Hi");  // OK
```

**Workaround**: Use regular functions instead of lambdas with parameters.

---

### 4. Null Coalesce Operator (??) Not Parsing with Method Calls
**Status**: OPEN
**Severity**: MEDIUM
**Description**: The `??` operator fails to parse when combined with method calls.

```viper
Integer count = items?.count() ?? 0;  // ERROR: expected ), got ??
```

**Workaround**: Use explicit null checks with if statements.

---

### 5. List count() Method Not Available
**Status**: OPEN
**Severity**: MEDIUM
**Description**: The `count()` method is not callable on List objects.

```viper
List[Integer] mylist = [];
mylist.add(1);
Integer count = mylist.count();  // ERROR: Expression is not callable
```

**Workaround**: Track list size manually with a separate counter variable.

---

## Semantic Analysis Bugs

### 6. Import Path Resolution
**Status**: NEEDS INVESTIGATION
**Description**: Import paths like `import "./entities"` may not resolve correctly depending on working directory.

**Workaround**: Use absolute paths or ensure correct working directory.

---

## Fixed Bugs

### 7. Lambda Block Body Parsed as Map/Set Literal
**Status**: ✅ FIXED (December 2025)
**Description**: When parsing `() => { statements }`, the `{...}` was being parsed as a Map/Set literal instead of a block expression.

```viper
var greet = () => {
    Viper.Terminal.Say("Hello");  // Was hanging during parse
};
```

**Resolution**: Added special handling in `parsePrimary` for lambda block bodies.

---

### 8. Match Expression Not Parsing as Value
**Status**: ✅ FIXED (December 2025)
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
**Status**: ✅ FIXED (December 2025)
**Description**: The `lowerExpr` switch statement was missing cases for `ExprKind::Block` and `ExprKind::Match`, causing infinite loops.

**Resolution**: Added `lowerBlockExpr` and `lowerMatchExpr` implementations.

---

### 10. Visibility Enforcement Breaking Existing Code
**Status**: ✅ FIXED (December 2025)
**Description**: After adding visibility enforcement, all entity methods became private by default, breaking cross-module access.

**Resolution**: Added `expose` keyword to mark public methods. Demo files updated to use `expose`.

---

### 11. Slots Not Cleared Between Methods
**Status**: ✅ FIXED (December 2025)
**Severity**: CRITICAL
**Description**: The `slots_` map in the Lowerer wasn't cleared between method lowering. Parameter slots from one method (e.g., `speed` parameter in Frog.setOnPlatform) would persist and incorrectly shadow entity field assignments in later methods (e.g., `speed = spd` in Vehicle.init).

**Symptoms**: Field assignments would store to wrong locations (block parameters instead of entity fields). IL verification would fail with "pointer type mismatch" errors.

**Resolution**: Added `slots_.clear();` after `locals_.clear();` in both `lowerFunctionDecl` and `lowerMethodDecl`.

---

### 12. lookupLocal("self") Fails for Slot-Based Self
**Status**: ✅ FIXED (December 2025)
**Severity**: CRITICAL
**Description**: Entity method parameters are stored in slots (for cross-block SSA correctness), but the code used `lookupLocal("self")` which only checks `locals_`, not `slots_`. This caused field access/assignment to fail silently in entity methods.

**Resolution**: Added `getSelfPtr()` helper function that checks both `slots_` and `locals_`, and updated all 7 locations that looked up "self".

---

### 13. Wrong Runtime Function Name for String Equality
**Status**: ✅ FIXED (December 2025)
**Severity**: HIGH
**Description**: `kStringEquals` was defined as `"Viper.String.Equals"` but the actual runtime function is `"Viper.Strings.Equals"`.

**Resolution**: Fixed the constant in `RuntimeNames.hpp`.

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

### L3. No Break/Continue in Loops
**Status**: NEEDS VERIFICATION
**Description**: May not have break/continue statements for loop control.

**Workaround**: Use flag variables to control loop exit.

---

## Summary

### Open Bugs (Blocking)
- Bug #1: Generic type fields in entities (CRITICAL)
- Bug #2: Module-level complex type declarations (HIGH)

### Open Bugs (Non-Blocking)
- Bug #3: Lambda with typed parameters
- Bug #4: Null coalesce with method calls
- Bug #5: List count() method

### Fixed Bugs
- ✅ Bug #7: Lambda block body parsing
- ✅ Bug #8: Match expression as value
- ✅ Bug #9: BlockExpr/MatchExpr lowering
- ✅ Bug #10: Visibility enforcement
- ✅ Bug #11: Slots not cleared between methods (CRITICAL)
- ✅ Bug #12: lookupLocal("self") fails for slot-based self (CRITICAL)
- ✅ Bug #13: Wrong string equality function name

---

## Runtime / Demo Bugs

### 14. Escape Sequences Cause Compiler to Hang
**Status**: OPEN
**Severity**: CRITICAL
**Description**: Using escape sequences like `\r` (carriage return) or `\n` (newline) in string literals causes the ViperLang compiler to hang indefinitely during parsing/lowering.

**Reproduction**:
```viper
module Test;

func start() {
    String key = "\r";  // Compiler hangs here
}
```

**Workaround**: Do not use escape sequences in strings. Use alternative detection methods for special keys.

**Impact**: Cannot detect enter key or use multi-line strings with escaped newlines.

---

### 15. Module-Level Entity Instantiation Causes Compiler to Hang
**Status**: OPEN
**Severity**: HIGH
**Description**: Declaring entity instances at module level with `new` causes the compiler to hang.

**Reproduction**:
```viper
module Test;

Frog frog = new Frog();  // Compiler hangs

func start() { }
```

**Workaround**: Create all entity instances inside functions, not at module level.

---

## Notes

This file will be updated as more bugs are discovered during game development.
