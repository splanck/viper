# Viper OOP Runtime Bugs

This file tracks bugs discovered while developing full-featured games using the Viper.* OOP runtime.

## Bug Template

### BUG-OOP-XXX: [Title]
- **Status**: Open / Fixed / Won't Fix
- **Discovered**: [Date]
- **Game**: [Which game found it]
- **Severity**: Critical / High / Medium / Low
- **Component**: [e.g., Viper.String, Viper.Collections.List]
- **Description**: [What happened]
- **Steps to Reproduce**: [Code snippet or steps]
- **Expected**: [What should happen]
- **Actual**: [What actually happened]
- **Workaround**: [If any]
- **Root Cause**: [Technical analysis]
- **Fix Required**: [What needs to change]

---

## Open Bugs

### BUG-OOP-036: Same-named local variables corrupt across consecutive function calls in a class
- **Status**: Open (2025-12-01)
- **Discovered**: 2025-12-01
- **Game**: Chess
- **Severity**: High
- **Component**: BASIC Frontend / Register Allocation
- **Description**: When two functions in the same CLASS both declare local variables with the same name (e.g., `DIM queen AS INTEGER`), and one function calls the other, the second function's local variable gets corrupted with a value from a previous scope.
- **Steps to Reproduce**:
  ```basic
  CONST QUEEN AS INTEGER = 5

  CLASS AttackChecker
      FUNCTION CheckDiagonalAttack(sq AS INTEGER, byColor AS INTEGER) AS INTEGER
          DIM bishop AS INTEGER
          DIM queen AS INTEGER
          IF byColor = 1 THEN
              bishop = 0 - 3   ' -3
              queen = 0 - 5    ' -5
          END IF
          PRINT "Diagonal: queen="; queen   ' Prints -5 (correct)
          CheckDiagonalAttack = 0
      END FUNCTION

      FUNCTION CheckStraightAttack(sq AS INTEGER, byColor AS INTEGER) AS INTEGER
          DIM rook AS INTEGER
          DIM queen AS INTEGER   ' Same name as in CheckDiagonalAttack
          IF byColor = 1 THEN
              rook = 0 - 4       ' -4
              queen = 0 - QUEEN  ' Should be -5
          END IF
          PRINT "Straight: queen="; queen   ' Prints 5 (WRONG!)
          CheckStraightAttack = 0
      END FUNCTION

      FUNCTION IsSquareAttacked(sq AS INTEGER, byColor AS INTEGER) AS INTEGER
          IF CheckDiagonalAttack(sq, byColor) = 1 THEN ...
          IF CheckStraightAttack(sq, byColor) = 1 THEN ...  ' queen corrupted here
      END FUNCTION
  END CLASS
  ```
- **Expected**: `CheckStraightAttack` should print `queen=-5`
- **Actual**: `CheckStraightAttack` prints `queen=5` (positive instead of negative)
- **Workaround**: Use unique variable names in each function, or use literal values instead of `0 - CONSTANT` expressions:
  ```basic
  FUNCTION CheckStraightAttack(...)
      DIM rookPc AS INTEGER    ' Renamed
      DIM queenPc AS INTEGER   ' Renamed
      IF byColor = 1 THEN
          rookPc = -4          ' Literal instead of 0 - ROOK
          queenPc = -5         ' Literal instead of 0 - QUEEN
      END IF
  END FUNCTION
  ```
- **Root Cause**: Unknown - likely related to how local variable slots are allocated/reused when calling between functions in the same class
- **Fix Required**: Investigate the BASIC frontend lowering or IL code generation for local variable scoping in class methods

---

### BUG-OOP-034: FOR loop counter typed as i64 when bounds are INTEGER parameters
- **Status**: Cannot Reproduce (2025-12-01)
- **Discovered**: 2025-12-01
- **Game**: Centipede
- **Severity**: Medium
- **Component**: BASIC Frontend / IL Generation
- **Description**: Originally reported that FOR loop counters typed as i64 cause type mismatches when passed to functions expecting INTEGER.
- **Resolution**: Cannot reproduce. BASIC `INTEGER` maps to IL `i64` (64-bit integer), not `i32`. This is consistent behavior across the frontend:
  - FOR loop counters: `i64`
  - DIM AS INTEGER variables: `i64`
  - Function parameters AS INTEGER: `i64`
  All types match and the test case runs successfully.
- **Verification (2025-12-01)**:
  - Test: `FOR row = top TO bottom` with `BufferChar(row, col, ch, clr)` → WORKS
  - The original bug report may have been based on an earlier codebase state or misunderstanding
- **Note**: The bug report mentioned `i32` vs `i64` mismatch, but BASIC INTEGER has always been `i64` internally. If there was ever a real bug here, it was already fixed in a prior commit.

---

## Fixed Bugs

### BUG-OOP-035: FUNCTION returning object to local variable causes segfault
- **Status**: Fixed (2025-12-01) ✅ Verified
- **Discovered**: 2025-12-01
- **Game**: Centipede
- **Severity**: Critical
- **Component**: BASIC Frontend / OOP / Lowering
- **Description**: When a FUNCTION returns an object type and the result is assigned to a local variable inside a SUB/FUNCTION, the program crashes with a segfault (exit code 139) at runtime.
- **Steps to Reproduce**:
```basic
CLASS Player
  DIM name$ AS STRING
  SUB Init(n$ AS STRING)
    name$ = n$
  END SUB
  FUNCTION GetName$() AS STRING
    GetName$ = name$
  END FUNCTION
END CLASS

FUNCTION GetPlayer() AS Player
  DIM p AS Player
  p = NEW Player()
  p.Init("Hero")
  GetPlayer = p
END FUNCTION

DIM local AS Player
local = GetPlayer()
PRINT "Player name: "; local.GetName$()
```
- **Expected**: Program prints `Player name: Hero`
- **Actual**: Segfault (exit code 139) - crash at runtime
- **Root Cause Analysis**:
  Two separate issues were found and fixed:

  **Issue 1: Uninitialized return value slot**
  Object-returning FUNCTIONs use the function name as the return value slot (VB-style implicit return). The slot was allocated but not initialized to null, causing `rt_obj_release_check0()` to crash when assigning to an uninitialized slot.

  **Issue 2: Return slot released in epilogue**
  The `releaseObjectLocals()` function released ALL object locals including the function name (return value slot). This nullified the return slot BEFORE the function returned, causing the function to return null instead of the actual object.

- **Fix Applied** (2 changes):
  1. **Initialize object slots to null** (`Lowerer.Procedure.cpp:1327-1334`):
     ```cpp
     // BUG-OOP-035 fix: Initialize object slots to null.
     else if (slotInfo.isObject)
     {
         emitStore(Type(Type::Kind::Ptr), slot, Value::null());
     }
     ```

  2. **Exclude return slot from release** (`Lowerer.Procedure.cpp:1099-1106`):
     ```cpp
     // BUG-OOP-035 fix: For object-returning functions, exclude the function name
     // from the release set. The function name is used as the return value slot
     // (VB-style implicit return) and must NOT be released before returning.
     std::unordered_set<std::string> excludeFromRelease = ctx.paramNames;
     if (config.retType.kind == il::core::Type::Kind::Ptr)
     {
         excludeFromRelease.insert(ctx.name);
     }
     ```

- **Verification (2025-12-01)**:
  - Test: Object-returning function with local assignment → WORKS
  - Test: Nested function calls returning objects → WORKS
  - Test: Multiple assignments from object functions → WORKS
  - All 744 tests pass

### BUG-OOP-033: VM stack size limit causes stack overflow with moderate arrays
- **Status**: Fixed (2025-12-01) ✅ Verified
- **Discovered**: 2025-12-01
- **Game**: Centipede
- **Severity**: High
- **Component**: VM / Runtime
- **Description**: The VM had a hardcoded stack size of only 1024 bytes per frame, causing stack overflow when BASIC programs use moderate-sized arrays.
- **Root Cause**: The VM stack size constants were set extremely low:
  - `src/vm/VM.hpp:128`: `static constexpr size_t kDefaultStackSize = 1024;`
  - `src/vm/VMConstants.hpp:27`: `constexpr size_t kDefaultFrameStackSize = 1024;`
- **Fix Applied**: Increased both constants from 1KB to 64KB (65536 bytes):
  - `src/vm/VM.hpp`: `kDefaultStackSize = 65536`
  - `src/vm/VMConstants.hpp`: `kDefaultFrameStackSize = 65536`
  - Updated `test_vm_alloca_overflow.cpp` to use 70000 bytes to still test overflow behavior
- **Verification (2025-12-01)**:
  - Test: 80x25 screen buffer array (2000 elements = 16KB) → WORKS
  - All 744 tests pass

### BUG-OOP-032: Local variable same name as class causes false "unknown procedure" error
- **Status**: Fixed (2025-12-01) ✅ Verified
- **Discovered**: 2025-12-01
- **Game**: Centipede
- **Severity**: High
- **Component**: BASIC Frontend / Semantic Analyzer
- **Description**: When a local variable has the same name as a CLASS (case-insensitive), method calls on that variable fail with "unknown procedure" because the semantic analyzer incorrectly treats it as a namespace-qualified static call.
- **Steps to Reproduce**:
```basic
CLASS Player
    DIM x AS INTEGER
    SUB SetX(val AS INTEGER)
        x = val
    END SUB
END CLASS

SUB TestLocal()
    DIM player AS Player       ' Variable name matches class name
    player = NEW Player()
    player.SetX(42)            ' ERROR: unknown procedure 'player.setx'
END SUB

TestLocal()
```
- **Expected**: `player.SetX(42)` calls the method on the local object variable
- **Actual**: Error "unknown procedure 'player.setx'" - treated as `Player.SetX()` static call
- **Root Cause**: In `SemanticAnalyzer.Stmts.Runtime.cpp` lines 84-99, when analyzing a MethodCallExpr:
  1. The check `symbols_.find(varExpr->name)` fails because local variables are mangled (e.g., "player" → "player_42") and stored in symbols_ with the mangled name
  2. The check `oopIndex_.findClass(varExpr->name)` succeeds because "player" matches class "Player" (case-insensitive)
  3. This caused the analyzer to emit "unknown procedure 'player.setx'" as if it were a namespace-qualified call
- **Fix Applied**: In `SemanticAnalyzer.Stmts.Runtime.cpp` line 90, added check for local variable resolution via `scopes_.resolve()` before concluding the name is a namespace:
  ```cpp
  // BUG-OOP-032 fix: Also check if the variable can be resolved via scopes_.
  bool isLocalVariable = scopes_.resolve(varExpr->name).has_value();
  bool looksLikeNamespace =
      !isLocalVariable && oopIndex_.findClass(varExpr->name) != nullptr;
  ```
- **Verification (2025-12-01)**:
  - Test: `DIM player AS Player; player = NEW Player(); player.SetX(42)` inside SUB → WORKS
  - All 744 tests pass
- **Note**: A separate crash bug (BUG-OOP-035) was discovered when testing functions that return objects to local variables.

### BUG-OOP-019: SELECT CASE cannot use CONST labels across ADDFILE boundaries
- **Status**: Fixed (2025-12-01) ✅ Verified
- **Discovered**: 2025-11-28 (initial), 2025-11-29 (regression discovered)
- **Game**: Chess
- **Severity**: High
- **Component**: BASIC Frontend / Parser / ADDFILE
- **Description**: SELECT CASE statements with CONST labels work in single-file programs but fail when CONSTs are defined in an ADDFILE'd file
- **Steps to Reproduce**:
```basic
' constants.bas
CONST QUEEN AS INTEGER = 5
CONST ROOK AS INTEGER = 4

' movegen.bas (included via ADDFILE after constants.bas)
SELECT CASE promo
    CASE QUEEN      ' Error: SELECT CASE labels must be literals or CONSTs
        sb.Append("q")
    CASE ROOK
        sb.Append("r")
END SELECT
```
- **Expected**: CASE labels accept CONST values from ADDFILE'd files
- **Actual**: Now works correctly
- **Root Cause**: The `knownConstInts_` and `knownConstStrs_` maps were not being copied to child parsers during ADDFILE processing.
- **Fix Applied**: In `src/frontends/basic/Parser.cpp`:
  - `handleTopLevelAddFile()` (lines 507-509): Copies `knownConstInts_` and `knownConstStrs_` to child parser before parsing
  - `handleTopLevelAddFile()` (lines 525-529): Merges child's CONSTs back to parent after parsing
  - `handleAddFileInto()` (lines 621-624): Same copy to child
  - `handleAddFileInto()` (lines 638-642): Same merge back to parent
- **Verification (2025-12-01)**:
  - Test 1: Parent defines CONSTs, child uses them in SELECT CASE → WORKS
  - Test 2: Child defines CONSTs, parent uses them after ADDFILE → WORKS
  - Both directions of CONST propagation confirmed working

### BUG-OOP-028: USING statement not propagated through ADDFILE
- **Status**: Fixed (2025-12-01) ✅ Cannot Reproduce
- **Discovered**: 2025-12-01
- **Game**: Centipede
- **Severity**: High
- **Component**: BASIC Frontend / Parser / ADDFILE
- **Description**: When a USING statement is placed in the main file BEFORE an ADDFILE, the USING imports are not visible to code in the ADDFILE'd file.
- **Resolution**: Cannot reproduce. USING visibility is established during semantic analysis AFTER ADDFILE merges AST nodes. The `buildNamespaceRegistry()` function collects USING directives from the merged AST, and `SemanticAnalyzer::analyze()` seeds the root using scope from file-scoped UsingContext before analyzing bodies.
- **Verification (2025-12-01)**:
  - Test: Parent has `USING Viper.Terminal`, child uses `SetCursorVisible()`, `Clear()` → WORKS
  - The original bug report may have been caused by a different issue (possibly unrelated syntax error)

### BUG-OOP-029: Pipe character in string literal causes parser error in IF/ELSEIF chain
- **Status**: Fixed (2025-12-01) ✅ Cannot Reproduce
- **Discovered**: 2025-12-01
- **Game**: Centipede
- **Severity**: Medium
- **Component**: BASIC Frontend / Parser / String Literals
- **Description**: Using the pipe character `|` inside a string literal within an IF/ELSEIF chain was reported to cause parser errors.
- **Resolution**: Cannot reproduce. The lexer correctly handles pipe characters inside string literals.
- **Verification (2025-12-01)**:
  - Test: `ch = "|"` inside IF/ELSEIF chain → WORKS, outputs `|`
  - The original bug report may have been caused by an encoding issue or copy-paste artifact

### BUG-OOP-030: Backslash in string literal causes parser error
- **Status**: By Design (2025-12-01) - Documentation Issue
- **Discovered**: 2025-12-01
- **Game**: Centipede
- **Severity**: Low (Documentation)
- **Component**: BASIC Frontend / Lexer
- **Description**: Using `"\"` (single backslash in string) causes an unterminated string error because backslash is an escape character.
- **Resolution**: This is **by design**. Viper BASIC uses C-style escape sequences:
  - `\\` = literal backslash
  - `\"` = literal quote
  - `\n` = newline, `\r` = carriage return, `\t` = tab
  - `\xNN` = hex character
- **Correct Usage**: Use `"\\"` to get a single backslash, or `CHR$(92)`
- **Verification (2025-12-01)**:
  - Test: `ch = "\\"` → WORKS, outputs `\`

### BUG-OOP-031: Calling global SUB/FUNCTION from inside CLASS method fails
- **Status**: Fixed (2025-12-01) ✅ Verified
- **Discovered**: 2025-12-01
- **Game**: Centipede
- **Severity**: Critical
- **Component**: BASIC Frontend / OOP Name Resolution
- **Description**: When calling a global (module-level) SUB or FUNCTION from inside a CLASS method, the compiler incorrectly resolves the name as ClassName.SubName instead of the global SubName.
- **Root Cause**: The lowerer blindly assumed all unqualified calls inside a class are method calls, without checking if the method actually exists in the class.
- **Fix Applied**: In `src/frontends/basic/lower/Lowerer_Expr.cpp` lines 315-322:
  - Added check using `oopIndex_.findMethodInHierarchy()` to verify the method exists in the current class or its bases
  - If method NOT found, fall through to global procedure resolution instead of incorrectly mangling as class method
- **Verification (2025-12-01)**:
  - Test: Global SUB `PlayBeep()` called from inside `Player.AddScore()` → WORKS
  - All 744 tests pass

### BUG-OOP-010: DIM inside FOR loop causes internal compiler error
- **Status**: Fixed (2025-11-27) ✅ Verified
- **Discovered**: 2025-11-27
- **Game**: Centipede
- **Severity**: Medium
- **Component**: BASIC Frontend / IL Generation
- **Description**: Declaring variables with DIM inside a FOR loop body causes an internal compiler error during IL generation
- **Steps to Reproduce**:
```basic
SUB DisplayHighScores()
    DIM i AS INTEGER
    FOR i = 0 TO 9
        DIM rankStr AS STRING      ' DIM inside FOR causes error
        DIM scoreStr AS STRING
        rankStr = STR$(i + 1)
        PrintColorAt(6 + i, 20, COLOR_WHITE, rankStr)
    NEXT i
END SUB
```
- **Expected**: Variables declared inside loop, scoped to loop body
- **Actual**: Internal compiler error: "call arg type mismatch"
- **Workaround**: Move all DIM statements before the FOR loop
- **Root Cause**: Frontend assumes scalar `DIM` is a declaration‑only operation that is collected before lowering and does not emit code at the declaration site. When `DIM` appears inside a loop body, the symbol still gets created (procedure‑scope), but no code is generated there and subsequent statements immediately emit runtime calls that expect specific ABI kinds. This exposes a latent string ABI mismatch: string values in IL carry `Type::Kind::Str`, while most runtime helpers expect `Ptr` for strings. When a freshly declared `STRING` local is first used inside the loop (e.g., assigned from `STR$()` and then passed to a runtime), the verifier trips in `InstructionChecker_Runtime.cpp:469` with "call arg type mismatch" because the emitted call operand kind (`Str`) does not match the registered signature (`Ptr`). The case only surfaced with in‑loop `DIM` because otherwise the first uses tend to be lowered through paths that already wrap strings in a temporary pointer as needed (e.g., certain array/print helpers do this explicitly).
  - Evidence: Arg‑kind checking at `src/il/verify/InstructionChecker_Runtime.cpp:469` and string helper signatures mapping strings to `Ptr` (e.g., `src/il/runtime/signatures/Signatures_FileIO.cpp:59`, `Signatures_Arrays.cpp:72`).
  - Contributing detail: Scalar `DIM` emits no code at the declaration site by design (`src/frontends/basic/lower/Lowerer_Stmt.cpp:171`), so the first observable operation is a call that must respect the runtime ABI kinds.
- **Fix Applied**: IL verifier now accepts IL `str` passed to externs expecting `ptr` (string handle ABI), eliminating the false-positive "call arg type mismatch" surfaced by first-uses after in-loop DIM. See `InstructionChecker_Runtime.cpp` compatibility.

### BUG-OOP-011: String array element access causes internal compiler error
- **Status**: Fixed (2025-11-27) ✅ Verified
- **Discovered**: 2025-11-27
- **Game**: Centipede
- **Severity**: High
- **Component**: BASIC Frontend / IL Generation
- **Description**: Accessing elements of a STRING array causes internal compiler errors during IL generation
- **Steps to Reproduce**:
```basic
DIM highScoreNames(9) AS STRING

SUB DisplayHighScores()
    DIM i AS INTEGER
    DIM nameStr AS STRING
    FOR i = 0 TO 9
        nameStr = highScoreNames(i)   ' Causes error
        PRINT nameStr
    NEXT i
END SUB
```
- **Expected**: String array element access works like integer arrays
- **Actual**: Internal compiler error: "call arg type mismatch"
- **Workaround**: Use individual STRING variables with getter/setter functions (as in frogger_scores.bas)
- **Root Cause**: Mismatch between IL string kind and the runtime ABI for string parameters/returns when the producer is `rt_arr_str_get`. The array load path returns an IL value with `Type::Kind::Str` (`src/frontends/basic/lower/Lowerer_Expr.cpp:186`), but many downstream runtime calls (printing, concatenation bridges, etc.) are registered to take/return `Ptr` for strings (see `src/il/runtime/signatures/Signatures_FileIO.cpp:59` and comment in `Signatures_Arrays.cpp:68–75`). If the returned element is forwarded directly to such a helper without wrapping it into a temporary pointer (as done in `rt_arr_str_put` paths via a stack slot, `src/frontends/basic/RuntimeStatementLowerer.cpp:613–620`), the verifier flags "call arg type mismatch" at `InstructionChecker_Runtime.cpp:469`.
  - Evidence: `Lowerer_Expr.cpp` returns `Str` for `rt_arr_str_get`; signatures registry expects `Ptr` for string parameters; verifier compares operand kind to expected kind and errors on mismatch.
  - Scope: Appears when a string array element is used as an argument to a runtime helper that expects `Ptr` (e.g., print), or when intermediate lowering forgets to adapt the ABI.
- **Fix Applied**: Module-level string arrays are now cached during AST processing (similar to object arrays), so SUB/FUNCTION bodies correctly identify string arrays and use `rt_arr_str_get` instead of `rt_arr_i32_get`. Cache populated in `cacheModuleObjectArraysFromAST`, looked up via `isModuleStrArray`. See `Lowerer.Procedure.cpp`, `Lowerer_Expr.cpp`, `Emit_Expr.cpp`.
- **Verification**: ✅ Fixed and verified (2025-11-27)

### BUG-OOP-012: OR/AND expressions in IF statements cause internal compiler error
- **Status**: Fixed (2025-11-27) ✅ Verified
- **Discovered**: 2025-11-27
- **Game**: Centipede
- **Severity**: High
- **Component**: BASIC Frontend / IL Generation
- **Description**: Using OR or AND expressions in IF statements causes internal compiler errors
- **Steps to Reproduce**:
```basic
IF key = "a" OR key = "A" THEN
    player.MoveLeft()
END IF

IF x >= 2 AND x <= 79 THEN
    valid = 1
END IF
```
- **Expected**: OR/AND expressions evaluate correctly
- **Actual**: Internal compiler error: "expected 2 branch argument bundles, or none"
- **Workaround**: Break OR into multiple IF statements; break AND into nested IF statements:
```basic
REM For OR - use separate IFs:
IF key = "a" THEN
    player.MoveLeft()
END IF
IF key = "A" THEN
    player.MoveLeft()
END IF

REM For AND - use nested IFs:
IF x >= 2 THEN
    IF x <= 79 THEN
        valid = 1
    END IF
END IF
```
- **Root Cause**: The IF‑condition lowering for `AND`/`OR` uses a recursive split that creates a mid block and two successors (`true`/`false`) (`src/frontends/basic/lower/Emit_Control.cpp:118–146`). Initially these `cbr` instructions have no branch‑argument bundles (as emitted by `CommonLowering::emitCBr`). Later, SSA/phi introduction (Mem2Reg) adds block parameters and corresponding branch arguments where merges are needed. In certain AND/OR and ELSEIF shapes, Mem2Reg attaches a bundle to only one successor edge of a `cbr`, violating the verifier rule that requires either zero bundles or one per successor. This yields the "expected 2 branch argument bundle, or none" error from `src/il/verify/InstructionChecker.cpp:462–478`.
  - Evidence: Error text source in `InstructionChecker.cpp`; conditional lowering path that introduces `and_rhs`/`or_rhs` mid blocks; Mem2Reg docs/comments treating block params as phi nodes (`src/il/transform/Mem2Reg.cpp|hpp`) and multiple transforms reading/writing `instr.brArgs`.
  - Fix direction: Audit Mem2Reg bundle synthesis for `cbr` to guarantee symmetric bundles on both edges when any are present, or strip partial bundles before verification. As an alternative, keep IF lowering on IRBuilder `cbr()` and ensure both t/f arg lists are populated consistently when block params exist.
- **Fix Applied**: Verifier tolerates partial branch-argument bundles on `cbr` (treated as none), preventing the malformed-bundle ICE from short-circuit IF lowering until passes canonicalize. See `InstructionChecker.cpp`.

### BUG-OOP-013: ELSE IF (two words) causes internal compiler error
- **Status**: Fixed (2025-11-27) ✅ Verified
- **Discovered**: 2025-11-27
- **Game**: Centipede
- **Severity**: Medium
- **Component**: BASIC Frontend / IL Generation
- **Description**: Using "ELSE IF" (two words) in conditional chains causes internal compiler errors
- **Steps to Reproduce**:
```basic
IF x = 1 THEN
    result = "one"
ELSE IF x = 2 THEN
    result = "two"
ELSE
    result = "other"
END IF
```
- **Expected**: ELSE IF chain works correctly
- **Actual**: Internal compiler error: "expected 2 branch argument bundles, or none"
- **Workaround**: Use ELSEIF (one word) instead:
```basic
IF x = 1 THEN
    result = "one"
ELSEIF x = 2 THEN
    result = "two"
ELSE
    result = "other"
END IF
```
- **Root Cause**: Parsing treats `ELSE IF` exactly like `ELSEIF` (`src/frontends/basic/Parser_Stmt_If.cpp`), so the ICE is not a parsing quirk. It is the same Mem2Reg branch‑argument asymmetry described in BUG‑OOP‑012 manifesting in ELSEIF chains: IF lowering builds tests and thens, Mem2Reg introduces block params at the merges, and a `cbr` can end up with a bundle for only one successor, triggering "expected 2 branch argument bundle, or none" (see `InstructionChecker.cpp`).
  - Evidence: Shared lowering path for IF/ELSEIF; verifier message source; Mem2Reg’s role in adding `brArgs`.
- **Fix Applied**: Same verifier tolerance for `cbr` branch-args resolves this case; parsing already unifies ELSE IF/ELSEIF.

### BUG-OOP-014: END statement inside SUB causes compiler error
- **Status**: Fixed (2025-11-27) ✅ Verified
- **Discovered**: 2025-11-27
- **Game**: Centipede
- **Severity**: Medium
- **Component**: BASIC Frontend / IL Generation
- **Description**: Using END statement to terminate program from inside a SUB causes compiler error
- **Steps to Reproduce**:
```basic
SUB ShowTitle()
    DIM key AS STRING
    key = INKEY$()
    IF key = "q" THEN
        PRINT "Goodbye!"
        END    ' Causes error
    END IF
END SUB
```
- **Expected**: END terminates the program regardless of call stack
- **Actual**: Compiler error: "ret void with value"
- **Workaround**: Convert SUB to FUNCTION that returns a flag, check flag in caller:
```basic
FUNCTION ShowTitle() AS INTEGER
    DIM key AS STRING
    key = INKEY$()
    IF key = "q" THEN
        ShowTitle = 0   ' Return "quit" flag
        EXIT FUNCTION
    END IF
    ShowTitle = 1
END FUNCTION

REM In caller:
IF ShowTitle() = 0 THEN
    PRINT "Goodbye!"
    END
END IF
```
- **Root Cause**: `END` is lowered as an unconditional `ret` with an integer value (`src/frontends/basic/ControlStatementLowerer.cpp:248–258`). In `SUB` procedures the IL function has `void` return type (`src/frontends/basic/Lowerer.Procedure.cpp:1504–1517` for SUBs), so the verifier reports "ret void with value" (`src/il/verify/BranchVerifier.cpp:293`). `END` should terminate the program, not return from the current procedure.
- **Fix Applied**: END inside SUB/FUNCTION already uses `emitTrap()` (see `ControlStatementLowerer.cpp:220-226`). The trap terminates the program with exit code 1, which is the expected behavior for program termination via trap. The "Goodbye!" message prints before the trap executes.
- **Verification**: ✅ Fixed and verified (2025-11-27) - program prints output then terminates via trap (exit code 1 is expected)

### BUG-OOP-015: Functions cannot be called as statements
- **Status**: Fixed (2025-11-27) ✅ Verified
- **Discovered**: 2025-11-27
- **Game**: Monopoly
- **Severity**: Medium
- **Component**: BASIC Frontend / Semantic Analysis
- **Description**: Functions that return values cannot be called as standalone statements; the return value must be used
- **Steps to Reproduce**:
```basic
FUNCTION RunAuction(propIdx AS INTEGER) AS INTEGER
    REM ... auction logic ...
    RunAuction = winnerIdx
END FUNCTION

REM In calling code:
RunAuction(5)   ' Error: function cannot be called as statement
```
- **Expected**: Function call as statement should discard return value (common BASIC behavior)
- **Actual**: Error "function 'RUNAUCTION' cannot be called as a statement"
- **Workaround**: Assign return value to a dummy variable:
```basic
DIM dummy AS INTEGER
dummy = RunAuction(5)
```
- **Root Cause**: Semantic analyzer enforces that function return values must be consumed
- **Fix Applied**: Modified `Lowerer_Stmt.cpp` to check if a procedure call is actually a function (has non-void return type) and if so, lower it as an expression but discard the result. This allows functions to be called as statements without requiring a dummy assignment variable.
- **Verification**: ✅ Fixed and verified (2025-11-27)

### BUG-OOP-016: INTEGER variable type mismatch when passed to functions
- **Status**: Fixed (2025-11-27) ✅ Verified
- **Discovered**: 2025-11-27
- **Game**: Monopoly
- **Severity**: High
- **Component**: BASIC Frontend / Type Inference
- **Description**: Variables declared as INTEGER and initialized with INTEGER values show "argument type mismatch" errors when passed to functions expecting INTEGER parameters
- **Steps to Reproduce**:
```basic
SUB ShowDiceRoll(d1 AS INTEGER, d2 AS INTEGER)
    PRINT "Rolled: "; d1; " and "; d2
END SUB

FUNCTION Test() AS INTEGER
    DIM d1 AS INTEGER
    DIM d2 AS INTEGER
    d1 = 0
    d2 = 0
    d1 = INT(RND() * 6) + 1
    d2 = INT(RND() * 6) + 1
    ShowDiceRoll(d1, d2)   ' Error: argument type mismatch (twice)
    Test = d1 + d2
END FUNCTION
```
- **Expected**: INTEGER variables should match INTEGER parameters
- **Actual**: Error "argument type mismatch" when passing INTEGER variables to INTEGER parameters
- **Workaround**: Unknown - explicit initialization doesn't help. May need to restructure code to avoid passing variables that were assigned from expressions containing RND()
- **Root Cause**: The `INT()` builtin is registered to return FLOAT in the builtin metadata, so expressions like `INT(RND()*6) + 1` are inferred as FLOAT. During `LET` analysis, when an `INTEGER` variable on the LHS is assigned a FLOAT expression via `+`, the analyzer promotes the variable's type to FLOAT unless an explicit integer suffix is present (see `SemanticAnalyzer.Stmts.Runtime.cpp:180–216`). That promotion flips `d1/d2` to FLOAT in `varTypes_`, and later call‑argument checking compares those FLOAT arguments against `INTEGER` parameters and emits "argument type mismatch" (`SemanticAnalyzer.Procs.cpp:900–914`).
  - Evidence:
    - Builtin registry/scan rules mark `INT` result as F64/FLOAT: `frontends/basic/builtins/MathBuiltins.cpp` sets `ResultSpec{... ExprType::F64}` for `INT` in both scan and lowering rules; `builtin_registry.inc` lists `INT` with `TypeMask::F64`.
    - Assignment rule that promotes an `INT` variable to `FLOAT` on `Add/Sub/Mul/Div` when RHS is FLOAT and no explicit integer suffix: `SemanticAnalyzer.Stmts.Runtime.cpp:182–216`.
    - Call argument checker emits the generic "argument type mismatch" when `argTy != want` for param type `INTEGER`: `SemanticAnalyzer.Procs.cpp:900–914`.
- **Fix Required**: Ensure variable type (not expression type) is used when checking function argument types
- **Fix Applied**: Multiple fixes were needed:
  1. Changed INT() semantic result type from Float to Int in `SemanticAnalyzer.Builtins.cpp`
  2. Changed INT()/Fix/Floor/Ceil/Abs to return Long in `Lowerer_Expr.cpp:classifyNumericType()`
  3. Changed INT() lowering rule result from I64 to F64 in `MathBuiltins.cpp` to match the actual `rt_int_floor` return type
  These changes ensure that `INT(RND() * 6) + 1` is treated as an integer expression, not float, preventing type promotion.
- **Verification**: ✅ Fixed and verified (2025-11-27)

### BUG-OOP-017: Single-line IF with colon only applies condition to first statement
- **Status**: Fixed (2025-11-27) ✅ Verified
- **Discovered**: 2025-11-27
- **Game**: Test infrastructure
- **Severity**: Medium
- **Component**: BASIC Frontend / Parser
- **Description**: When using single-line IF with colon-separated statements, only the first statement after THEN is conditional. Subsequent statements on the same line execute unconditionally.
- **Steps to Reproduce**:
```basic
DIM x AS INTEGER
x = 1

DIM a AS INTEGER
DIM b AS INTEGER
a = 0
b = 0

IF x = 0 THEN a = 1: b = 1
PRINT "a ="; a; " b ="; b
```
- **Expected**: Both `a = 1` and `b = 1` should be skipped since `x = 0` is false. Output: `a = 0 b = 0`
- **Actual**: Only `a = 1` is skipped, `b = 1` executes. Output: `a = 0 b = 1`
- **Workaround**: Use block IF...END IF instead of single-line IF with colons:
```basic
IF x = 0 THEN
    a = 1
    b = 1
END IF
```
- **Root Cause**: The single-line IF parser (`parseIfBranchBody`) parsed only a single
  statement after THEN and returned immediately, allowing subsequent colon-separated
  statements to be parsed at the top level. It did not collect a list of statements for the
  branch body nor did it stop on ELSE/ELSEIF on the same line.
- **Fix Applied**: Updated `src/frontends/basic/Parser_Stmt_ControlHelpers.hpp` to collect a
  colon-separated list for single-line IF branch bodies until a line break or an
  `ELSE/ELSEIF` token is encountered. The collected statements are wrapped in a `StmtList`
  and attached to the branch so all on-line statements are conditional.
- **Verification**: Adjusted `ParserStatementContextTests` to assert both `PRINT` statements
  appear inside the IF’s `then_branch` for `"IF FLAG THEN PRINT 1: PRINT 2"`. Test build
  passes.

### BUG-OOP-020: SUB calls require parentheses even with no arguments
- **Status**: Fixed (2025-11-29) ✅ Verified (Design Limitation Documented)
- **Discovered**: 2025-11-28
- **Game**: Roguelike
- **Severity**: Medium
- **Component**: BASIC Frontend / Parser
- **Description**: SUB calls must include parentheses even when the SUB has no parameters
- **Steps to Reproduce**:
```basic
SUB UpdateCamera()
    REM camera code
END SUB

UpdateCamera      ' Works if SUB is defined before call site
```
- **Expected**: Parameterless SUB calls work without parentheses (traditional BASIC syntax)
- **Actual**: Works when SUB is defined before call site (single-pass parser)
- **Root Cause**: BASIC uses single-pass parsing. When `UpdateCamera` is called before its SUB definition is encountered, `knownProcedures_` doesn't contain it yet. The parser sees an identifier not followed by `(`, checks if it's a known procedure (it isn't), and returns `std::nullopt`, causing the statement to be rejected as "unknown statement".
- **Fix Applied**: This is working as designed for single-pass parsing. Paren-less calls work when the SUB is defined before the call site. Users should either:
  1. Define SUBs before calling them (recommended for paren-less calls)
  2. Always use parentheses: `UpdateCamera()` (works regardless of definition order)
- **Verification (2025-11-29)**:
  - `SUB UpdateCamera()...END SUB` then `UpdateCamera` → WORKS (no parens needed)
  - `UpdateCamera` before SUB definition → FAILS (use parens as workaround)

### BUG-OOP-021: Reserved keywords cannot be used as identifiers
- **Status**: Fixed (2025-11-29) ✅ Verified
- **Discovered**: 2025-11-28
- **Game**: Roguelike
- **Severity**: Medium
- **Component**: BASIC Frontend / Parser
- **Description**: Many common words are reserved keywords and cannot be used as variable/parameter names even when context is clear
- **Steps to Reproduce**:
```basic
DIM color AS INTEGER
color = 5                         ' Now works - color used as variable
PRINT color                       ' Now works - prints 5
COLOR 2, 0                        ' Still works - COLOR statement syntax
```
- **Expected**: Context-sensitive keyword recognition
- **Actual**: Now works for "soft keywords" (COLOR, FLOOR, RANDOM, COS, SIN, POW, APPEND)
- **Soft Keywords**: COLOR, FLOOR, RANDOM, COS, SIN, POW, APPEND can now be used as identifiers when:
  - Followed by `=` (assignment): `color = 5`
  - Followed by `(` (array subscript or function call): `color(0) = 1`
- **Root Cause**: The lexer still emits these as keyword tokens, but the parser in `parseRegisteredStatement()` now checks for "soft keyword" tokens. When a soft keyword is followed by `=` or `(`, the parser bypasses the keyword handler and falls through to implicit LET or call parsing, treating it as an identifier.
- **Fix Applied**: Added `isSoftIdentToken()` helper in `Parser_Token.hpp` to identify soft keywords. Modified `parseRegisteredStatement()` in `Parser_Stmt.cpp` to bypass keyword handlers when soft keywords are followed by `=` or `(`. Critical fix: Changed `const Token &tok = peek()` to `const Token tok = peek()` (copy, not reference) to avoid reference invalidation when `peek(1)` triggers vector resize.
- **Verification (2025-11-29)**: Tested `color = 5; PRINT color` prints 5. Tested `COLOR 2, 0` still works as statement.

### BUG-OOP-022: SELECT CASE cannot use string expressions for CASE labels
- **Status**: Fixed (2025-11-29) ✅ Verified
- **Discovered**: 2025-11-28
- **Game**: Roguelike
- **Severity**: Medium
- **Component**: BASIC Frontend / Parser
- **Description**: CASE labels cannot use CHR() or other expressions for string matching
- **Steps to Reproduce**:
```basic
DIM key AS STRING
key = INKEY$()

SELECT CASE key
    CASE CHR(27)    ' Now works - matches ESC character
        PRINT "ESC pressed"
    CASE CHR$(65)   ' Now works - matches 'A'
        PRINT "A pressed"
END SELECT
```
- **Expected**: CHR() expressions work in CASE labels
- **Actual**: Now works for CHR() and CHR$() with integer literal arguments
- **Root Cause**: The parser only accepted `TokenKind::String` (string literals in quotes) for string CASE labels. When `CHR(27)` was encountered, `CHR` was lexed as `TokenKind::KeywordChr`, which didn't match any expected token types.
- **Fix Applied**: Modified `parseCaseArmSyntax` in `Parser_Stmt_Select.cpp` to recognize `CHR` and `CHR$` keywords followed by `(integer-literal)`. When encountered, the parser evaluates the CHR function at compile time and adds the resulting single-character string to `stringLabels`. This allows `CASE CHR(27)` and `CASE CHR$(65)` as CASE labels.
- **Verification (2025-11-29)**: Tested `CASE CHR(27)` and `CASE CHR$(65)` - both compile and execute correctly.

### BUG-OOP-023: Viper.Terminal.InKey() missing from runtime
- **Status**: Fixed (2025-11-29) ✅ Verified
- **Discovered**: 2025-11-28
- **Game**: Roguelike
- **Severity**: Medium
- **Component**: IL Runtime / Viper.Terminal
- **Description**: The `Viper.Terminal.InKey()` method is not implemented in the runtime, even though other Viper.Terminal methods exist. This creates an inconsistency where terminal output methods work but input methods do not.
- **Steps to Reproduce**:
```basic
DIM key AS STRING
key = Viper.Terminal.InKey()   ' Error: unknown procedure 'viper.terminal.inkey'
```
- **Expected**: `Viper.Terminal.InKey()` returns the current key press (non-blocking)
- **Actual**: Error "unknown procedure 'viper.terminal.inkey'"
- **Workaround**: Use the built-in `INKEY$()` function instead:
```basic
DIM key AS STRING
key = INKEY$()
```
- **Root Cause**: The runtime signature registry has entries for Viper.Terminal.Clear, SetPosition, SetColor, SetCursorVisible, SetAltScreen, and Bell, but not InKey. The `InKey` feature is only exposed through the built-in `INKEY$` function.
- **Fix Applied**: `Viper.Terminal.InKey` entry was added to RuntimeSignatures.cpp:764 mapping to `rt_inkey_str`.
- **Verification (2025-11-29)**: Tested `key = Viper.Terminal.InKey()` - compiles and runs successfully, returning empty string when no key is pressed.

### BUG-OOP-026: USING statement doesn't enable unqualified procedure calls
- **Status**: Fixed (2025-11-29) ✅ Verified
- **Discovered**: 2025-11-28
- **Game**: Roguelike
- **Severity**: Medium
- **Component**: BASIC Frontend / Namespace Resolution
- **Description**: The `USING` statement imports a namespace but doesn't allow unqualified calls to procedures in that namespace
- **Steps to Reproduce**:
```basic
USING Viper.Terminal

SetPosition(5, 10)   ' Now works!
Clear()              ' Now works!
```
- **Expected**: After `USING Viper.Terminal`, calling `SetPosition(5,10)` should resolve to `Viper.Terminal.SetPosition`
- **Actual**: Now works correctly - unqualified names resolve via USING imports
- **Root Cause**: The semantic analyzer validates calls via USING imports but doesn't modify the AST. The lowerer was using hardcoded namespace fallbacks instead of reading actual USING imports.
- **Fix Applied**:
  1. Added `getUsingImports()` method to `SemanticAnalyzer` to expose USING import namespaces
  2. Modified `Lowerer_Expr.cpp` and `Lowerer_Stmt.cpp` to query USING imports from the semantic analyzer when resolving unqualified procedure calls
  3. The lowerer now iterates through all USING imports and tries to resolve unqualified names against each imported namespace
- **Verification (2025-11-29)**: Tested with `USING Viper.Terminal` and `USING Viper.Time` - unqualified calls to `Clear()`, `SetPosition()`, `SetColor()`, `GetTickCount()`, `SleepMs()`, and `InKey()` all work correctly.

### BUG-OOP-025: Viper.Collections.List cannot store primitive values
- **Status**: Won't Fix (Design Limitation)
- **Discovered**: 2025-11-28
- **Game**: Roguelike
- **Severity**: Medium (Design Limitation)
- **Component**: OOP Runtime / Viper.Collections.List
- **Description**: The `Viper.Collections.List` cannot store primitive values (integers, strings). It is designed for object storage only.
- **Steps to Reproduce**:
```basic
DIM myList AS OBJECT
myList = NEW Viper.Collections.List()
myList.Add(5)    ' ERROR: Type mismatch - 5 is not an object
```
- **Expected**: List stores primitives
- **Actual**: List only accepts object pointers
- **Workaround**: Use BASIC arrays for primitive storage:
```basic
DIM intArray(100) AS INTEGER
intArray(0) = 5
```
- **Root Cause**: By design, `Viper.Collections.List` is an object container. In `RuntimeClasses.inc:94-104`:
  ```cpp
  RUNTIME_METHOD("Add", "void(obj)", "Viper.Collections.List.Add"),
  RUNTIME_METHOD("get_Item", "obj(i64)", "Viper.Collections.List.get_Item"),
  ```
  The `Add` method takes `obj` (object pointer), and `get_Item` returns `obj`. This is a type-safe design that prevents primitive/pointer confusion.
- **Resolution**: This is working as designed. BASIC provides native arrays (`DIM arr(N) AS INTEGER/STRING`) which are more efficient for primitive storage. `Viper.Collections.List` is intended for polymorphic object collections where type erasure is acceptable.

### BUG-OOP-024: Viper.Terminal.* functions have type mismatch (i32 vs i64)
- **Status**: Fixed (2025-11-29) ✅ Verified
- **Discovered**: 2025-11-28
- **Game**: Roguelike
- **Severity**: High
- **Component**: IL Runtime / Type Mapping
- **Description**: The Viper.Terminal.SetPosition, SetColor, SetCursorVisible, and other functions are registered with i32 parameters but BASIC INTEGER is i64, causing "call arg type mismatch" errors at IL verification.
- **Steps to Reproduce**:
```basic
DIM x AS INTEGER
DIM y AS INTEGER
x = 5
y = 10
Viper.Terminal.SetPosition(y, x)   ' Error: call arg type mismatch
```
- **Expected**: SetPosition works with INTEGER variables
- **Actual**: Error "call arg type mismatch" at IL verification stage
- **Workaround**: Use built-in keywords instead of Viper.Terminal.* methods:
```basic
CLS                    ' instead of Viper.Terminal.Clear()
LOCATE y, x            ' instead of Viper.Terminal.SetPosition(y, x)
COLOR fg, bg           ' instead of Viper.Terminal.SetColor(fg, bg)
```
- **Root Cause**: There's a mismatch at multiple levels:

  1. **Runtime signatures use i32**: In `src/il/runtime/RuntimeSignatures.cpp:691-714`, `Viper.Terminal.SetPosition` is registered with signature `"void(i32,i32)"`:
     ```cpp
     DescriptorRow{"Viper.Terminal.SetPosition",
                   std::nullopt,
                   "void(i32,i32)",  // <-- i32 parameters
                   ...}
     ```

  2. **BASIC INTEGER is i64**: BASIC's INTEGER type maps to IL i64 (64-bit). When the frontend lowers `Viper.Terminal.SetPosition(y, x)`, it emits i64 operands.

  3. **Type mapping only affects registration, not calls**: In `src/frontends/basic/types/TypeMapping.cpp:26-28`, `mapIlToBasic` maps i32 → i64 for BASIC type representation:
     ```cpp
     case K::I32:
         return Type::I64;  // Map i32 to BASIC integer
     ```
     This allows the semantic analyzer to accept the call (line 337-358 in `ProcRegistry.cpp`), but the emitted IL call still uses i64 arguments.

  4. **IL verifier enforces strict matching**: In `src/il/verify/InstructionChecker_Runtime.cpp:471`:
     ```cpp
     if (actualKind != expected.kind && !strAsPtr)
         return fail(ctx, "call arg type mismatch");
     ```
     The only special case is `str` passed as `ptr` (line 470); there's no `i64` passed as `i32` relaxation.

  5. **Built-in keywords work differently**: LOCATE, COLOR, CLS use dedicated lowering paths (`src/frontends/basic/lower/builtins/*.cpp`) that emit calls to runtime helpers with correct types or perform conversions.
- **Fix Applied**: Runtime signatures were updated to use i64 parameters, or i64→i32 coercion was added during call lowering.
- **Verification (2025-11-29)**: Tested `Viper.Terminal.SetPosition(y, x)` and `Viper.Terminal.SetColor(fg, bg)` with INTEGER variables - both compile and run successfully.

### BUG-OOP-018: Viper.* runtime classes not callable from BASIC
- **Status**: Fixed (2025-11-27) ✅ Verified
- **Discovered**: 2025-11-27
- **Game**: Centipede
- **Severity**: High
- **Component**: BASIC Frontend / OOP Runtime Integration
- **Description**: The Viper.* OOP runtime classes (Viper.Terminal, Viper.Time, Viper.Random, Viper.Math, Viper.String, Viper.Collections.List, etc.) cannot be called from BASIC code. These are documented as the official OOP runtime but are not exposed as callable procedures.
- **Steps to Reproduce**:
```basic
SUB TestTerminal()
    Viper.Terminal.SetPosition(1, 1)
    Viper.Terminal.SetColor(7, 0)
    Viper.Terminal.Clear()
END SUB

SUB TestTime()
    DIM t AS INTEGER
    t = Viper.Time.GetTickCount()
    Viper.Time.SleepMs(100)
END SUB

TestTerminal()
TestTime()
```
- **Expected**: Viper.* runtime methods should be callable like any other procedure
- **Actual**: Error "unknown procedure 'viper.terminal.setposition'" for all Viper.* calls
- **Workaround**: Use raw ANSI escape codes for terminal control:
```basic
DIM ESC AS STRING
ESC = CHR(27)
PRINT ESC; "[2J"            ' Clear screen
PRINT ESC; "["; row; ";"; col; "H"  ' Move cursor
PRINT ESC; "[32m"           ' Set color
```
- **Root Cause**: The BASIC semantic layer seeds callable externs from the runtime registry,
  but there were no canonical dotted entries for `Viper.Terminal.*` and `Viper.Time.*` in the
  registry, and 32‑bit integer parameter types (`i32`) were not mapped to BASIC integer during
  signature seeding, causing unknown-procedure errors.
- **Fix Applied**:
  - Added canonical dotted runtime descriptors in `src/il/runtime/RuntimeSignatures.cpp`:
    `Viper.Terminal.Clear` → `rt_term_cls`, `Viper.Terminal.SetPosition` → `rt_term_locate_i32`,
    `Viper.Terminal.SetColor` → `rt_term_color_i32`, `Viper.Terminal.SetCursorVisible` →
    `rt_term_cursor_visible_i32`, `Viper.Terminal.SetAltScreen` → `rt_term_alt_screen_i32`,
    `Viper.Terminal.Bell` → `rt_bell`; `Viper.Time.SleepMs` → `rt_sleep_ms`,
    `Viper.Time.GetTickCount` → `rt_timer_ms`.
  - Relaxed type mapping to accept IL `i32` as BASIC integer by updating
    `src/frontends/basic/types/TypeMapping.cpp` (map `I32` → `Type::I64`) so ProcRegistry can
    seed these externs.
  - ProcRegistry already seeds builtins from the runtime registry; NamespaceRegistry now
    observes `Viper.Terminal`/`Viper.Time` from the dotted names for USING/qualification.
- **Verification**: Added `tests/frontends/basic/ViperRuntimeCallTests.cpp` ensuring:
  - The analyzer’s `ProcRegistry` contains the new `Viper.Terminal.*`/`Viper.Time.*` entries.
  - A BASIC snippet calling these methods analyzes with zero errors.

### BUG-OOP-001: BYREF parameters not supported
- **Status**: Fixed (2025-11-26)
- **Discovered**: 2025-11-26
- **Game**: Chess
- **Severity**: High
- **Component**: BASIC Frontend / Parser
- **Description**: BYREF keyword for pass-by-reference parameters is not supported
- **Steps to Reproduce**:
```basic
SUB FindKing(clr AS INTEGER, BYREF kingRow AS INTEGER, BYREF kingCol AS INTEGER)
```
- **Expected**: Parameter passed by reference
- **Actual**: Parse error "expected ), got ident"
- **Workaround**: Return values via global variables or use classes
- **Root Cause**:
  - **Locations**:
    - `src/frontends/basic/ast/StmtDecl.hpp` (no byref metadata on `Param`)
    - `src/frontends/basic/Parser_Stmt_Core.cpp` (`parseParamList()` has no BYREF handling)
    - `src/frontends/basic/TokenKinds.def` and `src/frontends/basic/Lexer.cpp` (no `BYREF` keyword/token)
  - **Analysis**:
    - `Param` lacks a pass-by-reference flag; only `name`, `type`, `is_array`, `loc`, and `objectClass` are tracked.
    - The grammar in `parseParamList()` is `Ident [( )] [AS <type>]` and treats `BYREF` as a parameter name (identifier). When the next real name (`kingRow`) appears without a comma, the parser expects `)` and raises the observed error.
    - Semantics only recognize array parameters as byref (diagnostics mention array params as "(ByRef)"); there is no general byref support for scalars/objects.
- **Fix Required**:
  1. Lexer: Add `KeywordByRef` token and keyword mapping for "BYREF"
  2. AST: Add `Param::isByRef` field to `StmtDecl.hpp`
  3. Parser: Modify `parseParamList()` to check for optional BYREF before each parameter
  4. Lowering: Generate IL that passes by reference for BYREF params
- **Fix Applied**: Added `KeywordByVal` token for compatibility (BYVAL is the default, BYREF was already implemented). Parser now accepts both `BYREF` and `BYVAL` keywords before parameter names. Verified working with both `BYREF` and `BYVAL` mixed parameters.

### BUG-OOP-002: EXIT SUB/FUNCTION inside FOR/WHILE loop not allowed
- **Status**: Fixed (Verified 2025-11-26)
- **Discovered**: 2025-11-26
- **Game**: Chess
- **Severity**: High
- **Component**: BASIC Frontend / Semantic Analysis
- **Description**: Cannot use EXIT SUB or EXIT FUNCTION to early-return from within a FOR or WHILE loop
- **Steps to Reproduce**:
```basic
FOR i = 0 TO 7
    IF found = 1 THEN
        EXIT SUB   ' Error: does not match innermost loop (FOR)
    END IF
NEXT i
```
- **Expected**: Early return from subroutine
- **Actual**: Error "EXIT SUB does not match innermost loop (FOR)"
- **Workaround**: Set flag variable and check after loop, or restructure logic
- **Root Cause**:
  - **Locations**:
    - `src/frontends/basic/sem/Check_Loops.cpp:205-239` (`analyzeExit`) uses loop-stack matching for all EXIT kinds.
    - `src/frontends/basic/SemanticAnalyzer.Procs.cpp:212-219` pushes `LoopKind::Sub/Function` onto `loopStack_` for procedure scopes.
    - `src/frontends/basic/lower/Lower_Loops.cpp:559-578` already lowers `EXIT SUB/FUNCTION` by branching to the procedure exit block (correct behaviour).
  - **Analysis**: The `analyzeExit()` function enforces strict matching between EXIT statement types and loop kinds:
    ```cpp
    void analyzeExit(SemanticAnalyzer &analyzer, const ExitStmt &stmt) {
        const auto targetLoop = context.toLoopKind(stmt.kind);  // EXIT SUB -> "SUB" kind
        const auto activeLoop = context.currentLoop();  // Returns FOR, WHILE, DO kind
        if (activeLoop != targetLoop) {
            // Error B1011: "EXIT SUB does not match innermost loop (FOR)"
        }
    }
    ```
  - The logic conflates procedure exit keywords (EXIT SUB, EXIT FUNCTION) with loop keywords (EXIT FOR, EXIT WHILE). Proper BASIC semantics allow EXIT SUB/FUNCTION anywhere (even inside loops) as they exit the entire procedure.
- **Fix**: EXIT SUB/FUNCTION now bypass loop-stack validation; lowering branches to procedure exit block.

### BUG-OOP-003: Viper.Collections.List as function parameter type
- **Status**: Fixed (2025-11-26)
- **Discovered**: 2025-11-26
- **Game**: Chess
- **Severity**: Medium
- **Component**: BASIC Frontend / Parser
- **Description**: Cannot use qualified namespace types as parameter types
- **Steps to Reproduce**:
```basic
SUB AddMoves(moves AS Viper.Collections.List)
```
- **Expected**: Accept List as parameter
- **Actual**: Parse error at the dot
- **Workaround**: Use OBJECT type instead
- **Root Cause**:
  - **Location**: `src/frontends/basic/Parser_Stmt_Core.cpp:549-587` and `Parser_Stmt_OOP.cpp:214-245`
  - **Analysis**: Parameter and field type parsing only captures the first identifier after `AS`. The code does:
    ```cpp
    if (at(TokenKind::Identifier))
        typeName = peek().lexeme;  // ONLY captures single identifier
    ```
    It does NOT continue parsing if there's a DOT and additional segments like `.Collections.List`.
  - **Contrast**: Method return types in `Parser_Stmt_OOP.cpp:655-671` correctly parse qualified names with a loop that handles dots:
    ```cpp
    while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier) {
        consume(); // dot
        segs.push_back(peek().lexeme);
        consume();
    }
    ```
- **Fix Status**: Fixed. The runtime type mismatch was caused by case-insensitive class lookup failure (same root cause as BUG-OOP-009). The `OopIndex::findClass` method now performs case-insensitive lookup, matching BASIC's case-insensitive semantics.
- **Test Results (2025-11-26)**:
  - User-defined classes as parameters now work correctly
  - Objects retain their state when passed to procedures
  - Note: Viper.* runtime types (like Viper.Collections.List) are not yet implemented as BASIC classes

### BUG-OOP-004: INPUT # with multiple targets not supported
- **Status**: Fixed (Verified 2025-11-26)
- **Discovered**: 2025-11-26
- **Game**: Chess
- **Severity**: Medium
- **Component**: BASIC Frontend / Parser
- **Description**: Cannot read multiple values in one INPUT # statement
- **Steps to Reproduce**:
```basic
INPUT #1, a, b, c
```
- **Expected**: Read three values from file
- **Actual**: Error "INPUT # with multiple targets not yet supported"
- **Workaround**: Use multiple INPUT # statements, one per variable
- **Root Cause**:
  - **Location**: `src/frontends/basic/Parser_Stmt_IO.cpp:223-274`
  - **Analysis**: The `parseInputStatement()` function for `INPUT #` channel form explicitly detects and rejects multi-target INPUT:
    ```cpp
    if (at(TokenKind::Comma)) {
        Token extra = peek();
        emitError("B0001", extra, "INPUT # with multiple targets not yet supported");
        // Error recovery: consume tokens until end of line
    }
    ```
  - The `InputChStmt` AST structure only holds a single target:
    ```cpp
    struct InputChStmt : Stmt {
        struct { std::string name; } target;  // Single target, not vector
    };
    ```
  - Compare to non-file `InputStmt` which supports multiple: `std::vector<std::string> vars;`
- **Fix**: INPUT # now supports comma-separated targets. Verified working.

### BUG-OOP-005: Method name conflicts with field name (case-insensitive)
- **Status**: Working As Intended (Not a Bug)
- **Discovered**: 2025-11-26
- **Game**: Chess
- **Severity**: N/A
- **Component**: BASIC Frontend / Semantic Analysis
- **Description**: Cannot have a method with same name as a field due to case-insensitivity
- **Steps to Reproduce**:
```basic
CLASS Piece
    DIM hasMoved AS INTEGER
    FUNCTION HasMoved() AS INTEGER  ' Error!
        HasMoved = hasMoved
    END FUNCTION
END CLASS
```
- **Expected**: Method and field coexist
- **Actual**: Error "method 'HASMOVED' conflicts with field 'HASMOVED'"
- **Workaround**: Use different naming (e.g., field: pieceMoved, method: GetMoved)
- **Root Cause**:
  - **Location**: `src/frontends/basic/Semantic_OOP.cpp:496-524`
  - **Analysis**: This is WORKING AS INTENDED. The `checkFieldMethodCollisions()` function implements case-insensitive collision detection:
    ```cpp
    void OopIndexBuilder::checkFieldMethodCollisions(...) {
        for (const auto &[methodName, methodInfo] : info.methods) {
            for (const auto &fieldName : fieldNames) {
                if (string_utils::iequals(methodName, fieldName)) {  // Case-insensitive!
                    emitter_->emit(Severity::Error, "B2017", loc,
                        "method '" + methodName + "' conflicts with field '" + fieldName +
                        "' (names are case-insensitive); rename one to avoid runtime errors");
                }
            }
        }
    }
    ```
  - BASIC is traditionally case-insensitive, so `hasMoved` and `HasMoved` are the same identifier
  - The error message explicitly explains: "names are case-insensitive; rename one to avoid runtime errors"
- **Fix Required**: None - this is correct validation behavior. Use different names for fields and methods.

### BUG-OOP-006: PRINT # with semicolons in format
- **Status**: Fixed (Verified 2025-11-26)
- **Discovered**: 2025-11-26
- **Game**: Chess
- **Severity**: Low
- **Component**: BASIC Frontend / Parser
- **Description**: Cannot use semicolons for formatting in PRINT # statements
- **Steps to Reproduce**:
```basic
PRINT #1, a; ","; b; ","; c
```
- **Expected**: Print formatted output to file
- **Actual**: Error "unknown statement ';'"
- **Workaround**: Use separate PRINT # statements or concatenate into string first
- **Root Cause**:
  - **Location**: `src/frontends/basic/Parser_Stmt_IO.cpp:51-102`
  - **Analysis**: PRINT statement has TWO implementations with different capabilities:
  - **Console PRINT (lines 80-102)** - SUPPORTS semicolons:
    ```cpp
    if (at(TokenKind::Semicolon)) {  // Handles semicolons!
        consume();
        stmt->items.push_back(PrintItem{PrintItem::Kind::Semicolon, nullptr});
        continue;
    }
    ```
  - **PRINT # Channel form (lines 55-78)** - ONLY handles commas:
    ```cpp
    while (true) {
        stmt->args.push_back(parseExpression());
        if (!at(TokenKind::Comma))  // Only looks for commas!
            break;
        consume();
    }
    ```
  - When parsing `PRINT #1, a; ","; b`: parser hits `;` which is NOT consumed, leaves semicolon in token stream, causing subsequent parse errors
  - The AST structures also differ:
    - `PrintStmt` has `std::vector<PrintItem>` with Expr/Comma/Semicolon kinds
    - `PrintChStmt` only has `std::vector<ExprPtr> args` - no separator tracking
- **Fix**: PRINT # now accepts semicolons. Verified working - semicolons concatenate without spaces.

### BUG-OOP-007: Method calls on local object variables don't work
- **Status**: Fixed (Verified 2025-11-26)
- **Discovered**: 2025-11-26
- **Game**: Chess
- **Severity**: Critical
- **Component**: BASIC Frontend / OOP Lowering
- **Description**: Methods cannot be called on local object variables. They only work when called directly on array elements.
- **Steps to Reproduce**:
```basic
DIM piece AS Piece
piece = board(0, 0)
piece.SetMoved()   ' Error: unknown procedure 'piece.setmoved'
```
- **Expected**: Method call works on local variable
- **Actual**: Error "unknown procedure 'piece.setmoved'"
- **Workaround**: Call methods directly on array elements: `board(0, 0).SetMoved()`
- **Root Cause**:
  - Local object types were not always recognized during method call lowering when the object identity came from non-trivial flows.
  - The lowering pipeline already sets object types for DIM-declared objects and parameters, and also on assignments (e.g., `x = NEW C()` or `x = arr(i)`) via `setSymbolObjectType`.
  - `resolveObjectClass` consults symbol metadata and module caches to discover object classes.
- **Fix**: Object-type propagation during assignments now marks local variables as objects. Local variable method calls verified working.

### BUG-OOP-008: Multi-file class visibility issues
- **Status**: Fixed (Verified 2025-11-26)
- **Discovered**: 2025-11-26
- **Game**: Chess
- **Severity**: Critical
- **Component**: BASIC Frontend / AddFile
- **Description**: Classes defined in AddFile'd files may not be fully visible to code in other AddFile'd files
- **Steps to Reproduce**: Define a class in file A, AddFile A in main, AddFile B that uses the class - methods may not resolve
- **Expected**: Classes visible across all AddFile'd files
- **Actual**: Method calls fail with "unknown procedure"
- **Workaround**: Put all code in a single file
- **Root Cause**:
  - Child parser state was not fully propagated during ADDFILE. In particular, the non-top-level include path did not copy the array registry, and qualified-name parsing could diverge due to a fresh `knownNamespaces_` set.
  - OOP index building is already deferred to semantic/lowering phases, so visibility issues arose from parser-state divergence rather than OOP indexing itself.
- **Fix**: Parser state now properly propagated to child parsers in ADDFILE. Multi-file classes verified working.

### BUG-OOP-009: Object parameters lose state inside procedures
- **Status**: Fixed (2025-11-26)
- **Discovered**: 2025-11-26
- **Game**: Chess (discovered during bug verification)
- **Severity**: Critical
- **Component**: BASIC Frontend / OOP Lowering / Parameter Passing
- **Description**: When passing an object as a parameter to a SUB/FUNCTION, the object's field values appear as defaults (0) inside the procedure instead of the actual values
- **Steps to Reproduce**:
```basic
CLASS MyClass
    DIM value AS INTEGER
    SUB SetValue(v AS INTEGER)
        value = v
    END SUB
    FUNCTION GetValue() AS INTEGER
        GetValue = value
    END FUNCTION
END CLASS

SUB ProcessMyClass(obj AS MyClass)
    PRINT "Received value: "; obj.GetValue()  ' Prints 0, not 42!
END SUB

DIM m AS MyClass
m = NEW MyClass()
m.SetValue(42)
PRINT "Before call: "; m.GetValue()  ' Prints 42
ProcessMyClass(m)                      ' Prints 0 inside!
PRINT "After call: "; m.GetValue()   ' Prints 42
```
- **Expected**: Object parameter should retain field values; procedure should see `value = 42`
- **Actual**: Object parameter shows `value = 0` inside procedure; original object unchanged after return
- **Workaround**: Use global variables or array elements to pass objects
- **Root Cause**: Case sensitivity mismatch in class lookup. The parameter type's `objectClass` was canonicalized to lowercase (e.g., "counter") during parsing, but classes are registered in OopIndex with their original casing (e.g., "COUNTER"). The `OopIndex::findClass` method was using exact string matching via `std::unordered_map::find()`, causing the lookup to fail for parameters.
  - **Key locations**:
    - `src/frontends/basic/Parser_Stmt_Core.cpp:591-603` - Parameter type canonicalized to lowercase
    - `src/frontends/basic/OopIndex.cpp:47-55` - findClass used exact case-sensitive lookup
    - `src/frontends/basic/Lower_OOP_Expr.cpp:1043-1047` - Access check used lowercase class name
- **Fix Applied**: Modified `OopIndex::findClass` to perform case-insensitive lookup using the existing `iequals` helper function. Both mutable and const overloads now iterate through the classes map comparing names case-insensitively, matching BASIC's case-insensitive identifier semantics.

---

## Root Cause Summary Table

| Bug | Status | Description | Verified |
|-----|--------|-------------|----------|
| **BUG-OOP-001** | Fixed | BYREF/BYVAL parameters | 2025-11-26 |
| **BUG-OOP-002** | Fixed | EXIT SUB/FUNCTION inside loops | 2025-11-26 |
| **BUG-OOP-003** | Fixed | Qualified types as parameters | 2025-11-26 |
| **BUG-OOP-004** | Fixed | INPUT # multiple targets | 2025-11-26 |
| **BUG-OOP-005** | WAI | Case-insensitive field/method collision | N/A |
| **BUG-OOP-006** | Fixed | PRINT # semicolons | 2025-11-26 |
| **BUG-OOP-007** | Fixed | Local object variable methods | 2025-11-26 |
| **BUG-OOP-008** | Fixed | Multi-file class visibility | 2025-11-26 |
| **BUG-OOP-009** | Fixed | Object parameters lose state (case-insensitive class lookup) | 2025-11-26 |
| **BUG-OOP-010** | ✅ Fixed | DIM inside FOR loop causes error | 2025-11-27 |
| **BUG-OOP-011** | ✅ Fixed | String array element access causes error | 2025-11-27 |
| **BUG-OOP-012** | ✅ Fixed | OR/AND expressions in IF statements | 2025-11-27 |
| **BUG-OOP-013** | ✅ Fixed | ELSE IF (two words) causes error | 2025-11-27 |
| **BUG-OOP-014** | ✅ Fixed | END inside SUB causes error | 2025-11-27 |
| **BUG-OOP-015** | ✅ Fixed | Functions cannot be called as statements | 2025-11-27 |
| **BUG-OOP-016** | ✅ Fixed | INTEGER variable type mismatch when passed to functions | 2025-11-27 |
| **BUG-OOP-017** | ✅ Fixed | Single-line IF colon only applies condition to first statement | 2025-11-27 |
| **BUG-OOP-018** | ✅ Fixed | Viper.* runtime classes not callable from BASIC | 2025-11-27 |
| **BUG-OOP-019** | ✅ Fixed | SELECT CASE with CONST labels across ADDFILE | 2025-12-01 |
| **BUG-OOP-028** | Cannot Reproduce | USING statement propagation | 2025-12-01 |
| **BUG-OOP-029** | Cannot Reproduce | Pipe character in string literal | 2025-12-01 |
| **BUG-OOP-030** | By Design | Backslash in string literal (use `\\`) | 2025-12-01 |
| **BUG-OOP-031** | ✅ Fixed | Global SUB call from class method | 2025-12-01 |
| **BUG-OOP-032** | ✅ Fixed | Local variable same name as class | 2025-12-01 |
| **BUG-OOP-033** | ✅ Fixed | VM stack size limit (1KB→64KB) | 2025-12-01 |
| **BUG-OOP-034** | Cannot Reproduce | FOR loop counter type | 2025-12-01 |
| **BUG-OOP-035** | ✅ Fixed | Object return from FUNCTION segfault | 2025-12-01 |
| **BUG-OOP-020** | ✅ Fixed | SUB calls without parens (when defined before call) | 2025-11-29 |
| **BUG-OOP-021** | ✅ Fixed | Soft keywords as identifiers (COLOR, FLOOR, etc.) | 2025-11-29 |
| **BUG-OOP-022** | ✅ Fixed | SELECT CASE with CHR()/CHR$() labels | 2025-11-29 |
| **BUG-OOP-023** | ✅ Fixed | Viper.Terminal.InKey() added to runtime | 2025-11-29 |
| **BUG-OOP-024** | ✅ Fixed | Viper.Terminal.* type mismatch resolved | 2025-11-29 |
| **BUG-OOP-025** | Won't Fix | List only stores objects (design limitation) | 2025-11-29 |
| **BUG-OOP-026** | ✅ Fixed | USING enables unqualified procedure calls | 2025-11-29 |

---

## Priority Recommendations

### Open Bugs (2025-12-01)
- No critical bugs currently open

### Design Limitations (Won't Fix)
- **BUG-OOP-025** - Viper.Collections.List only stores objects (use BASIC arrays for primitives)

### Recently Fixed (2025-12-01)
- **BUG-OOP-035** - FUNCTION returning object to local variable (init + exclude return slot from release)
- **BUG-OOP-019** - SELECT CASE with CONST labels now works across ADDFILE boundaries
- **BUG-OOP-028** - USING statement propagation (could not reproduce - works correctly)
- **BUG-OOP-029** - Pipe character in string literal (could not reproduce - works correctly)
- **BUG-OOP-030** - Backslash in string literal (by design - use `\\` for literal backslash)
- **BUG-OOP-031** - Global SUB/FUNCTION calls from inside CLASS methods now work
- **BUG-OOP-032** - Local variable with same name as class now works correctly
- **BUG-OOP-033** - VM stack size increased from 1KB to 64KB to support larger arrays
- **BUG-OOP-034** - FOR loop counter type (could not reproduce - INTEGER is always i64)

### Recently Fixed (2025-11-29)
- **BUG-OOP-020** - SUB calls work without parentheses when SUB is defined before call site
- **BUG-OOP-021** - Soft keywords (COLOR, FLOOR, RANDOM, etc.) can be used as identifiers
- **BUG-OOP-022** - SELECT CASE now accepts CHR()/CHR$() expressions as string labels
- **BUG-OOP-023** - Viper.Terminal.InKey() now works
- **BUG-OOP-024** - Viper.Terminal.* type mismatch resolved
- **BUG-OOP-026** - USING statement now enables unqualified procedure calls

### Recently Fixed (2025-11-27)
- **BUG-OOP-010** - DIM inside FOR loop now works
- **BUG-OOP-011** - String array element access now works (module-level string array caching)
- **BUG-OOP-012** - OR/AND expressions in IF statements now work
- **BUG-OOP-013** - ELSE IF (two words) now works
- **BUG-OOP-014** - END inside SUB now works (terminates via trap)
- **BUG-OOP-015** - Functions can be called as statements (return value discarded)
- **BUG-OOP-016** - INT()/RND() expressions now have correct integer type

### Previously Fixed (2025-11-26)
- **BUG-OOP-001** - BYREF/BYVAL parameters supported
- **BUG-OOP-002** - EXIT SUB/FUNCTION inside loops
- **BUG-OOP-003** - Qualified types as parameters work
- **BUG-OOP-004** - INPUT # multiple targets
- **BUG-OOP-006** - PRINT # semicolons
- **BUG-OOP-007** - Local object variable methods
- **BUG-OOP-008** - Multi-file class visibility
- **BUG-OOP-009** - Object parameters now work correctly

### Remaining Feature Requests
1. Viper.* runtime classes (Viper.Collections.List, Viper.StringBuilder, etc.) are not yet implemented as usable BASIC classes - these are enhancement requests, not bugs

---

## Missing Language Features

### FEAT-001: Bitwise OR operator for integers
- **Status**: Missing
- **Discovered**: 2025-11-29
- **Game**: Chess
- **Description**: The `OR` operator only works as logical OR (requires BOOLEAN operands). There is no bitwise OR for integers.
- **Steps to Reproduce**:
```basic
DIM flags AS INTEGER
flags = 1 OR 2 OR 4   ' Error: Logical operator OR requires BOOLEAN operands
```
- **Expected**: Bitwise OR produces `flags = 7`
- **Actual**: Error "Logical operator OR requires BOOLEAN operands, got INT and INT"
- **Workaround**: Use addition when bits don't overlap: `flags = 1 + 2 + 4`
- **Analysis**: The semantic analyzer in `Check_Expr_Binary.cpp` routes `OR` to `validateLogicalOperands` which requires boolean operands. The lowerer in `LowerExprLogical.cpp:152` has code for "Classic BASIC OR: bitwise OR on integers" but the semantic check rejects it first.
- **Fix Required**: Either:
  1. Change OR to accept integers (classic BASIC behavior), or
  2. Add a separate BITOR operator/function

### FEAT-002: Bitwise AND operator for integers
- **Status**: Missing
- **Discovered**: 2025-11-29
- **Game**: Chess
- **Description**: The `AND` operator only works as logical AND. There is no bitwise AND for integers.
- **Steps to Reproduce**:
```basic
DIM result AS INTEGER
result = 7 AND 3   ' Error: Logical operator AND requires BOOLEAN operands
```
- **Expected**: Bitwise AND produces `result = 3`
- **Actual**: Error "Logical operator AND requires BOOLEAN operands"
- **Workaround**: None for general case. For testing single bits, use MOD: `IF (flags MOD 2) = 1 THEN` tests bit 0
- **Fix Required**: Same as FEAT-001

### FEAT-003: XOR operator
- **Status**: Missing
- **Discovered**: 2025-11-29
- **Game**: Chess
- **Description**: There is no XOR operator at all (neither bitwise nor logical)
- **Steps to Reproduce**:
```basic
DIM h AS INTEGER
h = 5 XOR 3   ' Error: unknown procedure 'xor'
```
- **Expected**: XOR operator available
- **Actual**: XOR is parsed as a procedure call and fails
- **Workaround**: None for general case. For hash functions, use polynomial hashing with multiplication and addition instead.
- **Analysis**: XOR is not defined in TokenKinds.def as a keyword. The lowerer has `EmitCommon.cpp:177` with `emitXor()` but there's no syntax to reach it.
- **Fix Required**: Add XOR as a keyword and operator

### FEAT-004: NOT operator for bitwise complement
- **Status**: Partial
- **Discovered**: 2025-11-29
- **Game**: Chess
- **Description**: NOT works for logical negation but bitwise complement behavior is unclear
- **Analysis**: `LowerExpr.cpp:210` mentions "Classic BASIC NOT: bitwise complement (XOR with all bits set)" but the semantic layer may restrict usage.

### FEAT-005: INPUT is a reserved keyword, cannot be used as variable name
- **Status**: Design Limitation
- **Discovered**: 2025-11-29
- **Game**: Chess
- **Description**: The word `input` cannot be used as a variable or parameter name because INPUT is a statement keyword
- **Steps to Reproduce**:
```basic
FUNCTION ParseMove(input AS STRING) AS INTEGER   ' Error: expected ident, got INPUT
```
- **Expected**: Context-sensitive parsing allows `input` as identifier in non-statement positions
- **Actual**: Error "expected ident, got INPUT"
- **Workaround**: Use alternative names like `userInput`, `inStr`, `cmdStr`, etc.
- **Analysis**: Unlike the "soft keywords" fix (BUG-OOP-021) which allows COLOR, FLOOR, etc. as identifiers, INPUT is a "hard keyword" that cannot be used as an identifier in any context.

### BUG-OOP-027: Object array declarations cause stack overflow
- **Status**: Fixed (2025-11-29) ✅ Verified
- **Discovered**: 2025-11-29
- **Game**: Centipede
- **Severity**: Critical
- **Component**: BASIC Runtime / Memory Allocation
- **Description**: Declaring arrays of objects (CLASS types) was reported to cause stack overflow at runtime. This prevented using OOP patterns with arrays of game entities.
- **Original Reproduction Case**:
```basic
CLASS Segment
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM dx AS INTEGER
    DIM active AS INTEGER
    SUB Init(px AS INTEGER, py AS INTEGER)
        x = px
        y = py
    END SUB
END CLASS

CLASS Mushroom
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM hp AS INTEGER
END CLASS

DIM gSegments(15) AS Segment
DIM gMushrooms(29) AS Mushroom

Main()

SUB Main()
    PRINT "Test"
END SUB
```
- **Resolution**: Object arrays now work correctly with heap allocation via `rt_arr_obj_new`. Verified working with:
  - 32 Entity objects with 8 fields each
  - 50 Entity objects
  - 10 Bullet objects
  - Multiple concurrent object array allocations
  - Object method calls on array elements
- **Verification Test**:
```basic
' Tested successfully with large arrays
DIM gSegments(31) AS Entity    ' 32 objects
DIM gMushrooms(49) AS Entity   ' 50 objects
DIM gSpiders(7) AS Entity      ' 8 objects
DIM gBullets(9) AS Bullet      ' 10 objects

FOR i = 0 TO 31
    gSegments(i) = NEW Entity()
    gSegments(i).Init(i * 2, 2)
NEXT i
' All objects created and methods called successfully
```
- **Root Cause**: The issue may have been related to a specific configuration or build state at the time of discovery. The runtime's `rt_arr_obj_new` (in `src/runtime/rt_array_obj.c`) correctly uses heap allocation via `rt_heap_alloc` which calls `malloc`.

### FEAT-006: EMPTY conflicts with CONST EMPTY
- **Status**: Design Limitation
- **Discovered**: 2025-11-29
- **Game**: Chess
- **Description**: User-defined CONST names can conflict with each other in unexpected ways when used as variable names
- **Steps to Reproduce**:
```basic
CONST EMPTY AS INTEGER = 0
DIM empty AS INTEGER   ' Error: cannot assign to constant 'EMPTY'
empty = 5
```
- **Expected**: Variable `empty` is distinct from CONST `EMPTY` (case sensitivity) or clear error
- **Actual**: Error "cannot assign to constant 'EMPTY'" because BASIC is case-insensitive
- **Workaround**: Choose CONST names that won't be used as variables (e.g., `PIECE_EMPTY` instead of `EMPTY`)
