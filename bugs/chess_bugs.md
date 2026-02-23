# Chess Game — Viper/Zia Bug Log

Bugs and workarounds discovered while building `demos/zia/chess/`.

Each entry records: symptom, root cause, workaround/fix applied, and fix location.

---

## Open Bugs

*(none currently)*

---

## Fixed in Compiler

These bugs were fixed in the Zia compiler or AArch64 codegen. The chess demo's workarounds
remain in place (removing them is optional since the original code still compiles correctly),
but the patterns are now valid Zia.

---

### BUG-NAT-007 — AArch64 regalloc: `AddFpImm` treated as USE instead of DEF

**Symptom:**
```
EXC_BAD_ACCESS (SIGSEGV) — write to address 0x0000000000000019 (= 25)
```
Crash triggered when clicking a chess piece (first call into `MoveGen.legalMoves` that
copies a `Move` value type to the stack under register pressure).

**Root cause:**
`RegAllocLinear::operandRoles` had no case for `MOpcode::AddFpImm` and fell through to
the default `{true, false}` (USE-only). `AddFpImm` should be `{false, true}` (DEF-only):
it computes `fp + immediate → dest_vreg`; the dest operand is written, not read.

Because `isDef` was `false`, the `dirty` flag was never set after `AddFpImm` executed.
When register pressure caused the allocator to evict the result vreg as a spill victim
(it had `UINT_MAX` next-use-distance — no future uses past the immediately following
`AddRI`), `spillVictim` saw `dirty=false` and skipped the store to the spill slot,
leaving it uninitialised. The subsequent `handleSpilledOperand` reload then read garbage
from that uninitialised slot, producing a junk alloca base (e.g. `x25 = 1`). Adding the
field offset (`+24`) gave address `25 = 0x19` → crash.

The bug was silent for fields 0–2 (offsets 0, 8, 16) because register pressure hadn't
yet exhausted all GPRs by the time those GEPs were processed. Field 3 (offset 24) was
consistently the first victim.

Note: `Peephole.cpp::operandRoles` already correctly handled `AddFpImm` as `{false, true}`;
the two independent operand-role tables were out of sync.

**Fix applied (codegen):**
Added explicit case in `RegAllocLinear::operandRoles`:
```cpp
if (ins.opc == MOpcode::AddFpImm)
    return {false, idx == 0}; // operand 0 is def-only
```
This marks the result dirty → eviction emits the spill store → reload is correct.

**Test:** `Arm64Bugfix.AddFpImmDirtyFlagUnderPressure` in
`src/tests/unit/codegen/test_codegen_arm64_bugfix.cpp`

**Files changed:** `src/codegen/aarch64/RegAllocLinear.cpp`

---

---

### ZIA-007 — `Int()` not available in `Viper.Terminal`

**Symptom:**
```
main.zia:19:29: error[V3000]: Undefined identifier: Int
```
Triggered when trying to use `Int(x)` for integer-to-string conversion without a
`bind Viper.Fmt;` import.

**Root cause:**
`Viper.Terminal` did not include the `Int(i64) -> str` method. Only `Viper.Fmt` had it
(as `FmtInt`). The function already existed in the C runtime as `rt_fmt_int`; it was
just missing from the `Terminal` class binding.

**Fix applied (compiler):**
Added `RT_METHOD("Int", "str(i64)", FmtInt)` to the `Viper.Terminal` class block in
`src/il/runtime/runtime.def`.

**Workaround in chess demo:**
Added `bind Viper.Fmt;` to `main.zia` and used `Int(x)` via the `Fmt` namespace.
Both the workaround and the direct `Int()` call now work.

**Files changed:** `src/il/runtime/runtime.def`

---

### ZIA-006 — Empty list `[]` loses element type; breaks arithmetic on retrieved elements

**Symptom:**
```
moves.zia:293:26: error[V3000]: Invalid operands for arithmetic operation
```
Triggered when:
```rust
var items: List[Integer] = [];
items.add(someInt);
var x = items.get(0) + 1;   // ERROR: items.get() returns Unknown type
```

**Root cause:**
`Sema_Expr_Advanced.cpp::analyzeListLiteral()` initialized `elementType = types::unknown()`
and never updated it when the element list was empty. When the declared type annotation
`List[Integer]` was available, it was resolved in `Sema_Stmt.cpp::analyzeVarStmt()` but
not communicated to the list-literal analyzer. So the empty literal `[]` was inferred as
`List[Unknown]`, and subsequent `.get(i)` calls returned `Unknown`, causing type errors
in arithmetic.

**Fix applied (compiler):**
Three-file fix:

1. **`src/frontends/zia/Sema.hpp`**: Added `TypeRef expectedListElementType_` member
   (reset to `nullptr` between statements).
2. **`src/frontends/zia/Sema_Stmt.cpp::analyzeVarStmt()`**: Before analyzing the
   initializer expression, if the declared type is `List[T]`, set
   `expectedListElementType_ = T`. Clear after the call.
3. **`src/frontends/zia/Sema_Expr_Advanced.cpp::analyzeListLiteral()`**: For empty
   lists, check `expectedListElementType_`. If set, use it as the inferred element type
   instead of `Unknown`.

**Workaround in chess demo:**
Replaced dynamically-built direction lists (built with `[]` + `.add()`) with:
- Explicit static list literals: `var x = [-1, 0, 1]` (type inferred from literal).
- Per-direction helper methods taking individual `Integer` parameters instead of lists.

**Files changed:**
`src/frontends/zia/Sema.hpp`,
`src/frontends/zia/Sema_Stmt.cpp`,
`src/frontends/zia/Sema_Expr_Advanced.cpp`

---

### ZIA-004 — `if` is statement-only, not an expression

**Symptom:**
```
board.zia:268:29: error[V3000]: expected expression
```
Triggered by:
```rust
var promPiece = if isWhite { move.promotePiece } else { -move.promotePiece };
```

**Root cause:**
`Parser_Expr.cpp::parsePrimary()` had no case for `TokenKind::KwIf`. The `IfExpr` AST
node already existed in `AST_Expr.hpp` and `Sema_TypeResolution.cpp` already handled
`ExprKind::If`, but the parser never produced an `IfExpr` node from `if cond { a } else { b }`
syntax, and `Lowerer_Expr.cpp` had no `ExprKind::If` lowering case.

**Fix applied (compiler):**
Two-file fix:

1. **`src/frontends/zia/Parser_Expr.cpp::parsePrimary()`**: Added case for `KwIf` at
   the top of the function. Parses `if condition { thenExpr } else { elseExpr }` and
   returns an `IfExpr` node.
2. **`src/frontends/zia/Lowerer_Expr.cpp`**: Added `ExprKind::If` case delegating to
   new `lowerIfExpr()`. Implementation uses the same alloca-based pattern as `lowerTernary()`:
   alloca a result slot → CBr on condition → then-block stores then-expr → else-block
   stores else-expr → merge-block loads and returns result.

**Workaround in chess demo:**
All `var x = if cond { a } else { b }` patterns converted to two-statement form:
```rust
var x = a;
if !cond { x = b; }
```

**Files changed:**
`src/frontends/zia/Parser_Expr.cpp`,
`src/frontends/zia/Lowerer_Expr.cpp`

---

### ZIA-003 — Negative range literals in `for x in -1..2` — NOT A BUG

**Symptom (reported):**
Potentially incorrect behavior when using negative start values in range loops.

**Investigation result:**
The Zia parser's `parseRange()` function sits above `parseUnary()` in the precedence
chain, so unary minus correctly binds tighter than `..`. A test was added:

```rust
// ZIA003_NegativeRangeForLoop in test_zia_bugfixes.cpp
var sum = 0;
for x in -1..2 { sum = sum + x; }
// Expected: -1 + 0 + 1 = 0
```

The test **passed on first run** — no fix was needed. This was a pre-emptive workaround
applied before the syntax was tested.

**Status:** Reclassified as "Not a Bug." Negative-start ranges work correctly.

**Workaround in chess demo (still in place, not reverted):**
Direction arrays (`var kDR = [-1, -1, ...]`) remain since they work fine and are explicit.

---

### ZIA-001 — No struct-literal initialization for `value` types

**Symptom:**
```
board.zia:211:29: error[V2000]: expected ;, got {
```
Triggered by:
```rust
var undo = UndoInfo { enPassantFile = self.enPassantFile, ... };
```

**Root cause:**
`Parser_Expr.cpp::parsePrimary()` had no case for `TypeName { field = val, ... }` syntax.
The grammar rule for struct-literal initialization (Rust-style named-field syntax) was
simply not implemented. The `StructLiteralExpr` AST node and supporting sema/lowerer
infrastructure were all added as part of the fix.

**Fix applied (compiler):**
Five-file fix:

1. **`src/frontends/zia/AST_Expr.hpp`**: Added `StructLiteralExpr` AST node class and
   `ExprKind::StructLiteral` to the kind enum.
2. **`src/frontends/zia/Parser_Expr.cpp::parsePrimary()`**: Struct literal detection is
   gated behind an `allowStructLiterals_` flag (to avoid ambiguity with `for`/`if`/`while`
   block bodies). When set, after parsing an identifier, checks if next token is `{` followed
   by `Identifier =` (named-field pattern). If so, parses the full struct literal.
3. **`src/frontends/zia/Parser_Stmt.cpp`**: Sets `allowStructLiterals_ = true` around
   the initializer expression in `parseVarDecl()` and the return value in `parseReturnStmt()`.
4. **`src/frontends/zia/Sema_Expr_Advanced.cpp`**: Added `analyzeStructLiteral()` —
   looks up the type name, verifies fields exist and types match.
5. **`src/frontends/zia/Lowerer_Expr.cpp`**: Added `ExprKind::StructLiteral` case
   delegating to new `lowerStructLiteral()` — reorders fields to match declaration order
   and calls the value type's constructor.

**Key disambiguation:** `TypeName { field = expr }` is a struct literal only when
`allowStructLiterals_` is `true` (i.e., in `var` initializer or `return` value position).
In all other expression positions (for-loop iterables, if conditions, etc.), `{` after an
identifier starts a block statement, not a literal.

**Workaround in chess demo (still in place):**
Added `expose func init(...)` constructors to each value type (`Move`, `UndoInfo`), then
used `new TypeName(arg1, arg2, ...)` calls.

**Files changed:**
`src/frontends/zia/AST_Expr.hpp`,
`src/frontends/zia/Parser_Expr.cpp`,
`src/frontends/zia/Parser_Stmt.cpp`,
`src/frontends/zia/Sema_Expr_Advanced.cpp`,
`src/frontends/zia/Lowerer_Expr.cpp`

---

### ZIA-002 — Missing: fixed-size integer arrays as entity fields

**Symptom:**
Cannot declare `expose Integer[64] squares;` inside an `entity` body.

**Root cause:**
Five separate gaps in the Zia compiler:
1. **Parser** (`Parser_Type.cpp`): No parsing of `T[N]` syntax — `[N]` after a type name
   was not recognized as a fixed-size array.
2. **Type system** (`Types.hpp`): No `TypeKindSem::FixedArray` variant, no `elementCount`
   storage, no `types::fixedArray(elementType, count)` factory.
3. **Sema** (`Sema_TypeResolution.cpp`): No resolution path for `FixedArray` type nodes.
4. **Entity layout** (`Lowerer_Decl.cpp`): No size computation for array-typed fields —
   `fieldSize = elementSize * elementCount` inline storage was missing.
5. **`elementType()` method** (`Types.hpp`): Only handled `List` and `Set` kinds, so
   `analyzeIndex()` on a `FixedArray` returned `nullptr` → `types::unknown()`, causing
   type-mismatch errors on subscript access (this was the last bug found during testing).

**Fix applied (compiler):**
Five-file fix:

1. **`src/frontends/zia/Types.hpp`**: Added `TypeKindSem::FixedArray`, added
   `elementCount` field to `ViperType`, added `types::fixedArray(elemTy, count)` factory,
   extended `elementType()` to include `FixedArray` in its condition.
2. **`src/frontends/zia/Parser_Type.cpp::parseBaseType()`**: After parsing a named type,
   checks for `[` followed by an integer literal (not a type name — that's `List[T]`
   generic syntax). If matched, consumes `[N]` and returns a `FixedArrayTypeExpr`.
3. **`src/frontends/zia/Sema_TypeResolution.cpp`**: Added `FixedArrayType` resolution —
   resolves the element type and returns `types::fixedArray(elemTy, count)`.
4. **`src/frontends/zia/Lowerer_Decl.cpp::getOrCreateEntityTypeInfo()`**: Added
   `FixedArray` case in field-size computation: `fieldSize = elementSize * elementCount`.
   Zero-initializes array elements in the entity constructor via a loop emitting
   `gep + store 0` for each element.
5. **`src/frontends/zia/Sema_Expr_Advanced.cpp::analyzeIndex()`**: Added `FixedArray`
   case — validates integer index, emits `IdxChk`, returns `elementType()`.
6. **`src/frontends/zia/Lowerer_Expr.cpp`** and **`Lowerer_Stmt.cpp`**: Added GEP-based
   subscript access (read: `gep + load`, write: `gep + store`) for `FixedArray` entity
   fields.

**Workaround in chess demo (still in place):**
Used `hide List[Integer] sq;` initialized with 64 elements at startup, accessed via
`expose func get(i)` / `expose func set(i, v)` methods. The workaround performs correctly.

**Files changed:**
`src/frontends/zia/Types.hpp`,
`src/frontends/zia/Parser_Type.cpp`,
`src/frontends/zia/Sema_TypeResolution.cpp`,
`src/frontends/zia/Lowerer_Decl.cpp`,
`src/frontends/zia/Sema_Expr_Advanced.cpp`,
`src/frontends/zia/Lowerer_Expr.cpp`,
`src/frontends/zia/Lowerer_Stmt.cpp`

---

### ZIA-005 — `const` keyword not recognized; use `final`

**Symptom:**
```
config.zia:7:1: error[V3000]: Unknown type: const
```
Triggered by module-level constant declarations using C/Java/Rust syntax.

**Status:** Language Design — NOT A BUG

`const` is not part of the Zia language. Zia uses `final` for module-level constants.
This is an intentional language design choice. No compiler change is planned.

**Workaround in chess demo:** Replaced `const NAME = VALUE;` with `final NAME = VALUE;`
throughout `config.zia`.

---

## Notes

- See `bugs/chess_plan.md` for the full game design document.
- See `demos/zia/sqldb/PLATFORM_BUGS.md` for prior platform bug patterns.
- Phase 1 gate **PASSED**: `perft(startPos, 4) == 197281` ✓
- All 6 compiler fixes have ctests in `src/tests/zia/test_zia_bugfixes.cpp`.
