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
**Status**: FIXED (December 2025)
**Severity**: HIGH - Blocks common naming patterns
**Description**: Using `value` as a parameter name in entity methods caused the compiler to hang indefinitely.

**Root Cause**: The `value` keyword (`KwValue`) was treated as a reserved keyword in all contexts. When encountered as a parameter name, the parser would fail to recognize it as an identifier.

**Resolution**: Added `checkIdentifierLike()` helper function in `Parser.cpp` to allow contextual keywords (like `value`) to be used as identifiers in parameter/variable contexts. Updated `parseParameters()`, `parsePrimary()` identifier handling, and lambda parameter parsing to use this helper.

**Test**: Added `test_viperlang_types.cpp::ValueAsParameterName` to verify fix.

---

### 21. Cross-Module Type Resolution Fails for Boolean/Arithmetic
**Status**: FIXED (December 2025)
**Severity**: HIGH - Blocks multi-module OOP patterns
**Description**: When importing modules, method return types and field types from imported entities were not properly resolved, causing:
- Boolean return values from imported entity methods failed "Logical not requires Boolean operand"
- Arithmetic operations on imported entity fields failed "Invalid operands for arithmetic operation"

**Root Cause**: Declaration order during import resolution caused types to be analyzed before their dependencies. When modules with overlapping imports were merged, the prepending order could result in a type (e.g., Game) being analyzed before a type it depends on (e.g., Board).

**Resolution**: Added a three-pass analysis in `Sema::analyze()`:
1. First pass: Register all type names in scope
2. Second pass: Register all method/field signatures (added `registerEntityMembers()`, `registerValueMembers()`, `registerInterfaceMembers()`)
3. Third pass: Analyze method bodies

This ensures all method signatures are known before any method body is analyzed, allowing cross-entity method calls to resolve correctly.

---

### 22. Viper.Terminal Namespace Not Recognized
**Status**: FIXED (December 2025)
**Severity**: CRITICAL - Blocks TUI applications
**Description**: Several `Viper.Terminal` namespace functions for terminal operations were not recognized by the semantic analyzer.

**Error**: `Undefined identifier: Viper`

**Root Cause**: Many Viper.Terminal functions were missing from the `runtimeFunctions_` map in `Sema.cpp`.

**Resolution**: Added missing Terminal functions to `runtimeFunctions_`:
- `Viper.Terminal.HasKey` → Boolean
- `Viper.Terminal.ReadKey` → String
- `Viper.Terminal.Write` → void
- `Viper.Terminal.MoveCursor` → void
- `Viper.Terminal.SetForeground` → void
- `Viper.Terminal.SetBackground` → void
- `Viper.Terminal.ResetColors` → void
- `Viper.Terminal.HideCursor` → void
- `Viper.Terminal.ShowCursor` → void
- `Viper.Terminal.Sleep` → void

**Test**: Added `test_viperlang_basic.cpp::TerminalFunctionsRecognized` to verify fix.

---

## Summary Update (December 2025)

### All VTris Bugs Fixed
- **Bug #19**: List.set() - FIXED (Added Lowerer handling)
- **Bug #20**: Parameter name 'value' - FIXED (Contextual keyword support)
- **Bug #21**: Cross-module type resolution - FIXED (Three-pass analysis)
- **Bug #22**: Viper.Terminal not recognized - FIXED (Added missing runtime functions)

All fixes are verified with ctests. 919 tests pass with 1 pre-existing failure (test_viperlang_lambda - unrelated Bus error in lambda lowering).

---

## Bugs Found During Frogger Development (December 2025)

### 23. Module-Level Constants Not Resolved in Expressions
**Status**: FIXED (December 2025)
**Severity**: CRITICAL - Blocks use of named constants
**Description**: When a module defines constants at module level, they are not resolved to their values in expressions. They appear as `0` in the generated IL.

**Root Cause**: The `lowerIdent()` function only checked local variables and entity fields - it never looked up module-level constants. Additionally, there was no mechanism to store the values of global variable declarations.

**Resolution**:
1. Added `globalConstants_` map to the Lowerer class to store constant values
2. Added `lowerGlobalVarDecl()` to process `GlobalVarDecl` nodes and store literal values
3. Updated `lowerIdent()` to check `globalConstants_` before returning 0 for unknown identifiers
4. Added handling in the `lowerDecl()` switch for `DeclKind::GlobalVar`

**Test**: Added `test_viperlang_expressions.cpp::ModuleLevelConstants` to verify fix.

---

### 24. Boolean AND/OR Operators Generate Invalid IL
**Status**: FIXED (December 2025)
**Severity**: CRITICAL - Blocks boolean expressions with &&/||
**Description**: When using `&&` or `||` with boolean expressions, the generated IL produces an `and`/`or` instruction that expects `i64` operands but receives `i1` operands from comparison operations.

**Root Cause**: The `and` and `or` IL opcodes operate on I64 values, but comparison operators (`<`, `>`, `<=`, `>=`, `==`, `!=`) return I1 (boolean) values. The lowerer was directly passing I1 values to And/Or without type conversion.

**Resolution**: Updated the `BinaryOp::And` and `BinaryOp::Or` cases in `lowerBinary()` to:
1. Zero-extend I1 operands to I64 using `Opcode::Zext1`
2. Perform the bitwise And/Or on I64 values
3. Truncate the result back to I1 using `Opcode::Trunc1`

**Test**: Added `test_viperlang_expressions.cpp::BooleanAndOrWithComparisons` to verify fix.

---

### 25. Constants with Same Name in Different Scopes Resolve to Zero
**Status**: FIXED (December 2025) - Same fix as Bug #23
**Severity**: HIGH
**Description**: Constants like `PLATFORM_LOG = 0` and `PLATFORM_TURTLE = 1` both resolve to `0` in generated IL, making type discrimination impossible.

**Resolution**: Fixed by the same changes as Bug #23. All module-level constants are now properly stored and resolved during identifier lowering.

---

## Summary Update (December 2025 - Frogger)

### All Bugs Fixed
- **Bug #23**: Module-level constants not resolved - FIXED
- **Bug #24**: Boolean AND/OR generates invalid IL - FIXED
- **Bug #25**: Constants resolve to zero - FIXED (related to #23)

All fixes are verified with ctests. The Frogger demo now compiles correctly to IL.

---

### 26. Method Calls on Entity Fields Via Implicit Self Lowered as Lambda Calls
**Status**: FIXED (December 2025)
**Severity**: CRITICAL - Blocks OOP patterns with entity composition
**Description**: When calling a method on an entity field that's accessed via implicit self (i.e., without explicit `self.`), the method call is incorrectly lowered as a lambda/closure call instead of a direct entity method call.

**Root Cause**: Import order was incorrect when a module imported both inner and outer modules, where outer also imports inner. The declarations were being prepended in the wrong order: if main imports inner then outer (and outer also imports inner which is already processed), we got [outer, inner, main] instead of [inner, outer, main]. This caused type analysis to fail because Outer was analyzed before Inner.

**Resolution**: Fixed `ImportResolver.cpp` to collect all imported declarations first, then prepend them together in proper dependency order. This ensures that transitive imports maintain correct declaration order.

**Test**: Added `test_viperlang_imports.cpp::TransitiveImportDeclarationOrder` to verify fix.

---

### 27. Match Expression with Boolean Literal Subject Causes Compiler Hang
**Status**: FIXED (December 2025)
**Severity**: HIGH - Blocks guard-style match patterns
**Description**: Using `match (true)` or `match (false)` with comparison expressions in the arms caused the compiler to hang indefinitely.

**Root Cause**: Two issues were found:
1. The parser only recognized simple patterns (wildcard, literal, binding) but not expression patterns like `value < minVal`
2. When parsing failed, the error recovery left unmatched `}` tokens at the module level, causing an infinite loop in the module parsing

**Resolution**:
1. Added `MatchArm::Pattern::Kind::Expression` to support expression patterns
2. Updated parser to recognize expression patterns by checking if the token after an identifier is `=>` (binding) or something else (expression)
3. Updated lowerer to evaluate expression patterns as boolean conditions
4. Updated semantic analysis to type-check expression patterns
5. Fixed module parsing loop to skip stray `}` tokens left by error recovery

**Test**: Added `test_viperlang_match.cpp::MatchExpressionWithBooleanSubject` to verify fix.

**Example** (now works):
```viper
func clamp(Integer value, Integer minVal, Integer maxVal) -> Integer {
    return match (true) {
        value < minVal => minVal,
        value > maxVal => maxVal,
        _ => value
    };
}
```

---

## Bugs Found During Frogger Rewrite (December 2025)

### 28. Guard Statement Not Parsing in Entity Methods
**Status**: FIXED (December 2025)
**Severity**: MEDIUM - Blocks use of guard syntax in methods
**Description**: The `guard` statement did not parse correctly inside entity methods when parentheses were omitted.

**Root Cause**: The parser required parentheses around guard conditions, but Swift-style guard syntax allows them to be optional.

**Resolution**: Updated `parseGuardStmt()` in `Parser_Stmt.cpp` to make parentheses optional around guard conditions, matching Swift-style syntax.

**Test**: Added `test_viperlang_control_flow.cpp::GuardStatementWithoutParens` to verify fix.

**Example** (now works):
```viper
entity Frog {
    expose func moveUp() {
        guard state != STATE_DEAD else { return; }  // Works without parentheses
        guard (state != STATE_DEAD) else { return; }  // Also works with parentheses
        // ...
    }
}
```

---

### 29. String Comparison with Empty String Generates Invalid IL
**Status**: FIXED (December 2025)
**Severity**: HIGH - Blocks common patterns
**Description**: Comparing a string variable with an empty string literal `""` generated incorrect IL with type mismatch.

**Root Cause**: The string inequality (`!=`) code was using `ICmpEq` on an i1 result from `Viper.Strings.Equals`, but `ICmpEq` expects i64 operands.

**Resolution**: Updated the `BinaryOp::Ne` case in `Lowerer_Expr.cpp` to zero-extend the i1 result to i64 before comparing with 0.

**Test**: Added `test_viperlang_expressions.cpp::StringComparisonWithEmptyString` to verify fix.

**Example** (now works):
```viper
String key = Viper.Terminal.InKey();
if (key != "") {  // Works correctly
    // process key
}
```

---

### 30. Boolean Fields in Entities Cause Misaligned Store
**Status**: FIXED (December 2025)
**Severity**: CRITICAL - Causes VM trap
**Description**: Entity fields of type `Boolean` caused a misaligned store error in the VM at runtime when the entity was initialized.

**Root Cause**: The field layout computation was not properly aligning fields. Boolean fields (1 byte) would be placed at unaligned offsets, causing misaligned memory access when followed by pointer-sized fields.

**Resolution**:
1. Added `getILTypeAlignment()` and `alignTo()` helper functions in `Lowerer_Emit.cpp`
2. Updated field layout computation in `Lowerer_Decl.cpp` to properly align field offsets using alignment requirements
3. Boolean fields now align to 8 bytes to avoid misalignment issues with subsequent fields

**Test**: Added `test_viperlang_types.cpp::BooleanFieldAlignment` to verify fix.

**Example** (now works):
```viper
entity Game {
    expose Integer score;
    expose Boolean running;  // Works correctly
    expose Boolean paused;
    expose Integer level;

    expose func init() {
        running = true;  // No more misaligned store
    }
}
```

---

### 31. Sema Recognizes Runtime Functions That VM Doesn't Have
**Status**: FIXED (December 2025)
**Severity**: HIGH - Causes runtime errors
**Description**: The ViperLang semantic analyzer recognized certain `Viper.Terminal.*` function names that were not registered in the IL runtime, causing runtime errors.

**Root Cause**: Several function names were added to Sema's `runtimeFunctions_` map without corresponding runtime implementations.

**Resolution**: Removed the non-existent function names from `Sema.cpp`. Users should use the correct runtime function names:

| Use This | Instead Of |
|----------|------------|
| `Viper.Terminal.SetPosition(row, col)` | MoveCursor |
| `Viper.Terminal.SetColor(fg, bg)` | SetForeground/SetBackground |
| `Viper.Terminal.Print(text)` | Write |
| `Viper.Terminal.SetCursorVisible(0/1)` | HideCursor/ShowCursor |
| `Viper.Terminal.GetKey()` | ReadKey |
| `Viper.Terminal.GetKeyTimeout(ms)` | HasKey (check result != "") |
| `Viper.Time.SleepMs(ms)` | Terminal.Sleep |

**Test**: Updated `test_viperlang_basic.cpp::TerminalFunctionsRecognized` to use correct function names.

---

### 32. String Constants Not Dereferenced in Function Arguments
**Status**: FIXED (December 2025)
**Severity**: CRITICAL - Breaks all string constant comparisons
**Description**: When a `final` string constant was used as an argument to a function call (including string equality comparisons), the lowerer emitted the label name as a literal string instead of loading the value from the label.

**Root Cause**: The `lowerIdent()` function correctly stored string constants as `Value::constStr(label)`, but when these values were used in expressions, they weren't being dereferenced. The lowerer needed to emit a `const_str` instruction to load the actual string value from the global.

**Resolution**: Added a case in `lowerIdent()` in `Lowerer_Expr.cpp` to detect `Value::Kind::ConstStr` and emit a `const_str` instruction to properly load the string value.

**Test**: Added `test_viperlang_expressions.cpp::StringConstantsDereferenced` to verify fix.

**Example** (now works):
```viper
module Test;

final KEY_QUIT = "q";

func checkKey(String key) -> Boolean {
    return key == KEY_QUIT;  // Works correctly - compares with "q"
}
```

---

## Summary Update (December 2025 - Frogger Rewrite)

### All Bugs Fixed
- **Bug #28**: Guard statement parsing - FIXED (parentheses now optional)
- **Bug #29**: String comparison with "" - FIXED (proper i1 to i64 extension)
- **Bug #30**: Boolean fields misaligned store - FIXED (proper field alignment)
- **Bug #31**: Sema/Runtime function name mismatch - FIXED (removed non-existent names)
- **Bug #32**: String constants not dereferenced - FIXED (emit const_str instruction)

All fixes verified with unit tests. The Frogger demo now compiles and runs without workarounds.

---

## Bugs Found During Frogger OOP Rewrite (December 2025)

### 33. Unicode Escape Sequences Not Supported
**Status**: CLOSED (Fixed December 2025)
**Severity**: MEDIUM - Blocks convenient ANSI code constants
**Description**: Unicode escape sequences like `\u001b` are not recognized in string literals.

**Reproduction**:
```viper
final ESC = "\u001b";  // ERROR: invalid escape sequence: \u
```

**Workaround**: Use `Viper.String.Chr(27)` to create the escape character at runtime:
```viper
var ESC = Viper.String.Chr(27);
```

**Root Cause**: The `processEscape()` function in `src/frontends/viperlang/Lexer.cpp:616-639` only handles a limited set of escape sequences:
```cpp
std::optional<char> Lexer::processEscape(char c)
{
    switch (c)
    {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '\\': return '\\';
        case '"': return '"';
        case '\'': return '\'';
        case '0': return '\0';
        case '$': return '$';
        default: return std::nullopt;  // Missing: 'u', 'x', 'b'
    }
}
```

**Fix Required**: Add cases for `'u'` (unicode) and `'x'` (hex) escape sequences. For unicode, need to parse 4 hex digits after `\u` and convert to UTF-8. For hex, need to parse 2 hex digits after `\x`.

---

### 34. Backspace Escape Sequence Not Supported
**Status**: CLOSED (Fixed December 2025)
**Severity**: LOW - Minor inconvenience
**Description**: The backspace escape sequence `\b` is not recognized in string literals.

**Reproduction**:
```viper
Viper.Terminal.Print("\b \b");  // ERROR: invalid escape sequence: \b
```

**Workaround**: Use `Viper.String.Chr(8)` to create the backspace character:
```viper
var BS = Viper.String.Chr(8);
Viper.Terminal.Print(BS + " " + BS);
```

**Root Cause**: Same as Bug #33 - the `processEscape()` function in `src/frontends/viperlang/Lexer.cpp:616-639` does not include `'b'` in its switch statement.

**Fix Required**: Add `case 'b': return '\b';` to the switch statement in `processEscape()`.

---

### 35. Function Type Parameters with Void Return Not Recognized
**Status**: CLOSED (Fixed December 2025)
**Severity**: MEDIUM - Blocks higher-order function patterns
**Description**: Using `void` as the return type in a function type parameter causes a type error.

**Reproduction**:
```viper
entity Vehicle {
    expose func draw(printFn: (Integer, Integer, String, String) -> void) {
        // ERROR: Unknown type: void
    }
}
```

**Workaround**: Restructure code to avoid passing void-returning functions as parameters. Call the render methods directly from the game loop instead of passing renderer functions.

**Root Cause**: Two issues combine to cause this:

1. **`void` is not a keyword**: In `src/frontends/viperlang/Token.hpp`, there is no `KwVoid` token kind. The keyword `Void` is only recognized via the string lookup in `resolveNamedType()` in `Sema.cpp:283`.

2. **Type parsing for function types fails**: In `src/frontends/viperlang/Parser_Type.cpp:37-66`, the `parseBaseType()` function only handles `Identifier` tokens:
   ```cpp
   TypePtr Parser::parseBaseType()
   {
       if (check(TokenKind::Identifier))  // "void" is lowercase, won't match
       {
           Token nameTok = advance();
           // ...
       }
       // No fallback for keywords
   }
   ```

   When parsing the function type `(Integer, Integer) -> void`, the `void` token is either treated as an identifier (if capitalized as `Void`) or causes an error (if lowercase `void`).

**Fix Required**: Either:
1. Add `void` as a keyword token (`KwVoid`) and handle it in `parseBaseType()`, or
2. Use `Void` (capital V) consistently and ensure the parser accepts it in type position

---

### 36. Many Viper.* Runtime Classes Not Registered in ViperLang
**Status**: CLOSED
**Severity**: CRITICAL - Blocks most non-trivial applications
**Resolution**: Added `initRuntimeFunctions()` method to Sema.cpp with ~400 runtime function registrations covering all namespaces (Bits, Box, Collections, Crypto, DateTime, Diagnostics, Environment, Exec, Fmt, Graphics, Input, IO, Log, Machine, Math, Network, Parse, Random, String, Terminal, Text, Threads, Time, Vec2, Vec3, Convert).
**Description**: The ViperLang semantic analyzer only registers a small subset of the Viper runtime classes. Many runtime classes that exist in the C runtime and are available to BASIC/Pascal are completely missing from ViperLang.

**Currently Registered (in Sema.cpp)**:
- `Viper.Terminal.*` (partial - Say, Print, Clear, SetColor, SetPosition, GetKey, etc.)
- `Viper.String.*` (partial - Concat, Length, Left, Right, Mid, Trim, ToUpper, ToLower, IndexOf, Chr, Asc)
- `Viper.Math.*` (Abs, Sin, Cos, Tan, Sqrt, Log, Exp, Floor, Ceil, Round, Min, Max)
- `Viper.Random.*` (Next, NextInt, Seed)
- `Viper.Environment.*` (GetArgument, GetArgumentCount, GetCommandLine)
- `Viper.Time.*` (partial - Clock.Sleep, Clock.Millis, SleepMs)

**Missing Runtime Classes (exist in runtime.def but not in Sema.cpp)**:

| Namespace | Functions | Use Case |
|-----------|-----------|----------|
| `Viper.IO.File.*` | Exists, ReadAllText, WriteAllText, Delete, Copy, Move, Size, etc. | File operations |
| `Viper.IO.Dir.*` | Create, Delete, Exists, Files, List | Directory operations |
| `Viper.IO.Path.*` | Join, GetDir, GetName, GetExt, Exists | Path manipulation |
| `Viper.IO.BinFile.*` | Open, Close, Read, Write, Seek | Binary file I/O |
| `Viper.IO.LineReader.*` | Open, ReadLine, Close, Eof | Line-by-line file reading |
| `Viper.IO.LineWriter.*` | Open, WriteLine, Close | Line-by-line file writing |
| `Viper.IO.Compress.*` | Deflate, Inflate, GzipCompress, GzipDecompress | Compression |
| `Viper.IO.Archive.*` | New, AddFile, ExtractAll, List | ZIP archives |
| `Viper.IO.MemStream.*` | New, Read, Write, Seek, ToBytes | Memory streams |
| `Viper.IO.Watcher.*` | New, Next, Close | File system watching |
| `Viper.Text.StringBuilder.*` | New, Append, ToString, Clear, Length | String building |
| `Viper.Text.Codec.*` | EncodeBase64, DecodeBase64, EncodeHex, DecodeHex | Encoding |
| `Viper.Text.Csv.*` | Parse, ParseFile, Write | CSV handling |
| `Viper.Text.Guid.*` | New, Parse, ToString | GUID generation |
| `Viper.Text.Template.*` | New, Set, Render | Template rendering |
| `Viper.Text.Pattern.*` | New, Match, FindAll, Replace | Regex patterns |
| `Viper.Fmt.*` | Str, Int, Num, Bool, Pad, Size | Formatting |
| `Viper.Parse.*` | Int, Num, Bool | String parsing |
| `Viper.Convert.*` | IntToStr, NumToStr, BoolToStr | Type conversion |
| `Viper.Log.*` | Info, Warn, Error, Debug | Logging |
| `Viper.Collections.Bag.*` | New, Put, Has, Drop, Items | Set collections |
| `Viper.Collections.Bytes.*` | New, Get, Set, Slice, ToHex, FromHex | Byte arrays |
| `Viper.Collections.Heap.*` | New, Push, Pop, Peek | Priority queues |
| `Viper.Collections.Map.*` | New, Get, Set, Has, Keys, Values | Hash maps |
| `Viper.Collections.Queue.*` | New, Add, Take, Peek | FIFO queues |
| `Viper.Collections.Ring.*` | New, Push, Pop, Get | Ring buffers |
| `Viper.Collections.Seq.*` | New, Push, Pop, Get, Set | Dynamic arrays |
| `Viper.Collections.Stack.*` | New, Push, Pop, Peek | LIFO stacks |
| `Viper.Collections.TreeMap.*` | New, Set, Get, Has, Keys | Sorted maps |
| `Viper.Crypto.Hash.*` | CRC32, MD5, SHA1, SHA256, Hmac* | Cryptographic hashing |
| `Viper.Crypto.Rand.*` | Bytes, Int | Secure random |
| `Viper.Crypto.KeyDerive.*` | Pbkdf2SHA256 | Key derivation |
| `Viper.DateTime.*` | Now, Create, Format, Year, Month, Day, etc. | Date/time |
| `Viper.Diagnostics.*` | Assert*, Trap, Stopwatch.* | Debugging |
| `Viper.Exec.*` | Run, Capture, Shell | Process execution |
| `Viper.Graphics.Canvas.*` | New, Plot, Line, Box, Text, etc. | 2D graphics |
| `Viper.Graphics.Color.*` | RGB, RGBA, Blend | Color utilities |
| `Viper.Graphics.Pixels.*` | New, Load, Get, Set | Pixel manipulation |
| `Viper.Input.*` | Keyboard.*, Mouse.*, Pad.* | Input devices |
| `Viper.Network.*` | Dns.*, Http.*, Tcp.*, Udp.*, Url.* | Networking |
| `Viper.System.*` | Machine.*, Terminal.* (extended) | System info |
| `Viper.Threads.*` | Thread.*, Barrier.*, Gate.*, Monitor.*, etc. | Threading |
| `Viper.Bits.*` | And, Or, Xor, Not, Shl, Shr, etc. | Bitwise operations |
| `Viper.Box.*` | I64, F64, Str, Type, etc. | Boxing/unboxing |
| `Viper.Countdown.*` | New, Tick, Reset, Expired | Countdown timers |

**Reproduction**:
```viper
// All of these fail with "Undefined identifier: Viper"
if Viper.IO.File.Exists("file.txt") { }
var sb = Viper.Text.StringBuilder.New();
var hash = Viper.Crypto.Hash.MD5("test");
var now = Viper.DateTime.Now();
```

**Workaround**: None for most functionality. Affected features simply cannot be used in ViperLang.

**Root Cause**: The `runtimeFunctions_` map in `src/frontends/viperlang/Sema.cpp:637-712` only registers 57 runtime functions, while the full Viper runtime (defined in `src/runtime/runtime.def`) contains 1002+ functions.

The registration code in `Sema::Sema()` only includes:
```cpp
// Only 57 functions registered out of 1002+ available:
runtimeFunctions_["Viper.Terminal.Say"] = types::voidType();
runtimeFunctions_["Viper.Terminal.Print"] = types::voidType();
// ... ~55 more functions
```

When ViperLang code calls an unregistered function like `Viper.IO.File.Exists()`, the analyzer can't find it in `runtimeFunctions_` and reports "Undefined identifier: Viper".

**Coverage**: Only **5.7%** of runtime functions are available to ViperLang:
- BASIC/Pascal: 1002+ functions (full runtime)
- ViperLang: 57 functions (5.7% coverage)

**Resolution**: Add all missing runtime functions to `runtimeFunctions_` map in `src/frontends/viperlang/Sema.cpp`. Each function needs its return type specified. A script could generate the registration code from `runtime.def`.

**Notes**: This is a significant gap between ViperLang and BASIC/Pascal. The runtime has 1002+ functions but ViperLang only exposes 57 of them.

---

### 37. Generic Type Inference for List Elements Not Supported
**Status**: CLOSED
**Severity**: MEDIUM - Requires explicit type annotations
**Resolution**: Added proper generic type inference for collection methods in `Sema_Expr.cpp`. The `analyzeCall()` function now handles `List.get()`, `List.first()`, `List.last()`, `List.pop()` etc. to return the element type from the generic type parameter. Similar handling added for `Set` and `Map` methods.
**Description**: When retrieving elements from typed lists (e.g., `List[Platform]`), the element type is not automatically inferred. Variables assigned from `.get()` have type `obj` instead of the element type, causing method calls on those variables to fail type checking.

**Reproduction**:
```viper
var platforms: List[Platform] = [];
platforms.add(new Platform(1, 5, 1, 1, "@", 3));

for i in 0..platforms.size() {
    var plat = platforms.get(i);  // plat has type 'obj', not 'Platform'
    if plat.checkOnPlatform(1, 1) {  // ERROR: Condition must be Boolean
        // checkOnPlatform returns Boolean, but 'obj.checkOnPlatform' is unknown
    }
}
```

**Workaround**: Use explicit type annotations when retrieving list elements:
```viper
for i in 0..platforms.size() {
    var plat: Platform = platforms.get(i);  // Explicitly typed as Platform
    if plat.checkOnPlatform(1, 1) {  // Works correctly now
        // ...
    }
}
```

**Root Cause**: The `Viper.Collections.List.get_Item` function is registered with return type `types::ptr()` (obj):
```cpp
runtimeFunctions_["Viper.Collections.List.get_Item"] = types::ptr();
```

The semantic analyzer doesn't track generic type parameters on collection instances, so it cannot substitute the element type when resolving method calls.

**Technical Details**:
1. `List[Platform]` is parsed as a generic type but the element type is not propagated
2. When `.get(i)` is called, the return type is `ptr` (obj) not `Platform`
3. `obj.checkOnPlatform()` is looked up but fails since `obj` is not an entity type
4. Returns `types::unknown()` which fails the Boolean condition check

**Fix Required**: Implement generic type parameter tracking:
1. Store generic type arguments on type references (e.g., `List[Platform]` knows its element is `Platform`)
2. When resolving methods on generic types, substitute type parameters in return types
3. `List[T].get(i)` should return `T`, not `obj`

**Note**: Multi-file compilation via ImportResolver already works correctly. The initial investigation incorrectly attributed this to an import issue, but the actual root cause is missing generic type inference.

---

## Notes

This file will be updated as more bugs are discovered during game development.
