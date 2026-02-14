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

- **Status**: CONFIRMED (workaround exists)
- **Severity**: Medium
- **Component**: Zia frontend — call lowering
- **Symptom**: Calling `.add()` on a List accessed through an entity field chain (e.g., `result.committedTxns.add(value)`) fails with IL verification error: `call arg type mismatch: @Viper.Collections.List.Push parameter 0 expects ptr but got i64`. Direct List locals and simple entity field Lists work fine.
- **Root cause**: The lowerer resolves the entity field chain to a value, but when the List is accessed through a multi-level entity field dereference, the resulting IL type is `i64` instead of `ptr`, causing a type mismatch in the generated call instruction.
- **Workaround**: Assign the entity field List to a local variable before calling methods on it:
  ```zia
  // FAILS: result.committedTxns.add(record.txnId)
  // WORKS:
  var committed = result.committedTxns;
  committed.add(record.txnId);
  result.committedTxns = committed;
  ```
- **Regression test**: `test_zia_bugfixes.cpp:BugFE006_ListParamMethodCalls` (simple case — passes)
- **Found**: 2026-02-13
- **Reproduced**: 2026-02-14 (in WAL recovery code with entity field chains)

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

- **Status**: CONFIRMED (native workaround)
- **Severity**: High
- **Component**: VM interpreter — memory management
- **Symptom**: Programs that create many entity objects (e.g., multiple Executor/StorageEngine instances across test functions) crash with SIGSEGV after a threshold of cumulative allocations. The crash point is non-deterministic but correlates with total allocation pressure — e.g., 3 rounds of 50-row INSERT+DELETE tests work, but adding a 4th round with 100 rows crashes.
- **Root cause**: The VM's entity/heap object pool appears to have a resource exhaustion issue. Each Zia entity method call allocates temporary objects (BinaryBuffer, Row, Value, etc.) that accumulate without proper collection. After enough rounds of heavy operations, some internal data structure overflows or a freed object is accessed.
- **Reproduction**: Run `test_stress3.zia` with original 100-row DELETE tests after the multi-database test — crashes during the 3rd or 4th test function.
- **Workaround**: (1) Reduce per-test row counts to stay within VM limits when running in the interpreter. (2) Build as native binary (`viper build`) which does not have this limitation — native binaries successfully handle 1000+ row operations across many test functions.
- **Found**: 2026-02-14

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
