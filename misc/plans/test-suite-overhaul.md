# Test Suite Overhaul — Professional-Grade Compiler Test Suite

## Context

The Viper test suite has 1,307 ctests, 185K lines of test code against 508K lines of production code (0.36 test:code ratio — mature projects target 0.5-1.0). The suite works but has architectural gaps that professional compiler projects (LLVM, V8, Zig) would not tolerate:

1. **Test framework is underpowered** — no test filtering, no fixtures, no value-printing assertions, no JUnit XML output
2. **No structured regression testing** — golden file updates are manual, no `--update` mode
3. **Sanitizer runs only cover namespace tests** — not the full suite
4. **Fuzz testing is Zia-only** — IL, BASIC, VM, and runtime are unfuzzed
5. **Property-based differential testing exists but is one test** — could be a systematic approach
6. **No coverage tracking** — no way to know what code paths tests actually exercise
7. **228 tests use raw assert()** — crashes give no diagnostic information on failure

**User decisions:** Delete redundant NO_CTEST tests; full consolidation of standalone tests; fill ALL identified gaps.

---

## PART 0: FUNDAMENTAL FRAMEWORK IMPROVEMENTS

### 0A. Enhance TestHarness.hpp — Missing Assertion Macros
**Problem:** The test harness only has `EXPECT_TRUE/FALSE/EQ/NE` and `ASSERT_TRUE/FALSE/EQ/NE`. Tests constantly work around missing macros by writing `EXPECT_TRUE(a > b)` instead of `EXPECT_GT(a, b)` — which gives zero diagnostic info on failure (just "a > b failed" with no actual values).

**Add:**
```cpp
EXPECT_GT(a, b) / ASSERT_GT(a, b)     // Greater than
EXPECT_LT(a, b) / ASSERT_LT(a, b)     // Less than
EXPECT_GE(a, b) / ASSERT_GE(a, b)     // Greater or equal
EXPECT_LE(a, b) / ASSERT_LE(a, b)     // Less or equal
EXPECT_NEAR(a, b, eps)                  // Float near-equality
EXPECT_CONTAINS(haystack, needle)       // String contains
EXPECT_THROWS(expr, ExceptionType)      // Exception expected
EXPECT_NO_THROW(expr)                   // No exception expected
```

**And value-printing on failure** — when `EXPECT_EQ(a, b)` fails, print "expected: 42, got: 17" instead of just "a == b".
**File:** `src/tests/TestHarness.hpp`

### 0B. Add Test Filtering to TestHarness
**Problem:** `init()` ignores argc/argv entirely. There's no way to run a single test from a multi-test executable. When consolidating 196 files into 30, this becomes critical.

**Add:** `--filter=Suite.Test` argument to `init()` that filters `registry()` before `run_all_tests()`. Support glob patterns (`--filter=ZiaLexer.*`).
**File:** `src/tests/TestHarness.hpp`

### 0C. Add TEST_F (Fixture) Support
**Problem:** No fixture support means tests that need shared setup/teardown (like `CodegenFixture`, `VmFixture`) can't use the `TEST()` macro cleanly. Tests currently instantiate fixtures manually in each test body.

**Add:**
```cpp
class MyFixture : public viper_test::TestFixture {
    void SetUp() override { ... }
    void TearDown() override { ... }
};
TEST_F(MyFixture, TestName) { ... }
```
**File:** `src/tests/TestHarness.hpp`

### 0D. JUnit XML Output
**Problem:** CI systems (GitHub Actions, Jenkins, etc.) can't parse test results natively. Professional projects emit JUnit XML for test result dashboards.

**Add:** `--xml=output.xml` flag to `run_all_tests()` that emits JUnit XML alongside console output.
**File:** `src/tests/TestHarness.hpp`

### 0E. Expand Fuzz Testing Beyond Zia
**Problem:** Only `fuzz_zia_lexer.cpp` and `fuzz_zia_parser.cpp` exist. A compiler project should fuzz ALL input surfaces.

**Add fuzz harnesses for:**
- `fuzz_basic_lexer.cpp` — BASIC lexer fuzzing
- `fuzz_basic_parser.cpp` — BASIC parser fuzzing
- `fuzz_il_parser.cpp` — IL text format parser (finds crashes in `il::io::Parser`)
- `fuzz_il_verifier.cpp` — Feed random IL to verifier (finds assertion failures)
- `fuzz_il_optimizer.cpp` — Feed valid IL through optimizer pipeline (finds miscompiles via VM differential)
- `fuzz_vm.cpp` — Feed valid IL to VM (finds traps, hangs, crashes)

**Files:** `src/tests/fuzz/` — 6 new harnesses, update `CMakeLists.txt`

### 0F. Sanitizer CI for Full Suite
**Problem:** `ci_sanitizer_tests.sh` only runs namespace tests under ASan/UBSan. The full test suite should run under sanitizers.

**Add:** `scripts/ci_full_sanitizer.sh` that runs ALL ctests under ASan, then UBSan. Add TSan run for thread-related tests only (`ctest -L runtime -L vm`).
**File:** `scripts/ci_full_sanitizer.sh`

### 0G. Golden File Update Mode
**Problem:** When a golden file intentionally changes (e.g., after improving an error message), the developer must manually edit each `.stderr`/`.stdout`/`.il` file. Professional projects have `--update` mode.

**Add:** A `--update` flag to `check_error.cmake`, `check_il.cmake`, etc. that overwrites the expected file with actual output when the test fails. Also add a script: `scripts/update_goldens.sh` that re-runs all golden tests with `--update`.
**Files:** `src/tests/golden/*.cmake`, `scripts/update_goldens.sh`

### 0H. Differential Testing Framework
**Problem:** `test_diff_vm_native_property.cpp` is a single 500-line test that does property-based differential testing (VM vs native). This approach should be a reusable framework.

**Add:** A `DifferentialTestFixture` in `src/tests/common/` that:
1. Takes an IL program
2. Runs it through VM
3. Runs it through native codegen (AArch64 or x86_64)
4. Compares stdout, stderr, exit code
5. Optionally runs through optimizer at O0/O1/O2 and compares all

Then extend existing `test_il_opt_equivalence.cpp` and codegen tests to use it.
**File:** `src/tests/common/DifferentialFixture.hpp`

---

## PART A: INFRASTRUCTURE & CLEANUP

### A1. Deduplicate label assignment logic
**Problem:** `_viper_assign_test_label()` in `TestHelpers.cmake` (lines 6-40) and the bottom of `src/tests/CMakeLists.txt` (lines 496-551) contain near-identical pattern-matching logic — a maintenance hazard.
**Fix:** Remove the duplicate block at the bottom of `CMakeLists.txt`. Convert raw `add_test()` calls (smoke_term_basic, basic_sum_no_linenos, basic_repros, smoke_basic_oop, smoke_basic_point_ctor) to use `viper_add_ctest()` so they get labels automatically.
**Files:** `src/tests/CMakeLists.txt`, `src/tests/cmake/TestHelpers.cmake`

### A2. Fix 31 unlabeled tests
**Problem:** 31 tests have no label, making `ctest -L` filtering incomplete.
**Fix:** Extend `_viper_assign_test_label()` patterns:
- `test_binenc_*`, `test_linker_*`, `test_objfile_*`, `codegen_x86_64_*`, `test_x86_*` → `codegen`
- `basic_arith_*`, `test_shift_conformance`, `test_subwidth_arith`, `test_crosslayer_arith` → `conformance`
- `test_bytecode_vm` → `vm`
**Files:** `src/tests/cmake/TestHelpers.cmake`

### A3. Triage 10 NO_CTEST tests (delete if redundant, fix if unique)
These 10 tests compile but never run:
- `test_basic_oop_string_array_field`, `test_basic_oop_object_array_field`
- `test_basic_input_typing`, `test_basic_nested_member_method_call`
- `test_runtime_class_catalog`, `test_runtime_catalog_targets_resolve`
- `test_oop_static_members`, `test_oop_properties`
- `test_oop_destructor_dispose`, `test_oop_destructor_chain`

For each: check if covered elsewhere → delete. If unique → fix and enable.
**Files:** `src/tests/CMakeLists.txt`, each .cpp file

### A4. Golden test CMake consolidation
**Problem:** `golden/CMakeLists.txt` is 1,204 lines of repetitive boilerplate using 10 unique runner scripts.
**Fix:** Create helper functions per runner pattern:
- `viper_add_golden_error_test(name bas stderr_file)` → wraps `check_error.cmake`
- `viper_add_golden_run_test(name bas out_file)` → wraps `check_run_output.cmake`
- `viper_add_golden_il_test(name bas il_file)` → wraps `check_il.cmake`
- etc. for all 10 runners

Convert 131 `viper_add_ctest` calls to one-liners. Target: ~200 lines from ~1200.
**Files:** `src/tests/golden/CMakeLists.txt`, `src/tests/cmake/TestHelpers.cmake`

---

## PART B: TEST CONSOLIDATION (Full)

### B1. Runtime standalone tests (196 files → ~30-40 themed files)
196 files in `src/tests/runtime/` use raw `main()+assert()` — no test names, no failure output, each compiles as its own binary.

**STATUS:** The namespace group (8 files) was consolidated as proof of concept. The runtime
consolidation was attempted with two approaches:
- **Namespace wrapping** (v2): Failed — `#include <string>` inside a namespace pollutes std
- **Full TEST() macro conversion** (v1): Works (proven with namespace tests) but requires
  per-file attention for helper functions, commented-out tests, conflicting names, and
  `test_result()` patterns. 48 files use `test_result()`, 2 override `malloc`.

**Approach for remaining work:** Use the proven TEST() macro conversion (as done for namespace
tests). Process ~10 files per session, verifying each group compiles and passes.

**Consolidation groups** (merge by subsystem, convert to `TEST()` macro):

| Group | Source Files | Target File |
|-------|-------------|-------------|
| Allocation | RTAlloc*.cpp (5) | test_rt_alloc.cpp |
| Arrays | RTArray*.cpp (3) | test_rt_array.cpp |
| Strings | RTStr*, RTConcat*, RTSubstring* (~8) | test_rt_string.cpp |
| Collections | RTBag, RTBiMap, RTBigint, etc. (~6) | test_rt_collections.cpp |
| Crypto/Archive | RTAes, RTArchive, RTHash (~4) | test_rt_crypto.cpp |
| Audio | RTAudio*, RTPlaylist* (~3) | test_rt_audio.cpp |
| Network | RTNetwork*, RTSocket*, RTTLS* (~8) | test_rt_network.cpp |
| File I/O | RTFile*, RTBinFile*, ErrorsIo (~6) | test_rt_fileio.cpp |
| Math/Numeric | Float*, Int64*, RTArithmetic* (~5) | test_rt_numeric.cpp |
| Concurrency | RTAsync*, RTThread*, RTAtomic* (~5) | test_rt_concurrency.cpp |
| Game engine | RTCollision*, RTCamera*, RTParticle*, RTTween* (~12) | test_rt_game_engine.cpp |
| GUI | RTButton*, RTInput*, RTScreen* (~5) | test_rt_gui.cpp |
| Misc | Remaining (~20) | test_rt_misc.cpp |

Each original file becomes one `TEST(Suite, OriginalFileName)` case.

### B2. Unit single-test files (69 files → ~15-20 themed files)
Merge 69 files with exactly 1 `TEST()`:

| Group | Source Files | Target File |
|-------|-------------|-------------|
| BASIC parser | test_basic_parse_*.cpp (3) | test_basic_parser.cpp |
| Namespace | test_namespace_*, test_ns_*, test_using_* (8) | test_namespace.cpp |
| BASIC builtins | test_basic_builtins_* (3) | test_basic_builtins.cpp |
| BASIC OOP | test_basic_oop_* (4) | test_basic_oop.cpp |
| BASIC lowering | test_lowerer_*, test_lowering_* (3) | test_basic_lowering.cpp |
| Runtime classes | TestProperty*, TestMethod*, TestCatalog* (4) | test_runtime_classes.cpp |
| VM | VM_TailCall*, VM_Step* (4) | test_vm_advanced.cpp |
| etc. | ... | ... |

### B3. Build time impact
~300+ executables → ~80-100. Each has ~0.5-1s link overhead → saves 2-3 minutes on full build.

---

## PART C: COVERAGE GAPS — NEW TESTS NEEDED

### C1. IL Optimizer — 3 Completely Untested Passes (P0)

| Pass | Source | What It Does | Tests Needed |
|------|--------|-------------|-------------|
| **EHOpt** | `src/il/opt/EHOpt.cpp` | Removes dead eh.push/eh.pop pairs with no throwing instructions between them | Nested EH, cross-block EH, dead handler elimination, trap instruction detection |
| **LoopRotate** | `src/il/opt/LoopRotate.cpp` | Converts while-loops to do-while (header → latch) | Single-entry loops, header with memory ops (should reject), header with calls, nested loops, preheader creation |
| **Reassociate** | `src/il/opt/Reassociate.cpp` | Sorts operands for commutative ops (temps desc, constants last) to enable CSE | Mixed commutative chains, reassociation enabling CSE, Add/Mul/And/Or/Xor coverage |

**New files:** `src/tests/unit/il/transform/test_EHOpt.cpp`, `test_LoopRotate.cpp`, `test_Reassociate.cpp`

### C2. IL Optimizer — Under-tested Passes (P1)

| Pass | Current | Specific Missing Edge Cases |
|------|---------|----------------------------|
| **GVN** | 2 files | MemorySSA interaction, partial aliasing, MustAlias vs MayAlias fallback |
| **EarlyCSE** | 1 file | Dominator-scoped CSE chain propagation, expression scope popping |
| **DSE** | 2 files | MemorySSA precision with non-escaping allocas, cross-block store chains, call clobbering |
| **LICM** | 2 files | Pure/readonly call hoisting, store aliasing preventing load hoist, outer-loop invariants |
| **SiblingRecursion** | 2 files | Mutual recursion, non-tail position |
| **LoopUnroll** | 3 files | Unroll factor selection, trip count analysis |
| **IndVarSimplify** | 3 files | Nested loops, non-canonical forms |
| **ConstFold** | 1 file | Systematic opcode coverage: all arithmetic + bitwise + comparison ops with overflow variants |

**New files:** ~8-10 additional test files in `src/tests/unit/il/transform/`

### C3. MemorySSA — Zero Unit Tests (P1)
Only 1 test file (`analysis/MemorySSATests.cpp`) exists. Missing:
- Cycle detection in memory def-use chains
- Partial alias overlaps
- Non-escaping alloca detection
- Phi node insertion at irreducible loops

**New file:** `src/tests/analysis/MemorySSAEdgeCases.cpp`

### C4. Pass Pipeline Interactions (P1)
No tests verify that pass ordering doesn't introduce bugs.
- What if GVN runs before EarlyCSE vs after?
- Does SimplifyCFG + DCE sequence correctly clean up?
- Does Reassociate + EarlyCSE interact correctly?

**New file:** `src/tests/unit/il/transform/test_pipeline_interactions.cpp`
- Run same IL through different pass orderings, verify semantic equivalence via VM.

### C5. AArch64 Peephole — 6 Sub-passes with Zero Unit Tests (P0)
Confirmed: These 6 files in `src/codegen/aarch64/peephole/` have ZERO test references:

| Sub-pass | File | LOC | What It Does |
|----------|------|-----|-------------|
| **IdentityElim** | IdentityElim.cpp | ~100 | Removes `mov x0, x0` identity moves |
| **MemoryOpt** | MemoryOpt.cpp | ~150 | Memory access strength reduction |
| **LoopOpt** | LoopOpt.cpp | ~200 | Loop-specific MIR optimizations |
| **CopyPropDCE** | CopyPropDCE.cpp | ~180 | Copy propagation + dead code elimination |
| **BranchOpt** | BranchOpt.cpp | ~120 | Compare-branch folding, dead branch removal |
| **StrengthReduce** | StrengthReduce.cpp | ~150 | Arithmetic strength reduction (mul→shift, div→mul-high) |

**New file:** `src/tests/unit/codegen/test_codegen_arm64_peephole_subpasses.cpp`
— Test each sub-pass in isolation with crafted MIR sequences.

### C6. x86_64 Codegen Gaps (P1)
x86_64 has 30 tests vs AArch64's 86. Specific missing areas:

| Gap | Details |
|-----|---------|
| **ISel: SIB addressing fold** | `[base + index*scale + disp]` folding untested |
| **ISel: SUB INT64_MIN** | Guard at ISel.cpp:157 but never tested |
| **ISel: CMOV lowering** | CMOVNErr in MachineIR but no ISel test |
| **ISel: SETCC+MOVZX** | Boolean result materialization untested |
| **ISel: Bitwise immediate canon** | AND/OR/XOR rr/ri normalization untested |
| **Callee-saved registers** | AArch64 has dedicated test, x86_64 doesn't |
| **Cross-block phi spill** | AArch64 has dedicated test, x86_64 doesn't |
| **Peephole edge cases** | Only test_x86_peephole.cpp (17 tests) |

**New files:** 5-8 in `src/tests/codegen/x86_64/`

### C7. Mach-O & PE Linker Writers (P1)
- `test_macho_writer.cpp` exists (613 LOC) — coverage confirmed
- **PE writer has ZERO tests** — no `test_pe_writer.cpp`
- Mach-O code signing (`MachOCodeSign.cpp`) and bind/rebase tables (`MachOBindRebase.cpp`) may lack dedicated tests

**New file:** `src/tests/codegen/objfile/test_pe_writer.cpp`

### C8. IL Verifier — 240 Diagnostic Strings, Only 7 Negative Tests (P1)
The verifier has ~240 unique diagnostic messages across 23 source files, but only 7 `.il` negative test files and 10 unit test files. That's ~3% coverage of error paths.

**High-priority missing negative tests:**
- Pointer type mismatch (store non-ptr to ptr location)
- Call signature validation (wrong arity, return type mismatch, indirect call type mismatch)
- Memory operation type safety (load of non-pointer, store to non-pointer, GEP on non-pointer)
- Overflow opcode enforcement (plain `Add` where spec requires `IAddOvf`)
- EhEntry outside handler block
- Resume token typing errors

**New files:** 10-15 new `.il` + `.expected` pairs in `src/tests/il/negatives/`

### C9. Runtime Library — 40 Untested Source Files (P2)

**Security-critical (must test):**
| File | Subsystem | Risk |
|------|-----------|------|
| `rt_http_url.c` | network | URL parsing — injection risk |
| `rt_network_http.c` | network | HTTP request handling |
| `rt_tls_verify.c` | network | TLS cert verification |
| `rt_ecdsa_p256.c` | network | ECDSA signature crypto |

**Functional gaps:**
| File(s) | Subsystem |
|---------|-----------|
| `rt_drawing.c`, `rt_drawing_advanced.c` | graphics — drawing primitives |
| `rt_tilemap.c`, `rt_font.c` | graphics — game engine |
| `rt_gui_*.c` (7 files) | graphics — entire GUI subsystem |
| `rt_soundbank.c`, `rt_synth.c` | audio |
| `rt_string_encode.c`, `rt_string_format.c` | text encoding/formatting |
| `rt_array_f64/i64/obj.c` | typed arrays |
| `rt_intmap.c`, `rt_pqueue.c` | collections |
| `rt_exec.c`, `rt_machine.c` | system — process execution |
| `rt_crc32.c`, `rt_hash_util.c` | utilities |
| `rt_savedata.c` | io — save game data |
| `rt_mat3.c` | math — 3x3 matrices |

**Note:** GUI and graphics tests may require `#ifdef VIPER_ENABLE_GRAPHICS` stubs, consistent with existing pattern.

### C10. Runtime Error Path Coverage (P2)
Current Zia integration tests are almost entirely happy-path. Missing:
- **NULL pointer handling** in runtime functions
- **OOM simulation** (only RTAllocOOMTests exists; extend to collections, string ops)
- **Invalid arguments** (negative indices, empty strings, out-of-range values)
- **Buffer overflow** scenarios (only RTFileReadLineOverflowTests exists)
- **Network failure** paths (connection refused, timeout, DNS failure)
- **File I/O errors** (missing file, permission denied, disk full)

**New files:** ~10-15 C++ test files adding error-path coverage per subsystem.

### C11. Frontend Gaps

**Zia parser — untested constructs:**
- Bitwise operators (`&`, `|`, `^`, `~`) — ExprKind::BitAnd/BitOr/BitXor/BitNot exist in AST
- AddressOf operator (`&func`)
- Struct literals (StructLiteralExpr)
- Tuple literals and indexing (TupleExpr, TupleIndexExpr)
- Await expressions (AwaitExpr)

**Zia sema — missing golden diagnostic tests:**
- No golden test directory for Zia diagnostics (unlike BASIC which has basic_errors/)
- Sema tests check `result.succeeded()` but don't verify exact diagnostic text
- Missing: type errors, visibility violations, overload resolution failures, incompatible match arms

**Zia lowerer — untested lowerings:**
- Bitwise operations → IL opcodes
- String interpolation → StringConcat IL
- Lambda closure capture
- Range expression lowering
- Compound assignment lowering (`x += 1`)

**BASIC parser:**
- Error recovery not systematically tested
- Duplicate line label edge cases

**New files:** ~8-12 test files across Zia and BASIC frontends.

---

## PART D: STRUCTURAL IMPROVEMENTS

### D1. Migrate standalone tests to TEST() framework
Handled naturally during Part B consolidation. Each `main()+assert()` file becomes a `TEST()` case with proper failure reporting.

### D2. Label taxonomy documentation
Add a reference comment block in `src/tests/CMakeLists.txt` documenting all labels and their usage with `ctest -L`.

### D3. Test cost ordering
Verify `CTestCustom.cmake.in` has up-to-date cost data for parallel ctest scheduling.

---

## PART E: DOCUMENTATION UPDATES

Every change in this plan requires corresponding documentation. Documentation lives in two places:
- `docs/testing.md` — the main testing guide (currently 320 lines, covers differential testing well but is silent on most test categories)
- `src/tests/CMakeLists.txt` — inline CMake comments for developers navigating the build system

### E1. Rewrite `docs/testing.md` — Comprehensive Testing Guide
The current doc focuses heavily on differential testing (a single test!) and concurrency testing but omits the majority of the test suite. Rewrite to cover:

**New sections to add:**
1. **Test Suite Overview** — Architecture diagram showing test layers (unit → golden → e2e → differential → fuzz), with counts per category
2. **Test Framework Reference** — Document all TestHarness.hpp macros including new ones (0A):
   - All `EXPECT_*`/`ASSERT_*` macros with examples
   - `TEST()` and `TEST_F()` usage patterns
   - `VIPER_TEST_SKIP()` for conditional tests
   - `--filter=` for test selection
   - `--xml=` for JUnit output
3. **Test Labels** — Document the complete label taxonomy (`ctest -L basic`, `ctest -L codegen`, etc.) with what each label includes
4. **Adding Tests** — Expand the template section:
   - How to add a golden test (with helper functions from A4)
   - How to add a Zia runtime test (.zia file)
   - How to add an IL verifier negative test (.il + .expected)
   - How to add a codegen test (AArch64 vs x86_64 patterns)
   - How to add a fuzz harness
5. **Running Tests** — Common developer workflows:
   - Run all: `./scripts/build_viper.sh`
   - Run by label: `ctest -L codegen`
   - Run single test: `ctest -R test_name`
   - Run with filter: `./build/test_exe --filter=Suite.Name`
   - Run with sanitizers: reference `ci_full_sanitizer.sh`
   - Update golden files: reference `scripts/update_goldens.sh`
6. **Golden File Testing** — How golden tests work, the 10 runner scripts, how to update expected output
7. **Fuzz Testing** — How to run fuzz harnesses, how to add new ones, corpus management
8. **Coverage Tracking** — How to generate coverage reports (if added), how to interpret them
9. **Platform-Specific Notes** — Windows dialog suppression, ARM64 gating, x86_64 gating

**Sections to update:**
- **Differential Testing** — Already good, update to reference DifferentialFixture (0H)
- **Concurrency Testing** — Already good, keep as-is
- **Future Work** — Remove items we've now implemented, add new roadmap items

**File:** `docs/testing.md`

### E2. Update `docs/codemap.md` — Test Directory Map
Add or update the test directory section in the code map to reflect:
- New consolidated test files (B1, B2)
- New fuzz harnesses (0E)
- New infrastructure files (DifferentialFixture, etc.)
- Golden test helper functions

**File:** `docs/codemap.md`

### E3. Inline CMake Documentation
Add a reference comment block at the top of `src/tests/CMakeLists.txt`:
```cmake
# ============================================================================
# Viper Test Suite — Quick Reference
# ============================================================================
# Labels (use with ctest -L <label>):
#   basic       — BASIC frontend tests (lexer, parser, sema, lowerer)
#   il          — IL core tests (parsing, serialization, verification, analysis)
#   vm          — VM runtime tests (opcodes, traps, debugging, concurrency)
#   runtime     — C runtime library tests (strings, collections, I/O, etc.)
#   codegen     — Code generation tests (x86_64, AArch64, linker, binary encoding)
#   oop         — Object-oriented programming tests
#   golden      — Golden file regression tests (diagnostic messages, IL output)
#   e2e         — End-to-end pipeline tests (source → IL → VM/native → compare)
#   ilopt       — IL optimizer pass tests
#   conformance — Arithmetic semantics cross-layer equivalence
#   zia         — Zia language frontend tests
#   namespace   — Namespace/module system tests
#   tools       — CLI tool tests, language server tests
#   smoke       — Quick sanity tests
#   unit        — Focused unit tests for support libraries
#   perf        — Performance benchmarks (excluded from default runs)
#   slow        — Long-running tests (excluded from default runs)
#
# Common commands:
#   ctest --test-dir build                     # Run all tests
#   ctest --test-dir build -L codegen          # Run codegen tests only
#   ctest --test-dir build -R test_zia_lexer   # Run specific test
#   ctest --test-dir build --print-labels      # List all labels
#   ctest --test-dir build -j$(nproc)          # Parallel execution
# ============================================================================
```
**File:** `src/tests/CMakeLists.txt`

### E4. Update CLAUDE.md Testing Policy
Update the Testing Policy section to reference:
- New assertion macros available
- Test filtering capability
- Golden file update workflow
- Fuzz testing as part of the test suite

**File:** `CLAUDE.md`

### E5. TestHarness.hpp Header Documentation
The header already has extensive doxygen-style docs (99 lines of header comments). Update to document:
- New macros (EXPECT_GT/LT/GE/LE/NEAR/CONTAINS/THROWS)
- New `--filter` and `--xml` CLI options
- TEST_F fixture support
- The Architecture Overview table with new features

**File:** `src/tests/TestHarness.hpp`

---

## EXECUTION ORDER

**Phase I — Framework Foundation (do first, everything else depends on it):**

| Step | Phase | What | Files Changed | Risk |
|------|-------|------|---------------|------|
| 1 | 0A | Enhanced assertion macros | 1 (TestHarness.hpp) | Low |
| 2 | 0B | Test filtering (--filter) | 1 (TestHarness.hpp) | Low |
| 3 | 0C | TEST_F fixture support | 1 (TestHarness.hpp) | Low |
| 4 | 0D | JUnit XML output | 1 (TestHarness.hpp) | Low |

**Phase II — Infrastructure Cleanup (unblocks consolidation):**

| Step | Phase | What | Files Changed | Risk |
|------|-------|------|---------------|------|
| 5 | A2 | Fix unlabeled tests | 1 | Very Low |
| 6 | A1 | Deduplicate label logic | 2 | Low |
| 7 | A3 | Triage NO_CTEST tests | 10-12 | Low-Medium |
| 8 | A4 | Golden CMake consolidation | 2 | Medium |
| 9 | 0G | Golden file update mode | 10+ cmake scripts | Medium |

**Phase III — Gap Filling (new tests, highest regression-prevention value):**

| Step | Phase | What | Files Changed | Risk |
|------|-------|------|---------------|------|
| 10 | C1 | EHOpt/LoopRotate/Reassociate tests | 3 new | Low |
| 11 | C5 | AArch64 peephole sub-pass tests | 1 new | Low |
| 12 | C6 | x86_64 codegen gap tests | 5-8 new | Low |
| 13 | C7 | PE writer tests | 1 new | Low |
| 14 | C8 | Verifier negative tests | 10-15 new .il files | Low |
| 15 | C2 | Under-tested optimizer pass tests | 8-10 new | Low |
| 16 | C3 | MemorySSA edge cases | 1 new | Low |
| 17 | C4 | Pipeline interaction tests | 1 new | Low |
| 18 | C9 | Untested runtime tests (security first) | 4-8 new | Medium |
| 19 | C10 | Runtime error path tests | 10-15 new | Medium |
| 20 | C11 | Frontend gap tests | 8-12 new | Low |

**Phase IV — Consolidation (largest diff, do after framework supports filtering):**

| Step | Phase | What | Files Changed | Risk |
|------|-------|------|---------------|------|
| 21 | B1 | Runtime test consolidation (196→~30) | ~200 modified | Medium |
| 22 | B2 | Unit test consolidation (69→~15) | ~70 modified | Medium |
| 23 | D1-D3 | Structural improvements | Misc | Low |

**Phase V — Advanced Testing Infrastructure:**

| Step | Phase | What | Files Changed | Risk |
|------|-------|------|---------------|------|
| 24 | 0E | Expand fuzz testing (6 new harnesses) | 7 new | Low |
| 25 | 0F | Full-suite sanitizer CI script | 1 new | Low |
| 26 | 0H | Differential testing framework | 1 new + refactor | Medium |

**Phase VI — Documentation (accompanies each phase above):**

| Step | Phase | What | Files Changed | Risk |
|------|-------|------|---------------|------|
| 27 | E5 | TestHarness.hpp header docs (with Phase I) | 1 | Very Low |
| 28 | E3 | Inline CMake label docs (with Phase II) | 1 | Very Low |
| 29 | E1 | Rewrite docs/testing.md (after Phases I-IV) | 1 | Low |
| 30 | E2 | Update docs/codemap.md | 1 | Very Low |
| 31 | E4 | Update CLAUDE.md testing policy | 1 | Very Low |

**Documentation rule:** Each phase should update docs as it goes — don't leave docs as a final step. E5 ships with Phase I, E3 ships with Phase II, and E1 (the big testing guide rewrite) ships after Phase IV when all features are in place.

---

## VERIFICATION

After each step:
```sh
./scripts/build_viper.sh                    # Full build + test
ctest --test-dir build -N | tail -1         # Verify test count
ctest --test-dir build --print-labels       # Verify labels
ctest --test-dir build -L codegen           # Spot-check label filtering
```

---

## SUMMARY

| Metric | Current | After Overhaul |
|--------|---------|---------------|
| **Framework** | | |
| Assertion macros | 8 (EQ/NE/TRUE/FALSE × 2) | 20+ (GT/LT/GE/LE/NEAR/CONTAINS/THROWS) |
| Test filtering | None | `--filter=Suite.Test*` glob support |
| Fixture support (TEST_F) | None | Full SetUp/TearDown lifecycle |
| CI output format | Console only | Console + JUnit XML |
| Value printing on failure | Expression text only | Actual vs expected values |
| **Coverage** | | |
| CTest count | 1,307 | ~1,380-1,420 |
| Test executables | ~300+ | ~80-100 |
| Unlabeled tests | 31 | 0 |
| Disabled (NO_CTEST) | 10 | 0 |
| Untested optimizer passes | 3 | 0 |
| Untested AArch64 peephole sub-passes | 6 | 0 |
| Untested runtime files | 40 | ~5 (GUI stubs only) |
| Verifier negative tests | 7 | ~20 |
| Error path test coverage | <10% | ~50% |
| PE writer tests | 0 | 1+ |
| Pipeline interaction tests | 0 | 1+ |
| Zia diagnostic golden tests | 0 | 10+ |
| **Infrastructure** | | |
| Fuzz harnesses | 2 (Zia only) | 8 (all input surfaces) |
| Sanitizer CI scope | Namespace tests only | Full test suite |
| Golden file update mode | Manual | `--update` flag + script |
| Differential test framework | 1 ad-hoc test | Reusable DifferentialFixture |
| Golden CMakeLists lines | 1,204 | ~200 |
| Label logic duplication | 2x | 1x |
| Test:code line ratio | 0.36 | ~0.45 |
| **Documentation** | | |
| docs/testing.md scope | Differential + concurrency only | All 16 test categories documented |
| Test label documentation | None | Full taxonomy in CMake + testing.md |
| TestHarness.hpp API docs | Basic (8 macros) | Complete (20+ macros, filtering, XML, fixtures) |
| Golden file workflow docs | None | Update mode documented with examples |
| Fuzz testing docs | None | Setup, running, corpus management |
