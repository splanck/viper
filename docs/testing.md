---
status: active
audience: developers
last-verified: 2026-06-20
---

# Testing Guide

This document describes the testing infrastructure for the Viper compiler stack. The test suite
contains 1,740 tests across unit, golden, end-to-end, differential, conformance, audit, and fuzz
categories.

## Test Suite Overview

```
Unit Tests (src/tests/unit/, src/tests/il/, src/tests/vm/)
    |  Individual component testing: IL, VM opcodes, codegen, runtime
    v
Golden Tests (src/tests/golden/)
    |  Textual stability: diagnostic messages, IL output, optimizer output
    v
End-to-End Tests (src/tests/e2e/)
    |  Full pipeline: source -> IL -> VM/native -> compare output
    v
Differential Tests (src/tests/unit/codegen/)
    |  VM vs native backend equivalence (property-based)
    v
Conformance Tests (src/tests/conformance/)
    |  Cross-layer arithmetic semantics equivalence
    v
Fuzz Tests (src/tests/fuzz/)
       Continuous fuzzing of parser/lexer inputs
```

## Running Tests

```bash
# Build and run all tests
./scripts/build_viper_linux.sh   # Linux
./scripts/build_viper_mac.sh     # macOS
```

The build scripts honor environment variables for faster iteration on all
platforms (ccache is auto-detected; disable with `VIPER_NO_CCACHE=1`):

| Variable | Effect |
|----------|--------|
| `VIPER_BUILD_TYPE=RelWithDebInfo` | Override the full-suite script default build type (`Debug`) |
| `VIPER_JOBS=<n>` | Override build parallelism |
| `VIPER_CTEST_JOBS=<n>` | Override CTest parallelism independently from build jobs; macOS defaults to performance-core count |
| `VIPER_FAST_DEBUG=0` | Disable the default fast-Debug compile mode (`-Og` on Linux/macOS or `/O1` with lean STL checks on Windows) |
| `VIPER_SKIP_CLEAN=1` | Skip the clean-all step (incremental rebuild) |
| `VIPER_SKIP_TESTS=1` | Build without running ctest |
| `VIPER_TEST_LABEL=<label>` | Run only tests with the given ctest label |
| `VIPER_RUN_SLOW_TESTS=1` | Include tests labeled `slow` |
| `VIPER_SKIP_LINT=1`, `VIPER_SKIP_AUDIT=1`, `VIPER_SKIP_SMOKE=1`, `VIPER_SKIP_INSTALL=1` | Skip the corresponding post-build stages |
| `VIPER_EXTRA_CMAKE_ARGS="-DVIPER_ENABLE_INDIVIDUAL_BASIC_TO_IL_GOLDEN_TESTS=ON"` | Register legacy per-case BASIC-to-IL golden tests alongside the default batch shards |
| `VIPER_GFX_NO_ACTIVATE=1` | On macOS and Linux, show new ViperGFX windows without making them the active app/window; CTest applies this automatically to `requires_display` and `graphics3d` tests |
| `VIPER_GFX_HIDE_WINDOWS=1` | On macOS and Linux, keep ViperGFX windows hidden while preserving framebuffer rendering; CTest applies this automatically to `requires_display` and `graphics3d` tests |
| `VIPER_AUDIO_SILENT=1` | Keep platform-device output silent while still advancing voices, music, effects, and playback state; CTest applies this automatically to the main repository test suite |

Each build script holds an exclusive lock for its resolved build directory until
all build, test, validation, and install stages finish. A concurrent invocation
using the same directory exits with the owning process ID instead of cleaning or
regenerating files underneath the active run. Use a distinct `VIPER_BUILD_DIR`
when concurrent builds are intentional.

```bash

# Run all tests (after building)
ctest --test-dir build

# Run tests by label
ctest --test-dir build -L codegen          # Code generation tests only
ctest --test-dir build -L bytecode         # Bytecode VM and VM/bytecode parity
ctest --test-dir build -L golden           # Golden file tests only
ctest --test-dir build -L "vm"             # VM tests only
ctest --test-dir build -L audit            # Local structural/source-health audits

# Run a specific test
ctest --test-dir build -R test_zia_lexer

# Run tests in parallel
ctest --test-dir build -j$(nproc)

# Run with verbose output on failure
ctest --test-dir build --output-on-failure

# List all labels
ctest --test-dir build --print-labels

# Update golden files after intentional changes
./scripts/update_goldens.sh                # Update all
./scripts/update_goldens.sh il_opt         # Update only optimizer goldens

# Run with sanitizers
./scripts/ci_full_sanitizer.sh
```

### Measuring demo build performance

Use a forced rebuild for repeatable cold compiler measurements and a second
unforced run to validate dependency-stamp behavior:

```bash
./scripts/build_demos_linux.sh --rebuild --jobs "$(nproc)" --timings
./scripts/build_demos_linux.sh --jobs "$(nproc)" --timings
./scripts/build_demos_linux.sh --release --rebuild --jobs "$(nproc)" --timings
```

The first command measures the interactive O1/fast-link path, the second should
report unchanged demos as up to date, and the third measures release O2. For an
optimizer change-report audit, rerun a representative target with
`VIPER_VERIFY_PASS_CHANGE_REPORTS=1`; this deliberately restores full-module
fingerprints around every pass and is not a performance configuration.

## Test Labels

| Label | Count | Description |
|-------|-------|-------------|
| `basic` | 95 | BASIC frontend (lexer, parser, sema, lowerer) |
| `bytecode` | — | Bytecode VM direct tests and IL VM/bytecode parity |
| `il` | 196 | IL core (parsing, serialization, verification, analysis, transforms) |
| `vm` | 112 | VM runtime (opcodes, traps, debugging, concurrency) |
| `runtime` | 373 | C runtime library (strings, collections, I/O, math, graphics, etc.) |
| `codegen` | 125 | Code generation (x86_64, AArch64, linker, binary encoding) |
| `oop` | 31 | Object-oriented programming (classes, inheritance, interfaces) |
| `golden` | 203 | Golden file regression (diagnostic messages, IL/optimizer output) |
| `e2e` | 19 | End-to-end pipeline tests |
| `examples` | — | Example/demo manifest audit and fast smoke |
| `fuzz` | — | Fuzz corpus replay and fuzz-lane self-checks |
| `audit` | 46 | Zia audit corpus plus local source-health and structural drift audits |
| `ilopt` | 4 | IL optimizer pass golden tests |
| `conformance` | 10 | Arithmetic semantics cross-layer equivalence |
| `zia` | 101 | Zia language frontend tests |
| `namespace` | 5 | Namespace/module system tests |
| `tools` | 29 | CLI tools, language server tests |
| `smoke` | 5 | Quick sanity tests |
| `tui` | 28 | Terminal UI / REPL tests |
| `perf` | — | Performance benchmarks (excluded from default runs) |
| `slow` | 1 | Long-running tests (excluded from default runs) |

## Test Categories

### Unit Tests

Located in `src/tests/unit/`, `src/tests/il/`, `src/tests/vm/`. Test individual components:

- IL core types, parsing, serialization
- IL optimizer passes (EHOpt, LoopRotate, Reassociate, SCCP, GVN, etc.)
- IL verifier checks (positive and negative)
- VM opcode semantics, traps, debugging
- Codegen utilities (peephole, register allocation, ISel)
- Runtime C functions (strings, collections, I/O, math)
- Runtime audio coverage, including `test_rt_audio_fx`,
  `test_rt_audio_integration`, `test_rt_sound3d_contract`, and
  `test_rt_sound3d_objects`
- Frontend parser, sema, lowerer for both Zia and BASIC

### Golden Tests

Located in `src/tests/golden/`. Test textual stability of outputs:

- IL serialization format
- Zia/BASIC compiler diagnostic messages
- IL optimizer transformations
- Constant folding results

Golden tests use 10 CMake runner scripts. Helper functions in `TestHelpers.cmake` reduce
registration to one line per test.

BASIC-to-IL goldens are batched by default through an in-process runner split across
`VIPER_BASIC_TO_IL_GOLDEN_BATCH_SHARDS` CTest shards (default `8`). Set
`-DVIPER_ENABLE_INDIVIDUAL_BASIC_TO_IL_GOLDEN_TESTS=ON` in `VIPER_EXTRA_CMAKE_ARGS` to also
register the legacy one-CTest-per-case entries for targeted debugging.

### End-to-End Tests

Located in `src/tests/e2e/`. Test complete pipelines:

- Zia programs compiled and executed
- BASIC programs compiled and executed
- IL programs through VM and native backends

### Differential Tests

Located in `src/tests/unit/codegen/`. Verify that VM and native backends produce identical results
for the same IL programs.

`src/tests/codegen/aarch64/` is the dedicated home for AArch64 backend tests that
consume shared or end-to-end corpus inputs. Low-level instruction, pass, or
allocator tests may remain in `src/tests/unit/codegen/` when that is the clearer
ownership boundary.

### Shared IL Corpus

`src/tests/shared_corpus/il/` contains deterministic IL programs used by more
than one backend or execution engine. `success/` programs return a stable `i64`
and may write stable runtime stdout; `traps/` programs intentionally terminate
with one of the shared VM trap kinds. Avoid time, randomness, host file system
state, network access, and unbounded loops in this corpus.

The main consumers are:

- `test_bytecode_full_program_parity`: runs each success program on the IL VM,
  bytecode switch dispatch, and bytecode threaded dispatch, then compares return
  value and runtime stdout. It also checks trap programs and locks down
  bytecode/IL `TrapKind` value alignment for kinds 0-11.
- `test_codegen_aarch64_shared_corpus`: compiles representative success programs
  through the AArch64 command pipeline on every host without executing ARM64
  code, and checks deterministic assembly plus stable mnemonic markers.

Cross-backend native parity is enforced transitively through the VM: x86_64
native and AArch64 native each compare covered programs to the same VM
semantics, so widening shared-corpus VM-differential coverage widens backend
equivalence. Direct both-native comparison remains a slow, opt-in workflow for
hosts with emulation.

---

## Property-Based Differential Testing

### Overview

The differential testing framework generates random-but-valid IL programs and verifies that:

1. The IL passes verification
2. VM execution produces a result
3. Native (AArch64) execution produces the same result

This approach catches semantic differences between execution backends that hand-written tests might miss.

### IL Generator

The `ILGenerator` class (`src/tests/common/ILGenerator.hpp`) generates random IL modules:

```cpp
#include "common/ILGenerator.hpp"

// Create generator with specific seed for reproducibility
viper::tests::ILGenerator generator(12345);

// Configure generation
viper::tests::ILGeneratorConfig config;
config.minInstructions = 5;
config.maxInstructions = 15;
config.includeComparisons = true;

// Generate IL module
auto result = generator.generate(config);
std::cout << result.ilSource << "\n";
```

#### Configuration Options

| Option               | Default | Description                            |
|----------------------|---------|----------------------------------------|
| `minInstructions`    | 3       | Minimum instructions per block         |
| `maxInstructions`    | 20      | Maximum instructions per block         |
| `minBlocks`          | 1       | Minimum basic blocks                   |
| `maxBlocks`          | 4       | Maximum basic blocks                   |
| `includeComparisons` | true    | Include comparison operations          |
| `includeControlFlow` | true    | Include branches and control flow       |
| `minConstant`        | -10     | Minimum constant value                 |
| `maxConstant`        | 10      | Maximum constant value                 |
| `includeFloats`      | false   | Include floating-point operations      |
| `includeBitwise`     | true    | Include bitwise operations             |
| `includeShifts`      | true    | Include shift operations               |

#### Generated Operations

The generator produces IL using checked operations per the IL spec:

- `iadd.ovf` - Signed addition with overflow trap
- `isub.ovf` - Signed subtraction with overflow trap
- `imul.ovf` - Signed multiplication with overflow trap
- `sdiv.chk0` - Signed division with divide-by-zero trap
- `icmp_eq`, `icmp_ne` - Equality comparisons
- `scmp_lt`, `scmp_le`, `scmp_gt`, `scmp_ge` - Signed comparisons

### Reproducibility

All tests use seeded random number generators. When a test fails:

1. The seed is printed in the error message
2. Re-run with the same seed to reproduce the failure:
   ```cpp
   ILGenerator generator(failing_seed);
   auto result = generator.generate(config);
   ```

The `ReproducibilityWithSeed` test verifies that identical seeds produce identical IL.

### Running Differential Tests

```bash
# Build the test
cmake --build build --target test_diff_vm_native_property

# Run via ctest
ctest --test-dir build -R diff_vm_native_property --output-on-failure

# Run directly (shows iteration progress)
./build/src/tests/test_diff_vm_native_property
```

### Test Iterations

By default, the test runs 10 iterations per test case. You can override this with
`VIPER_DIFF_ITERATIONS` (e.g., `VIPER_DIFF_ITERATIONS=50` for local fuzzing). Each iteration:

1. Generates a new IL program from a unique seed
2. Verifies the IL
3. Runs on VM
4. Runs on native backend (AArch64 on Apple Silicon)
5. Compares exit codes (masked to 8 bits)

---

## Test Fixtures

### VmFixture

Located in `src/tests/common/VmFixture.hpp`. Provides a clean VM environment for testing:

```cpp
VmFixture fixture;
int64_t result = fixture.run(module);
```

### CodegenFixture

Located in `src/tests/common/CodegenFixture.hpp`. Orchestrates comparison between VM and native execution.

---

## Test Framework (TestHarness.hpp)

The framework is a lightweight, header-only, dependency-free test harness in `src/tests/TestHarness.hpp`.

### Assertion Macros

| Macro | Fatal | Description |
|-------|-------|-------------|
| `EXPECT_TRUE(expr)` / `ASSERT_TRUE(expr)` | No/Yes | Expression is truthy |
| `EXPECT_FALSE(expr)` / `ASSERT_FALSE(expr)` | No/Yes | Expression is falsy |
| `EXPECT_EQ(a, b)` / `ASSERT_EQ(a, b)` | No/Yes | Equality (prints both values) |
| `EXPECT_NE(a, b)` / `ASSERT_NE(a, b)` | No/Yes | Inequality (prints both values) |
| `EXPECT_GT(a, b)` / `ASSERT_GT(a, b)` | No/Yes | Greater than |
| `EXPECT_LT(a, b)` / `ASSERT_LT(a, b)` | No/Yes | Less than |
| `EXPECT_GE(a, b)` / `ASSERT_GE(a, b)` | No/Yes | Greater or equal |
| `EXPECT_LE(a, b)` / `ASSERT_LE(a, b)` | No/Yes | Less or equal |
| `EXPECT_NEAR(a, b, eps)` | No | Float near-equality |
| `EXPECT_CONTAINS(str, sub)` | No | String contains substring |
| `EXPECT_THROWS(expr, Type)` | No | Exception of Type thrown |
| `EXPECT_NO_THROW(expr)` | No | No exception thrown |
| `VIPER_TEST_SKIP(reason)` | — | Skip test with reason |

Comparison macros (`EQ`, `NE`, `GT`, `LT`, `GE`, `LE`) print actual operand values on failure.

### Command-Line Options

- `--filter=PATTERN` — Run only tests matching glob (e.g., `--filter=ZiaLexer.*`)
- `--xml=PATH` — Write JUnit XML results for CI integration

### Fixtures (TEST_F)

```cpp
class MyFixture : public viper_test::TestFixture {
protected:
    void SetUp() override { /* before each test */ }
    void TearDown() override { /* after each test */ }
};

TEST_F(MyFixture, TestName) {
    // fixture members accessible here
}
```

## Adding New Tests

### Unit Test Template

```cpp
#include "tests/TestHarness.hpp"

TEST(MySuite, MyTest) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_GT(result.size(), 0U);
    EXPECT_CONTAINS(output, "expected text");
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
```

### Golden Error Test

Add a `.bas` + `.stderr` file pair, then one line in `golden/CMakeLists.txt`:

```cmake
_golden_error(basic_error_my_test ${_BE} my_test)
```

### Golden Run Test

Add a `.bas` + `.stdout` file pair:

```cmake
_golden_basic_run(my_run_test ${_DIR} my_program my_program.stdout)
```

### IL Verifier Negative Test

Add a `.il` + `.expected` file pair in `src/tests/il/negatives/`, then register:

```cmake
viper_add_ctest(il_verify_negative_my_case
    ${CMAKE_COMMAND} -DIL_VERIFY=${IL_VERIFY}
    -DFILE=${_DIR}/my_case.il -DEXPECT_FILE=${_DIR}/my_case.expected
    -P ${_DIR}/check_negative.cmake)
```

### Zia Runtime Test

Add a `.zia` file in `tests/runtime/` that prints `RESULT: ok` on success:

```zia
// test_my_feature.zia
func main() {
    // ... test logic ...
    Say("RESULT: ok")
}
```

Register in `src/tests/CMakeLists.txt` under `ZIA_RUNTIME_TESTS`.

### Differential Test Template

For new differential tests, extend the pattern in `test_diff_vm_native_property.cpp`:

1. Create a test case with specific `ILGeneratorConfig`
2. Use `runDifferentialTest()` helper
3. Check `result.passed` and report `result.errorMessage` on failure

---

## Platform Support

- **VM**: All platforms
- **AArch64 Native**: macOS on Apple Silicon (tested)
- **x86_64 Native**: Validated on Windows with full codegen test suite passing

Differential tests automatically skip native execution on unsupported platforms.

---

## Concurrency Testing

### Overview

The VM concurrency tests verify thread-safety of the VM execution model:

- Each VM instance is single-threaded
- Thread-local storage (`tlsActiveVM`) correctly tracks active VM per thread
- `ActiveVMGuard` RAII pattern properly manages VM context binding
- Runtime callbacks preserve thread-local context
- Trap reports include correct VM context

`Viper.Threads` adds additional tests that verify shared-memory threading semantics:

- FIFO-fair, re-entrant monitor behavior (`Viper.Threads.Monitor`)
- Thread lifecycle and join timeouts (`Viper.Threads.Thread`)
- FIFO-serialized safe variables (`Viper.Threads.SafeI64`)
- Future/Async/Parallel result retention, listener trap isolation, and one-shot pool task error reporting
- Scheduler, debouncer/throttler, cancellation-token, channel, and concurrent collection synchronization behavior
- VM thread start override (`Viper.Threads.Thread.Start`) and shared globals behavior

### Stress Test

Located in `src/tests/unit/test_vm_concurrency_stress.cpp`. Exercises:

- Multiple VMs running concurrently across threads
- Runtime function callbacks (e.g., `Viper.Math.AbsInt`)
- Rapid VM creation and destruction
- Nested `ActiveVMGuard` usage

```bash
# Run with default settings (8 threads, 100 iterations)
./build/src/tests/test_vm_concurrency_stress

# Run with debug logging
./build/src/tests/test_vm_concurrency_stress --debug

# Custom thread/iteration counts
./build/src/tests/test_vm_concurrency_stress --threads 16 --iterations 500
```

### Running with ThreadSanitizer (TSan)

Sanitizer lanes are local opt-in checks. `scripts/ci_full_sanitizer.sh` is the
canonical entry point for broad ASan/UBSan coverage; the legacy
`scripts/ci_sanitizer_tests.sh` wrapper delegates to it. Use the TSan variants after VM,
runtime, or graphics concurrency changes.

```bash
# Broad ASan + UBSan lane
./scripts/ci_full_sanitizer.sh

# Generic VM/runtime TSan lane
./scripts/ci_full_sanitizer.sh --tsan

# Focused graphics3d concurrency TSan lane
./scripts/g3d_tsan_concurrency_lane.sh

# Confirm sanitizer toolchain availability without running the full lane
./scripts/ci_full_sanitizer.sh --self-test
```

#### Interpreting TSan Output

TSan reports data races with stack traces. Example:

```text
WARNING: ThreadSanitizer: data race
  Write of size 8 at 0x7fff5fbff9a0 by thread T2:
    #0 vm::someFunction() src/vm/VMContext.cpp:123
  Previous read of size 8 at 0x7fff5fbff9a0 by thread T1:
    #0 vm::otherFunction() src/vm/VMContext.cpp:456
```

Key fields:

- **Location**: Memory address involved in race
- **Operation**: Read or write, with size
- **Threads**: Which threads are racing
- **Stack traces**: Where the racing accesses occur

#### Known TSan Suppressions

Some benign races may be suppressed in a `tsan.supp` file:

```text
# Example suppression file (create as tsan.supp)
race:deliberate_benign_race_function
```

Run with suppressions:

```bash
TSAN_OPTIONS="suppressions=tsan.supp" ./build-tsan/src/tests/test_vm_concurrency_stress
```

### Measuring Coverage

Clang source-based coverage is available through the opt-in
`VIPER_ENABLE_COVERAGE` CMake option and the local coverage lane:

```bash
./scripts/coverage.sh
```

The script configures `build-coverage/`, runs CTest with coverage profile output, and
writes:

- `coverage/summary.txt` — `llvm-cov report` summary.
- `coverage/subsystems.txt` — ranked per-subsystem line-coverage rollup.
- `coverage/html/index.html` — drill-down HTML report.

Coverage is for visibility only; it does not enforce thresholds. Use it when adding a new
subsystem or when ranking weak areas by measured coverage instead of intuition.

### Example Smoke

Examples are classified by `examples/smoke_manifest.tsv` and checked by
`scripts/example_smoke.sh`.

```bash
./scripts/example_smoke.sh --audit              # all example sources classified
./scripts/example_smoke.sh --fast               # fast runnable/checkable subset
./scripts/example_smoke.sh --all                # full manifest sweep
ctest --test-dir build -L examples --output-on-failure
```

The manifest keeps graphical, project, benchmark, and non-standalone examples explicit
while letting CTest run a fast headless smoke over compact Zia language examples,
tutorial BASIC, and runnable IL samples.

### Performance Baselines

`scripts/benchmark.sh` stores JSONL runs in `misc/benchmarks/results.jsonl`; checked
baselines live in `misc/benchmarks/baseline.jsonl`.

```bash
./scripts/benchmark.sh --viper-only
./scripts/benchmark_compare.sh
./scripts/benchmark_compare.sh --self-test
```

Use `--viper-only` for the canonical local regression lane when external language
toolchains are not relevant. `benchmark_compare.sh` compares only common
program/mode pairs and fails on Viper-mode regressions above the configured threshold.
Refresh baselines with `benchmark.sh --set-baseline` only after reviewing the measured
delta and host metadata in the JSONL output.

### Threading Model Invariants

The tests verify these invariants from `docs/vm.md`:

1. **Single-threaded per VM**: Each VM instance processes instructions on one thread at a time
2. **Thread-local binding**: `tlsActiveVM` holds the active VM for the current thread
3. **Nesting allowed**: Same VM can be bound multiple times (recursive callbacks)
4. **Different VM forbidden**: Attempting to bind a different VM on the same thread triggers assertion failure (debug
   builds)
5. **Clean state**: After VM::run() completes, thread-local state is cleared

### Defined vs Undefined Threaded Programs

Viper’s VM/native determinism guarantee applies to **defined** threaded programs: shared mutable state must be accessed
via `Viper.Threads.Monitor` (or the `Viper.Threads.Safe*` wrappers). Programs with data races are **undefined** (VM and
native are not required to match).

---

## Golden File Update Workflow

When you intentionally change compiler diagnostics, optimizer output, or IL format:

```bash
# Update all failing golden files
./scripts/update_goldens.sh

# Update only optimizer golden files
./scripts/update_goldens.sh il_opt

# Update only BASIC error golden files
./scripts/update_goldens.sh basic_error
```

The script runs each golden test, skips those already passing, and re-runs failures with
`UPDATE_GOLDEN=1` to overwrite expected output files with actual output.

## Fuzz Testing

Fuzz harnesses are in `src/tests/fuzz/` (requires `VIPER_ENABLE_FUZZ=ON`). Each harness
has a committed seed corpus under `src/tests/fuzz/corpus/<target-without-fuzz-prefix>/`.

Two cadences are supported:

- **Replay:** fuzz-enabled CMake builds register `<target>_replay` CTests over committed
  corpora, labelled `fuzz`.
- **Exploration:** `scripts/fuzz_smoke.sh` builds every discovered fuzzer and time-boxes
  mutation over each corpus.

```bash
./scripts/fuzz_smoke.sh --self-test
./scripts/fuzz_smoke.sh --list
VIPER_FUZZ_SECONDS=10 ./scripts/fuzz_smoke.sh

cmake -S . -B build-fuzz -DVIPER_ENABLE_FUZZ=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-fuzz --target fuzz_zia_parser
ctest --test-dir build-fuzz -L fuzz --output-on-failure
```

When a new parser, wire format, or protocol surface is added, add a harness plus a small
minimized corpus before treating the surface as covered. New crashes should be minimized
and checked into the relevant corpus directory.

## Source-Health Audit

`scripts/source_health_audit.sh` is a local structural guardrail for
high-ownership subsystems. It tracks 35 source-backed risk and coverage counters
across runtime surface policy, VM duplication and callback gaps, backend
unsupported paths, graphics-disabled stubs, fuzz corpus coverage, platform
policy debt, large files, manual allocation hotspots, machine-readable tooling,
MCP/LSP server coverage, ViperIDE capability gates, debugger protocol coverage,
and packaging verification.

```bash
scripts/source_health_audit.sh --summary
scripts/source_health_audit.sh --check
ctest --test-dir build -R source_health_audit --output-on-failure
ctest --test-dir build -L audit --output-on-failure
```

The check compares current values against
`scripts/source_health_baseline.tsv`. Debt metrics should move down over time.
Coverage/scaffolding metrics should not drop. See
[Source Health Guardrails](source-health.md).

## Future Work

- Reusable DifferentialTestFixture for VM vs native comparisons
- Test consolidation: merge 196 standalone runtime tests into ~30 themed files
- LoopRotate, GVN, DSE, LICM edge-case tests
- x86_64 codegen parity with AArch64 test coverage
- Zia diagnostic golden test directory
