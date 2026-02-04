---
status: active
audience: developers
last-verified: 2026-01-09
---

# Testing Guide

This document describes the testing infrastructure for the Viper compiler stack, with a focus on property-based
differential testing between execution backends.

## Test Categories

### Unit Tests

Located in `src/tests/unit/`. Test individual components in isolation:

- IL core types and operations
- Verifier checks
- VM opcode semantics
- Codegen utilities

### Golden Tests

Located in `src/tests/golden/`. Test textual stability of outputs:

- IL serialization format
- Zia/BASIC compiler output
- Diagnostic messages

### End-to-End Tests

Located in `src/tests/e2e/`. Test complete pipelines:

- Zia programs compiled and executed
- BASIC programs compiled and executed
- IL programs through VM and native backends

### Differential Tests

Located in `src/tests/unit/codegen/`. Verify that VM and native backends produce identical results for the same IL
programs.

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
| `includeControlFlow` | true    | Include branches (not yet implemented) |
| `minConstant`        | -1000   | Minimum constant value                 |
| `maxConstant`        | 1000    | Maximum constant value                 |

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

## Adding New Tests

### Unit Test Template

```cpp
#include "tests/TestHarness.hpp"

TEST(MySuite, MyTest) {
    // Arrange
    // Act
    // Assert
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
```

### Differential Test Template

For new differential tests, extend the pattern in `test_diff_vm_native_property.cpp`:

1. Create a test case with specific `ILGeneratorConfig`
2. Use `runDifferentialTest()` helper
3. Check `result.passed` and report `result.errorMessage` on failure

---

## Platform Support

- **VM**: All platforms
- **AArch64 Native**: macOS on Apple Silicon (tested)
- **x86_64 Native**: Implemented but experimental

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

```
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

```
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

## Future Work

- Add control flow (branches) to IL generator
- Add floating-point operations
- Add string and array operations
- CLI fuzzing tool for continuous differential testing
- Exception/trap handling tests for concurrency
