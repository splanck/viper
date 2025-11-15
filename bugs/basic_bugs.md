# VIPER BASIC Known Bugs and Issues

*Last Updated: 2025-11-15*
*Source: Empirical testing during language audit*

**Bug Statistics**: 66 resolved, 0 outstanding bugs (66 total documented)

**STATUS**: ✅ BUG-065 RESOLVED — array field assignments preserved (2025-11-15)

---

## OUTSTANDING BUGS (0 bugs)

None at this time.

---

### BUG-065: Array Field Assignments Silently Dropped by Compiler
**Status**: ✅ RESOLVED (2025-11-15)
**Discovered**: 2025-11-15 (Adventure Game Testing - root cause of BUG-064)
**Category**: Frontend / Code Generation / OOP
**Test File**: `/bugs/bug_testing/adventure_player_v2.bas`, `/bugs/bug_testing/debug_parse_test.bas`
**Root Cause Analysis**: `/bugs/bug_testing/BUG-065_ROOT_CAUSE_ANALYSIS.md`

**Symptom**: Assignment statements to array fields inside methods are silently dropped - no error, no warning, assignment just doesn't happen.

**Reproduction**:
```basic
CLASS Player
    DIM inventory(10) AS STRING
    FUNCTION AddItem(item AS STRING) AS BOOLEAN
        inventory(0) = item  ' SILENTLY DROPPED - no IL emitted!
        RETURN TRUE
    END FUNCTION
END CLASS
```

**IL Evidence**: The `%t12 = load str, %t3` loads the parameter, but then NO array store operation is emitted. The assignment is recognized but abandoned.

**Expected/Now**: Emits `rt_arr_str_put` for string arrays and `rt_arr_i32_set` for numeric arrays

**Workaround**: No longer needed

**Impact**: Fixed — implicit field array stores inside methods now work reliably.

**Root Cause**:
1. **Information Loss in OOP Scan** (`Lower_OOP_Scan.cpp:150-165`): AST `ClassDecl::Field` has `bool isArray` and `arrayExtents`, but `ClassLayout::Field` struct does NOT preserve these fields. The OOP scan uses `field.isArray` to compute storage size but discards the metadata.

2. **Incorrect Field Scope Metadata** (`Lowerer.Procedure.cpp:441`): When `pushFieldScope` creates `SymbolInfo` for fields, it hardcodes `info.isArray = false` for ALL fields because `ClassLayout::Field` lacks the array information.

3. **Assignment Recovery Fails** (`LowerStmt_Runtime.cpp:312-341`): The `assignArrayElement` function has recovery logic for implicit field arrays, but either `isFieldInScope("ARR")` returns false or the recovery code executes with wrong metadata, preventing the `rt_arr_str_put` call from being emitted.

**Key Finding**: The `ClassLayout::Field` struct is missing `bool isArray` and `arrayExtents` fields that exist in the AST version, causing array metadata to be lost during the AST → ClassLayout translation.

**Affected Files**:
- `src/frontends/basic/Lowerer.hpp:803-809` (ClassLayout::Field struct definition - missing isArray)
- `src/frontends/basic/Lower_OOP_Scan.cpp:150-165` (builds ClassLayout, discards isArray)
- `src/frontends/basic/Lowerer.Procedure.cpp:430-449` (pushFieldScope hardcodes isArray=false)
- `src/frontends/basic/LowerStmt_Runtime.cpp:286-368` (assignArrayElement recovery logic)

**Fix Implemented**:
- Added `bool isArray` to `ClassLayout::Field` and preserved it during OOP scan.
- `pushFieldScope` now propagates array metadata to field symbols.
- Implicit field-array assignments in methods correctly emit `rt_arr_*_set` calls.

**Verification**:
- New unit test: `tests/unit/test_basic_oop_numeric_array_field.cpp` confirms numeric arrays emit `rt_arr_i32_set/get`.
- Existing string array field test already verifies `rt_arr_str_put/get`.

---

### BUG-058: String array fields in classes don't retain values
**Status**: ✅ RESOLVED (2025-11-15)
**Category**: Frontend / OOP / Array stores

**Fix Summary**:
- Solidified implicit field-array stores inside methods by deriving element type from class layout and recomputing the array handle from `ME` + field offset.
- String arrays now use `rt_arr_str_put` with proper retain semantics; numeric arrays use `rt_arr_i32_set`. Values persist and read back correctly via `rt_arr_str_get`.

**Files**: `src/frontends/basic/LowerStmt_Runtime.cpp` (assignArrayElement)

**Notes**: This also covers the previously observed "likely duplicate of BUG-065" symptom for string element arrays when using implicit member access.

---

## RECENTLY FIXED BUGS

- ✅ **BUG-056**: Arrays not allowed as class fields - RESOLVED 2025-11-15
- ✅ **BUG-057**: BOOLEAN return type in class methods causes type mismatch - RESOLVED 2025-11-15
- ✅ **BUG-059**: Cannot access array fields within class methods - RESOLVED 2025-11-15
- ✅ **BUG-060**: Cannot call methods on class objects passed as SUB/FUNCTION parameters - RESOLVED 2025-11-15
- ✅ **BUG-061**: Cannot assign class field value to local variable (regression) - RESOLVED 2025-11-15
- ✅ **BUG-062**: CONST with CHR$() not evaluated at compile time - RESOLVED 2025-11-15
- ✅ **BUG-063**: Module-level initialization cleanup code leaks into subsequent functions - RESOLVED 2025-11-15
- ✅ **BUG-058**: String array fields don't retain values - RESOLVED 2025-11-15

---

## RESOLVED BUGS (65 bugs)

**Note**: Includes BUG-052 (ON ERROR GOTO terminators), BUG-056 (array class fields), BUG-057 (boolean returns), BUG-059 (field array access), BUG-060 (class parameters), BUG-061 (field value reads), BUG-062 (CHR$ const folding), and BUG-063 (cleanup code leak) which were all resolved on 2025-11-15.

**Note**: See `basic_resolved.md` for full details on all resolved bugs.

- ✅ **BUG-001**: String concatenation requires $ suffix for type inference - RESOLVED 2025-11-12
- ✅ **BUG-002**: & operator for string concatenation not supported - RESOLVED 2025-11-12
- ✅ **BUG-003**: FUNCTION name assignment syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-004**: Procedure calls require parentheses even with no arguments - RESOLVED 2025-11-12
- ✅ **BUG-005**: SGN function not implemented - RESOLVED 2025-11-12
- ✅ **BUG-006**: Limited trigonometric/math functions - RESOLVED 2025-11-12
- ✅ **BUG-056**: Arrays not allowed as class fields - RESOLVED 2025-11-15
- ✅ **BUG-007**: Multi-dimensional arrays not supported - RESOLVED 2025-11-13
- ✅ **BUG-008**: REDIM PRESERVE syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-009**: CONST keyword not implemented - RESOLVED 2025-11-12
- ✅ **BUG-010**: STATIC keyword not implemented - RESOLVED 2025-11-14
- ✅ **BUG-011**: SWAP statement not implemented - RESOLVED 2025-11-12
- ✅ **BUG-012**: BOOLEAN type incompatibility with TRUE/FALSE constants - RESOLVED 2025-11-14
- ✅ **BUG-013**: SHARED keyword not supported - RESOLVED 2025-11-13
- ✅ **BUG-014**: String arrays not supported (duplicate of BUG-032) - RESOLVED 2025-11-13
- ✅ **BUG-015**: String properties in classes cause runtime error - RESOLVED 2025-11-13
- ✅ **BUG-016**: Local string variables in methods cause compilation error - RESOLVED 2025-11-13
- ✅ **BUG-017**: Accessing global strings from methods causes segfault - RESOLVED 2025-11-14
- ✅ **BUG-018**: FUNCTION methods in classes cause code generation error - RESOLVED 2025-11-12
- ✅ **BUG-019**: Float literals assigned to CONST are truncated to integers - RESOLVED 2025-11-14
- ✅ **BUG-020**: String constants cause runtime error - RESOLVED 2025-11-13
- ✅ **BUG-021**: SELECT CASE doesn't support negative integer literals - RESOLVED 2025-11-12
- ✅ **BUG-022**: Float literals without explicit type default to INTEGER - RESOLVED 2025-11-12
- ✅ **BUG-023**: DIM with initializer not supported - RESOLVED 2025-11-12
- ✅ **BUG-024**: CONST with type suffix causes assertion failure - RESOLVED 2025-11-12
- ✅ **BUG-025**: EXP of large values causes overflow trap - RESOLVED 2025-11-13
- ✅ **BUG-026**: DO WHILE loops with GOSUB cause "empty block" error - RESOLVED 2025-11-13
- ✅ **BUG-027**: MOD operator doesn't work with INTEGER type (%) - RESOLVED 2025-11-12
- ✅ **BUG-028**: Integer division operator (\\) doesn't work with INTEGER type (%) - RESOLVED 2025-11-12
- ✅ **BUG-029**: EXIT FUNCTION and EXIT SUB not supported - RESOLVED 2025-11-12
- ✅ **BUG-030**: SUBs and FUNCTIONs cannot access global variables - RESOLVED 2025-11-14
- ✅ **BUG-031**: String comparison operators (<, >, <=, >=) not supported - RESOLVED 2025-11-12
- ✅ **BUG-032**: String arrays not supported - RESOLVED 2025-11-13
- ✅ **BUG-033**: String array assignment causes type mismatch error (duplicate of BUG-032) - RESOLVED 2025-11-13
- ✅ **BUG-034**: MID$ does not convert float arguments to integer - RESOLVED 2025-11-13
- ✅ **BUG-035**: Global variables not accessible in SUB/FUNCTION (duplicate of BUG-030) - RESOLVED 2025-11-14
- ✅ **BUG-036**: String comparison in OR condition causes IL error - RESOLVED 2025-11-13
- ✅ **BUG-037**: SUB methods on class instances cannot be called - RESOLVED 2025-11-15
- ✅ **BUG-038**: String concatenation with method results fails in certain contexts - RESOLVED 2025-11-14
- ✅ **BUG-039**: Cannot assign method call results to variables - RESOLVED 2025-11-15
- ✅ **BUG-040**: Cannot use custom class types as function return types - RESOLVED 2025-11-15
- ✅ **BUG-041**: Cannot create arrays of custom class types - RESOLVED 2025-11-14
- ✅ **BUG-042**: Reserved keyword 'LINE' cannot be used as variable name - RESOLVED 2025-11-14
- ✅ **BUG-043**: String arrays reported not working (duplicate of BUG-032/033) - RESOLVED 2025-11-13
- ✅ **BUG-044**: CHR() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-045**: STRING arrays not working with AS STRING syntax - RESOLVED 2025-11-15
- ✅ **BUG-046**: Cannot call methods on array elements - RESOLVED 2025-11-15
- ✅ **BUG-047**: IF/THEN/END IF inside class methods causes crash - RESOLVED 2025-11-15
- ✅ **BUG-048**: Cannot call module-level SUB/FUNCTION from within class methods - RESOLVED 2025-11-15
- ✅ **BUG-049**: RND() function signature incompatible with standard BASIC - BY DESIGN
- ✅ **BUG-050**: SELECT CASE with multiple values causes IL generation error - VERIFIED NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-051**: DO UNTIL loop causes IL generation error - VERIFIED NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-052**: ON ERROR GOTO handler blocks missing terminators - RESOLVED 2025-11-15
- ✅ **BUG-053**: Cannot access global arrays in SUB/FUNCTION - RESOLVED 2025-11-15 (already fixed via resolveVariableStorage)
- ℹ️ **BUG-054**: STEP is reserved word - BY DESIGN (intentional, STEP is FOR loop keyword)
- ℹ️ **BUG-055**: Cannot assign to FOR loop variable - BY DESIGN (intentional semantic check to prevent bugs)
- ✅ **BUG-059**: Cannot access array fields in methods - RESOLVED 2025-11-15
- ✅ **BUG-060**: Cannot call methods on class parameters - RESOLVED 2025-11-15
- ✅ **BUG-061**: Cannot read class field values (REGRESSION) - RESOLVED 2025-11-15

---

## UPDATE HISTORY

**Recent Fixes (2025-11-13)**:

- ✅ **BUG-007 RESOLVED**: Multi-dimensional arrays now work
- ✅ **BUG-013 RESOLVED**: SHARED keyword now accepted (compatibility no-op)
- ✅ **BUG-014 RESOLVED**: String arrays now work (duplicate of BUG-032)
- ✅ **BUG-015 RESOLVED**: String properties in classes now work
- ✅ **BUG-016 RESOLVED**: Local string variables in methods now work
- ✅ **BUG-020 RESOLVED**: String constants (CONST MSG$ = "Hello") now work
- ✅ **BUG-025 RESOLVED**: EXP of large values no longer crashes
- ✅ **BUG-026 RESOLVED**: DO WHILE loops with GOSUB now work
- ⚠️ **BUG-032/033 PARTIAL**: String arrays reported in basic_resolved.md but verification shows issues remain
- ✅ **BUG-034 RESOLVED**: MID$ float argument conversion now works
- ✅ **BUG-036 RESOLVED**: String comparison in OR conditions now works

**Behavior Tweaks (2025-11-13)**:
- IF/ELSEIF conditions now accept INTEGER as truthy (non-zero = true, 0 = false) in addition to BOOLEAN. Prior negative tests expecting an error on `IF 2 THEN` were updated.

**Boolean Type System Changes**: Modified `isBooleanType()` to only accept `Type::Bool` (not `Type::Int`) for logical operators (AND/ANDALSO/OR/ORELSE). This makes the type system stricter and fixes some test cases.

**Bug Statistics**: 54 resolved, 5 NEW CRITICAL BUGS (59 total documented)

**Recent Investigation (2025-11-14)**:
- ✅ **BUG-012 NOW RESOLVED**: BOOLEAN variables can now be compared with TRUE/FALSE constants and with each other; STR$(boolean) now works
- ✅ **BUG-017 NOW RESOLVED**: Class methods can now access global strings without crashing
- ✅ **BUG-019 NOW RESOLVED**: Float CONST preservation - CONST PI = 3.14159 now correctly stores as float
- ✅ **BUG-030 NOW RESOLVED**: Global variables now properly shared across SUB/FUNCTION (fixed as side effect of BUG-019)
- ✅ **BUG-035 NOW RESOLVED**: Duplicate of BUG-030, also fixed
- ✅ **BUG-010 NOW RESOLVED**: STATIC keyword now fully functional - procedure-local persistent variables work correctly
- ❌ **NEW OOP BUGS DISCOVERED (2025-11-13)**: During BasicDB development, discovered 6 OOP limitations (BUG-037 through BUG-042). After re-testing (2025-11-14), only 3 remain outstanding.
- ✅ **BUG-038 NOW RESOLVED**: String concatenation with method results works correctly
- ✅ **BUG-041 NOW RESOLVED**: Arrays of custom class types work perfectly
- ✅ **BUG-042 NOW RESOLVED**: LINE keyword no longer reserved, can be used as variable name
- ✅ **BUG-043 VERIFIED RESOLVED**: String arrays work correctly (duplicate/false report of BUG-032/033)

**Recent Fixes (2025-11-15)**:
- ✅ **BUG-037 NOW RESOLVED**: SUB methods on class instances can now be called (parser heuristic fix)
- ✅ **BUG-039 NOW RESOLVED**: Method call results can now be assigned to variables (OOP expression lowering fix)
- ✅ **BUG-040 NOW RESOLVED**: Functions can now return custom class types (symbol resolution and return statement fix)

**Stress Testing (2025-11-15)**:
Conducted comprehensive OOP stress testing by building text-based adventure games. Created 26 test files (1501 lines of code) exercising classes, arrays, strings, math, loops, and file inclusion. See `/bugs/bug_testing/STRESS_TEST_SUMMARY.md` for full report.

**New Bugs Discovered During Stress Testing (2025-11-15)**:
- ✅ **BUG-044 RESOLVED (2025-11-15)**: CHR() supported via alias to CHR$; ANSI codes work
- ✅ **BUG-045 RESOLVED (2025-11-15)**: STRING arrays honored with AS STRING; assignments validated correctly
- ✅ **BUG-046 RESOLVED (2025-11-15)**: Methods on array elements parse and run (e.g., `array(i).Method()`)
- ✅ **BUG-047 RESOLVED (2025-11-15)**: IF/THEN inside class methods no longer crashes (fixed OOP exit block emission)
- ✅ **BUG-048 RESOLVED (2025-11-15)**: Class methods can call module SUB/FUNCTIONs (signature pre-collection)
- ℹ️ **BUG-049 BY DESIGN**: RND() is zero-argument; propose ADR if argumentized forms are desired
- ✅ **BUG-050 VERIFIED**: SELECT CASE with multiple values works; not reproducible as an error
- ✅ **BUG-051 VERIFIED**: DO UNTIL loop works; not reproducible as an error
- ✅ **BUG-052 RESOLVED (2025-11-15)**: ON ERROR GOTO handler blocks now automatically emit terminators

**Grep Clone Stress Testing (2025-11-15)**:
Built a complete grep clone (vipergrep) testing file I/O, string searching, pattern matching, multiple file processing, and OOP. Successfully demonstrated OPEN/LINE INPUT/CLOSE, INSTR, LCASE$, and case-insensitive searching. Confirmed BUG-052 behavior: handlers are created and entered, but must end with RESUME/END; missing terminators trigger IL verify. See `/bugs/bug_testing/vipergrep_simple.bas` for working implementation.

**Priority**: Former CRITICAL items (BUG-047, BUG-048) resolved.

**Recent Fixes (2025-11-15 PM)**:
- Closed BUG-044/045/046/047/048 based on targeted frontend changes
- Verified BUG-050/051 pass in current build
- Left BUG-049 as design note (ADR recommended)
- ✅ **BUG-052 NOW RESOLVED (2025-11-15)**: ON ERROR GOTO handler blocks now emit proper terminators

**🚨 CRITICAL REGRESSIONS DISCOVERED (2025-11-15 Evening)**:
During adventure game stress testing, discovered 5 new bugs including 3 CRITICAL issues that block OOP usage:
- 🚨 **BUG-061 CRITICAL REGRESSION**: Cannot read class field values (likely from BUG-056 fix)
- 🚨 **BUG-059 CRITICAL**: Cannot access array fields in methods
- 🚨 **BUG-060 CRITICAL**: Cannot call methods on class parameters
- **BUG-058 HIGH**: String array fields don't retain values
- **BUG-057 MODERATE**: BOOLEAN return types fail in methods

**OOP System Status**: 🚨 SEVERELY BROKEN - BUG-061 blocks all OOP usage

**✅ CRITICAL BUGS RESOLVED (2025-11-15 Late Evening)**:
All three CRITICAL bugs from adventure game testing have been resolved:
- ✅ **BUG-061 RESOLVED**: Field value reads now work - `resolveObjectClass()` checks actual field type
- ✅ **BUG-059 RESOLVED**: Array field access in methods works - field arrays detected and routed to array lowering
- ✅ **BUG-060 RESOLVED**: Methods on class parameters work - `Param` extended with `objectClass` support

**OOP System Status**: ✅ CORE FUNCTIONALITY RESTORED - Only 2 moderate bugs remain (BUG-057, BUG-058)

**Othello Game Stress Testing (2025-11-15)**:
Built an OOP Othello/Reversi game testing arrays, game logic, move validation, and complex control flow. Discovered 4 new bugs including BUG-053 (CRITICAL compiler crash when accessing global arrays in functions). See `/bugs/bug_testing/othello_simple.bas` for working implementation demonstrating board representation, piece flipping, and score tracking.

**New Bugs from Othello Testing**:
- ✅ **BUG-053 RESOLVED (2025-11-15)**: Cannot access global arrays in SUB/FUNCTION - already fixed via resolveVariableStorage()
- ℹ️ **BUG-054 BY DESIGN**: STEP reserved word cannot be used as variable name (intentional, STEP is FOR loop keyword)
- ℹ️ **BUG-055 BY DESIGN**: Cannot assign to FOR loop variable inside loop body (intentional semantic check)
- ✅ **BUG-056 RESOLVED (2025-11-15)**: Arrays not allowed as class fields - FULLY FIXED (parser + semantic + lowering + ctor allocation)

**Root Cause Investigation (2025-11-15)**:
Conducted detailed root cause analysis of all 5 bugs from Othello testing. Found that BUG-053 was already fixed in recent commits, BUG-054 and BUG-055 are intentional language design decisions, BUG-052 is partially implemented but needs error handler block terminators, and BUG-056 was fully resolved with complete implementation.

**BUG-056 Complete Fix (2025-11-15)**:
Fully implemented array fields in classes with parser, AST, semantic analysis, lowering, and constructor initialization:
1. ✅ Parser recognizes array field declarations (`Parser_Stmt_OOP.cpp:147-186`)
2. ✅ AST extended with array field metadata (`ast/StmtDecl.hpp:224-227`)
3. ✅ Semantic analysis validates array field access as MethodCallExpr (`SemanticAnalyzer.Stmts.Runtime.cpp:403-438`)
4. ✅ Lowering handles array field loads/stores (`Lower_OOP_Expr.cpp`, `Emit_Expr.cpp`)
5. ✅ Constructor allocates array storage (`Lower_OOP_Emit.cpp` - `rt_arr_*_new` calls)
6. ✅ Field layout and offsets correctly calculated for pointer-sized array handles

---
