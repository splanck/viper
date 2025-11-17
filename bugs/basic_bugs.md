# VIPER BASIC Known Bugs and Issues

*Last Updated: 2025-11-17*

**Bug Statistics**: 78 resolved, 2 outstanding bugs, 1 design decision (81 total documented)

**Test Suite Status**: 642/642 tests passing (100%)

**STATUS**: ⚠️ 2 new OOP limitation bugs found during advanced stress testing (2025-11-17)

---

## OUTSTANDING BUGS

**2 bugs** - Found during advanced OOP stress testing (2025-11-17)

### BUG-081: FOR Loop Variable Cannot Be Object Member
**Status**: 🟡 **MEDIUM** - Limitation, not critical
**Discovered**: 2025-11-17 (Advanced baseball game stress test)
**Category**: Control Flow / OOP / Parsing
**Severity**: MEDIUM - Can work around with temp variables

**Symptom**: FOR loops cannot use object member fields as loop variables. The parser expects a simple identifier, not a member access expression.

**Error Message**:
```
error[B0001]: expected =, got .
FOR game.inning = 1 TO 9
        ^
```

**Reproduction**:
```basic
CLASS Game
    inning AS INTEGER
END CLASS

DIM game AS Game
game = NEW Game()

FOR game.inning = 1 TO 9  ' ERROR: Cannot use object member
    PRINT game.inning
NEXT
```

**Root Cause** (CODE INVESTIGATION 2025-11-17):

**Location**: `src/frontends/basic/Parser_Stmt_Loop.cpp:110-134` (`parseForStatement` function)

**Specific Issue**: Line 116 shows the limitation:
```cpp
Token varTok = expect(TokenKind::Identifier);  // Line 116
stmt->var = varTok.lexeme;                     // Line 117
```

The FOR parser expects a single `Identifier` token for the loop variable. It does not call `parseExpression()` or handle member access operators (`.`).

**Design Limitation**: The parser is designed to only accept simple variable names (identifiers) as FOR loop control variables. When it encounters `game.inning`, it:
1. Reads `game` as the identifier (line 116)
2. Expects `=` next
3. Finds `.` instead → parse error

**Impact**:
- Cannot use object members directly in FOR loops
- Must use temporary variable workaround
- Minor inconvenience, not a blocker

**Workaround**: Use a temporary variable:
```basic
DIM i AS INTEGER
FOR i = 1 TO 9
    game.inning = i
    PRINT game.inning
NEXT
```

**Test File**: `/bugs/bug_testing/baseball_full_game.bas` (line 335)

**Detailed Implementation Plan**:

**Step 1**: Modify ForStmt AST structure
- File: `src/frontends/basic/ast/StmtControl.hpp:197`
- Change `std::string var` to `ExprPtr varExpr`
- Accept both VarExpr (simple variable) and MemberAccessExpr (object member)
- Maintains backward compatibility with existing code

**Step 2**: Update FOR statement parser
- File: `src/frontends/basic/Parser_Stmt_Loop.cpp:116`
- Replace `expect(TokenKind::Identifier)` with expression parser
- Parse "lvalue expression" (assignable location)
- Accept: VarExpr, ArrayExpr, MemberAccessExpr
- Reject: literals, complex expressions, function calls
- Store parsed expression in `ForStmt::varExpr`

**Step 3**: Update semantic analyzer FOR handling
- File: `src/frontends/basic/sem/Check_Stmt_For.cpp` or similar
- Modify `trackForVariable()` to handle expressions, not just strings
- For VarExpr: extract name, use existing logic
- For MemberAccessExpr: validate base exists and field is numeric type
- For ArrayExpr: validate array element is numeric
- Update forStack_ to track expression or identifier

**Step 4**: Update FOR loop lowering
- File: `src/frontends/basic/lower/Lower_Loops.cpp:477-494`
- Replace `resolveVariableStorage(stmt.var)` with lvalue lowering
- Use `lowerLvalue(stmt.varExpr)` to get storage location pointer
- For member access: lower base object, compute field offset
- For array: lower array and index, compute element pointer
- Rest of loop logic unchanged (initialize, increment, test, branch)

**Step 5**: Handle NEXT statement matching
- Current code tracks loop variable names in forStack_ for NEXT validation
- With expressions, need new approach:
  - For VarExpr: extract name as before
  - For complex expressions: either skip NEXT matching or use heuristic
  - May need to store both expression and identifier

**Files to Modify**:
- `src/frontends/basic/ast/StmtControl.hpp` - ForStmt structure
- `src/frontends/basic/Parser_Stmt_Loop.cpp` - Parser logic
- `src/frontends/basic/sem/Check_Stmt_For.cpp` - Semantic analysis
- `src/frontends/basic/lower/Lower_Loops.cpp` - Lowering logic
- `src/frontends/basic/SemanticAnalyzer.hpp` - forStack_ type if needed

**Test Cases**:
- Simple variable: `FOR i = 1 TO 10`
- Member variable: `FOR game.inning = 1 TO 9` (BUG-081 reproduction)
- Nested member: `FOR team.player.number = 1 TO 25`
- Array element: `FOR scores(i) = 1 TO 100`
- Error: `FOR 5 = 1 TO 10` (literal, not lvalue)
- Error: `FOR Func() = 1 TO 10` (function call, not lvalue)
- NEXT statement with and without variable names

---

### BUG-082: Cannot Call Methods on Nested Object Members
**Status**: 🟡 **IN PROGRESS** - Infrastructure complete, semantic analyzer work needed
**Discovered**: 2025-11-17 (Advanced baseball game stress test)
**Last Updated**: 2025-11-17 (Partial fix implemented)
**Category**: OOP / Type Resolution / Method Calls / Semantic Analysis
**Severity**: HIGH - Limits OOP composition patterns

**Symptom**: Method calls on nested object members fail with "unknown procedure" error. The compiler cannot resolve the type of object fields to find their methods.

**Error Message**:
```
error[B1006]: unknown procedure 'game.awayteam.initplayer'
game.awayTeam.InitPlayer(1, "Trout", "CF")
^^^^^^^^^^^^^^^^^^^^^^^^
```

**Reproduction**:
```basic
CLASS Team
    SUB InitPlayer(num AS INTEGER, name AS STRING)
        PRINT "Player added"
    END SUB
END CLASS

CLASS Game
    awayTeam AS Team
END CLASS

DIM game AS Game
game = NEW Game()
game.awayTeam = NEW Team()
game.awayTeam.InitPlayer(1, "Test")  ' ERROR: Method not found
```

**Root Cause** (CODE INVESTIGATION 2025-11-17):

**PRIMARY ISSUE** - Semantic Analyzer Doesn't Validate MethodCallExpr:

**Location**: `src/frontends/basic/SemanticAnalyzer.Exprs.cpp:143-146` (MethodCallExpr visitor)

The semantic analyzer has a stub implementation for MethodCallExpr:
```cpp
/// @brief Method calls are treated as Unknown until OOP semantics are added.
void visit(MethodCallExpr &) override
{
    result_ = SemanticAnalyzer::Type::Unknown;
}
```

This means:
1. The parser correctly creates `MethodCallExpr` nodes with `base` expression and `method` name
2. The semantic analyzer accepts them without validation
3. No checks for: valid object base, method existence, or signature matching
4. Errors appear later as "unknown procedure" when trying to lower the call

**SECONDARY ISSUE** - Missing Metadata (FIXED):

The class field metadata (`FieldInfo`, `ClassLayout::Field`) didn't store object class names. This has been fixed:
- ✅ Added `objectClassName` to `ClassDecl::Field` (StmtDecl.hpp)
- ✅ Added `objectClassName` to `ClassInfo::FieldInfo` (Semantic_OOP.hpp)
- ✅ Added `objectClassName` to `ClassLayout::Field` (Lowerer.hpp)
- ✅ Updated parser to capture class names (Parser_Stmt_OOP.cpp)
- ✅ Updated semantic OOP indexing (Semantic_OOP.cpp)
- ✅ Updated layout building (Lower_OOP_Scan.cpp)
- ✅ Updated `resolveObjectClass()` to use stored names (Lower_OOP_Expr.cpp:108-117)

**Impact**:
- Cannot call methods on object fields (composition pattern broken)
- Cannot chain object member access for method calls
- Must use temporary variables for all nested object method calls
- Major limitation for OOP design patterns

**Workaround**: Use temporary variables:
```basic
DIM tempTeam AS Team
tempTeam = game.awayTeam
tempTeam.InitPlayer(1, "Test")  ' Works with direct object reference
```

**Test Files**:
- `/bugs/bug_testing/baseball_full_game.bas` (lines 304-312)
- `/tmp/test_bug082_exact.bas`, `/tmp/test_bug082_debug.bas`, `/tmp/test_bug082_simple.bas`

**Detailed Implementation Plan**:

**Step 1**: Create helper to resolve expression class type
- Add `std::string SemanticAnalyzer::resolveExpressionClass(const Expr& expr)`
- Handle VarExpr: look up variable's declared type in symbol table
- Handle MemberAccessExpr: recursively resolve base, then find field class
- Handle MeExpr: return current class context
- Return qualified class name or empty string

**Step 2**: Implement MethodCallExpr validation
- Replace stub at SemanticAnalyzer.Exprs.cpp:143-146
- Call `resolveExpressionClass(expr.base)` to get receiver class
- Look up method in `oopIndex_.findClass(className)->methods`
- Emit diagnostic if method not found on that class
- Validate arguments using existing call arg validation
- Infer and return method's return type

**Step 3**: Consider creating dedicated validation file
- Follow pattern: `src/frontends/basic/sem/Check_Expr_MethodCall.cpp`
- Implement `SemanticAnalyzer::Type analyzeMethodCallExpr(SemanticAnalyzer&, const MethodCallExpr&)`
- Keep visitor delegation in SemanticAnalyzer.Exprs.cpp

**Step 4**: Testing
- Test simple method call: `obj.Method()`
- Test nested member method: `game.awayTeam.InitPlayer()` (BUG-082 case)
- Test deep nesting: `a.b.c.Method()`
- Test ME.Method() in class context
- Error cases: method not found, wrong arguments

---

---

### BUG-075: Arrays of Custom Class Types Use Wrong Runtime Function
**Status**: ✅ RESOLVED (2025-11-17)
**Discovered**: 2025-11-17 (Baseball game stress test)
**Category**: OOP / Type System / Arrays
**Severity**: CRITICAL - Cannot use arrays of objects

**Symptom**: Arrays of custom class types are treated as integer arrays, causing type mismatch errors when assigning objects to array elements.

**Error Messages**:
```
error: call %t17 1 %t14: @rt_arr_i32_set value operand must be i64
```

**Reproduction**:
```basic
CLASS Player
    name AS STRING
END CLASS

CLASS Team
    lineup(9) AS Player

    SUB Init()
        ME.lineup(1) = NEW Player()  ' ERROR: Uses rt_arr_i32_set instead of rt_arr_ptr_set
    END SUB
END CLASS
```

**Root Cause** (CODE INVESTIGATION 2025-11-17):

**Location**: `src/frontends/basic/LowerStmt_Runtime.cpp:286-368` (`assignArrayElement` function)

**Specific Issue**: Lines 354-359 check for object arrays with the condition:
```cpp
else if (!isMemberArray && info && info->isObject)
{
    requireArrayObjPut();
    emitCall("rt_arr_obj_put", {access.base, access.index, value.value});
}
```

The condition `!isMemberArray` **excludes member arrays** (dotted names like `ME.lineup`). For member arrays, the code only checks if `memberElemAstType == ::il::frontends::basic::Type::Str` (line 344) to use string array setters.

**There is no check for object member arrays** - they are not detected as object types, so they fall through to the default case at lines 362-367 which calls `rt_arr_i32_set` (integer array setter) instead of `rt_arr_obj_put`.

**Fix needed**: Add object type check for `memberElemAstType` similar to the string check at line 354, or extend the condition at line 354 to include member arrays with object element types.

**Impact**:
- Cannot use arrays of custom classes
- Severely limits OOP capabilities
- Requires ugly workarounds with individual fields

**Workaround**: Use individual fields instead of arrays:
```basic
CLASS Team
    player1 AS Player
    player2 AS Player
    ' ... etc (very ugly but works)
END CLASS
```

**Test File**: `/bugs/bug_testing/baseball_team.bas` (line 21, 31)

**Related Code**:
- Array lowering for object types
- Type analysis for class member arrays
- Runtime array setter selection

---

### BUG-076: Object Assignment in SELECT CASE Causes Type Mismatch
**Status**: ✅ RESOLVED (2025-11-17)
**Discovered**: 2025-11-17 (Baseball game stress test)
**Category**: OOP / Control Flow / Type System

**Symptom**: Assigning objects to local variables or fields inside SELECT CASE blocks causes "operand type mismatch" errors.

**Error Message**:
```
error: store %t27 %t25: operand type mismatch: operand 1 must be i64
```

**Reproduction**:
```basic
FUNCTION GetPlayer(idx AS INTEGER) AS Player
    DIM result AS Player
    SELECT CASE idx
        CASE 1
            result = ME.player1  ' ERROR: Type mismatch
    END SELECT
    GetPlayer = result
END FUNCTION
```

**Root Cause** (CODE INVESTIGATION 2025-11-17):

**Location**: SELECT CASE code generation (likely `src/frontends/basic/SelectCaseLowering.cpp` or `src/frontends/basic/lower/Lower_Switch.cpp`)

**Specific Issue**: When object assignments occur within SELECT CASE blocks, the IL store instruction receives mismatched operand types. The error message "operand type mismatch: operand 1 must be i64" indicates that:
1. The target slot expects an i64 (integer) value
2. But the source value is a pointer (object reference)

**Root Analysis**: This is likely related to **BUG-075's underlying type system issue**. Within SELECT CASE blocks, variable type resolution may be failing to correctly identify object types, defaulting them to INTEGER (i64) instead of POINTER types. This could be:
- Type inference failure in CASE block scope
- Incorrect slot type assignment when variables are used in CASE blocks
- SELECT CASE lowering not preserving type metadata for object variables

The same symbol/type resolution bug that affects array element type detection (BUG-075) appears to affect local variable types within SELECT CASE contexts.

**Fix needed**: Ensure SELECT CASE lowering preserves correct type information for all variables, especially object/pointer types. May require fixing the underlying type resolution system that both BUG-075 and BUG-076 depend on.

**Impact**:
- Cannot safely use object assignments in SELECT CASE
- Limits control flow options for OOP code

**Test File**: `/bugs/bug_testing/baseball_team_v2.bas` (line 38, 71)

---

### BUG-078: FOR Loop Variable Stuck at Zero When Global Variable Used in SUB
**Status**: ✅ RESOLVED (2025-11-17)
**Discovered**: 2025-11-17 (Baseball game stress test)
**Category**: Control Flow / Scoping / FOR Loops
**Severity**: CRITICAL - FOR loops don't work correctly with globals

**Symptom**: When a FOR loop uses a global variable that is accessed inside a SUB/FUNCTION, the variable remains at 0 for all iterations instead of incrementing.

**Error**: No compile/runtime error - silent logic bug

**Reproduction**:
```basic
DIM inning AS INTEGER

SUB ShowInfo()
    PRINT "  In SUB, inning = "; inning  ' Always shows 0!
END SUB

FOR inning = 1 TO 3
    PRINT "Main: inning = "; inning  ' Also shows 0!
    ShowInfo()
NEXT
```

**Output** (WRONG):
```
Main: inning = 0
  In SUB, inning = 0
Main: inning = 0
  In SUB, inning = 0
Main: inning = 0
  In SUB, inning = 0
```

**Expected Output**:
```
Main: inning = 1
  In SUB, inning = 1
Main: inning = 2
  In SUB, inning = 2
Main: inning = 3
  In SUB, inning = 3
```

**Root Cause** (CODE INVESTIGATION 2025-11-17):

**Location**: `src/frontends/basic/lower/Lower_Loops.cpp:468-483` (`lowerFor` function)

**Specific Issue**: Lines 475-478 show the problem:
```cpp
const auto *info = findSymbol(stmt.var);  // Line 475
assert(info && info->slotId);             // Line 476
Value slot = Value::temp(*info->slotId);  // Line 477
emitStore(Type(Type::Kind::I64), slot, start.value);  // Line 478 - STORES TO SLOT
```

The FOR loop stores the loop counter to `slot`, which is retrieved from `findSymbol(stmt.var)`. When a global variable is used as a FOR loop variable:

1. **Symbol Resolution Issue**: `findSymbol(stmt.var)` may return a **local symbol entry** created for the FOR loop's local scope, NOT the global variable's symbol entry
2. **Storage Mismatch**: The FOR loop updates the local slot (`*info->slotId`)
3. **Global Unchanged**: The global variable's storage location is never updated
4. **SUB Reads Global**: When SUBs call `findSymbol("inning")`, they resolve to the **global** symbol, which remains at its initial value (0)
5. **Main Code Also Reads Local**: Even main code reads from the local slot within the FOR loop

**The Fix Needed**: When a FOR loop variable matches a global variable name, the lowering should:
- Detect that the variable is global (check parent scopes)
- Store directly to the global's storage location, not create a loop-local slot
- OR: Update both the loop-local slot AND the global storage on each iteration

**Impact**:
- FOR loops with global variables don't work
- Silent data corruption - no error message
- Major control flow bug affecting real programs

**Workaround**: Use a different variable name for the loop, copy to global:
```basic
DIM inning AS INTEGER
DIM i AS INTEGER

FOR i = 1 TO 3
    inning = i  ' Manually copy
    ShowInfo()
NEXT
```

**Test File**: `/bugs/bug_testing/test_for_in_sub.bas`
**Also affects**: `/bugs/bug_testing/baseball_v2.bas`

**Related Code**:
- FOR loop lowering and variable scoping
- Global variable resolution in loops
- Loop counter management

---

### BUG-079: Global String Arrays Cannot Be Assigned From SUBs
**Status**: ✅ RESOLVED (2025-11-17)
**Discovered**: 2025-11-17 (Baseball game stress test)
**Category**: Arrays / Type System / Scoping
**Severity**: CRITICAL - String arrays broken in SUBs

**Symptom**: Global string arrays can be assigned at module level, but attempting to assign to them from within a SUB/FUNCTION causes type mismatch error using wrong runtime function.

**Error Message**:
```
error: call %t5 1 %t2: @rt_arr_i32_set value operand must be i64
```

**Reproduction**:
```basic
DIM names(3) AS STRING

SUB TestAssign()
    names(1) = "Alice"  ' ERROR: Uses rt_arr_i32_set instead of string array setter
END SUB

TestAssign()
```

**Works at module level**:
```basic
DIM names(3) AS STRING
names(1) = "Alice"  ' This works fine!
PRINT names(1)  ' Prints "Alice"
```

**Root Cause** (CODE INVESTIGATION 2025-11-17):

**Location**: `src/frontends/basic/LowerStmt_Runtime.cpp:286-368` (`assignArrayElement` function)

**Specific Issue**: Same root cause as **BUG-075**. The problematic code at lines 343-353:
```cpp
if ((info && info->type == AstType::Str) ||
    (isMemberArray && memberElemAstType == ::il::frontends::basic::Type::Str) ||
    (isImplicitFieldArray && memberElemAstType == ::il::frontends::basic::Type::Str))
{
    // String array: use rt_arr_str_put
    Value tmp = emitAlloca(8);
    emitStore(Type(Type::Kind::Str), tmp, value.value);
    emitCall("rt_arr_str_put", {access.base, access.index, tmp});
}
```

**The Problem**: When accessing global string arrays from within SUBs/FUNCTIONs:
1. `info = findSymbol(target.name)` at line 293 **fails to find the global array** or returns wrong type information
2. `info->type` is NOT `AstType::Str` even though the array IS a string array
3. The array is neither a member array (`isMemberArray == false`) nor an implicit field array
4. Falls through to the default case (lines 362-367) which calls `rt_arr_i32_set`

**Symbol Resolution Failure**: In SUB/FUNCTION contexts, `findSymbol()` is not correctly resolving global array types. The symbol lookup either:
- Returns `nullptr` (info is null)
- Returns symbol info with wrong type (info->type != AstType::Str)
- Uses a different scope chain that doesn't see global array metadata

**Module level works** because at module scope, `findSymbol()` correctly finds the array's type information.

**Fix needed**: Fix `findSymbol()` to correctly resolve global variable types when called from SUB/FUNCTION contexts, ensuring `info->type` correctly reflects the actual array element type (STRING, OBJECT, etc.) regardless of calling context.

**Impact**:
- Cannot use string arrays in SUBs/FUNCTIONs
- Regression of previously "resolved" BUG-071
- Severely limits string array usage in real programs
- Related to BUG-075 (object arrays) - same underlying type system issue

**Workaround**: Only assign to string arrays at module level (very restrictive):
```basic
DIM names(3) AS STRING
DIM tempName AS STRING

SUB AddName(n AS STRING)
    tempName = n  ' Store in temp
END SUB

AddName("Alice")
names(1) = tempName  ' Assign at module level
```

**Test Files**:
- `/bugs/bug_testing/test_string_array_sub.bas` - Minimal reproduction
- `/bugs/bug_testing/baseball_stats.bas` - Real-world usage that triggered it

**Related Code**:
- Array type resolution in SUB/FUNCTION contexts
- Global variable type lookup
- Same root cause as BUG-075 (object arrays)

**Note**: BUG-071 was marked as resolved but clearly string arrays are not fully working. This appears to be a scoping/context-specific regression.

---

### BUG-080: INPUT Statement Only Works with INTEGER Type
**Status**: ✅ RESOLVED (2025-11-17)
**Discovered**: 2025-11-17 (Baseball game stress test)
**Category**: Input/Output / Type System
**Severity**: HIGH - Cannot read STRING or SINGLE from console

**Symptom**: The INPUT statement only works with INTEGER variables. Attempting to INPUT into STRING or SINGLE variables causes a runtime trap with "expected numeric value" error.

**Error Message**:
```
Trap @main#1 line X: DomainError (code=0): INPUT: expected numeric value
```

**Reproduction**:
```basic
DIM name AS STRING
PRINT "Enter name: ";
INPUT name  ' ERROR: expected numeric value

DIM score AS SINGLE
PRINT "Enter score: ";
INPUT score  ' ERROR: expected numeric value

DIM num AS INTEGER
PRINT "Enter number: ";
INPUT num  ' WORKS!
```

**Root Cause** (CODE INVESTIGATION 2025-11-17):

**Location**: `src/frontends/basic/LowerStmt_IO.cpp:466-539` (`lowerInput` function)

**Specific Issue**: The `storeField` lambda (lines 483-516) handles type dispatching:
```cpp
auto storeField = [&](const std::string &name, Value field)
{
    auto storage = resolveVariableStorage(name, stmt.loc);  // Line 485
    assert(storage && "INPUT target should have storage");
    SlotType slotInfo = storage->slotInfo;                   // Line 487
    Value target = storage->pointer;
    if (slotInfo.type.kind == Type::Kind::Str)               // Line 489 - STRING CHECK
    {
        emitStore(Type(Type::Kind::Str), target, field);
        return;
    }

    if (slotInfo.type.kind == Type::Kind::F64)               // Line 495 - FLOAT CHECK
    {
        Value f = emitCallRet(Type(Type::Kind::F64), "rt_to_double", {field});
        emitStore(Type(Type::Kind::F64), target, f);
        // ... release code
        return;
    }

    Value n = emitCallRet(Type(Type::Kind::I64), "rt_to_int", {field});  // Line 504 - FALLBACK
    // ... integer storage
}
```

**The Problem**: The error "INPUT: expected numeric value" comes from `rt_to_int()` at runtime (see `src/runtime/rt_string_format.c:68`). This means:

1. For STRING variables: `slotInfo.type.kind` is **NOT** `Type::Kind::Str`, so line 489's check fails
2. For SINGLE variables: `slotInfo.type.kind` is **NOT** `Type::Kind::F64`, so line 495's check fails
3. Both fall through to line 504 which calls `rt_to_int()` on the user's input
4. `rt_to_int()` tries to parse the input as a number and traps when it's not numeric

**Root Cause**: `resolveVariableStorage()` at line 485 is returning **incorrect type information** for STRING and SINGLE variables. The `slotInfo.type.kind` field does not match the actual variable type.

**This is the SAME symbol/type resolution bug** affecting BUG-075, BUG-076, and BUG-079. In INPUT contexts, `resolveVariableStorage()` or `storage->slotInfo` incorrectly reports the type, likely defaulting all non-integer types to INTEGER.

**Fix needed**: Fix `resolveVariableStorage()` to correctly identify variable types (STRING, SINGLE, etc.) so that `slotInfo.type.kind` accurately reflects the actual declared type.

**Impact**:
- Cannot create interactive programs with text input
- Cannot read floating-point numbers from console
- Severely limits usability for games, utilities, and user-facing programs
- Confusing error message (says "expected numeric value" even for STRING variables)

**Workaround**: Only use INTEGER for console INPUT. For other types, need to INPUT as string from file using LINE INPUT #.

**Test Files**:
- `/bugs/bug_testing/test_input_simple.bas` - INTEGER works
- `/bugs/bug_testing/test_input_string.bas` - STRING fails
- `/bugs/bug_testing/test_input_single.bas` - SINGLE fails
- `/bugs/bug_testing/test_input.bas` - Multi-type test (all fail except INTEGER)

**Related Code**:
- INPUT runtime function implementation
- Type dispatching for console input
- String/float parsing in INPUT handler

---

## DESIGN DECISIONS (Not Bugs)

- ℹ️ **BUG-049**: RND() function signature incompatible with standard BASIC - BY DESIGN (zero-argument form)
- ℹ️ **BUG-054**: STEP is reserved word - BY DESIGN (FOR loop keyword)
- ℹ️ **BUG-055**: Cannot assign to FOR loop variable - BY DESIGN (intentional semantic check)
- ℹ️ **BUG-069**: Objects not auto-initialized by DIM - BY DESIGN (explicit NEW required for reference semantics)

---

## RESOLVED BUGS (73 bugs)

### Recently Resolved (2025-11-17)
- ✅ **BUG-067**: Array fields - RESOLVED 2025-11-17
- ✅ **BUG-068**: Function name implicit returns - RESOLVED 2025-11-17
- ✅ **BUG-070**: Boolean parameters - RESOLVED 2025-11-17
- ✅ **BUG-071**: String arrays - RESOLVED 2025-11-17
- ✅ **BUG-072**: SELECT CASE blocks after exit - RESOLVED 2025-11-17
- ✅ **BUG-073**: Object parameter methods - RESOLVED 2025-11-17
- ✅ **BUG-074**: Constructor corruption - RESOLVED 2025-11-17

### All Resolved Bugs (BUG-001 through BUG-074)
- ✅ **BUG-001**: String concatenation requires $ suffix for type inference - RESOLVED 2025-11-12
- ✅ **BUG-002**: & operator for string concatenation not supported - RESOLVED 2025-11-12
- ✅ **BUG-003**: FUNCTION name assignment syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-004**: Procedure calls require parentheses even with no arguments - RESOLVED 2025-11-12
- ✅ **BUG-005**: SGN function not implemented - RESOLVED 2025-11-12
- ✅ **BUG-006**: Limited trigonometric/math functions - RESOLVED 2025-11-12
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
- ✅ **BUG-050**: SELECT CASE with multiple values causes IL generation error - VERIFIED NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-051**: DO UNTIL loop causes IL generation error - VERIFIED NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-052**: ON ERROR GOTO handler blocks missing terminators - RESOLVED 2025-11-15
- ✅ **BUG-053**: Cannot access global arrays in SUB/FUNCTION - RESOLVED 2025-11-15
- ✅ **BUG-056**: Arrays not allowed as class fields - RESOLVED 2025-11-15
- ✅ **BUG-057**: BOOLEAN return type in class methods causes type mismatch - RESOLVED 2025-11-15
- ✅ **BUG-058**: String array fields don't retain values - RESOLVED 2025-11-15
- ✅ **BUG-059**: Cannot access array fields within class methods - RESOLVED 2025-11-15
- ✅ **BUG-060**: Cannot call methods on class objects passed as SUB/FUNCTION parameters - RESOLVED 2025-11-15
- ✅ **BUG-061**: Cannot assign class field value to local variable (regression) - RESOLVED 2025-11-15
- ✅ **BUG-062**: CONST with CHR$() not evaluated at compile time - RESOLVED 2025-11-15
- ✅ **BUG-063**: Module-level initialization cleanup code leaks into subsequent functions - RESOLVED 2025-11-15
- ✅ **BUG-064**: ASC() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-065**: Array field assignments silently dropped by compiler - RESOLVED 2025-11-15
- ✅ **BUG-066**: VAL() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-067**: Array fields - RESOLVED 2025-11-17
- ✅ **BUG-068**: Function name implicit returns - RESOLVED 2025-11-17
- ✅ **BUG-070**: Boolean parameters - RESOLVED 2025-11-17
- ✅ **BUG-071**: String arrays - RESOLVED 2025-11-17
- ✅ **BUG-072**: SELECT CASE blocks after exit - RESOLVED 2025-11-17
- ✅ **BUG-073**: Object parameter methods - RESOLVED 2025-11-17
- ✅ **BUG-074**: Constructor corruption - RESOLVED 2025-11-17

---

## ADDITIONAL INFORMATION

**For detailed bug descriptions, reproduction cases, root cause analyses, and implementation notes:**
- See `basic_resolved.md` - Complete documentation of all resolved bugs
- See `/bugs/bug_testing/` - Test cases and stress tests that discovered these bugs

**Testing Sources:**
- Language audit and systematic feature testing
- Stress tests: Dungeon, Frogger, Adventure, BasicDB, Othello, Vipergrep
- OOP stress tests with complex class hierarchies and interactions
