---
status: active
audience: developers
last-verified: 2026-04-09
---

# Testing Guide

This document describes the testing infrastructure for the Viper compiler stack. The test suite
contains 1,393 tests across unit, golden, end-to-end, differential, conformance, and fuzz
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

# Run all tests (after building)
ctest --test-dir build

# Run tests by label
ctest --test-dir build -L codegen          # Code generation tests only
ctest --test-dir build -L golden           # Golden file tests only
ctest --test-dir build -L "vm"             # VM tests only

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
./scripts/ci_sanitizer_tests.sh
```

## Test Labels

| Label | Count | Description |
|-------|-------|-------------|
| `basic` | 95 | BASIC frontend (lexer, parser, sema, lowerer) |
| `il` | 196 | IL core (parsing, serialization, verification, analysis, transforms) |
| `vm` | 112 | VM runtime (opcodes, traps, debugging, concurrency) |
| `runtime` | 373 | C runtime library (strings, collections, I/O, math, graphics, etc.) |
| `codegen` | 125 | Code generation (x86_64, AArch64, linker, binary encoding) |
| `oop` | 31 | Object-oriented programming (classes, inheritance, interfaces) |
| `golden` | 203 | Golden file regression (diagnostic messages, IL/optimizer output) |
| `e2e` | 19 | End-to-end pipeline tests |
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
- Frontend parser, sema, lowerer for both Zia and BASIC

### Golden Tests

Located in `src/tests/golden/`. Test textual stability of outputs:

- IL serialization format
- Zia/BASIC compiler diagnostic messages
- IL optimizer transformations
- Constant folding results

Golden tests use 10 CMake runner scripts. Helper functions in `TestHelpers.cmake` reduce
registration to one line per test.

### End-to-End Tests

Located in `src/tests/e2e/`. Test complete pipelines:

- Zia programs compiled and executed
- BASIC programs compiled and executed
- IL programs through VM and native backends

### Differential Tests

Located in `src/tests/unit/codegen/`. Verify that VM and native backends produce identical results
for the same IL programs.

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

TSan detects data races at runtime. To run with TSan:

```bash
# Configure with TSan enabled
cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_C_FLAGS="-fsanitize=thread -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"

# Build
cmake --build build-tsan -j

# Run concurrency tests under TSan
./build-tsan/src/tests/test_vm_concurrency_stress
./build-tsan/src/tests/test_vm_threading_model
./build-tsan/src/tests/test_vm_runtime_concurrency

# Or run all tests under TSan (slower)
ctest --test-dir build-tsan --output-on-failure
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

Fuzz harnesses are in `src/tests/fuzz/` (requires `VIPER_ENABLE_FUZZ=ON`):

- `fuzz_zia_lexer.cpp` — Zia lexer fuzzing via libFuzzer
- `fuzz_zia_parser.cpp` — Zia parser fuzzing via libFuzzer

```bash
cmake -S . -B build-fuzz -DVIPER_ENABLE_FUZZ=ON
cmake --build build-fuzz --target fuzz_zia_lexer
./build-fuzz/src/tests/fuzz_zia_lexer corpus/ -max_len=4096
```

## Future Work

- Expand fuzz testing to BASIC lexer/parser, IL parser, IL verifier, VM
- Full-suite sanitizer CI (ASan + UBSan on all 1,393 tests)
- Reusable DifferentialTestFixture for VM vs native comparisons
- Test consolidation: merge 196 standalone runtime tests into ~30 themed files
- LoopRotate, GVN, DSE, LICM edge-case tests
- x86_64 codegen parity with AArch64 test coverage
- Zia diagnostic golden test directory
