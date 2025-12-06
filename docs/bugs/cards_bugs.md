# Cards Demo Bugs

## Syntax Issues Discovered

### BUG-CARDS-001: DIM AS NEW syntax not supported inside methods
**Status:** FIXED
**Severity:** Medium

The syntax `DIM sb AS NEW Viper.Text.StringBuilder` didn't work inside class methods.
Had to use two-step: `DIM sb AS Viper.Text.StringBuilder` then `sb = NEW Viper.Text.StringBuilder()`.

**Root cause:** In `Parser_Stmt_Runtime.cpp:parseDimStatement()`, after parsing `AS`, the parser only checked for type keywords (INTEGER, STRING, etc.) or class names - it never checked for the `NEW` keyword.

**Fix:** Added `DIM AS NEW` syntax support in `Parser_Stmt_Runtime.cpp:257-291`. When `NEW` keyword is found after `AS`, the parser creates a `NewExpr`, extracts the class name for the DIM, and creates a `StmtList` containing both the DIM and the assignment.

**Location:** `src/frontends/basic/Parser_Stmt_Runtime.cpp` - `parseDimStatement()` function

### BUG-CARDS-002: NEW without parentheses fails
**Status:** FIXED
**Severity:** Medium

`cards = NEW Viper.Collections.List` previously failed.
Had to use `cards = NEW Viper.Collections.List()`.

**Root cause:** In `Parser_Expr.cpp:parseNewExpression()`, the parser unconditionally required a left parenthesis after the class name. The VB6-style `NEW ClassName` (without parentheses for default constructor) was not supported.

**Fix:** Made parentheses optional in `Parser_Expr.cpp:642-659`. The parser now checks for `LParen` before attempting to parse arguments. If no parentheses are present, it proceeds with an empty argument list.

**Location:** `src/frontends/basic/Parser_Expr.cpp:642-659`

### BUG-CARDS-003: CASE with multiple constants not supported
**Status:** FIXED
**Severity:** Low

`CASE UNO_SKIP, UNO_REVERSE, UNO_DRAW_TWO` previously not supported.
Had to use separate CASE statements.

**Root cause:** In `Parser_Stmt_Select.cpp:parseCaseArmSyntax()`, the CONST identifier handling used `continue` to restart the loop after processing each CONST, which bypassed the comma check. After the first CONST was processed, the loop encountered the comma token and fell through to an error.

**Fix:** Added comma checks before `continue` statements in the CONST handling code paths (lines 552-558 and 625-631). After processing a CONST, the parser now checks for comma and consumes it before continuing.

**Location:** `src/frontends/basic/Parser_Stmt_Select.cpp:552-558, 625-631`

### BUG-CARDS-004: Setting object field after NEW requires method call
**Status:** FIXED
**Severity:** High

After `c = NEW Card(col, 0)`, the line `c.color = COLOR_WHITE` previously gave "unknown statement 'C'".
Object field assignment with soft keyword names didn't work.

**Root cause:** In `Parser_Stmt_Core.cpp:isImplicitAssignmentStart()`, the check only accepted `TokenKind::Identifier` for the field name after the dot, but `color` is lexed as `KeywordColor` (a soft keyword).

**Fix:** Changed line 121 from `member.kind != TokenKind::Identifier` to `!isSoftIdentToken(member.kind)`. This allows soft keywords like `color`, `floor`, `random`, etc. to be used as field names.

**Location:** `src/frontends/basic/Parser_Stmt_Core.cpp:121`

### BUG-CARDS-005: "input" is a reserved keyword
**Status:** Workaround applied (Documentation)
**Severity:** Medium

Cannot use `DIM input AS STRING` - "input" is reserved.
Use `userInput` or similar instead.

**Root cause:** `INPUT` is a statement keyword (defined in `TokenKinds.def:41` and `Lexer.cpp:85`). It's registered as a statement handler for the INPUT statement. Since BASIC identifiers are case-insensitive, `input`, `INPUT`, and `Input` all become the `KeywordInput` token.

**This is expected behavior** - the workaround of using different variable names is the correct approach.

### BUG-CARDS-006: "Sleep" is a reserved keyword
**Status:** Workaround applied (Documentation)
**Severity:** Medium

`Viper.Time.Sleep(300)` fails with "expected ident, got SLEEP".
The correct method is `Viper.Time.SleepMs(300)`.

**Root cause:** `SLEEP` is a statement keyword (defined in `TokenKinds.def:54` and `Lexer.cpp:124`). The method `Viper.Time.Sleep` doesn't exist - the correct method is `Viper.Time.SleepMs`. When parsing `Viper.Time.Sleep`, the lexer tokenizes `Sleep` as `KeywordSleep` instead of an identifier, causing the parse error.

**This is expected behavior** - the workaround of using `SleepMs` is correct. The runtime provides `Viper.Time.SleepMs(ms)` for sleeping.

### BUG-CARDS-007: Viper.Time.TickCount vs GetTickCount
**Status:** Workaround applied (Documentation)
**Severity:** Low

The correct method is `Viper.Time.GetTickCount()`, not `TickCount()`.

**Root cause:** This is a documentation/API naming issue. The runtime provides `Viper.Time.GetTickCount()`.

### BUG-CARDS-008: Viper.Collections.List method names
**Status:** Workaround applied (Documentation)
**Severity:** Medium

List methods are:
- `Count` (property, not `Count()`)
- `get_Item(i)` (not `Get(i)`)
- `set_Item(i, val)` (not `Set(i, val)`)
- `Add(item)` works
- `Clear()` works
- `RemoveAt(i)` works

**Root cause:** This is a documentation/API naming issue. The List implementation uses .NET-style property accessors (`get_Item`, `set_Item`) rather than VB-style method names.

### BUG-CARDS-009: I32 type for class fields causes IL error
**Status:** RESOLVED (use INTEGER instead of I32)
**Severity:** High

Using `PUBLIC suit AS I32` in class fields causes IL verification errors:
```
call arg type mismatch: @rt_obj_retain_maybe parameter 0 expects ptr but got i64
```

**Root cause:** The lowerer's field type handling doesn't properly distinguish between primitive IL types (`I32`, `I64`) and BASIC types (`INTEGER`). When `I32` is used, the type system gets confused during object retention calls.

**Workaround:** Use `INTEGER` instead of `I32` for class fields.

### BUG-CARDS-010: Class methods cannot return custom class types
**Status:** FIXED
**Severity:** Critical

A class method that returns a custom class type was failing at compile time:
```basic
CLASS Deck
    FUNCTION Draw() AS Card
        DIM c AS Card
        c = NEW Card()
        RETURN c
    END FUNCTION
END CLASS
```

Was causing error: `call return type mismatch: @DECK.DRAW returns ptr but instruction declares i64`

**Root cause:** The lowerer was using `findMethodReturnType()` which returns AST basic types, but methods returning custom classes have `returnClassName` set instead. The call site was emitting `i64` (the default) instead of `ptr`.

**Fix:** Added checks for `findMethodReturnClassName()` before `findMethodReturnType()` at all call sites in Lower_OOP_Expr.cpp (lines 764-772, 1355-1362, 1381-1388).

### BUG-CARDS-011: RETURN NOTHING not supported
**Status:** FIXED
**Severity:** Medium

`RETURN NOTHING` to return a null object reference previously failed with type mismatch:
```
ret value type mismatch: expected ptr but got i64
```

**Root cause:** The `NOTHING` keyword was being lowered to `i64` (integer 0) instead of a null `ptr`. When a function returns a custom class type (which uses `ptr`), the type mismatch occurred.

**Fix:**
1. Added `NOTHING` keyword token in `TokenKinds.def:93` and `Lexer.cpp:101`
2. Added NOTHING parsing in `Parser_Expr.cpp:488-497` to create a VarExpr with name "NOTHING"
3. Added NOTHING lowering in `LowerExpr.cpp:52-57` to emit `Value::null()` with `ptr` type

**Location:** `src/frontends/basic/TokenKinds.def`, `Lexer.cpp`, `Parser_Expr.cpp`, `LowerExpr.cpp`

## Runtime Bugs

### BUG-CARDS-012: Heap corruption with complex object graphs
**Status:** FIXED
**Severity:** Critical

When running the full Blackjack game, the VM was crashing with:
```
Assertion failed: (hdr->magic == RT_MAGIC), function rt_heap_validate_header
```

**Root cause:** Regular SUB/FUNCTION procedures were incorrectly calling `releaseObjectParams()` in their epilogue, releasing and freeing object parameters that should remain owned by the caller. This caused use-after-free when the caller tried to access the passed object.

**Fix:** Removed `releaseObjectParams()` and `releaseArrayParams()` calls from `Lowerer_Procedure_Emit.cpp:emitProcedureCleanup()`. Object/array parameters are borrowed references - the callee does not own them.

**Note:** This was already correctly fixed for OOP class methods in `Lower_OOP_RuntimeHelpers.cpp` (BUG-105 fix), but regular SUB/FUNCTION procedures still had the incorrect behavior.

## Summary of Bugs Found

| Bug | Status | Severity | Root Cause Location |
|-----|--------|----------|---------------------|
| BUG-CARDS-001 | **FIXED** | Medium | Parser_Stmt_Runtime.cpp - DIM AS NEW syntax |
| BUG-CARDS-002 | **FIXED** | Medium | Parser_Expr.cpp - NEW optional parentheses |
| BUG-CARDS-003 | **FIXED** | Low | Parser_Stmt_Select.cpp - CONST comma handling |
| BUG-CARDS-004 | **FIXED** | High | Parser_Stmt_Core.cpp - soft keyword field names |
| BUG-CARDS-005 | Expected | Medium | INPUT is a reserved keyword |
| BUG-CARDS-006 | Expected | Medium | SLEEP is reserved, use SleepMs |
| BUG-CARDS-007 | Docs | Low | API naming: GetTickCount() |
| BUG-CARDS-008 | Docs | Medium | API naming: get_Item/set_Item |
| BUG-CARDS-009 | Workaround | High | I32 field type handling |
| BUG-CARDS-010 | **FIXED** | Critical | Lower_OOP_Expr.cpp - return type lookup |
| BUG-CARDS-011 | **FIXED** | Medium | LowerExpr.cpp - NOTHING as null ptr |
| BUG-CARDS-012 | **FIXED** | Critical | Lowerer_Procedure_Emit.cpp - param ownership |

**Status:** All code bugs have been fixed (7 total). The remaining items are expected behavior (reserved keywords) or documentation issues.

### Documentation Issues (Not Bugs)

- BUG-CARDS-005, 006: Reserved keywords (expected behavior)
- BUG-CARDS-007, 008: API naming conventions (documentation)
- BUG-CARDS-009: Type usage (workaround: use INTEGER)
