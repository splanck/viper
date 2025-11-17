# VIPER BASIC Known Bugs and Issues

*Last Updated: 2025-11-17*
*Source: Empirical testing during language audit + Dungeon + Frogger + Adventure stress testing*

**Bug Statistics**: 72 resolved, 1 outstanding bug, 1 design decision (74 total documented)

**STATUS**: Major progress! BUG-071 (String Arrays) fixed. Only BUG-072 (SELECT CASE block ordering) remains outstanding.

---

## OUTSTANDING BUGS (1 bug)

**Active Issues:**
- **BUG-072** - SELECT CASE blocks after exit (HIGH priority - needs fix)

  Root Cause Summary (2025-11-17): SELECT CASE lowering appends new basic blocks to the end of the function using `IRBuilder::addBlock`, but the procedure/main skeleton pre-creates the function `exit` block at the end. As a result, all SELECT CASE arm/default/dispatch/end blocks are emitted after `exit`, making them unreachable and causing runtime crashes in larger cases. Key sites: `src/frontends/basic/SelectCaseLowering.cpp` (`prepareBlocks`, `emitCompareChain`) and `src/il/build/IRBuilder.cpp` (`addBlock` appends). Fix options: insert blocks before `exit`, defer creating `exit` until after lowering, or reorder blocks post-generation and update `exitIndex`.

**Design Decisions (Not Bugs):**
- **BUG-069** - Objects not auto-initialized by DIM (intentional reference semantics)

**Recently Resolved (2025-11-17):**
- BUG-067 - Array fields (previously fixed, verified)
- BUG-068 - Function name implicit returns (fixed)
- BUG-070 - Boolean parameters (fixed)
- BUG-071 - String arrays (fixed)
- BUG-073 - Object parameter methods (fixed)
- BUG-074 - Constructor corruption (fixed)

---

### BUG-072: SELECT CASE Blocks Generated After Function Exit
**Status**: 🚨 **OUTSTANDING** - HIGH priority IL block ordering bug
*See full details below*

---

## RESOLVED BUGS FROM THIS INVESTIGATION

### BUG-067: Array Fields in Classes Not Supported
**Status**: ✅ RESOLVED (Previously fixed, verified 2025-11-17)
**Discovered**: 2025-11-16 (Dungeon OOP stress test)
**Category**: Frontend / Parser / OOP
**Test File**: `/bugs/bug_testing/dungeon_entities.bas`

**Symptom**: Cannot declare array fields inside CLASS definitions. Parser fails with "expected END, got ident" error.

**Reproduction**:
```basic
CLASS Player
    inventory(10) AS Item  ' ERROR: Parse fails!
END CLASS
```

**Error**:
```
error[B0001]: expected END, got ident
    inventory(10) AS Item
    ^
```

**Expected**: Arrays should be valid field types in classes

**Workaround**: Use multiple scalar fields or manage arrays outside the class

**Impact**: Severely limits OOP design - cannot have collection fields in classes

**Root Cause**:
- **Parser lookahead constraint** (Lines 147-150): The `looksLikeFieldDecl` boolean only recognizes:
  - Simple form: `identifier AS type`
  - DIM form: `DIM identifier AS type` or `DIM identifier (`
  - **Missing**: Direct array form `identifier ( size ) AS type` without DIM prefix

  ```cpp
  const bool looksLikeFieldDecl =
      (at(TokenKind::Identifier) && peek(1).kind == TokenKind::KeywordAs) ||
      (at(TokenKind::KeywordDim) && peek(1).kind == TokenKind::Identifier &&
       (peek(2).kind == TokenKind::KeywordAs || peek(2).kind == TokenKind::LParen));
  ```

  The first clause checks for `AS` immediately after identifier, rejecting `identifier LParen`.

  **Fix**: Add `|| peek(1).kind == TokenKind::LParen` to first clause to accept array syntax.

- **Type system limitation** (Parser_Stmt_Core.cpp): Field parsing only captures primitive types via `parseTypeKeyword()` which maps INTEGER/FLOAT/STRING/BOOLEAN. Object class names for array elements are not preserved during field declaration parsing.

**Files**: `src/frontends/basic/Parser_Stmt_OOP.cpp` (lines 147-186), `Parser_Stmt_Core.cpp` (parseTypeKeyword)

**Resolution**:
This bug was already fixed in a previous update. The parser lookahead at `Parser_Stmt_OOP.cpp:153-154` now includes:

```cpp
// Shorthand with array dims: name '(' ... ')' AS TYPE
(at(TokenKind::Identifier) && peek(1).kind == TokenKind::LParen);
```

Array dimension parsing was also implemented at lines 166-190, supporting multi-dimensional arrays in class fields.

**Validation**:
- ✅ `test_bug067_array_fields.bas` - Array fields (STRING and INTEGER) work correctly
- ✅ Field arrays can be declared with dimensions: `inventory(10) AS STRING`
- ✅ Array fields can be accessed and modified in methods
- ✅ Multi-dimensional arrays supported

**Notes**:
The fix was likely part of BUG-056 (Array Field Initialization) work, which added comprehensive array field support to the parser and lowering logic.


### BUG-068: Function Name Assignment for Return Value Not Working
**Status**: ✅ RESOLVED (2025-11-17)
**Discovered**: 2025-11-16 (Dungeon OOP stress test)
**Category**: Frontend / Semantic Analysis + Lowering / Method Epilogue
**Test Files**: `/bugs/bug_testing/test_bug068_explicit_return.bas`, `/bugs/bug_testing/test_bug068_function_name_return.bas`

**Symptom**: Traditional BASIC pattern of assigning to function name to set return value had two issues:
1. Semantic analyzer incorrectly reported "missing return" error (now fixed via `methodHasImplicitReturn()`)
2. Runtime always returned default values (0, empty string) instead of assigned value

**Reproduction**:
```basic
CLASS Test
    FUNCTION GetValue() AS INTEGER
        GetValue = 42  ' Compiled but returned 0 instead of 42
    END FUNCTION
END CLASS
```

**Expected**: Assignment to function name should set return value (traditional BASIC/VB behavior)

**Root Cause**:
- **Semantic issue** (already fixed): OOP return checker in `Semantic_OOP.cpp` now uses `methodHasImplicitReturn()` (lines 127-171) which scans AST for function name assignments to prevent false "missing return" errors.

- **Runtime issue** (fixed 2025-11-17): Method epilogue in `Lower_OOP_Emit.cpp` always emitted default return values without checking if function-name variable was assigned:
  ```cpp
  // OLD CODE: Always returned default
  if (returnsValue)
  {
      Value retValue = Value::constInt(0);  // Hard-coded default!
      emitRet(retValue);
  }
  ```

**Resolution**: Added implicit return logic to method epilogue (Lower_OOP_Emit.cpp:560-588):

```cpp
if (returnsValue)
{
    Value retValue = Value::constInt(0);
    // BUG-068 fix: Check for VB-style implicit return via function name assignment
    auto methodNameSym = findSymbol(method.name);
    if (methodNameSym && methodNameSym->slotId.has_value())
    {
        // Function name was assigned - load from that variable
        Type ilType = ilTypeForAstType(*methodRetAst);
        Value slot = Value::temp(*methodNameSym->slotId);
        retValue = emitLoad(ilType, slot);
    }
    else
    {
        // No assignment - use default value
        switch (*methodRetAst)
        {
            case ::il::frontends::basic::Type::I64:
                retValue = Value::constInt(0);
                break;
            case ::il::frontends::basic::Type::Str:
                retValue = Value::constStrPtr("");
                break;
            // ... other types ...
        }
    }
    emitRet(retValue);
}
```

Now the epilogue checks if a variable with the method name exists in the symbol table. If it does, the return value is loaded from that variable. Otherwise, default values are used.

**Validation**:
- ✅ `test_bug068_explicit_return.bas` - Explicit RETURN: 42, Implicit (name assignment): 42
- ✅ `test_bug068_function_name_return.bas` - GetValue: 42, Calculate: 30
- ✅ Both explicit RETURN and implicit function-name assignment patterns work correctly
- ✅ Traditional BASIC/VB implicit return pattern fully functional


### BUG-069: Objects Not Initialized by DIM - NEW Required
**Status**: 🔴 DESIGN DECISION - Not a bug; explicit NEW required by reference semantics
**Discovered**: 2025-11-16 (Dungeon OOP stress test)
**Category**: Language Design / OOP / Object Lifecycle
**Test Files**: `/bugs/bug_testing/test_bug069_simple.bas`, `/bugs/bug_testing/test_me_in_init.bas`

**Symptom**: `DIM obj AS ClassName` creates a null object reference. Calling methods on uninitialized objects causes "null load" or "null store" traps.

**Reproduction**:
```basic
CLASS Simple
    value AS INTEGER
    SUB Show()
        PRINT "Value: "; ME.value  ' TRAP: "null load" - ME is null!
    END SUB
END CLASS

DIM obj AS Simple  ' Slot allocated, initialized to null
obj.Show()         ' TRAP: null pointer access!
```

**Trap**:
```
Trap @SIMPLE.SHOW#4 line 5: InvalidOperation (code=0): null load
```

**Current Behavior (Intentional)**:
```basic
DIM obj AS Simple  ' Allocates slot, stores null
obj = NEW Simple() ' Explicit allocation required
obj.Show()         ' Now safe - ME is valid pointer
```

**Deep Dive Root Cause Analysis** (2025-11-17):

**1. Slot Allocation Without Initialization**
- Objects get `%t0 = alloca 8` in function prologue (allocates 8-byte pointer slot)
- Alloca returns zero-initialized memory → slot contains `null` (0x0)
- **No explicit `store ptr, %t0, null` is emitted** (relies on alloca zeroing)

**IL Evidence**:
```il
func @main() -> i64 {
entry:
  %t0 = alloca 8        # Allocate object slot (zero-initialized by alloca)
  # NO: store ptr, %t0, null
  br body
body:
  %t1 = load ptr, %t0   # Load null pointer
  call @TEST.SHOW(%t1)  # Pass null as ME → TRAP when accessing ME.value
```

**2. Why Methods "Work" Without Fields**
- If method doesn't access `ME.field`, it doesn't trap:
  ```basic
  SUB Show()
      PRINT "Hello"  # No ME access → runs fine even with null ME!
  END SUB
  ```
- Only field/member access triggers null pointer trap

**3. What Would Be Needed for Auto-Allocation**

To automatically allocate objects during DIM, would require:

**a) Class metadata lookup during slot allocation** (Lowerer.Procedure.cpp:1195-1280):
```cpp
if (slotInfo.isObject && !slotInfo.objectClass.empty())
{
    // Need class ID and size
    const auto* classInfo = lookupClass(slotInfo.objectClass);
    long long classId = classInfo->id;
    long long size = classInfo->size;

    // Auto-allocate like NEW does
    Value obj = emitCallRet(Type(Type::Kind::Ptr), "rt_obj_new_i64",
                           {Value::constInt(classId), Value::constInt(size)});
    emitStore(Type(Type::Kind::Ptr), slot, obj);

    // Should constructor be called automatically?
    // If yes: emitCall(mangleClassCtor(className), {obj});
    // If no: object allocated but uninitialized fields
}
```

**b) Architectural questions**:
- Should parameterless constructors run automatically during DIM?
- Should DIM with constructors requiring parameters be an error?
- What about circular dependencies (Class A has Class B field, B has A field)?
- Performance impact of auto-allocation (every DIM = heap allocation + constructor call)

**4. Design Comparison**

| Approach | VIPER BASIC (Current) | VB6/VBA | Java/C# | C++ |
|----------|----------------------|---------|---------|-----|
| DIM behavior | Null reference | Auto-allocate on first access | Error (must new) | Undefined/default ctor |
| Explicit NEW | Required | Optional | Required | Optional |
| Null safety | Runtime traps | Runtime errors | Compiler enforced (C#) | Undefined behavior |
| Semantics | Reference | Reference | Reference | Value or Reference |

**5. Current Design Advantages**:
- ✅ **Explicit allocation** - Clear when heap allocation occurs
- ✅ **No hidden constructors** - NEW visibly calls constructor
- ✅ **Reference semantics** - Matches C#/Java model
- ✅ **Performance predictable** - DIM is stack-only, cheap
- ✅ **Simpler lowering** - No class metadata needed during slot allocation

**Conclusion**: This is **NOT a bug** - it's a design decision implementing reference semantics where objects must be explicitly allocated with NEW. This matches modern OOP languages (C#, Java) rather than VB6's auto-allocation model.

**Recommendation**:
- Keep current behavior (explicit NEW required)
- Document in language guide that DIM creates null references
- Consider adding optional null-safety checks in future (e.g., `DIM obj AS Simple?` for nullable vs non-nullable)

**Workaround** (Current Best Practice):
```basic
DIM obj AS Simple
obj = NEW Simple()  # Explicit allocation - clear and intentional
obj.Init(42)        # Now safe
```

**Files**: `src/frontends/basic/Lowerer.Procedure.cpp` (lines 1195-1280), `LowerStmt_Runtime.cpp` (lines 663-750), `Lower_OOP_Expr.cpp` (lines 174-205)


### BUG-070 HIGH: BOOLEAN Parameters Cause Type Mismatch Errors
**Status**: ✅ RESOLVED (2025-11-17)
**Discovered**: 2025-11-16 (Dungeon OOP stress test)
**Category**: Frontend / Type System
**Test File**: `/bugs/bug_testing/test_boolean.bas`

**Symptom**: Passing any value (including TRUE/FALSE constants) to a BOOLEAN parameter causes "call arg type mismatch" error.

**Reproduction**:
```basic
CLASS Test
    SUB SetFlag(value AS BOOLEAN)
        ' ...
    END SUB
END CLASS

DIM t AS NEW Test()
t.SetFlag(TRUE)   ' ERROR: call arg type mismatch
t.SetFlag(1)      ' ERROR: call arg type mismatch
```

**Error**:
```
error: main:entry: call %t4 -1: call arg type mismatch
```

**Expected**: TRUE/FALSE or integer values should be passable to BOOLEAN parameters

**Workaround**: Use INTEGER parameters instead:
```basic
SUB SetFlag(value AS INTEGER)  ' Use 0/1 instead
```

**Impact**: Cannot use BOOLEAN type for method parameters. Must use INTEGER flags everywhere.

**Root Cause**:
- **Type mismatch between literals and parameters**:

  **Literals** (Lowerer_Expr.cpp lines 102-111) emit i64:
  ```cpp
  void visit(const BoolExpr &expr) override
  {
      lowerer_.curLoc = expr.loc;
      IlValue intVal = IlValue::constInt(expr.value ? -1 : 0);
      result_ = Lowerer::RVal{intVal, IlType(IlType::Kind::I64)};  // i64!
  }
  ```

  **Parameters** (Lower_OOP_Emit.cpp lines 43-58) expect i1:
  ```cpp
  il::core::Type ilTypeForAstType(AstType ty)
  {
      switch (ty)
      {
          case AstType::Bool:
              return IlType(IlType::Kind::I1);  // i1!
          // ...
      }
  }
  ```

- **Call site**: When calling `obj.SetFlag(TRUE)`:
  1. Argument: `TRUE` → i64 constant -1
  2. Parameter: `value AS BOOLEAN` → i1 type in signature
  3. IL verifier: "call arg type mismatch" (i64 vs i1)

- **Slot allocation** (Lines 266, 505): BOOLEAN params get 1-byte slots:
  ```cpp
  Value slot = emitAlloca((!param.is_array && param.type == AstType::Bool) ? 1 : 8);
  ```

**Type mismatch summary**:
| Component | Type | Value |
|-----------|------|-------|
| TRUE literal | i64 | -1 |
| FALSE literal | i64 | 0 |
| BOOLEAN parameter | i1 | Expected |
| BOOLEAN slot size | 1 byte | Too small for i64 |

**Resolution**:
Implemented targeted i64→i1 coercion at call sites when a callee parameter is BOOLEAN (`i1`). Kept boolean literals lowered as legacy i64 values (-1 for TRUE, 0 for FALSE) to preserve existing IL goldens and BASIC semantics. This reconciles literal/param mismatches without broad type changes.

**What changed**:
- `src/frontends/basic/lower/Lowerer_Expr.cpp`: In general call lowering, when the callee signature expects `i1`, arguments are coerced via `coerceToBool(...)` before emission. Boolean literal lowering remains `i64 (-1/0)`.

**Validation**:
- Golden tests for boolean IL printing/combinations remain green.
- OOP boolean method parameter calls no longer trip IL verifier (“call arg type mismatch”).

**Notes**:
- This approach avoids spec changes and maintains deterministic IL outputs for existing tests while fixing call-site typing.


### BUG-071: String Arrays Cause IL Generation Error
**Status**: ✅ RESOLVED (2025-11-17)
**Discovered**: 2025-11-16 (Frogger OOP stress test)
**Category**: Code Generation / IL / Arrays / Temporary Lifetime
**Test Files**: `/bugs/bug_testing/test_array_string.bas`, `/bugs/bug_testing/test_arrays_local.bas`

**Symptom**: Using string arrays in loops causes IL generation to fail with "unknown temp" error due to def-use dominance violation.

**Reproduction**:
```basic
DIM names(1) AS STRING
DIM i AS INTEGER
names(0) = "Test"
FOR i = 0 TO 0
    PRINT names(i)  ' ERROR: unknown temp in exit block!
NEXT i
```

**Error**:
```
error: main:exit: call %t33: unknown temp %33; use before def of %33
```

**Root Cause**:
- **Deferred temp cleanup scope violation**: String temps from `rt_arr_str_get` were being marked for deferred cleanup at function exit, even when defined inside conditional blocks (like loop bodies).

- **What happened** (Lowerer_Expr.cpp lines 157-167):
  1. String array access calls `rt_arr_str_get` which returns a retained string
  2. The result temp is marked for deferred cleanup: `lowerer_.deferReleaseStr(val);`
  3. The consuming code (e.g., PRINT) emits immediate retain/use/release
  4. At function exit, `releaseDeferredTemps()` tries to release the temp again
  5. **Problem**: If the temp was defined inside a loop/conditional that didn't execute, it's undefined at exit → dominance violation

- **IL evidence**:
  ```il
  for_body:
    // ... bounds check, branch to bc_ok1 ...

  bc_ok1:
    %t33 = call @rt_arr_str_get(%base, %index)  // Defined in loop
    call @rt_str_retain_maybe(%t33)
    call @rt_print_str(%t33)
    call @rt_str_release_maybe(%t33)  // Immediate cleanup
    br for_inc

  exit:
    call @rt_str_release_maybe(%t33)  // ERROR: %t33 undefined if loop never ran!
  ```

**Resolution**: Removed deferred release for string array access results (Lowerer_Expr.cpp:163, 398):

```cpp
// OLD CODE:
IlValue val = lowerer_.emitCallRet(
    IlType(IlType::Kind::Str), "rt_arr_str_get", {access.base, access.index});
lowerer_.deferReleaseStr(val);  // Added to exit block cleanup!

// NEW CODE (BUG-071 fix):
IlValue val = lowerer_.emitCallRet(
    IlType(IlType::Kind::Str), "rt_arr_str_get", {access.base, access.index});
// Removed: lowerer_.deferReleaseStr(val);
// Consuming code (PRINT, assignment, etc.) handles lifetime
```

**Why this works**:
- `rt_arr_str_get` returns a retained string (refcount +1)
- Consuming statements (PRINT, assignment) emit their own retain/release around usage
- No need for deferred cleanup - immediate cleanup in consuming code handles lifetime correctly
- Avoids dominance violations by not tracking temps across block boundaries

**Validation**:
- ✅ `test_array_string.bas` - String arrays in loops work correctly
- ✅ `test_arrays_local.bas` - All array tests pass
- ✅ All test suite passes (640/641 tests, 1 pre-existing failure)


### BUG-072: SELECT CASE Blocks Generated After Function Exit
**Status**: 🚨 HIGH - IL block ordering bug (triggers with 4+ CASE arms + CASE ELSE)
**Discovered**: 2025-11-16 (Adventure game text adventure stress test)
**Category**: Code Generation / IL / Control Flow / Block Ordering
**Test File**: `/bugs/bug_testing/adventure_v1_parser.bas`

**Symptom**: SELECT CASE with multiple arms (4+) and CASE ELSE generates blocks AFTER the function exit block, making them unreachable and causing runtime crashes (SIGBUS exit code 138).

**Minimal Reproduction**:
```basic
DIM cmd AS STRING
cmd = "north"

SELECT CASE cmd
    CASE "north"
        PRINT "North"
    CASE "south"
        PRINT "South"
    CASE "east"
        PRINT "East"
    CASE "west"
        PRINT "West"
    CASE ELSE
        PRINT "Other"
END SELECT

PRINT "Done"
```

**Error**: Runtime crash with exit code 138 (SIGBUS - memory access violation)

**Deep Dive Root Cause Analysis** (2025-11-17):

**1. Trigger Conditions** (Empirically Determined):
- ✅ Simple SELECT (1-2 CASE arms): **Works**
- ✅ SELECT with CASE ELSE only: **Works**
- ✅ Multiple SELECT statements (2-3 arms each): **Works**
- ❌ Single SELECT with 4+ CASE arms + CASE ELSE: **FAILS**
- ❌ Multiple SELECT with 4+ arms each: **FAILS**

**Why 4+ arms triggers the bug**: With many CASE arms, `prepareBlocks()` creates enough blocks that the function's block vector likely reallocates during appending. If the exit block was already created, the new SELECT blocks end up after it in memory.

**2. IL Evidence** (from `/tmp/test_many_cases.bas`):
```
Line 97:  exit:
Line 99:  select_arm_0:    # ALL SELECT blocks after exit!
Line 110: select_arm_1:
Line 121: select_arm_2:
Line 132: select_arm_3:
Line 143: select_default:
Line 154: select_end:
```

**Block flow**:
```il
# Normal control flow
body:
  # ... code ...
  br exit

exit:
  ret 0              # Function returns here

# UNREACHABLE BLOCKS (after return!)
select_arm_0:
  %t25 = const_str @.L10
  call @rt_print_str(%t25)
  br select_end      # Never reached!
```

**3. Root Cause in SelectCaseLowering.cpp**:

**prepareBlocks() always appends** (Lines 141-178):
```cpp
void SelectCaseLowering::prepareBlocks(/* ... */)
{
    auto *func = ctx.function();
    auto *current = ctx.current();
    size_t curIdx = static_cast<size_t>(current - &func->blocks[0]);
    size_t startIdx = func->blocks.size();  // Current end of block list

    // Create SELECT CASE arm blocks
    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        std::string label = blockNamer ? blockNamer->generic("select_arm")
                                       : lowerer_.mangler.block("select_arm_" + ...);
        lowerer_.builder->addBlock(*func, label);  // APPENDS to end!
    }

    if (hasCaseElse)
        lowerer_.builder->addBlock(*func, defaultLabel);  // APPENDS

    lowerer_.builder->addBlock(*func, endLabel);  // APPENDS
    // ...
}
```

**IRBuilder::addBlock() implementation** (IRBuilder.cpp:113):
```cpp
BasicBlock &IRBuilder::createBlock(Function &fn, const std::string &label, ...)
{
    fn.blocks.push_back({label, {}, {}, false});  // ALWAYS APPENDS!
    return fn.blocks.back();
}
```

**The Problem**:
1. Function prologue creates entry block
2. Statement lowering may create exit block early (especially after PRINTs or simple statements)
3. SELECT CASE calls `prepareBlocks()` which appends all SELECT blocks to end
4. If exit block exists at position N, SELECT blocks get positions N+1, N+2, ...
5. Result: `exit` → `select_arm_0` → `select_arm_1` → ... (unreachable!)

**4. Why It Causes SIGBUS (Exit Code 138)**:

The IL verifies successfully in some cases but crashes at runtime because:
- Control flow reaches `exit` block and returns
- Some VM state or pointer arithmetic assumes blocks are contiguous/ordered
- Access to unreachable SELECT blocks causes memory access violation

**5. Proposed Fix Approaches**:

**Option A**: Insert blocks before exit block
```cpp
// In prepareBlocks(), instead of addBlock():
size_t exitIdx = ctx.exitIndex();
fn.blocks.insert(fn.blocks.begin() + exitIdx, newBlock);
// Then update all block indices/pointers
```

**Option B**: Defer exit block creation
```cpp
// Don't create exit block until all statement lowering complete
// Keep it as last block always
```

**Option C**: Reorder blocks after generation
```cpp
// After all blocks created, move exit to end:
auto exitBlock = std::move(fn.blocks[exitIdx]);
fn.blocks.erase(fn.blocks.begin() + exitIdx);
fn.blocks.push_back(std::move(exitBlock));
```

**6. Complexity**: Each approach requires updating:
- Block indices in branches/jumps
- ProcedureContext.exitIdx
- Pointer arithmetic in `setCurrent()` calls
- Any saved block pointers (vector reallocation invalidates them)

**Impact**: HIGH - Blocks text adventure games, command parsers, menu systems. Any SELECT CASE with realistic number of options fails.

**Workaround**:
- Limit CASE arms to 3 or fewer
- Use nested IF-ELSEIF instead of SELECT CASE
- Split large SELECT into multiple smaller ones

**Files**: `src/frontends/basic/SelectCaseLowering.cpp` (lines 141-178), `src/il/build/IRBuilder.cpp` (line 113)


### BUG-073: Cannot Call Methods on Object Parameters
**Status**: ✅ RESOLVED (2025-11-17)
**Discovered**: 2025-11-16 (Adventure game multi-object stress test)
**Category**: Code Generation / IL / OOP / Method Resolution
**Test File**: `/bugs/bug_testing/test_method_call_param.bas`, `/bugs/bug_testing/adventure_game_simple.bas`

**Symptom**: When an object is passed as a parameter to a method, and you try to call a method on that parameter object, the compiler generates invalid IL looking for an unqualified method name instead of the fully-qualified class method.

**Reproduction**:
```basic
CLASS Target
    SUB Modify(amount AS INTEGER)
        ' ...
    END SUB
END CLASS

CLASS Actor
    SUB DoAction(t AS Target)
        t.Modify(25)  ' ERROR: unknown callee @MODIFY
    END SUB
END CLASS
```

**Error**:
```
error: ACTOR.DOACTION:entry_ACTOR.DOACTION: call %t6 25: unknown callee @MODIFY
```

**IL Evidence**: The compiler looks for `@MODIFY` instead of the properly qualified method name like `@TARGET.MODIFY`.

**Expected**: Should generate IL that calls the correct class method on the parameter object.

**Workaround**: Cannot pass objects as parameters and call their methods. Must use globals or restructure code to avoid parameter objects.

**Impact**: CRITICAL - Severely limits OOP design. Cannot implement common patterns like:
- Visitor pattern
- Strategy pattern
- Inter-object communication
- Game entities interacting (Monster attacking Player)
- Any composition or delegation patterns

**Root Cause**:
- **Constructor parameters missing objectClass** (Lower_OOP_Emit.cpp lines 251-272):
  ```cpp
  for (std::size_t i = 0; i < ctor.params.size(); ++i)
  {
      const auto &param = ctor.params[i];
      // ... allocate slot ...
      setSymbolType(param.name, param.type);  // WRONG! Doesn't set objectClass
      // Should be: if (!param.objectClass.empty())
      //              setSymbolObjectType(param.name, param.objectClass);
  }
  ```

- **Method parameters HAVE the fix** (Lower_OOP_Emit.cpp lines 504-508):
  ```cpp
  if (!param.objectClass.empty())
      setSymbolObjectType(param.name, qualify(param.objectClass));  // CORRECT!
  else
      setSymbolType(param.name, param.type);
  ```

- **Resolution chain**:
  1. `lowerMethodCallExpr()` calls `resolveObjectClass(*expr.base)` (line 356)
  2. `resolveObjectClass()` calls `getSlotType(var->name)` (lines 72-162)
  3. `getSlotType()` retrieves `sym->objectClass` from symbol table (line 528)
  4. If objectClass is empty → returns empty string
  5. Call site emits unqualified name `@MODIFY` instead of `@TARGET.MODIFY`
  6. IL verifier: "unknown callee @MODIFY"

- **Code locations**:
  ```cpp
  // Lower_OOP_Expr.cpp:356
  std::string className = resolveObjectClass(*expr.base);

  // Lower_OOP_Expr.cpp:72-90
  if (const auto *var = as<const VarExpr>(expr))
  {
      SlotType slotInfo = getSlotType(var->name);
      if (slotInfo.isObject)
          return slotInfo.objectClass;  // Empty if not set!
      return {};
  }

  // Lowerer.Procedure.cpp:516-528
  if (const auto *sym = findSymbol(name))
  {
      if (sym->isObject)
      {
          info.objectClass = sym->objectClass;  // Retrieved here
          return info;
      }
  }
  ```

**Fix**: In constructor parameter materialization (line 272), check `!param.objectClass.empty()` and call `setSymbolObjectType()` like method parameters do.

**Files**: `src/frontends/basic/Lower_OOP_Emit.cpp` (lines 251-272 constructor params, 504-508 method params), `Lower_OOP_Expr.cpp` (lines 72-162, 356), `Lowerer.Procedure.cpp` (lines 516-558)

**Resolution**:
Fixed by applying the same object-class preservation pattern used in method parameters to constructor parameters in `Lower_OOP_Emit.cpp:262-271`:

```cpp
// BUG-073 fix: Preserve object-class typing for parameters so member calls on params resolve
if (!param.objectClass.empty())
    setSymbolObjectType(param.name, qualify(param.objectClass));
else
    setSymbolType(param.name, param.type);
// ...
Type ilParamTy = (!param.objectClass.empty() || param.is_array) ? Type(Type::Kind::Ptr)
                                                                : ilTypeForAstType(param.type);
```

This ensures that constructor parameters with object types preserve their class information in the symbol table, enabling the resolver to generate properly qualified method names (`@TARGET.MODIFY` instead of `@MODIFY`).

**Validation**:
- ✅ `test_bug073_object_param.bas` - Object parameters can now have methods called on them
- ✅ Pattern now consistent between constructor and method parameter handling
- ✅ All BASIC test suite passes (272/273 tests, 1 pre-existing failure)
- ✅ OOP patterns like visitor, strategy, and inter-object communication now possible

---

### BUG-074: Constructor Corruption When Class Uses Previously-Defined Class
**Status**: ✅ RESOLVED (2025-11-17)
**Discovered**: 2025-11-16 (Texas Hold'em Poker OOP stress test)
**Category**: Frontend / Lowering / OOP / Constructor Generation
**Test Files**: `/bugs/bug_testing/poker_v2_deck_class.bas` (fails), `/bugs/bug_testing/poker_v3_simple_deck.bas` (fails), `/bugs/bug_testing/poker_v4_reversed_order.bas` (works with workaround)

**Symptom**: When a class B uses another class A that was defined earlier in the source file, the constructor for class B becomes corrupted with string cleanup code from unrelated contexts, causing "use before def" errors.

**Reproduction**:
```basic
REM Define Card first
CLASS Card
    suit AS INTEGER
    rank AS INTEGER
    SUB Init(s AS INTEGER, r AS INTEGER)
        ME.suit = s
        ME.rank = r
    END SUB
END CLASS

REM Then define Deck that uses Card
CLASS Deck
    SUB Init()
        REM Empty init
    END SUB

    SUB ShowCard()
        DIM c AS Card
        c = NEW Card()    ' Triggers bug!
        c.Init(0, 14)
    END SUB
END CLASS

DIM deck AS Deck
deck = NEW Deck()    ' ERROR: DECK.__ctor:entry_DECK.__ctor: call %t27: unknown temp %27
deck.Init()
```

**Error**:
```
error: DECK.__ctor:entry_DECK.__ctor: call %t27: unknown temp %27; use before def of %27
```

**IL Evidence**: Constructor is corrupted with string release calls for temporaries that don't exist:
```
func @DECK.__ctor(ptr %ME) -> void {
entry_DECK.__ctor(%ME:ptr):
  %t1 = alloca 8
  store ptr, %t1, %t0        // ERROR: %t0 undefined, should be %ME
  %t2 = load ptr, %t1
  br ret_DECK.__ctor
ret_DECK.__ctor:
  call @rt_str_release_maybe(%t27)  // ERROR: %t27 from calling context!
  call @rt_str_release_maybe(%t30)  // ERROR: %t30 from calling context!
  ret
}
```

**Expected**: Constructor should only manage its own local state, not cleanup temporaries from other scopes.

**Workaround**: Define classes in reverse dependency order - define the "using" class before the "used" class. Forward references work correctly.

**Example Workaround**:
```basic
REM Define Deck BEFORE Card (forward reference)
CLASS Deck
    SUB Init()
    END SUB
    SUB ShowCard()
        DIM c AS Card    ' Forward reference OK!
        c = NEW Card()
        c.Init(0, 14)
    END SUB
END CLASS

REM Define Card after Deck
CLASS Card
    suit AS INTEGER
    rank AS INTEGER
    SUB Init(s AS INTEGER, r AS INTEGER)
        ME.suit = s
        ME.rank = r
    END SUB
END CLASS

REM Now it works!
DIM deck AS Deck
deck = NEW Deck()
deck.Init()
```

**Impact**: CRITICAL - Severely restricts multi-class programs. Classes must be defined in reverse dependency order, which is counterintuitive.

**Root Cause**: **Deferred temporary tracking state not cleared between constructor emissions**. When Class B's constructor is emitted after Class A's constructor, the Emitter's `deferredTemps_` vector retains temporaries from Class A's lowering. These stale temporaries are then incorrectly emitted during Class B's epilogue, corrupting its IL output.

**Detailed Analysis**:

1. **Global Emitter with Persistent State** (`Lowerer.hpp:664`):
   ```cpp
   std::unique_ptr<lower::Emitter> emitter_;
   ```
   The Lowerer holds a single, long-lived Emitter instance for the entire lowering session.

2. **Deferred Temps Accumulate Across Constructors** (`lower/Emitter.hpp:132-139`):
   ```cpp
   struct TempRelease {
       Value v;
       bool isString{false};
       std::string className; // optional, for object destructors
   };
   std::vector<TempRelease> deferredTemps_;  // NOT cleared between procedures!
   ```

3. **resetLoweringState() Does NOT Clear Deferred Temps** (`Lowerer.Procedure.cpp:1418-1424`):
   ```cpp
   void Lowerer::resetLoweringState() {
       resetSymbolState();
       context().reset();           // Resets ProcedureContext only
       stmtVirtualLines_.clear();
       synthSeq_ = 0;
       // BUG: deferredTemps_ in the Emitter is NOT cleared!
   }
   ```
   This is called at the start of each constructor (Lower_OOP_Emit.cpp:219) but leaves `deferredTemps_` intact.

4. **Constructor Emission Pattern** (`Lower_OOP_Emit.cpp:217-355`):
   ```cpp
   void Lowerer::emitClassConstructor(const ClassDecl &klass, const ConstructorDecl &ctor) {
       resetLoweringState();  // Line 219 - doesn't clear deferredTemps_!
       // ... parameter setup ...
       // Line 335: lowerStatementSequence() accumulates temps in deferredTemps_
       // Line 347: releaseDeferredTemps() emits ALL accumulated temps (stale + current)
       releaseDeferredTemps();
       emitRetVoid();
   }
   ```

5. **Data Flow Causing Corruption**:
   ```
   emitOopDeclsAndBodies(program)
     └─> emitClassConstructor(ClassA)
         ├─> resetLoweringState()  [doesn't clear emitter_.deferredTemps_]
         ├─> lowerStatementSequence() [adds temps to deferredTemps_]
         └─> releaseDeferredTemps()  [emits and clears deferredTemps_]
     └─> emitClassConstructor(ClassB)  <-- CORRUPTION HERE
         ├─> resetLoweringState()  [doesn't clear emitter_.deferredTemps_]
         ├─> lowerStatementSequence() [adds MORE temps to deferredTemps_]
         └─> releaseDeferredTemps()  [emits stale ClassA temps + ClassB temps!]
   ```

**Evidence from BUG-063 Fix**: The main function emission already has this fix (`LowerEmit.cpp:96-97`):
```cpp
void Lowerer::buildMainFunctionSkeleton(ProgramEmitContext &state) {
    // BUG-063 fix: Clear any deferred temps from prior procedures
    clearDeferredTemps();  // <-- Explicit clearing that OOP emission lacks!
}
```
This pattern was never applied to OOP constructor/method emission.

**Why Reverse Order Works**: When Deck is defined before Card, Deck's constructor is emitted first. Even if it accumulates deferred temps, Card's constructor emission happens later and those temps don't interfere with Deck (which was already emitted).

**Fix**: Add `clearDeferredTemps()` call in `resetLoweringState()` or at the start of each OOP function emission (`emitClassConstructor`, `emitClassDestructor`, `emitClassMethod`).

**Files**:
- Root cause: `src/frontends/basic/Lowerer.Procedure.cpp:1418-1424` (resetLoweringState)
- Manifestation: `src/frontends/basic/Lower_OOP_Emit.cpp:219, 347, 369, 449`
- Pattern to follow: `src/frontends/basic/LowerEmit.cpp:96-97` (BUG-063 fix)
- State holder: `src/frontends/basic/lower/Emitter.hpp:139` (deferredTemps_)

**Resolution**:
Fixed by adding `clearDeferredTemps()` call to `resetLoweringState()` in `Lowerer.Procedure.cpp:1425`:

```cpp
void Lowerer::resetLoweringState()
{
    resetSymbolState();
    context().reset();
    stmtVirtualLines_.clear();
    synthSeq_ = 0;
    // BUG-074 fix: Clear any deferred temps from prior procedures
    clearDeferredTemps();
}
```

This ensures that the `deferredTemps_` vector is cleared at the start of each procedure emission, preventing stale temporaries from prior constructors/methods from polluting subsequent IL generation.

**Validation**:
- ✅ `poker_v2_deck_class.bas` - Previously failed, now works
- ✅ `poker_v3_simple_deck.bas` - Previously failed, now works
- ✅ `poker_v4_reversed_order.bas` - Still works (workaround no longer needed)
- ✅ All BASIC test suite passes (272/273 tests, 1 pre-existing failure)
- ✅ Classes can now be defined in natural dependency order (Card before Deck)

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
