# VIPER BASIC — Resolved Bugs

Last Updated: 2025-11-17

This document records bugs from bugs/basic_bugs.md that have been verified and resolved in code, with notes on scope,
approach, and validation.

---

## BUG-067: Array Fields in Classes Not Supported

Status: RESOLVED (Previously fixed, verified 2025-11-17)
Discovered: 2025-11-16 (Dungeon OOP stress test)
Category: Frontend / Parser / OOP
Test File: `/bugs/bug_testing/dungeon_entities.bas`

Symptom: Cannot declare array fields inside CLASS definitions. Parser fails with "expected END, got ident" error.

Reproduction:

```basic
CLASS Player
    inventory(10) AS Item  ' ERROR: Parse fails!
END CLASS
```

Error:

```
error[B0001]: expected END, got ident
    inventory(10) AS Item
    ^
```

Expected: Arrays should be valid field types in classes

Workaround: Use multiple scalar fields or manage arrays outside the class

Impact: Severely limits OOP design - cannot have collection fields in classes

Root Cause:

- Parser lookahead constraint: `looksLikeFieldDecl` did not accept `identifier ( ... ) AS type` forms without DIM; fix
  includes allowing `peek(1) == LParen` after identifier.
- Type system limitation: `parseTypeKeyword()` only captured primitive types; object class names for array elements were
  not preserved in field parsing.

Files: `src/frontends/basic/Parser_Stmt_OOP.cpp`, `Parser_Stmt_Core.cpp`

Resolution: Parser lookahead updated to allow array dims shorthand; array dimension parsing implemented.
Multi-dimensional arrays supported.

Validation:

- `test_bug067_array_fields.bas` verifies array fields for STRING and INTEGER
- Array fields can be accessed/modified in methods

---

## BUG-068: Function Name Assignment for Return Value Not Working

Status: RESOLVED (2025-11-17)
Discovered: 2025-11-16 (Dungeon OOP stress test)
Category: Frontend / Semantic Analysis + Lowering / Method Epilogue
Test Files: `/bugs/bug_testing/test_bug068_explicit_return.bas`,
`/bugs/bug_testing/test_bug068_function_name_return.bas`

Symptom: Assigning to the function name failed to set return value; semantic checker also flagged false "missing return"
errors.

Resolution: Method epilogue loads from function-name variable if present; semantic checker uses
`methodHasImplicitReturn()`.

Validation: Explicit RETURN and implicit name-assignment both return expected values in tests.

---

## BUG-070: BOOLEAN Parameters Cause Type Mismatch Errors

Status: RESOLVED (2025-11-17)
Discovered: 2025-11-16 (Dungeon OOP stress test)
Category: Frontend / Type System

Symptom: Passing TRUE/FALSE to BOOLEAN parameters caused IL call-arg type mismatches (i64 vs i1).

Resolution: Coerce call arguments to `i1` when callee expects BOOLEAN; keep literals as legacy `i64` (-1/0) to preserve
IL goldens.

Validation: Boolean parameter calls compile and run; golden outputs remain consistent across runs.

---

## BUG-071: String Arrays Cause IL Generation Error

Status: RESOLVED (2025-11-17)
Category: Code Generation / IL / Arrays / Temporary Lifetime

Symptom: Deferred releases of string temps from `rt_arr_str_get` caused use-before-def at function exit.

Resolution: Removed deferred releases for array-get results; consuming code manages lifetime.

Validation: String array loop tests pass.

---

## BUG-073: Cannot Call Methods on Object Parameters

Status: RESOLVED (2025-11-17)
Category: Code Generation / IL / OOP / Method Resolution

Symptom: Calls on object-typed parameters emitted unqualified callee names due to missing `objectClass` typing for
constructor params.

Resolution: Preserve object-class typing for constructor params via `setSymbolObjectType`, matching method params.

Validation: Object-parameter method calls compile/run; suite green.

---

## BUG-074: Constructor Corruption When Class Uses Previously-Defined Class

Status: RESOLVED (2025-11-17)
Category: Frontend / Lowering / OOP / Constructor Generation

Symptom: Deferred temporaries leaked across procedures caused constructor corruption.

Resolution: Clear deferred temps in `resetLoweringState()` before each procedure; aligns with main-function handling.

Validation: Poker constructor cases pass.

---

## BUG-065: Array Field Assignments Silently Dropped by Compiler

Status: RESOLVED (2025-11-15)
Category: Frontend / Code Generation / OOP

Symptom: Array field assignments in methods were silently dropped.

Resolution: Preserve array metadata in `ClassLayout::Field`, propagate to symbols, and emit proper `rt_arr_*` stores.

Validation: New unit tests for numeric/string array field stores/loads pass.

---

## BUG-058: String array fields in classes don't retain values

Status: RESOLVED (2025-11-15)
Category: Frontend / OOP / Array stores

Resolution: Use `rt_arr_str_put` with retain semantics for string arrays; derive element type from class layout and
recompute handle from `ME` + field offset. Numeric arrays use `rt_arr_i32_set`.

Validation: Values persist; reads via `rt_arr_str_get` verified by tests.

---

## BUG-007: Multi-dimensional arrays parsing and lowering

- Status: RESOLVED (Parser + Analyzer + Lowerer support multi-dim)
- Summary: DIM accepts comma-separated extents (e.g., `DIM A(2,3,4)`), analyzer records per-dimension extents and total,
  and the lowerer flattens multi-dimensional indexing to a single zero-based index using row-major order when metadata
  is available. REDIM is supported for single-dimension today; multi-dimension REDIM remains out-of-scope as initially
  documented.
- Key paths:
    - Parser: `Parser_Stmt_Runtime.cpp` (`parseDimStatement`) parses comma-separated dimension sizes and stores them in
      `DimStmt::dimensions`.
    - Analyzer: `SemanticAnalyzer.Stmts.Runtime.cpp::analyzeDim` computes and stores `ArrayMetadata{extents,totalSize}`
      and tracks `ArrayString`/`ArrayInt` based on name suffix.
    - Lowerer: `lower/Emit_Expr.cpp::lowerArrayAccess` computes flattened index using recorded extents and emits bounds
      checks via runtime helpers.
- Validation: DIM/UBOUND/array element access tests exercise arrays; added extents plumbing and flattened indexing
  already used by lowering. Existing golden tests for arrays remain green.

---

## BUG-014: String arrays not supported (duplicate of BUG-032)

- Status: RESOLVED
- Summary: String array allocation, element get/put, and length queries are implemented via `rt_arr_str_*` helpers.
  Lowering was updated to pass a pointer to a temporary holding the string handle for stores to match the runtime ABI,
  and the semantic analyzer now recognizes `ARRAY STRING` element types.
- Changes (previous prompt):
    - `LowerStmt_Runtime.cpp`: for `assignArrayElement` to string arrays, allocate a temp slot, store the string handle,
      pass the slot pointer to `rt_arr_str_put`.
    - Runtime signatures left pointer-based for handles; no whitelist drift.
- Validation: Runtime and golden tests for string operations pass; string-array element assignments no longer report
  call-argument type mismatches.

---

## BUG-036: String comparison in OR condition causes IL error

- Status: RESOLVED
- Summary: Short-circuit logical lowering (`ANDALSO`/`ORELSE`) and eager logical (`AND`/`OR`) use a store-to-slot +
  join-block pattern that does not involve branch argument bundles, preventing the earlier IL verification error. String
  comparisons (`rt_str_eq`, `rt_str_lt`, etc.) return `i1` and are coerced to boolean where needed for short-circuiting.
- Key paths:
    - `LowerExprLogical.cpp`: short-circuit lowering uses `lowerBoolBranchExpr` and stores `i1` results; eager logical
      ops coerce to `i64` masks as needed.
    - `LowerExpr.cpp::lowerBoolBranchExpr`: emits dedicated then/else/join blocks without branch arguments, avoiding the
      verifier complaint.
- Validation: Existing string comparison and boolean short-circuit goldens pass; no `cbr` argument-bundle mismatches
  observed in current test suite.

---

## BUG-016: Local string variables in methods cause compilation error

- Status: RESOLVED (verified 2025-11-13)
- Summary: Local string variable declarations inside class methods now work correctly. The previous "empty block" error
  has been fixed.
- Test case:

```basic
CLASS Test
    SUB ShowMessage()
        DIM msg$ AS STRING
        msg$ = "Hello"
        PRINT msg$
    END SUB
END CLASS
```

- Validation: Test runs successfully and prints "Hello". String variables can now be declared and used within class
  methods without compilation errors.

---

## BUG-025: EXP of large values causes overflow trap

- Status: RESOLVED (verified 2025-11-13)
- Summary: EXP function no longer traps on large values. Instead of crashing with "fp overflow in
  cast.fp_to_si.rte.chk", it now returns the correct floating-point result (including infinity representation for very
  large values).
- Test case:

```basic
x% = 100
result# = EXP(x%)
PRINT result#  ' Output: 2.68811714181614e+43
```

- Validation: EXP(100) runs successfully and returns approximately 2.688e43, which is the mathematically correct result.
  No more runtime traps.

---

## Notes

- Related resolved issues: BUG-032/033 (string arrays - NOTE: still failing verification as of 2025-11-13), BUG-034 (
  MID$ float indices + one-based start) are tracked in devdocs/basic_bugs2.md and reflected in goldens.

---

## BUG-026: DO WHILE with GOSUB causes "empty block" error

- Status: RESOLVED (loop lowering fix + golden)
- Summary: DO/WHILE/FOR lowering previously marked the loop done block as terminated without emitting a terminator
  instruction when the loop body ended in a terminator (e.g., a GOSUB that generates branches). This produced IL
  verifier failures: `error: main:do_done: empty block`. The fix leaves the done block unterminated and lets the
  statement sequencer emit a fallthrough branch to the next line block, ensuring every block contains a terminator only
  when an instruction is emitted.
- Key paths:
    - src/frontends/basic/lower/Lower_Loops.cpp: stop setting `done->terminated` in `emitDo`, `emitWhile`, and FOR
      lowering; keep the done block open for the sequencer to wire fallthrough.
    - tests/golden/basic/do_gosub_loop.bas: new golden reproducing DO + GOSUB body with no extra statements; previously
      failed with IL verifier, now runs cleanly (empty stdout).
- Validation: Golden test added; prior to fix it triggered `empty block` in verifier, after fix it lowers and runs with
  no diagnostics. Broader loops continue to function; fallthrough to subsequent statements is handled by the sequencer.

---

## BUG-032/033: String arrays end-to-end support

- Status: RESOLVED (verified 2025-11-13)
- Summary: BASIC string arrays now work through allocation, assignment, element load, and printing. The root cause was a
  mismatch between IL-visible signatures and VM marshalling for string array helpers: `rt_arr_str_get` returned `ptr`
  instead of `string`, and `rt_arr_str_put` value marshalling dereferenced the wrong level (passing the address of the
  temporary rather than the string handle).
- Key paths:
    - src/il/runtime/RuntimeSignatures.cpp: signatures updated (`rt_arr_str_get` returns `string`) and wrappers fixed to
      correctly dereference pointer-typed operands for `rt_arr_str_{get,put,len,release}`.
    - src/frontends/basic/LowerStmt_Runtime.cpp: store path already passed a pointer to a temporary slot for
      `rt_arr_str_put`; now matches the corrected marshalling.
    - tests/golden/arrays/string_array_store_and_print.bas: new golden validates allocate → store → get → print.
- Validation: Golden passes; existing runtime/string tests remain green. Ownership semantics: `rt_arr_str_get` retains
  on read; `rt_arr_str_put` retains the new value and releases the previous.

---

## BUG-010: STATIC keyword support

- Status: RESOLVED (2025-11-14)
- Summary: STATIC keyword now fully functional for procedure-local persistent variables. Variables declared with STATIC
  persist across procedure calls while remaining isolated between different procedures. Uses the rt_modvar
  infrastructure with procedure-qualified names (e.g., "Counter.count") to ensure proper isolation.
- Key paths:
    - src/frontends/basic/Lowerer.Procedure.cpp (lines 564-620): Added STATIC variable resolution in
      `resolveVariableStorage()` before module-level global check. Detects `isStatic` flag and emits appropriate
      `rt_modvar_addr_*` calls with scoped names.
    - Runtime storage: Reuses existing `rt_modvar_*` helpers (same mechanism as module-level globals), ensuring
      zero-initialization on first access and persistence across calls.
- Test cases:
    - devdocs/basic/test_bug010_static.bas: Basic counter test (increments 1, 2, 3)
    - devdocs/basic/test_bug010_static_fixed.bas: Comprehensive suite covering multiple STATIC variables, same-named
      variables in different procedures (isolation), type suffixes, STATIC in FUNCTION, and mixed local/STATIC
      variables.
- Validation: All test scenarios pass. STATIC variables work with all types (INTEGER, SINGLE, STRING, etc.) in both SUB
  and FUNCTION.

---

## BUG-012: BOOLEAN type incompatibility with TRUE/FALSE constants

- Status: RESOLVED (2025-11-14)
- Summary: BOOLEAN variables can now be compared with TRUE/FALSE constants and with each other using = and <> operators.
  STR$() now accepts BOOLEAN arguments and converts them correctly (TRUE → "-1", FALSE → "0").
- Root cause: Type system didn't allow BOOLEAN-vs-BOOLEAN comparisons, and STR$() didn't whitelist BOOLEAN as a valid
  argument type. IL lowering also had operand coercion issues when comparing i16 BOOLEAN variables with i64 TRUE/FALSE
  constants.
- Key paths:
    - src/frontends/basic/Check_Expr_Binary.cpp: Modified `validateComparisonOperands()` to allow BOOLEAN-vs-BOOLEAN
      comparisons for Eq/Ne operators.
    - src/frontends/basic/SemanticAnalyzer.Builtins.cpp: Modified `checkArgType()` to whitelist BOOLEAN for STR$()
      conversion.
    - src/frontends/basic/lower/LowerExprNumeric.cpp: Fixed `normalizeNumericOperands()` to promote i16 BOOLEAN
      variables to i64 when compared with i64 TRUE/FALSE constants.
- Test cases:
    - devdocs/basic/test_bug012_boolean_comparisons.bas: Tests BOOLEAN equality/inequality with TRUE/FALSE and other
      BOOLEAN variables
    - devdocs/basic/test_bug012_str_bool.bas: Tests STR$() with BOOLEAN arguments
- Validation: All tests pass. BOOLEAN type is now fully usable for equality/inequality comparisons.

---

## BUG-017: Accessing global strings from class methods causes segfault

- Status: RESOLVED (2025-11-14)
- Summary: Class methods can now access module-level global string variables without segfaulting. This was fixed as a
  side effect of the BUG-019 type system fix.
- Root cause: Type precedence issue in lowering phase - getSlotType() was overriding semantic analysis types with parser
  symbol table types, causing wrong rt_modvar_addr_* helper selection for global strings accessed from methods.
- Key paths:
    - src/frontends/basic/Lowerer.Procedure.cpp: Modified `getSlotType()` to respect semantic analysis types (BUG-019
      fix)
    - The fix ensures correct helper selection (rt_modvar_addr_str for strings) when resolving global variable storage
- Validation: Class methods successfully access and modify global string variables. No more segfaults.

---

## BUG-019: Float CONST truncation

- Status: RESOLVED (2025-11-14)
- Summary: CONST declarations with float literals now correctly preserve float precision. `CONST PI = 3.14159` now
  stores 3.14159 instead of truncating to 3. STR$(PI) now produces "3.14159" instead of "3".
- Root cause: Two-phase type conflict - semantic analyzer correctly inferred Float type for CONST, but lowering phase's
  getSlotType() and classifyNumericType() used parser symbol table type (I64) instead of semantic analysis type.
- Key paths:
    - src/frontends/basic/Lowerer.Procedure.cpp (lines 527-535): Modified `getSlotType()` to only apply symbol->type
      override when semantic analysis has no type, preserving float CONST inference.
    - src/frontends/basic/lower/Lowerer_Expr.cpp (lines 519-542): Modified `classifyNumericType()` VarExpr visitor to
      consult semantic analyzer first before checking symbol table.
    - Added include for SemanticAnalyzer.hpp in Lowerer_Expr.cpp (line 25).
- Test cases:
    - devdocs/basic/test_bug019_const_float.bas: Original failing test
    - devdocs/basic/test_bug019_const_float_fixed.bas: Comprehensive test with various CONST scenarios
- Validation: All tests pass. CONST PI = 3.14159 now works correctly, mathematical constants preserve full precision.

---

## BUG-030: SUBs and FUNCTIONs cannot access global variables

- Status: RESOLVED (2025-11-14)
- Summary: Module-level global variables are now properly shared between main and SUB/FUNCTION procedures. Each
  procedure can read and modify the actual global values instead of seeing zero-initialized copies.
- Root cause: Infrastructure for rt_modvar_addr_* runtime helpers was already present, but BUG-019's type mismatch
  issues caused wrong helper selection, breaking global variable sharing.
- Key paths:
    - Fixed as a side effect of BUG-019 type precedence fix
    - The correct type resolution ensures proper rt_modvar_addr_* helper selection (rt_modvar_addr_i64,
      rt_modvar_addr_f64, rt_modvar_addr_str, etc.)
- Test cases:
    - devdocs/basic/test_bug030_fixed.bas: Comprehensive test with INTEGER, SINGLE, and STRING globals across
      SUB/FUNCTION boundaries
    - devdocs/basic/test_bug030_scenario1-6.bas: Various scenarios testing global variable sharing
- Validation: All 6 scenarios pass. Globals properly shared - modifications in SUB/FUNCTION are visible in main and vice
  versa. Modular programming now possible.

---

## BUG-035: Global variables not accessible in SUB/FUNCTION

- Status: RESOLVED (2025-11-14)
- Summary: Duplicate of BUG-030. Discovered independently during tic-tac-toe development. Resolved by the same BUG-019
  type system fix.
- Validation: Same tests as BUG-030 confirm resolution.

---

## BUG-040: Custom class types as FUNCTION return values

- Status: RESOLVED (2025-11-15)
- Summary: Functions declared `AS <Class>` now return object references correctly. RETURN of an object variable emits a
  pointer-typed load from the right slot and returns that value, avoiding the prior "ret value type mismatch: expected
  ptr but got i64" error.
- Root cause: `lowerReturn` always returned the value of `lowerExpr(stmt.value)` without special-casing pointer returns.
  In class-returning functions, `RETURN p` used scalar paths that could select the wrong slot or coerce to `i64`.
- Key paths:
    - src/frontends/basic/lower/Lowerer_Stmt.cpp: In `Lowerer::lowerReturn`, when the enclosing function retType is
      `Ptr` and the value is a `VarExpr`, resolve the variable storage and `emitLoad(Ptr, ...)` before `emitRet` to
      guarantee correct slot and IL type.
    - Existing implicit-return path in `Lowerer.Procedure.cpp::lowerFunctionDecl` already loads the function-name
      variable with `Ptr` for class returns; this change aligns explicit RETURN with that behavior.
- Tests:
    - Added unit test `tests/unit/test_basic_class_return.cpp` which compiles a minimal repro and asserts the `ret`
      operand originates from a `Load` typed as `Ptr`.
    - Registered via `tests/CMakeLists.txt` using test helper wrappers.
- Validation:
    - New test passes (`ctest -R test_basic_class_return`).
    - No regressions observed; full suite remains green locally.

---

## BUG-037: SUB methods on class instances cannot be called

- Status: RESOLVED (2025-11-15)
- Summary: SUB methods on class instances can now be called successfully. Previously, multi-letter variable names before
  a dot were misinterpreted as namespace qualifiers, causing method calls like `db.AddRecord()` to be parsed as
  qualified procedure calls instead of method calls.
- Root cause: Parser heuristic in call statement parsing (Parser_Stmt_Core.cpp:211-214) treated any multi-character
  identifier before a dot as a namespace, breaking method calls on variables with multi-letter names. Single-letter
  variables (e.g., `t.TestSub()`) always worked.
- Key paths:
    - **Parser fix** (src/frontends/basic/Parser_Stmt_Core.cpp, lines 201-211): Removed flawed multi-letter heuristic
      that forced qualified interpretation. Now only treats identifiers as namespaces if they're in the
      `knownNamespaces_` set, otherwise falls through to expression parser which correctly handles both method calls and
      qualified procedure calls.
    - **Semantic analyzer enhancement** (src/frontends/basic/SemanticAnalyzer.Stmts.Runtime.cpp, lines 66-91): Added
      smart error detection for undefined variables in method call positions. When a method call base is an undefined
      variable, reports "unknown procedure 'namespace.method'" instead of "unknown variable", preserving expected error
      messages for actual namespace-qualified call attempts.
- Test case:
    - devdocs/basic/test_bug037_method_calls_fixed.bas: Comprehensive validation demonstrating:
        - Multi-letter variables calling SUB methods (main fix target)
        - Single-letter variables calling SUB methods (always worked)
        - FUNCTION methods work inline (BUG-039 not yet addressed for assignment)
        - Mixed SUB/FUNCTION method usage
- Validation: All test cases pass. 99% test suite passes (622/623 tests). Method calls on class instances now work
  correctly regardless of variable name length.

---

## BUG-039: Cannot assign method call results to variables

- Status: RESOLVED (2025-11-15)
- Summary: Method calls returning primitive types (INTEGER, STRING, etc.) can now be assigned to variables. Previously,
  all method calls were assumed to return objects based on the receiver's class, causing "call arg type mismatch" errors
  when methods returned primitives.
- Root cause: `resolveObjectClass()` in Lower_OOP_Expr.cpp returned the receiver's class for all method calls without
  checking the method's actual return type. This caused primitive-returning methods to be incorrectly treated as
  object-returning methods.
- Key paths:
    - src/frontends/basic/Lower_OOP_Expr.cpp (lines 106-137): Modified `resolveObjectClass()` for MethodCallExpr to
      check the method's return type using `findMethodReturnType()`. Only returns a class name if the method returns a
      class type (ptr). For primitive types (I64, F64, Str, Bool), returns empty string to indicate "not an object".
- Example that now works:
  ```basic
  CLASS Counter
    count AS INTEGER
    FUNCTION GetCount() AS INTEGER
      RETURN Me.count
    END FUNCTION
  END CLASS

  DIM c AS Counter
  c = NEW Counter()
  c.count = 5
  DIM result AS INTEGER
  result = c.GetCount()  ' Now works!
  PRINT result           ' Outputs: 5
  ```
- Validation: Method calls returning primitives can be assigned to variables without type mismatch errors. Test cases
  demonstrate INTEGER, STRING, and other primitive return types working correctly.

---

## BUG-040: Cannot use custom class types as function return types

- Status: RESOLVED (2025-11-15)
- Summary: Functions can now return custom class types. Previously, attempting to return a class instance from a
  function would either load from the wrong slot (returning null/uninitialized data) or cause type mismatch errors due
  to incorrect symbol resolution.
- Root cause: Complex interaction of two issues:
    1. **Duplicate Symbol Creation**: The semantic analyzer creates mangled symbols (e.g., "X_0") for object-typed
       variables, while the lowerer also creates unmangled symbols (e.g., "X"). This resulted in two symbols for the
       same variable - one with `isObject=false` and one with `isObject=true`.
    2. **Wrong Symbol Resolution**: When `RETURN X` was lowered, `resolveVariableStorage("X")` would find the unmangled
       symbol with `isObject=false`, causing it to load as `i64` instead of `ptr`, or load from the wrong slot entirely.
- Key paths:
    - src/frontends/basic/Lowerer.Procedure.cpp (lines 1297-1306): Modified `postCollect` callback to only mark the
      function name symbol as an object type if it was actually referenced in the function body, preventing unnecessary
      symbol creation.
    - src/frontends/basic/lower/Lowerer_Stmt.cpp (lines 578-603): Enhanced `lowerReturn()` to handle the duplicate
      symbol issue by attempting to find the object-typed version of a variable (trying "_0" suffix if the first lookup
      returns a non-object symbol).
- Example that now works:
  ```basic
  CLASS Record
    id AS INTEGER
    name AS STRING
  END CLASS

  FUNCTION GetRecord() AS Record
    DIM rec AS Record
    rec = NEW Record()
    rec.id = 1
    rec.name = "Alice"
    RETURN rec  ' Now works correctly!
  END FUNCTION

  DIM r AS Record
  r = GetRecord()
  PRINT r.name  ' Outputs: Alice
  ```
- Test cases:
    - /tmp/trace_bug.bas: Function returning class instance with PRINT before RETURN
    - /tmp/test_symbols.bas: Simple function returning a class instance
    - /tmp/test_name_case.bas: Case-insensitive variable names with class returns
    - /tmp/test_debug_return.bas: Function with multiple local variables and object return
    - /tmp/test_func_assign.bas: Function that assigns to its own name before returning another variable
- Validation: All test cases pass. Functions can correctly return custom class types, and the returned instances can be
  used normally. Full test suite: 625/626 tests pass (99%).

---
