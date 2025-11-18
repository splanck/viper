# VIPER BASIC Known Bugs and Issues

*Last Updated: 2025-11-18*

**Bug Statistics**: 90 resolved, 2 outstanding bugs, 3 design decisions (95 total documented)

**Test Suite Status**: 642/642 tests passing (100%)

**STATUS**: ⚠️ **2 OUTSTANDING BUGS** - BUG-094 (2D arrays in classes), BUG-095 (Array bounds check false positive)

---

## OUTSTANDING BUGS

### BUG-094: 2D Array Assignments in Class Methods Store All Values at Same Location
**Status**: ⚠️ **OUTSTANDING** - Discovered 2025-11-18
**Discovered**: 2025-11-18 (Chess game development - board representation)
**Category**: OOP / Arrays / Memory Management
**Severity**: HIGH - Breaks 2D array usage in classes

**Symptom**: When assigning values to different elements of a 2D array that is a class field, all assignments appear to write to the same location. Reading back the values shows the last assigned value in all positions.

**Minimal Reproduction**:
```basic
CLASS Test
    DIM pieces(7, 7) AS INTEGER

    SUB New()
        me.pieces(7, 0) = 4
        me.pieces(7, 1) = 2
        me.pieces(7, 2) = 3
    END SUB

    SUB Display()
        PRINT me.pieces(7, 0)  ' Prints 3 (expect 4)
        PRINT me.pieces(7, 1)  ' Prints 3 (expect 2)
        PRINT me.pieces(7, 2)  ' Prints 3 (expect 3)
    END SUB
END CLASS
```

**Expected**: Each array element should store its assigned value
**Actual**: All elements contain the last assigned value (3 in this case)

**Impact**: HIGH - Makes it impossible to use 2D arrays as class fields for game boards, matrices, etc.

**Workaround**: Use 1D arrays with calculated indices: `index = row * width + col`

**Test File**: `/tmp/test_class_array.bas`

### BUG-095: Array Bounds Check Reports False Positive with Valid Indices
**Status**: ⚠️ **OUTSTANDING** - Discovered 2025-11-18
**Discovered**: 2025-11-18 (Chess game with ANSI colors - Display function)
**Category**: Runtime / Arrays / Bounds Checking
**Severity**: MEDIUM - False error reported but program continues to work

**Symptom**: Runtime reports "rt_arr_i32: index 72 out of bounds (len=64)" even though all array accesses use calculated indices that should be within bounds (0-63).

**Context**: Chess game using 1D array of size 64 (DIM pieces(63)) with index calculation `row * 8 + col` where row ∈ [0,7] and col ∈ [0,7].

**Observed Behavior**:
- Array declared as `DIM pieces(63) AS INTEGER` (64 elements, indices 0-63)
- All accesses use `idx = row * 8 + col` where both row and col are in range [0,7]
- Maximum index should be 7*8+7 = 63 (valid)
- Runtime reports accessing index 72 (which would be row=9, col=0: 9*8+0=72)
- Despite error, program continues to execute and display correctly

**Impact**: MEDIUM - Error message is confusing but doesn't prevent program from working

**Workaround**: None needed - program works despite error message

**Test File**: `/tmp/chess.bas`

**Notes**: Index 72 suggests row=9 is being calculated somewhere, but code inspection shows all loops use correct bounds (row: 7 TO 0, col: 0 TO 7)

---

## OUTSTANDING BUGS (Previously Resolved)

### BUG-092: Nested IF Statements in FUNCTIONs Execute Incorrect Branch
**Status**: ✅ **RESOLVED** - Fixed 2025-11-18
**Discovered**: 2025-11-18 (Chess game development - board display issue)
**Category**: Control Flow / Functions / Conditionals
**Severity**: HIGH - Causes incorrect program logic in FUNCTIONs

**Symptom**: When using nested IF statements inside a FUNCTION, the condition evaluation appears to be inverted or incorrect. The same code works correctly in SUBs.

**Minimal Reproduction**:
```basic
CONST WHITE = 0
CONST BLACK = 1
CONST PAWN = 1

FUNCTION GetSymbol(color AS INTEGER, piece AS INTEGER) AS STRING
    DIM symbol AS STRING

    REM This nested IF behaves incorrectly!
    IF color = WHITE THEN
        IF piece = PAWN THEN symbol = "P"
    ELSE
        IF piece = PAWN THEN symbol = "p"
    END IF

    GetSymbol = symbol
END FUNCTION

REM Calling GetSymbol(WHITE, PAWN) returns "p" instead of "P"
REM Calling GetSymbol(BLACK, PAWN) returns "" instead of "p"
```

**Expected**:
- `GetSymbol(WHITE, PAWN)` should return "P"
- `GetSymbol(BLACK, PAWN)` should return "p"

**Actual**:
- `GetSymbol(WHITE, PAWN)` returns "p" (ELSE branch executed instead of THEN)
- `GetSymbol(BLACK, PAWN)` returns "" (THEN branch executed but nested IF doesn't match)

**Test Files**: `/tmp/test_piece_symbol.bas`, `/tmp/test_if_condition.bas`, `/tmp/test_compound.bas`

**Impact**: HIGH - Affects program correctness in any FUNCTION using nested IF statements.

**Workaround**: Use flat compound conditions with AND instead of nesting:
```basic
FUNCTION GetSymbol(color AS INTEGER, piece AS INTEGER) AS STRING
    DIM symbol AS STRING
    IF color = WHITE AND piece = PAWN THEN symbol = "P"
    IF color = BLACK AND piece = PAWN THEN symbol = "p"
    GetSymbol = symbol
END FUNCTION
```

**Notes**:
- IF statements work correctly in SUBs and at module level
- Compound conditions with AND/OR work correctly as workaround
- SELECT CASE cannot be used as workaround (doesn't support CONST identifiers in labels)

**ROOT CAUSE** (Identified 2025-11-18):

The bug is **specifically with single-line IF statements (without END IF) nested inside IF/ELSE blocks in FUNCTIONs**. Investigation revealed:

1. **Pattern that triggers the bug**:
   ```basic
   IF outer_condition THEN
       IF inner1 THEN statement1    REM Single-line IF
       IF inner2 THEN statement2    REM Single-line IF
   ELSE
       IF inner3 THEN statement3    REM Single-line IF
   END IF
   ```

2. **Pattern that works correctly**:
   ```basic
   IF outer_condition THEN
       IF inner1 THEN           REM Block IF with END IF
           statement1
       END IF
   ELSE
       IF inner3 THEN
           statement3
       END IF
   END IF
   ```

3. **Test Results**:
   - Single-level IFs in FUNCTIONs: ✓ Work correctly
   - Block-form nested IFs in FUNCTIONs: ✓ Work correctly
   - Single-line nested IFs in FUNCTIONs: ✗ BROKEN (outer condition inverted)
   - All IF forms in SUBs: ✓ Work correctly
   - All IF forms at module level: ✓ Work correctly

4. **Specific Behavior**: When the outer IF condition evaluates to TRUE, the ELSE branch is executed. When it evaluates to FALSE, the THEN branch is executed (but nested single-line IFs don't match, leaving variables unassigned).

**Location**: Parser bug in `src/frontends/basic/Parser_Stmt_If.cpp:184`

**FIX** (Implemented 2025-11-18):

The bug was in the parser, not the lowerer. The `parseElseChain` function (which handles single-line IF statements) was calling `skipOptionalLineLabelAfterBreak` to look for ELSE/ELSEIF keywords. This function **skips line breaks**, allowing single-line IFs to incorrectly consume ELSE keywords from the next line when nested inside multi-line IF blocks.

**Example of the bug**:
```basic
IF 1 = 1 THEN
    IF 2 = 2 THEN PRINT "A"
ELSE               REM This ELSE should belong to outer IF
    PRINT "B"
END IF
```

Before the fix, the parser produced this AST:
```
IF (1 = 1) THEN
  (IF (2 = 2) THEN PRINT "A" ELSE PRINT "B")  ← ELSE wrongly attached to inner IF!
```

After the fix, the parser produces the correct AST:
```
IF (1 = 1) THEN
  (IF (2 = 2) THEN PRINT "A")
ELSE
  PRINT "B"
```

**Changed**: `src/frontends/basic/Parser_Stmt_If.cpp:184`
- **Before**: Called `skipOptionalLineLabelAfterBreak` which skips line breaks looking for ELSE
- **After**: Removed the line break skipping; single-line IFs now only consume ELSE on the same line

**Testing**: All 276 BASIC tests pass with the fix. The parser now correctly distinguishes between:
- Single-line IFs with ELSE on same line: `IF cond THEN stmt1 ELSE stmt2`
- Single-line IFs nested in multi-line blocks: ELSE belongs to the outer block

**Files Changed**: `src/frontends/basic/Parser_Stmt_If.cpp`
**Test Files**: `/tmp/test_parser_else_bug.bas`, `/tmp/test_single_line_if_func.bas`

### BUG-093: INPUT Statement Treats STRING Variables as Numbers
**Status**: ✅ **RESOLVED** - Fixed 2025-11-18
**Discovered**: 2025-11-18 (Chess game development - interactive input attempt)
**Category**: I/O / Type System / Variables
**Severity**: HIGH - Blocks interactive string input in programs

**Symptom**: When using INPUT to read into a STRING variable, the variable is subsequently treated as a numeric type by the compiler, causing type mismatch errors when passing to string functions.

**Minimal Reproduction**:
```basic
DIM move AS STRING
PRINT "Enter move: ";
INPUT move
REM This causes error: "LEFT$: arg 1 must be string (got number)"
PRINT LEFT$(move, 1)
```

**Expected**: Variable `move` should remain type STRING after INPUT

**Actual**: After INPUT, the variable is treated as a numeric type

**Test Files**: `/tmp/test_input_string.bas`, `/tmp/chess_v2_moves.bas`

**Impact**: HIGH - Makes it impossible to create interactive programs that read string input from users.

**Workaround**: None currently. Must use hardcoded moves or file-based input instead.

**ROOT CAUSE** (Identified 2025-11-18):

The bug is in the semantic analyzer's `resolveAndTrackSymbol` function in `src/frontends/basic/SemanticAnalyzer.cpp` at lines 149-169.

**The Problem**:
When INPUT is analyzed, it calls `resolveAndTrackSymbol(name, SymbolKind::InputTarget)`. This function has logic that **unconditionally resets the variable's type to the default type** when `kind == SymbolKind::InputTarget`:

```cpp
const bool forceDefault = kind == SymbolKind::InputTarget;  // Line 149
auto itType = varTypes_.find(name);
if (forceDefault || itType == varTypes_.end())
{
    Type defaultType = Type::Int;  // Line 153
    if (!name.empty())
    {
        if (name.back() == '$')
            defaultType = Type::String;
        else if (name.back() == '#' || name.back() == '!')
            defaultType = Type::Float;
    }
    // ...
    varTypes_[name] = defaultType;  // Line 168 - OVERWRITES existing type!
}
```

**Execution Flow for `DIM move AS STRING; INPUT move`**:
1. `DIM move AS STRING` sets `varTypes_["move"] = Type::String` ✓
2. `INPUT move` calls `resolveAndTrackSymbol("move", SymbolKind::InputTarget)`
3. `forceDefault = true` because kind is InputTarget (line 149)
4. Since "move" doesn't end with $, default type is `Type::Int` (line 153)
5. **BUG**: `varTypes_["move"] = Type::Int` (line 168) - **overwrites the STRING type!**
6. All subsequent uses of `move` now treat it as INTEGER

**FIX** (Implemented 2025-11-18):

The fix was simple: remove the `forceDefault` logic that unconditionally overwrites variable types when processing INPUT statements.

**Changed**: `src/frontends/basic/SemanticAnalyzer.cpp:149-152`

**Before**:
```cpp
const bool forceDefault = kind == SymbolKind::InputTarget;
auto itType = varTypes_.find(name);
if (forceDefault || itType == varTypes_.end())
{
    // Sets default type, overwriting DIM types!
    varTypes_[name] = defaultType;
}
```

**After**:
```cpp
// BUG-093 fix: Do not override explicitly declared types (from DIM) when processing
// INPUT statements. Only set default type if the variable has no type yet.
auto itType = varTypes_.find(name);
if (itType == varTypes_.end())
{
    // Only sets default type for undeclared variables
    varTypes_[name] = defaultType;
}
```

**Result**:
- Variables declared with `DIM name AS STRING` now retain their STRING type through INPUT
- Undeclared variables (e.g., `INPUT name$`) still get suffix-based default types
- All 276 BASIC tests pass

**Testing**:
- ✅ `DIM move AS STRING; INPUT move; PRINT LEFT$(move, 1)` - Works correctly
- ✅ `INPUT name$` (undeclared) - Still uses suffix-based STRING type
- ✅ `INPUT age` (undeclared) - Still uses default INTEGER type
- ✅ Mixed declared/undeclared variables - All work correctly

**Files Changed**: `src/frontends/basic/SemanticAnalyzer.cpp`
**Test Files**: `/tmp/test_input_string.bas`, `/tmp/test_input_undeclared.bas`, `/tmp/test_input_mixed.bas`

---

## RECENTLY RESOLVED BUGS

### BUG-091: Compiler Crash with 2D Array Access in Expressions
**Status**: ✅ **RESOLVED** - Fixed 2025-11-17
**Discovered**: 2025-11-17 (Chess Engine v2 stress test)
**Category**: Arrays / Expression Analysis / Compiler Crash
**Severity**: CRITICAL - Compiler crash, blocks multi-dimensional array usage

**Symptom**: Accessing 2D (or higher) arrays in expressions causes compiler crash with assertion failure in expression type scanner.

**Error Message**:
```
Assertion failed: (!stack_.empty()), function pop, file Scan_ExprTypes.cpp, line 108
```

**Minimal Reproduction**:
```basic
DIM board(8, 8) AS INTEGER
board(1, 1) = 5

DIM x AS INTEGER
x = board(1, 1) + 10  REM CRASH!
```

**ROOT CAUSE**: In `Scan_ExprTypes.cpp`, the `after(const ArrayExpr &expr)` method only handled the deprecated single-index field (`expr.index`), not the multi-dimensional indices vector (`expr.indices`). For 2D arrays with two indices, only one value was popped from the expression type stack, leaving the stack imbalanced.

**FIX**: Updated `after(const ArrayExpr &expr)` to loop through all indices in `expr.indices` and pop each one:
```cpp
void after(const ArrayExpr &expr)
{
    if (expr.index != nullptr)
    {
        (void)pop();  // Legacy single index
    }
    else if (!expr.indices.empty())
    {
        // Pop all indices for multi-dimensional arrays
        for (size_t i = 0; i < expr.indices.size(); ++i)
        {
            (void)pop();
        }
    }
    push(ExprType::I64);
}
```

**Files Modified**: `src/frontends/basic/lower/Scan_ExprTypes.cpp`

### BUG-090: Cannot Pass Object Array Field as Parameter (Runtime Crash)
**Status**: ✅ **RESOLVED** - Fixed 2025-11-17 (resolved together with BUG-089)
**Discovered**: 2025-11-17 (Chess Engine v2 stress test)
**Category**: OOP / Runtime / Arrays / Parameters
**Severity**: CRITICAL - Runtime crash

**Symptom**: Passing an object array that is a class field as a parameter causes runtime crash with assertion failure.

**Error Message**:
```
Assertion failed: (hdr->elem_kind == RT_ELEM_NONE), function rt_arr_obj_assert_header, file rt_array_obj.c, line 34.
```

**Minimal Reproduction**:
```basic
CLASS Item
    val AS INTEGER
END CLASS

SUB InitArray(items() AS Item)
    items(1) = NEW Item()
END SUB

CLASS Container
    items(3) AS Item
    SUB InitItems()
        InitArray(items)  REM CRASH!
    END SUB
END CLASS
```

**ROOT CAUSE**: Constructor generation in `Lower_OOP_Emit.cpp` allocated object array fields using `rt_arr_i32_new` (which sets `elem_kind = RT_ELEM_I32`) instead of `rt_arr_obj_new` (which sets `elem_kind = RT_ELEM_NONE`).

**FIX**: Added object array detection in constructor generation:
```cpp
else if (!field.objectClassName.empty())
{
    requireArrayObjNew();
    handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_obj_new", {length});
}
```

**Files Modified**: `src/frontends/basic/Lower_OOP_Emit.cpp`

### BUG-089: Cannot Call Methods on Object Array Fields from Class Methods
**Status**: ✅ **RESOLVED** - Fixed 2025-11-17
**Discovered**: 2025-11-17 (Chess Engine v2 stress test)
**Category**: OOP / Arrays / Method Calls / Code Generation
**Severity**: CRITICAL - Unknown callee error, blocks object-oriented programming

**Symptom**: Attempting to call a method on an object array element that is a class field results in "unknown callee" compiler error.

**Error Message**:
```
error: unknown callee @SETVAL
```

**Minimal Reproduction**:
```basic
CLASS Item
    SUB SetVal(v AS INTEGER)
        val = v
    END SUB
    val AS INTEGER
END CLASS

CLASS Container
    items(10) AS Item

    SUB InitItem(idx AS INTEGER)
        items(idx).SetVal(42)  REM ERROR: unknown callee @SETVAL
    END SUB
END CLASS
```

**ROOT CAUSE**: Multiple code paths didn't properly detect and handle object arrays in class fields:
1. Array access expressions didn't track if they were member object arrays
2. Method call lowering didn't recognize object array elements as objects
3. Object class resolution didn't handle array expressions for implicit fields

**FIX**: Enhanced 5 different code paths to detect and handle object arrays:
- `Lower_OOP_Emit.cpp`: Constructor generation using `rt_arr_obj_new`
- `Emit_Expr.cpp`: Track `isMemberObjectArray` for array access
- `LowerStmt_Runtime.cpp`: Handle object arrays in assignments and calls
- `Lower_OOP_Expr.cpp`: Resolve object class for array expressions
- `Lowerer_Expr.cpp`: Detect implicit field arrays

**Key Insight**: In BASIC, `items(i)` is parsed as `CallExpr`, not `ArrayExpr`.

**Files Modified**:
- `src/frontends/basic/Lower_OOP_Emit.cpp`
- `src/frontends/basic/lower/Emit_Expr.cpp`
- `src/frontends/basic/lower/LowerStmt_Runtime.cpp`
- `src/frontends/basic/Lower_OOP_Expr.cpp`
- `src/frontends/basic/lower/Lowerer_Expr.cpp`

---

## OLDER RESOLVED BUGS (from Chess Engine v2 Stress Test)

- ✅ **BUG-086**: Array Parameters Not Supported in SUBs/FUNCTIONs - RESOLVED 2025-11-17
- ✅ **BUG-087**: Nested IF Statements Inside SELECT CASE Causes IL Errors - RESOLVED 2025-11-17
- ✅ **BUG-083**: Cannot Call Methods on Object Array Elements - RESOLVED 2025-11-17
- ✅ **BUG-075**: Arrays of Custom Class Types Use Wrong Runtime Function - RESOLVED 2025-11-17
- ✅ **BUG-076**: Object Assignment in SELECT CASE Causes Type Mismatch - RESOLVED 2025-11-17
- ✅ **BUG-078**: FOR Loop Variable Stuck at Zero When Global Variable Used in SUB - RESOLVED 2025-11-17
- ✅ **BUG-079**: Global String Arrays Cannot Be Assigned From SUBs - RESOLVED 2025-11-17
- ✅ **BUG-080**: INPUT Statement Only Works with INTEGER Type - RESOLVED 2025-11-17
- ✅ **BUG-082**: Cannot Call Methods on Nested Object Members - RESOLVED 2025-11-17
- ✅ **BUG-084**: String FUNCTION Methods Completely Broken - RESOLVED 2025-11-17
- ✅ **BUG-085**: Object Array Access in ANY Loop Causes Code Generation Errors - RESOLVED 2025-11-17
- ✅ **BUG-077**: Cannot Use Object Member Variables in Expressions - RESOLVED 2025-11-17

---

## DESIGN DECISIONS (Not Bugs)

### BUG-088: COLOR Keyword Collision with Class Field Names
**Status**: ⚠️  **DESIGN DECISION** - Not a bug
**Category**: Keywords / Naming
**Resolution**: Reserved keywords cannot be used as identifiers. This is intentional language design.

### BUG-054: EXIT DO statement not supported
**Status**: ⚠️  **DESIGN DECISION** - Use EXIT WHILE/EXIT FOR instead

### BUG-055: Boolean expressions require extra parentheses
**Status**: ⚠️  **DESIGN DECISION** - Intentional for clarity

---

## RESOLVED BUGS (Previously Documented)

### Recently Resolved (2025-11-17)
- ✅ **BUG-067**: Array fields - RESOLVED 2025-11-17
- ✅ **BUG-068**: Function name implicit returns - RESOLVED 2025-11-17
- ✅ **BUG-070**: Boolean parameters - RESOLVED 2025-11-17
- ✅ **BUG-071**: String arrays - RESOLVED 2025-11-17
- ✅ **BUG-072**: SELECT CASE blocks after exit - RESOLVED 2025-11-17
- ✅ **BUG-073**: Object parameter methods - RESOLVED 2025-11-17
- ✅ **BUG-074**: Constructor corruption - RESOLVED 2025-11-17
- ✅ **BUG-081**: FOR loop with object member variables - RESOLVED 2025-11-17

### All Previously Resolved Bugs (BUG-001 through BUG-066)
- ✅ **BUG-001**: String concatenation requires $ suffix - RESOLVED 2025-11-12
- ✅ **BUG-002**: & operator for string concatenation not supported - RESOLVED 2025-11-12
- ✅ **BUG-003**: FUNCTION name assignment syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-004**: Procedure calls require parentheses - RESOLVED 2025-11-12
- ✅ **BUG-005**: SGN function not implemented - RESOLVED 2025-11-12
- ✅ **BUG-006**: Limited trigonometric/math functions - RESOLVED 2025-11-12
- ✅ **BUG-007**: Multi-dimensional arrays not supported - RESOLVED 2025-11-13
- ✅ **BUG-008**: REDIM PRESERVE syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-009**: CONST keyword not implemented - RESOLVED 2025-11-12
- ✅ **BUG-010**: STATIC keyword not implemented - RESOLVED 2025-11-14
- ✅ **BUG-011**: SWAP statement not implemented - RESOLVED 2025-11-12
- ✅ **BUG-012**: BOOLEAN type incompatibility with TRUE/FALSE - RESOLVED 2025-11-14
- ✅ **BUG-013**: SHARED keyword not supported - RESOLVED 2025-11-13
- ✅ **BUG-014**: String arrays not supported (duplicate of BUG-032) - RESOLVED 2025-11-13
- ✅ **BUG-015**: String properties in classes cause runtime error - RESOLVED 2025-11-13
- ✅ **BUG-016**: Local string variables in methods cause error - RESOLVED 2025-11-13
- ✅ **BUG-017**: Accessing global strings from methods causes segfault - RESOLVED 2025-11-14
- ✅ **BUG-018**: FUNCTION methods in classes cause code generation error - RESOLVED 2025-11-12
- ✅ **BUG-019**: Float literals assigned to CONST truncated to integers - RESOLVED 2025-11-14
- ✅ **BUG-020**: String constants cause runtime error - RESOLVED 2025-11-13
- ✅ **BUG-021**: SELECT CASE doesn't support negative integer literals - RESOLVED 2025-11-12
- ✅ **BUG-022**: Float literals without explicit type default to INTEGER - RESOLVED 2025-11-12
- ✅ **BUG-023**: DIM with initializer not supported - RESOLVED 2025-11-12
- ✅ **BUG-024**: CONST with type suffix causes assertion failure - RESOLVED 2025-11-12
- ✅ **BUG-025**: EXP of large values causes overflow trap - RESOLVED 2025-11-13
- ✅ **BUG-026**: DO WHILE loops with GOSUB cause empty block error - RESOLVED 2025-11-13
- ✅ **BUG-027**: MOD operator doesn't work with INTEGER type - RESOLVED 2025-11-12
- ✅ **BUG-028**: Integer division operator doesn't work with INTEGER type - RESOLVED 2025-11-12
- ✅ **BUG-029**: EXIT FUNCTION and EXIT SUB not supported - RESOLVED 2025-11-12
- ✅ **BUG-030**: SUBs and FUNCTIONs cannot access global variables - RESOLVED 2025-11-14
- ✅ **BUG-031**: String comparison operators not supported - RESOLVED 2025-11-12
- ✅ **BUG-032**: String arrays not supported - RESOLVED 2025-11-13
- ✅ **BUG-033**: String array assignment causes type mismatch (duplicate) - RESOLVED 2025-11-13
- ✅ **BUG-034**: MID$ does not convert float arguments to integer - RESOLVED 2025-11-13
- ✅ **BUG-035**: Global variables not accessible in SUB/FUNCTION (duplicate) - RESOLVED 2025-11-14
- ✅ **BUG-036**: String comparison in OR condition causes IL error - RESOLVED 2025-11-13
- ✅ **BUG-037**: SUB methods on class instances cannot be called - RESOLVED 2025-11-15
- ✅ **BUG-038**: String concatenation with method results fails - RESOLVED 2025-11-14
- ✅ **BUG-039**: Cannot assign method call results to variables - RESOLVED 2025-11-15
- ✅ **BUG-040**: Cannot use custom class types as function return types - RESOLVED 2025-11-15
- ✅ **BUG-041**: Cannot create arrays of custom class types - RESOLVED 2025-11-14
- ✅ **BUG-042**: Reserved keyword 'LINE' cannot be used as variable name - RESOLVED 2025-11-14
- ✅ **BUG-043**: String arrays not working (duplicate) - RESOLVED 2025-11-13
- ✅ **BUG-044**: CHR() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-045**: STRING arrays not working with AS STRING syntax - RESOLVED 2025-11-15
- ✅ **BUG-046**: Cannot call methods on array elements - RESOLVED 2025-11-15
- ✅ **BUG-047**: IF/THEN/END IF inside class methods causes crash - RESOLVED 2025-11-15
- ✅ **BUG-048**: Cannot call module-level SUB/FUNCTION from class methods - RESOLVED 2025-11-15
- ✅ **BUG-050**: SELECT CASE with multiple values causes IL error - NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-051**: DO UNTIL loop causes IL generation error - NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-052**: ON ERROR GOTO handler blocks missing terminators - RESOLVED 2025-11-15
- ✅ **BUG-053**: Cannot access global arrays in SUB/FUNCTION - RESOLVED 2025-11-15
- ✅ **BUG-056**: Arrays not allowed as class fields - RESOLVED 2025-11-15
- ✅ **BUG-057**: BOOLEAN return type in class methods causes type mismatch - RESOLVED 2025-11-15
- ✅ **BUG-058**: String array fields don't retain values - RESOLVED 2025-11-15
- ✅ **BUG-059**: Cannot access array fields within class methods - RESOLVED 2025-11-15
- ✅ **BUG-060**: Cannot call methods on class objects passed as parameters - RESOLVED 2025-11-15
- ✅ **BUG-061**: Cannot assign class field value to local variable - RESOLVED 2025-11-15
- ✅ **BUG-062**: CONST with CHR$() not evaluated at compile time - RESOLVED 2025-11-15
- ✅ **BUG-063**: Module-level initialization cleanup code leaks - RESOLVED 2025-11-15
- ✅ **BUG-064**: ASC() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-065**: Array field assignments silently dropped - RESOLVED 2025-11-15
- ✅ **BUG-066**: VAL() function not implemented - RESOLVED 2025-11-15

---

## ADDITIONAL INFORMATION

**For detailed bug descriptions and test cases:**
- See `/bugs/bug_testing/` directory for test cases and stress tests
- Recent bugs (089-091) documented above with full details
- Older bugs condensed to preserve space while maintaining history

**Testing Sources:**
- Chess Engine v2 stress test (discovered 12 critical bugs)
- Language audit and systematic feature testing
- Real-world application development (chess, baseball games)
