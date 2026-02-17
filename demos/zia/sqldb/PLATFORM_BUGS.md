# Viper Platform Bugs Found During ViperSQL Development

Bugs in the Viper compiler/runtime/codegen discovered while building ViperSQL.
These need to be fixed in the platform itself.

---

## BUG-NAT-001: AArch64 register allocator double-increment of instruction counter

- **Status**: FIXED
- **Severity**: High
- **Component**: Native codegen (AArch64) — `RegAllocLinear.cpp`
- **Symptom**: Native-compiled binary produces wrong arithmetic results or hangs with OOM (68 GB+ memory) when a function has enough local variables to cause register spilling. The spill victim selection uses "furthest next use" heuristic, but the instruction counter was incremented twice per instruction (once in `allocateInstruction()`, once in the calling loop), desynchronizing it from the use-position map built by `computeNextUses()`. This caused `getNextUseDistance()` to return UINT_MAX for vregs that DO have future uses, leading to:
  1. Wrong victim selection (evicting a still-needed vreg)
  2. The evicted vreg's register being reassigned without spilling
  3. Corrupted computation results
  4. In the ViperSQL case: corrupted serialized data causing `BinaryBuffer.readString()` to read a huge string length → infinite allocation loop
- **Root cause**: `RegAllocLinear.cpp` line 1238 had `++currentInstrIdx_` inside `allocateInstruction()`, but the caller `allocateBlock()` also incremented at line 1081. Regular instructions got double-incremented while calls/branches got single-incremented.
- **Fix**: Removed the duplicate `++currentInstrIdx_` from `allocateInstruction()` (one-line change).
- **Verification**: All 1149 existing tests pass. StorageEngine test suite (54 tests) passes in native. Arithmetic test with 25+ locals produces correct results.
- **Found**: 2026-02-13
- **Fixed**: 2026-02-14

---

## BUG-NAT-002: AArch64 peephole copy propagation corrupts ABI registers and cross-class moves

- **Status**: FIXED
- **Severity**: High
- **Component**: Native codegen (AArch64) — `Peephole.cpp`, `FastPaths_Arithmetic.cpp`
- **Symptom**: Native-compiled demos produce wrong results or crash. Three separate issues:
  1. **ABI register replacement**: Copy propagation followed MovRR chains into ABI registers (x0-x7, v0-v7), replacing the ABI operand with the chain origin. This created dead moves that DCE cannot eliminate, causing wrong values to reach call arguments.
  2. **Cross-class FMovRR**: The FMovRR copy propagation handler treated GPR→FPR bit-cast transfers (`fmov dN, xM`) as same-class copies, substituting GPR operands into FPR instructions (e.g., `fmul d18, d16, x23`).
  3. **RR fastpath normalization**: The arithmetic fastpath unconditionally moved params to x0/x1 via scratch register even when already in position (`src0==X0 && src1==X1`), wasting a register and causing incorrect results.
- **Root cause**: Peephole optimizer lacked guards for ABI register boundaries and register class boundaries in copy chain following.
- **Fix**: (1) Guard MovRR chain following and instruction operand replacement to skip ABI registers. (2) Restrict FMovRR handler to same-register-class copies only. (3) Skip RR fastpath normalization when params already in position.
- **Verification**: All existing tests pass. All 8 demo binaries rebuilt successfully.
- **Found**: 2026-02-12
- **Fixed**: 2026-02-12

---

## BUG-NAT-003: ARM64 native crash — rt_pow_f64_chkdom argument count mismatch

- **Status**: FIXED
- **Severity**: Critical
- **Component**: Native codegen + Runtime — `RuntimeSignatures.cpp`, `rt_fp.c`
- **Symptom**: Native-compiled code crashes when calling `Viper.Math.Pow()` or BASIC `^` operator on ARM64. The runtime function `rt_pow_f64_chkdom` takes 3 arguments `(base, exp, bool *ok)` but native codegen emitted a call with only 2 arguments, leaving the `ok` pointer as null/garbage from whatever was in x2.
- **Root cause**: The runtime signature for pow was registered as a 2-argument function, but the C implementation expected 3. No domain-check-free variant existed.
- **Fix**: Created two new 2-argument wrapper functions: `rt_math_pow` (simple `pow()` without domain check, used for `Viper.Math.Pow`) and `rt_pow_f64` (domain-checking wrapper for BASIC `^` operator). Updated runtime signatures to match.
- **Verification**: All tests pass. Math demos work correctly in both VM and native.
- **Found**: 2026-02-12
- **Fixed**: 2026-02-12

---

## BUG-NAT-004: AArch64 missing string retain on alloca stores causes use-after-free

- **Status**: FIXED
- **Severity**: High
- **Component**: Native codegen (AArch64) — `OpcodeDispatch.cpp`
- **Symptom**: Native-compiled ViperSQL binary crashes (SIGBUS/SIGSEGV) during string-heavy operations like INSERT after DELETE, or serialization round-trips. String values stored to alloca slots were not retained, so consuming runtime functions like `rt_str_concat` (which unrefs both inputs) could drop the refcount to zero prematurely, causing use-after-free.
- **Root cause**: The AArch64 instruction lowering for `StoreSlot` handled integer and float alloca stores but had no path for string stores. The VM's `storeSlotToPtr` always does retain+release for Str stores, but the native codegen just did a plain register store.
- **Fix**: Added a string-type branch in `OpcodeDispatch.cpp` that emits `rt_str_retain_maybe` before storing the string handle to the alloca slot. Release of the old value is intentionally omitted (matching VM behavior where alloca retains are "leaked" on function exit since alloca slots aren't zero-initialized).
- **Verification**: All existing tests pass. ViperSQL stress tests (INSERT/DELETE cycles, serialization) pass in native.
- **Found**: 2026-02-13
- **Fixed**: 2026-02-13

---

## BUG-FE-001: Zia module-qualified call regression — user functions resolved as runtime functions

- **Status**: FIXED
- **Severity**: High
- **Component**: Zia frontend — `Lowerer_Expr_Call.cpp`
- **Symptom**: Zia programs with module-qualified function calls (e.g., `grid.gridToScreenX()`) crash or produce wrong results. The lowerer was using the fully-qualified name (e.g., `Viper.Result.Ok`) for all dotted calls, but user-defined module functions should use the plain field name (e.g., `gridToScreenX`).
- **Root cause**: The call lowering logic did not distinguish between runtime functions (which need qualified names like `Viper.Result.Ok`) and user-defined module functions (which need plain names). After runtime API fixes added more `findRuntimeDescriptor` matches, previously-working user module calls started resolving as (wrong) runtime functions.
- **Fix**: Added `findRuntimeDescriptor` check — if the qualified name matches a runtime function, use the qualified name; otherwise use the plain field name for user-defined module functions.
- **Verification**: All tests pass. ViperSQL modules with cross-module calls work correctly.
- **Found**: 2026-02-12
- **Fixed**: 2026-02-12

---

## BUG-RT-001: 19 runtime class constructors use malloc instead of rt_obj_new_i64

- **Status**: FIXED
- **Severity**: Critical
- **Component**: Runtime (C) — multiple `rt_*.c` files
- **Symptom**: BASIC programs crash with RT_MAGIC assertion failures when constructing objects of 19 different runtime classes (Deque, SortedSet, Promise, Timer, Tween, etc.). The VM's garbage collector expects all heap objects to have RT_MAGIC headers, but these constructors used raw `malloc()`.
- **Root cause**: C constructor functions for these classes allocated memory with `malloc()` instead of `rt_obj_new_i64()`, which sets up the RT_MAGIC header, reference counting, and GC tracking. Without the header, any subsequent method call or GC pass would fail the magic number check.
- **Fix**: Converted all 19 constructors from `malloc()` to `rt_obj_new_i64()`. Affected files: rt_result.c, rt_option.c, rt_lazy.c, rt_scanner.c, rt_compiled_pattern.c, rt_dateonly.c, rt_lazyseq.c, rt_gui_app.c, rt_gui_features.c, rt_gui_codeeditor.c, rt_particle.c, rt_inputmgr.c, rt_spriteanim.c, rt_scene.c, rt_playlist.c, rt_restclient.c, rt_spritebatch.c, rt_tls.c, rt_gc.c, and others.
- **Verification**: All 1149 tests pass. All runtime class demos work correctly in BASIC VM.
- **Found**: 2026-02-12
- **Fixed**: 2026-02-12

---

## BUG-RT-002: Regex engine missing backtracking in concat handler

- **Status**: FIXED
- **Severity**: High
- **Component**: Runtime (C) — regex engine
- **Symptom**: Pattern matching with `Viper.Text.Pattern` produces incorrect results for patterns where the concat handler needs to backtrack. Simple patterns work but complex multi-part patterns fail to match valid strings.
- **Root cause**: The regex concat handler attempted a single match position and did not backtrack on failure.
- **Fix**: Added backtracking logic to the regex engine's concat handler.
- **Verification**: Pattern demo programs produce correct results in both Zia and BASIC.
- **Found**: 2026-02-12
- **Fixed**: 2026-02-12

---

## BUG-RT-003: Multiple C runtime function bugs (glob, JSON, JsonPath, rt_obj_to_string)

- **Status**: FIXED
- **Severity**: Medium
- **Component**: Runtime (C) — various `rt_*.c` files
- **Symptom**: Several runtime APIs produce wrong results or crash:
  - `Viper.IO.Glob` had swapped argument order
  - JSON validator rejected valid inputs
  - JsonPath returned unboxed raw values instead of proper objects
  - `rt_obj_to_string` returned "Object" for string handles and boxed values instead of the actual content
- **Root cause**: Individual implementation bugs in each C function.
- **Fix**: Fixed argument order in glob, corrected JSON validation logic, added proper unboxing in JsonPath, and made `rt_obj_to_string` auto-detect string handles and boxed values.
- **Verification**: All demos and tests pass.
- **Found**: 2026-02-12
- **Fixed**: 2026-02-12

---

## BUG-FE-002: Zia `new` rejects non-`.New` constructors (Open, Create, Parse, FromSeq, Today)

- **Status**: FIXED
- **Severity**: Medium
- **Component**: Zia frontend — `Sema.cpp`
- **Symptom**: Zia programs using `new FrozenSet(...)`, `new Stream(...)`, `new DateOnly(...)`, etc. fail with "can only be used with value, entity, or collection types". Any runtime class whose constructor is not named exactly `*New` is rejected.
- **Root cause**: `analyzeNew()` and `lowerNew()` hardcoded the `.New` suffix for constructor lookup. Constructors named `*Open`, `*Create`, `*Parse`, `*FromSeq`, `*Today` were not found.
- **Fix**: Changed `analyzeNew()`/`lowerNew()` to use actual constructor names from the RuntimeClass catalog instead of the hardcoded `.New` suffix.
- **Verification**: All affected classes (FrozenSet, FrozenMap, Stream, DateOnly, etc.) can be constructed in Zia.
- **Found**: 2026-02-12
- **Fixed**: 2026-02-12

---

## BUG-FE-003: Zia bind/static call failures for newer runtime functions

- **Status**: FIXED
- **Severity**: High
- **Component**: Zia frontend — `Sema_Decl.cpp`, `Sema_Expr_Call.cpp`
- **Symptom**: `bind Viper.Core.Box`, `bind Viper.Core.Parse`, `bind Viper.String`, etc. fail to resolve functions — all calls report "undefined function". Deeply-qualified static calls like `Viper.X.Y.Z()` also fail.
- **Root cause**: `importNamespaceSymbols()` in `Sema_Decl.cpp` didn't find newer RT_FUNC entries in the RuntimeRegistry catalog. `lookupSymbol()` in `Sema_Expr_Call.cpp` failed for deeply-nested or newer qualified names.
- **Fix**: Extended `importNamespaceSymbols()` and `lookupSymbol()` to search the full RuntimeRegistry catalog, including newer function entries and deeply-qualified names.
- **Verification**: All bind-based API access works. Runtime API audit demos pass.
- **Found**: 2026-02-12
- **Fixed**: 2026-02-12

---

## BUG-FE-004: BASIC type tracking loses class type for method return values

- **Status**: FIXED
- **Severity**: High
- **Component**: BASIC frontend — `SemanticAnalyzer.cpp`
- **Symptom**: BASIC programs accessing Vec2/Vec3/List/Map method return values get "unknown method" errors on chained calls. For example, `v = Vec2.Zero()` followed by `v.X` fails because `v` is typed as generic `obj` instead of `Vec2`.
- **Root cause**: `findMethodReturnClassName` didn't check the runtime class catalog. `lowerLet` didn't distinguish between authoritative type sources (NEW/constructor) and inferred types. Collection getters (`Get()`, etc.) polluted the type tracker with container types instead of element types.
- **Fix**: (1) `findMethodReturnClassName` now checks runtime class catalog. (2) `lowerLet` distinguishes authoritative vs inferred types. (3) Collection heuristic prevents container getter type pollution.
- **Verification**: Vec2/Vec3 chained calls work. Collection access preserves element types.
- **Found**: 2026-02-12
- **Fixed**: 2026-02-12

---

## BUG-FE-005: Zia compiler emits bad IL for complex functions with many locals

- **Status**: CANNOT REPRODUCE
- **Severity**: Medium
- **Component**: Zia frontend — IL code generation
- **Symptom**: Functions with many local variables and control flow (e.g., WAL `recover()` implementation with 15+ locals across nested while/if) fail IL verification with `operand type mismatch: pointer type mismatch: operand 0 must be ptr` on `load` instructions. The generated IL has incorrect pointer types for some stack slots.
- **Root cause**: Could not reproduce with standalone test cases. Functions with 17+ locals and complex nested while/if control flow compile and run correctly. The original failure may have been caused by interaction with BUG-FE-007 (calling non-existent entity methods), which has been fixed.
- **Regression test**: `test_zia_bugfixes.cpp:BugFE005_ManyLocalsComplexControlFlow`
- **Found**: 2026-02-13
- **Closed**: 2026-02-14

---

## BUG-FE-006: Zia compiler generates wrong IL for List.add() through entity field chains

- **Status**: FIXED
- **Severity**: Medium
- **Component**: Zia frontend — call lowering, declaration lowering
- **Symptom**: Calling `.add()` on a List accessed through an entity field chain (e.g., `result.committedTxns.add(value)`) fails with IL verification error: `call arg type mismatch: @Viper.Collections.List.Push parameter 0 expects ptr but got i64`. Direct List locals and simple entity field Lists work fine.
- **Root cause**: Entity declaration order in the lowerer's single-pass design. When entity A's methods reference entity B (declared later in the same file), `getOrCreateEntityTypeInfo("B")` returns `nullptr` because B hasn't been lowered yet. The `lowerField()` function falls through to return `{Value::constInt(0), Type(I64)}` — producing wrong IL types for non-integer fields (List, Entity, Ptr all got `i64` instead of `ptr`).
- **Fix**: Two-pass entity/value type registration. Added a pre-pass (`registerAllTypeLayouts()`) in `Lowerer::lower()` that registers all entity and value type layouts (field offsets, sizes, types) BEFORE any method bodies are lowered. Modified `lowerEntityDecl()`/`lowerValueDecl()` to skip layout computation for already-registered types. Also improved the `lowerField()` fallthrough to use sema-informed types as a safety net instead of hardcoded `I64`.
- **Verification**: All 1150 tests pass. Forward-reference entity field chain tests added.
- **Regression test**: `test_zia_bugfixes.cpp:BugFE006_EntityFieldChainListAdd_ForwardRef`, `BugFE006_EntityFieldChainListAdd_NormalOrder`, `BugFE006_EntityFieldChainMultipleCollections`, `BugFE006_EntityFieldChainEntityField_ForwardRef`
- **Found**: 2026-02-13
- **Fixed**: 2026-02-14

---

## BUG-FE-007: Zia entity field method dispatch produces null indirect callee

- **Status**: FIXED
- **Severity**: High
- **Component**: Zia frontend — `Lowerer_Expr_Call.cpp`
- **Symptom**: Calling a method on an entity field (e.g., `wal.enable()` where `wal` is a `WALManager` field) crashes at runtime with "Null indirect callee". The entity object is valid (fields are accessible), but method calls on it fail.
- **Root cause**: The original code was calling a method that did not exist on the entity type (e.g., `enable()` when the actual method was `initWithDir()`). The Zia lowerer's `lowerCall()` found the entity type via `getOrCreateEntityTypeInfo()` but when `findMethod()` and `findVtableSlot()` both returned null, it fell through to the generic indirect call path. This path called `lowerField()` on the non-existent method name, which returned `{Value::constInt(0), Type(I64)}` — a null function pointer. The generated `call.indirect 0` then crashed at runtime.
- **Fix**: Added proper error reporting to the lowerer. When an entity type is found but the method does not exist (and is not inherited from any parent), the lowerer now emits a compile-time error `"Entity type 'X' has no method 'Y'"` (code V3100) instead of silently generating a null indirect call. Also added a `DiagnosticEngine` reference to the Lowerer class to support proper error reporting.
- **Verification**: (1) Non-existent entity method calls now produce clear compile errors. (2) Valid entity field method dispatch works correctly (e.g., `engine.wal.isEnabled()`, `engine.wal.initWithDir(path)`). (3) All 1149 tests pass.
- **Regression test**: `test_zia_bugfixes.cpp:BugFE007_NonExistentEntityMethodError`, `test_zia_bugfixes.cpp:BugFE007_ValidEntityFieldMethodDispatch`
- **Found**: 2026-02-13
- **Fixed**: 2026-02-14

---

## BUG-VM-001: VM SIGSEGV on heavy entity allocation in long-running programs

- **Status**: HARDENED (defensive fixes applied)
- **Severity**: High
- **Component**: VM interpreter — memory management, runtime (C)
- **Symptom**: Programs that create many entity objects (e.g., multiple Executor/StorageEngine instances across test functions) crash with SIGSEGV after a threshold of cumulative allocations. The crash point is non-deterministic but correlates with total allocation pressure — e.g., 3 rounds of 50-row INSERT+DELETE tests work, but adding a 4th round with 100 rows crashes.
- **Root cause**: Multiple contributing factors: (1) GC tracking table silently fails when `realloc()` fails to grow the table, causing untracked objects and potential dangling references. (2) `rt_obj_new_i64()` returned NULL on allocation failure without any diagnostic, causing silent null-pointer dereferences downstream. (3) Entity reference counting may not properly release entity references when VM frames are torn down, leading to refcount leaks and eventual address space exhaustion.
- **Fix**: Defensive hardening: (1) `rt_gc_track()` now calls `rt_trap()` on realloc failure instead of silently returning — a failed GC track is a critical error that should not be ignored. (2) `rt_obj_new_i64()` now calls `rt_trap()` with a diagnostic message on allocation failure instead of returning NULL. These changes make memory management failures immediately visible rather than causing non-deterministic crashes later.
- **Reproduction**: Run `test_storage_stress.zia` with original 100-row DELETE tests after the multi-database test — previously crashed during the 3rd or 4th test function.
- **Workaround**: Build as native binary (`viper build`) which has a different memory management path and does not exhibit this limitation.
- **Regression test**: `EntityStressTests.cpp` (5 stress tests: many entities, List fields, chained entities, interleaved types, forward-ref field chains)
- **Found**: 2026-02-14
- **Hardened**: 2026-02-14

---

## BUG-STORAGE-003: rewriteTableRows() did not rebuild row location tracking

- **Status**: FIXED
- **Severity**: High
- **Component**: ViperSQL storage layer — `engine.zia`, `executor.zia`
- **Symptom**: After a DELETE that falls back to full table rewrite, subsequent operations on the same table in the same session could crash or produce wrong results. Additionally, the executor's DELETE handler called `clearRowLocations()` unconditionally before deciding whether to rewrite, leaving tracking empty. INSERT operations after DELETE worked but had stale tracking state.
- **Root cause**: `rewriteTableRows()` re-inserted all surviving rows into fresh data pages but never called `trackRowLocation()` for the new page/slot positions. Additionally, the DELETE incremental path had a fundamental design flaw: after compacting the in-memory row list, row indices shifted but tracking entries were not updated, making `findRowLocation()` return wrong pages/slots.
- **Fix**: (1) `rewriteTableRows()` now calls `clearRowLocations(tableName)` at the start and `trackRowLocation()` after each row insert, making it self-contained and correct. (2) The executor DELETE path was simplified to always compact in-memory first, then call `rewriteTableRows()` (which rebuilds tracking). The broken incremental DELETE path was removed. (3) The executor UPDATE rewrite fallback no longer calls a redundant `clearRowLocations()` since `rewriteTableRows()` handles it internally.
- **Verification**: All 131 Zia tests pass (54 engine + 26 persistence + 17 phase3 + 34 stress3). All 18 native-scale stress tests pass (including 1000-row DELETE, 200-row UPDATE, 50 open/close cycles, 100-row DELETE + 150-row re-INSERT). All 1149 C++ tests pass.
- **Found**: 2026-02-14
- **Fixed**: 2026-02-14

---

## BUG-STORAGE-001: BinaryBuffer negative integer serialization truncation

- **Status**: FIXED
- **Severity**: High
- **Component**: ViperSQL storage layer — `serializer.zia`
- **Symptom**: Negative integers (e.g., -42) written via `writeInt32`/`writeInt64` and read back via `readInt32`/`readInt64` produce incorrect positive values. `-42` becomes `214` after a round-trip through file I/O.
- **Root cause**: `writeInt32` used modulo (`%`) and division (`/`) to split integers into bytes, but negative values in Zia's truncated division produce negative byte values (e.g., `-42 % 256 = -42`). These negative values were stored in the BinaryBuffer's `List[Integer]` but when transferred to `Collections.Bytes` (uint8 array) for file I/O, they were silently converted to unsigned (e.g., -42 → 214). On read-back, `Collections.Bytes.Get` returned the unsigned value, losing the sign.
- **Fix**: Added two's complement handling: `writeInt32` converts negative values to unsigned (`val + 2^32`), `writeInt64` properly splits negative 64-bit values into unsigned 32-bit lo/hi words, and `readInt64` reconstructs the sign from the high word's bit 31.
- **Verification**: All serialization round-trip tests pass, including negative values (-42, -1, -273, -40). Stress tests 10 and 12 confirm correct behavior.
- **Found**: 2026-02-13
- **Fixed**: 2026-02-13

---

## BUG-STORAGE-002: Pager header not updated on flushAll — data loss without explicit close

- **Status**: FIXED
- **Severity**: High
- **Component**: ViperSQL storage layer — `pager.zia`, `buffer.zia`
- **Symptom**: If a persistent database is written to (INSERT/UPDATE/DELETE with proper flush) but `closeDatabase()` is never called, reopening the file loses all data pages allocated after creation. The file header's `pageCount` field stays at the initial value (2 = header + schema page), so `readPage()` rejects any page ID >= 2 as out of bounds.
- **Root cause**: The `pageCount` field in the file header was only updated during `closeDatabase()`. The `BufferPool.flushAll()` method wrote dirty data pages to disk but did not update the header, so the on-disk header was stale. When reopening, the pager read `pageCount=2` from the header and refused to load data pages that physically existed in the file.
- **Fix**: (1) Extracted header writing into a reusable `Pager.flushHeader()` method. (2) Made `BufferPool.flushAll()` call `pager.flushHeader()` after flushing dirty pages, ensuring the header is always in sync with the actual page count.
- **Verification**: Stress test 11 ("Data survives without explicit CLOSE") passes. All 41 stress tests pass in both VM and native.
- **Found**: 2026-02-13
- **Fixed**: 2026-02-13

---

## BUG-NAT-005: Native codegen string lifetime bug — concatenated strings corrupted after function calls

- **Status**: OPEN (workaround available)
- **Severity**: Medium
- **Component**: Native codegen (AArch64) — string lifetime management
- **Symptom**: Local variables holding concatenated strings (e.g., `var tbl = "prefix_" + Fmt.Int(i)`) become corrupted after calling functions that do heavy string operations internally (e.g., `executeSql()`). The variable's string pointer ends up pointing to overwritten memory, producing empty strings or fragments of other strings (like SQL column definitions).
- **Root cause**: The native codegen does not properly retain concatenated string results stored in local variables. The concatenation creates a temporary string, and while the variable holds a pointer to it, subsequent function calls that create their own temporary strings can reuse the same memory region. The VM's reference-counting keeps the string alive, but the native codegen's retain/release logic misses this case.
- **Reproduction**:
  ```zia
  var tbl = "sv_" + Fmt.Int(i);          // tbl = "sv_0" (correct)
  exec.executeSql("CREATE TABLE " + tbl); // works, but corrupts tbl
  Terminal.Say(tbl);                       // prints "" or garbage
  ```
- **Workaround**: Rebuild concatenated strings inline at each use site instead of storing in a variable:
  ```zia
  exec.executeSql("CREATE TABLE sv_" + Fmt.Int(i) + " ...");
  exec.executeSql("INSERT INTO sv_" + Fmt.Int(i) + " ...");
  ```
- **Found**: 2026-02-14

---

## BUG-PARSE-001: Zia parser rejects `match` as variable/parameter name

- **Status**: FIXED
- **Severity**: Medium
- **Component**: Zia frontend parser — `Parser_Tokens.cpp`, `Parser_Expr.cpp`, `Parser_Stmt.cpp`
- **Symptom**: Using `match` as a variable name causes parse error "expected variable name" in any context. For example: `var match = 0; if match { ... }` fails to compile. This also affects `match` used as function parameter name or any identifier context.
- **Root cause**: Three issues:
  1. `checkIdentifierLike()` in `Parser_Tokens.cpp` only allowed `KwValue` as a contextual keyword but not `KwMatch`, so `var match = 0;` was rejected.
  2. `parsePrimary()` in `Parser_Expr.cpp` unconditionally treated `KwMatch` as a match expression keyword, so `return match;` or `if match {` tried to parse a match expression instead of a variable reference.
  3. `parseStatement()` in `Parser_Stmt.cpp` unconditionally treated `KwMatch` as a match statement keyword, so `match = 10;` (assignment) was misinterpreted.
- **Fix**:
  1. Added `KwMatch` to the contextual keyword switch in `checkIdentifierLike()`.
  2. In `parsePrimary()` and `parseStatement()`, added lookahead: only treat `match` as a keyword when the next token is an identifier, literal, `(`, or boolean — tokens that start a scrutinee expression. When followed by `;`, `)`, `{`, `=`, operators, etc., treat `match` as an identifier.
- **Verification**: All 1150 existing tests pass. New test `ZiaStatements.MatchAsVariableName` added and passes.
- **Found**: 2026-02-15
- **Fixed**: 2026-02-15

---

## BUG-NAT-006: AArch64 callee-side stack parameter loading clobbers register allocator assignments

- **Status**: FIXED
- **Severity**: Critical
- **Component**: Native codegen (AArch64) — `LowerILToMIR.cpp`
- **Symptom**: Functions with more than 8 parameters (where overflow args are passed on the stack per AAPCS64) crash or produce wrong results when compiled natively. Register parameters that were correctly spilled to stack slots in the prologue get clobbered by the stack parameter loading code. In the ViperSQL case, `executeHashJoinStep` (13 params, 5 on stack) crashed with `rt_list_get: index out of bounds` because the `existing` parameter (a List) was corrupted — its count field read as 2 instead of 1, and list element data was garbage.
- **Root cause**: Two issues in `LowerILToMIR.cpp` entry block parameter setup:
  1. **Stack params not implemented**: The callee-side code had `continue; // Stack param - not handled yet` for parameters beyond the 8 register slots, silently ignoring them.
  2. **Physical register conflict**: After implementing stack param loading, the code used a hardcoded physical register (initially X9, then X10) to load from the caller's stack area `[FP + 16 + idx * 8]` and store to the spill slot. However, the register allocator independently assigned vregs to these same physical registers for earlier parameter values. The prologue sequence was: (a) spill register params to slots, (b) reload into vregs (allocator assigns phys regs like X10), (c) load stack params using hardcoded X10 — step (c) clobbered the vreg from step (b).

  Assembly showing the conflict:
  ```asm
  str x1, [x29, #-416]    ; spill 'existing' (param 1)
  ldr x10, [x29, #-416]   ; reload into vreg → allocator assigns X10
  ...                      ; X10 holds 'existing', still live
  ldr x10, [x29, #0x10]   ; CLOBBER! stack param load overwrites X10
  ```
- **Fix**: Changed stack param loading to use virtual registers instead of hardcoded physical registers. The temporary vreg is defined by `LdrRegFpImm` and used by `StrRegFpImm`, letting the register allocator choose a non-conflicting physical register. This also handles the secondary issue where `emitStrToFp` uses X9 (kScratchGPR) as scratch for large offsets, which would clobber X9 if used as the load target.
- **Verification**: All 1151 tests pass (including new `test_codegen_arm64_callee_stack_params` with 3 test cases: 10 params, 10 params with cross-call liveness, 13 params with register pressure). ViperSQL hash join test suite (129 tests) passes in both VM and native.
- **Regression test**: `test_codegen_arm64_callee_stack_params.cpp` (CalleeStackParamsSum10, CalleeStackParamsSurviveCall, CalleeStackParams13Wide)
- **Found**: 2026-02-15
- **Fixed**: 2026-02-15

---

## BUG-FE-008: Chained method calls on runtime class Ptr receivers fail

- **Status**: FIXED
- **Severity**: High
- **Component**: Zia frontend — `Sema_Expr_Call.cpp`
- **Symptom**: Chaining method calls on runtime class objects (e.g., `bytes.Slice(x,y).ToStr()`) fails to compile. The outer method call cannot resolve because the base expression is typed as a Function type instead of the actual return type. Users must break chained calls into separate statements as a workaround.
- **Root cause**: In `Sema_Expr_Call.cpp`, the runtime class method call handler (lines 622–680) returned `sym->type` directly. For extern functions with parameters, `defineExternFunction()` stores a `Function(params...) -> ReturnType` type. Returning this Function type meant the outer chained call saw its base as `TypeKindSem::Function` — no dispatch handler matches Function-typed bases, so method resolution failed silently.
- **Fix**: Added return type extraction: when `sym->type->kind == TypeKindSem::Function`, return `sym->type->returnType()` instead of the raw Function type. This ensures chained calls see the actual return type (e.g., `Ptr("Viper.Collections.Bytes")`) and can resolve subsequent methods correctly.
- **Verification**: All 1151 tests pass. Chained calls like `data.Slice(0,5).ToStr()` and double-chains like `data.Slice(0,11).Slice(6,11)` compile and run correctly.
- **Regression test**: `test_zia_bugfixes.cpp` (BugFE008_ChainedRuntimeMethodCalls, BugFE008_MultipleChainedCalls)
- **Found**: 2026-02-16
- **Fixed**: 2026-02-16

---

## BUG-FE-009: List[Boolean].get(i) causes IL type mismatch in boolean expressions

- **Status**: FIXED
- **Severity**: Medium
- **Component**: Zia frontend — `Lowerer_Emit.cpp`
- **Symptom**: Using `List[Boolean].get(i)` in boolean contexts (`if`, `&&`, `||`) causes an IL type mismatch. Users must use `List[Integer]` with 0/1 values as a workaround.
- **Root cause**: In `Lowerer_Emit.cpp`, the `emitUnbox()` handler for `Type::Kind::I1` emitted `emitCallRet(Type(Type::Kind::I1), kUnboxI1, ...)`, declaring the call result as `i1`. However, the runtime function `rt_unbox_i1` has signature `"i64(obj)"` in `runtime.def` — it returns `int64_t` (always 0 or 1), not a 1-bit boolean. The IL verifier caught the mismatch between the declared `i1` return type and the actual `i64` runtime return type.
- **Fix**: Changed the I1 unboxing case to use `Type(Type::Kind::I64)` for both the call result type and the returned LowerResult type, matching the actual runtime function signature. Boolean values from unboxing now travel as `i64` (0 or 1), which is universally compatible with all IL consumers (CBr conditions, And/Or operators, etc.).
- **Verification**: All 1151 tests pass. `List[Boolean].get(i)` works correctly in if-conditions and logical AND/OR expressions.
- **Regression test**: `test_zia_bugfixes.cpp` (BugFE009_ListBooleanGetInCondition, BugFE009_ListBooleanGetInLogicalExpr)
- **Found**: 2026-02-16
- **Fixed**: 2026-02-16

---

## BUG-TOKEN-001: Duplicate token constants in ViperSQL lexer

- **Status**: FIXED
- **Severity**: Critical
- **Component**: ViperSQL — `token.zia`
- **Symptom**: SQL queries using `CLOSE` or `FILE` keywords are silently misparsed because their token constants collide with `EXCEPT` and `INTERSECT`. Any SQL using `CLOSE` would be parsed as `EXCEPT`, and `FILE` as `INTERSECT`.
- **Root cause**: Token constant assignments had duplicates: `TK_EXCEPT = 137` and `TK_CLOSE = 137`; `TK_INTERSECT = 138` and `TK_FILE = 138`. The second definitions overwrote the first in practice, meaning `EXCEPT` and `INTERSECT` still worked (their constants were overwritten by `CLOSE`/`FILE`), but `CLOSE` and `FILE` would lex as `EXCEPT`/`INTERSECT`.
- **Fix**: Reassigned `TK_CLOSE = 219` and `TK_FILE = 220` to unique values, eliminating the collision. Verified that `EXCEPT` and `INTERSECT` queries produce correct results.
- **Verification**: All existing tests pass. New Phase 14 test suite includes specific token collision regression tests.
- **Found**: 2026-02-16
- **Fixed**: 2026-02-16

---

## BUG-IO-001: Binary data with null bytes corrupted by File.ReadAllText/WriteAllText

- **Status**: OPEN (workaround available)
- **Severity**: Medium
- **Component**: Runtime (C) — `Viper.IO.File`
- **Symptom**: Binary data containing null bytes (0x00) cannot survive a `WriteAllText` + `ReadAllText` round-trip. The data is silently truncated or corrupted at the first null byte. This prevents WAL files from being read back after writing, since the first serialized field (lsn.fileNumber) is often 0, producing null bytes at the start of the file.
- **Root cause**: `File.ReadAllText` and `File.WriteAllText` operate on Zia `String` values, which are null-terminated C strings internally. Any null byte in the data acts as a premature string terminator, truncating the content. The `String.Chr(0)` function can create a string containing a null byte, but it cannot be safely stored/retrieved through the string-based file I/O API.
- **Reproduction**:
  ```zia
  var buf = new BinaryBuffer();
  buf.init();
  buf.writeInt32(0);       // Writes 4 null bytes
  buf.writeInt32(42);      // Writes 4 more bytes
  var data = buf.toBytes().ToStr();
  IO.File.WriteAllText("/tmp/test.bin", data);
  var readBack = IO.File.ReadAllText("/tmp/test.bin");
  // readBack is truncated at the first null byte — length 0 or 1
  ```
- **Workaround**: Use `Viper.Collections.Bytes` with `IO.File.WriteAllBytes`/`IO.File.ReadAllBytes` if available, or avoid binary file I/O through the string API. For WAL files, the in-memory serialization works correctly; only disk persistence is affected.
- **Impact**: WAL crash recovery cannot re-read log files from disk. All WAL functionality works correctly in-memory.
- **Found**: 2026-02-17

---

## BUG-FE-010: Cross-class Ptr type inference loses property/method access

- **Status**: FIXED
- **Severity**: High
- **Component**: Zia frontend — `Sema_Expr_Advanced.cpp`, `Sema_Expr_Call.cpp`
- **Symptom**: When a runtime function returns `obj` but the actual object belongs to a different class than the function's owning class (e.g., `Network.Tcp.RecvExact` returns a `Bytes` object, not a `Tcp` object), property access (`.Len`, `.Get()`) and method calls (`.Slice()`, `.ToStr()`) silently compile to 0/null or fail to resolve. Users must add explicit type annotations (e.g., `var x: Bytes = ...`) as a workaround.
- **Root cause**: Three-stage type information loss:
  1. **Registration** (`Sema_Runtime.cpp:169-170`): When a class method returns `obj`, the heuristic infers the return type as the method's owning class. `Network.Tcp.RecvExact` gets typed as returning `Ptr("Viper.Network.Tcp")` instead of `Ptr("Viper.Collections.Bytes")`.
  2. **Property resolution** (`Sema_Expr_Advanced.cpp:280-298`): Property access constructs `Viper.Network.Tcp.get_Len` and looks it up — not found, since `Len` is a Bytes property. Falls through to return `types::unknown()`.
  3. **Method resolution** (`Sema_Expr_Call.cpp:622-680`): Same pattern — constructs `Viper.Network.Tcp.Slice` which doesn't exist.
- **Fix**: Added cross-class fallback search in both property access (`Sema_Expr_Advanced.cpp`) and method call resolution (`Sema_Expr_Call.cpp`). When the primary class lookup fails, the sema searches the full `RuntimeRegistry` catalog across all runtime classes for a matching property getter or method. The fallback only triggers when the primary lookup fails, preserving existing behavior for correctly-typed objects.
- **Verification**: All 1151 tests pass. Variables with cross-class Ptr types can access properties and call methods that belong to the actual runtime class.
- **Regression test**: `test_zia_bugfixes.cpp` (BugFE010_CrossClassPtrMethodFallback)
- **Found**: 2026-02-16
- **Fixed**: 2026-02-16

---

## BUG-FE-011: Cross-module `final` constant equality comparison always false

- **Status**: OPEN (workaround available)
- **Severity**: Low
- **Component**: Zia frontend — constant evaluation across module boundaries
- **Symptom**: When a `final` constant is defined in module A and used in module B via `bind`, equality comparisons (`==`, `!=`) against the constant always evaluate to false. The constant's value appears correct when used in arithmetic or as a function argument, but `if (x == MY_CONSTANT)` fails even when x holds the expected value.
- **Root cause**: Unknown. Possibly the cross-module constant binding creates a different representation that fails the equality comparison.
- **Reproduction**:
  ```zia
  // module_a.zia
  final MY_SENTINEL = 0 - 2147483647;

  // module_b.zia
  bind "./module_a";
  var x = 0 - 2147483647;
  if x == MY_SENTINEL { ... }  // Never enters
  ```
- **Workaround**: Use literal values or range comparisons (`x <= 0 - 2147483646`) instead of equality checks against imported constants.
- **Found**: 2026-02-17
