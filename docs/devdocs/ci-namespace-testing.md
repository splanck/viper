---
status: active
audience: contributors
last-updated: 2025-11-12
---

# Namespace Feature CI Testing Guide

This document describes the continuous integration (CI) gates and testing procedures for the Track A namespace
implementation.

## Overview

The namespace feature includes comprehensive testing at multiple levels to prevent regressions and ensure deterministic
UX:

- **11 unit tests** - Parser, semantic analysis, lowering, diagnostics
- **11 golden tests** - Positive flows (4) and error cases (7)
- **1 e2e test** - Multi-file compilation (currently disabled)
- **1 example program** - Runnable demonstration
- **Policy enforcement** - Reserved namespace checks
- **Sanitizer testing** - ASan and UBSan builds

## Quick Start

### Run all namespace tests

```bash
./scripts/ci_namespace_tests.sh
```

This runs:

1. Reserved namespace policy check
2. All 11 unit tests
3. All 11 golden tests
4. E2E test (if enabled)
5. Example compilation

Exit code 0 means all checks passed.

### Run with sanitizers

```bash
./scripts/ci_sanitizer_tests.sh
```

This creates separate builds with AddressSanitizer and UndefinedBehaviorSanitizer, then runs the full namespace test
suite with each.

**Requirements**: Clang compiler (sanitizers work best with Clang)

### Check reserved namespace policy

```bash
./scripts/check_reserved_namespaces.sh
```

Ensures user-facing code (tests/golden/basic, examples/basic) does not use the reserved `Viper` root namespace.

## Test Inventory

### Unit Tests (11 total)

Located in `tests/unit/`:

| Test                        | Purpose                                      |
|-----------------------------|----------------------------------------------|
| test_basic_parse_namespace  | NAMESPACE syntax parsing                     |
| test_basic_parse_using      | USING directive parsing                      |
| test_namespace_registry     | Namespace registration and lookup            |
| test_using_context          | USING directive management                   |
| test_type_resolver          | Type resolution with namespaces              |
| test_using_semantics        | USING semantic analysis                      |
| test_ns_resolve_pass        | Name resolution pass                         |
| test_lowerer_namespace      | IL lowering with namespaces                  |
| test_namespace_diagnostics  | All 9 error codes                            |
| test_namespace_integration  | End-to-end integration                       |
| test_using_compiletime_only | **NEW**: USING produces no runtime artifacts |

Run individually:

```bash
ctest -R test_namespace_registry --output-on-failure
```

### Golden Tests (11 total)

Located in `tests/golden/basic/` and `tests/golden/basic_errors/`:

**Positive flows (4):**

- namespace_simple.bas - Basic namespace declaration
- namespace_using.bas - USING directive examples
- namespace_inheritance.bas - Cross-namespace inheritance
- viper_root_example.bas - Illustrative Track B example

**Error cases (7):**

- namespace_notfound.bas → E_NS_001
- namespace_ambiguous.bas → E_NS_003
- namespace_duplicate_alias.bas → E_NS_004 (via E_NS_001)
- using_in_namespace.bas → E_NS_008
- using_after_decl.bas → E_NS_005
- reserved_root_user_decl.bas → E_NS_009
- reserved_root_user_using.bas → E_NS_009

Run individually:

```bash
ctest -R golden_basic_namespace_simple --output-on-failure
```

### E2E Test (1, disabled)

Located in `tests/e2e/test_namespace_e2e.cpp`:

Tests multi-file compilation with:

- Two-file base/derived with USING
- Three-file alias usage
- Multi-file ambiguity detection
- File-scoped USING isolation

**Status**: Infrastructure complete but disabled due to cross-compilation-unit type resolution issues.

### Example Program

Located in `examples/basic/namespace_demo.bas`:

Demonstrates:

- Nested namespace declarations
- Merged namespaces (multiple blocks)
- USING directives
- Cross-namespace inheritance
- Multi-level nesting
- Case-insensitive lookups

Compile with:

```bash
./build/src/tools/ilc/ilc front basic -emit-il examples/basic/namespace_demo.bas
```

## CI Integration

### For GitHub Actions / GitLab CI

Add to your pipeline:

```yaml
test-namespaces:
  script:
    - cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    - cmake --build build
    - ./scripts/ci_namespace_tests.sh

test-namespaces-sanitizers:
  script:
    - export CXX=clang++
    - ./scripts/ci_sanitizer_tests.sh
  only:
    - main
    - merge_requests
```

### For Jenkins / Other CI

```groovy
stage('Namespace Tests') {
    steps {
        sh 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug'
        sh 'cmake --build build'
        sh './scripts/ci_namespace_tests.sh'
    }
}

stage('Sanitizer Tests') {
    environment {
        CXX = 'clang++'
    }
    steps {
        sh './scripts/ci_sanitizer_tests.sh'
    }
}
```

## Policy Checks

### Reserved Namespace Enforcement

The script `check_reserved_namespaces.sh` prevents drift from the reserved namespace policy:

**Rule**: User code cannot use `NAMESPACE Viper` or `USING Viper` (root).

**Checked directories**:

- tests/golden/basic/
- tests/e2e/
- examples/basic/

**Allowed exceptions** (documented in script):

- tests/golden/basic/viper_root_example.bas - Track B illustration
- tests/golden/basic_errors/reserved_root_user_decl.bas - E_NS_009 test
- tests/golden/basic_errors/reserved_root_user_using.bas - E_NS_009 test

To add an exception:

```bash
# Edit scripts/check_reserved_namespaces.sh
ALLOWED_FILES=(
  "tests/golden/basic/viper_root_example.bas"
  "path/to/your/new/exception.bas"
)
```

### USING Compile-Time Verification

The test `test_using_compiletime_only` ensures USING directives produce no runtime artifacts:

**Checks**:

- IL size delta < 200 bytes vs empty program
- No additional function definitions
- No additional type definitions
- USING keyword does not appear in IL

**Why this matters**: USING is a compile-time directive. If it generates runtime code, something is wrong with the
implementation.

## Sanitizer Testing

### AddressSanitizer (ASan)

Detects:

- Use-after-free
- Heap buffer overflow
- Stack buffer overflow
- Memory leaks

Enable manually:

```bash
cmake -S . -B build_asan -DIL_SANITIZE_ADDRESS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_asan
BUILD_DIR=build_asan ./scripts/ci_namespace_tests.sh
```

### UndefinedBehaviorSanitizer (UBSan)

Detects:

- Integer overflow
- Null pointer dereference
- Division by zero
- Alignment violations

Enable manually:

```bash
cmake -S . -B build_ubsan -DIL_SANITIZE_UNDEFINED=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_ubsan
BUILD_DIR=build_ubsan ./scripts/ci_namespace_tests.sh
```

**Note**: Sanitizer tests take longer to run (~3x normal test time).

## Debugging Test Failures

### Unit test failure

```bash
# Run specific test with verbose output
ctest -R test_namespace_diagnostics --output-on-failure -V

# Or run the test binary directly
./build/tests/unit/test_namespace_diagnostics
```

### Golden test failure

```bash
# Run with verbose output to see diff
ctest -R golden_basic_namespace_simple --output-on-failure -V

# Check actual vs expected output
cat build/tests/golden/basic_namespace_simple.bas.out
cat tests/golden/basic/namespace_simple.stdout
```

### Policy check failure

```bash
# See which files violate the policy
./scripts/check_reserved_namespaces.sh

# Fix by either:
# 1. Remove NAMESPACE Viper / USING Viper from the file
# 2. Add file to ALLOWED_FILES in the script (if intentional)
```

### Example compilation failure

```bash
# Compile manually to see errors
./build/src/tools/ilc/ilc front basic -emit-il examples/basic/namespace_demo.bas
```

## Performance Benchmarking

The namespace tests are designed to run quickly:

| Test Category | Count | Typical Time |
|---------------|-------|--------------|
| Unit tests    | 11    | ~0.1 sec     |
| Golden tests  | 11    | ~0.2 sec     |
| E2E test      | 1     | ~0.01 sec    |
| Total         | 23    | ~0.3 sec     |

**With sanitizers**: ~1.0 sec (ASan) + ~1.0 sec (UBSan)

## Test Coverage Summary

The namespace test suite covers:

- ✅ Parser coverage: NAMESPACE and USING syntax
- ✅ Semantic coverage: All 9 error codes (E_NS_001 through E_NS_009)
- ✅ Type resolution: Current namespace, parents, USING imports, global
- ✅ Lowering: Fully-qualified IL type names, no USING artifacts
- ✅ Case insensitivity: All namespace/type lookups
- ✅ Reserved namespace: E_NS_009 enforcement
- ✅ Multi-file: AST merging (infrastructure, disabled pending fixes)
- ✅ Examples: Runnable demonstration compiles successfully

**Not yet covered**:

- ⏸️ Multi-file type resolution (e2e test disabled)
- ⏸️ Namespace alias in single-file scenarios (triggers false E_NS_004)

## Maintenance

### Adding new tests

1. Create test file in appropriate directory
2. Add to CMakeLists.txt:
   ```cmake
   viper_add_test(test_new_namespace_feature ${VIPER_TESTS_DIR}/unit/test_new_namespace_feature.cpp)
   target_link_libraries(test_new_namespace_feature PRIVATE fe_basic)
   viper_add_ctest(test_new_namespace_feature test_new_namespace_feature)
   ```
3. Add to `ci_namespace_tests.sh` UNIT_TESTS array (if unit test)
4. Verify with `./scripts/ci_namespace_tests.sh`

### Updating golden tests

1. Modify test file in `tests/golden/basic/` or `tests/golden/basic_errors/`
2. Update corresponding `.stdout` or `.stderr` file if needed
3. Re-run: `ctest -R golden_basic_<testname> --output-on-failure`

### Modifying policy checks

Edit `scripts/check_reserved_namespaces.sh`:

- Add/remove checked directories in `CHECK_DIRS`
- Add exceptions to `ALLOWED_FILES`
- Adjust regex patterns if needed

## Troubleshooting

**Q: Sanitizer tests fail with "Sanitizers require Clang compiler"**
A: Set `CXX=clang++` before running: `CXX=clang++ ./scripts/ci_sanitizer_tests.sh`

**Q: E2E test always shows as "Not Run (Disabled)"**
A: This is expected. The test infrastructure is complete but disabled pending multi-file type resolution fixes. See
tests/e2e/test_namespace_e2e.cpp:367 for the TODO.

**Q: Reserved namespace check fails on viper_root_example.bas**
A: This file is an allowed exception (it's illustrative for Track B). Make sure it's in the ALLOWED_FILES list in
check_reserved_namespaces.sh.

**Q: USING compile-time test fails with large delta**
A: The test allows up to 200 bytes difference. If you've changed the IL serialization format, you may need to adjust the
threshold or investigate why USING is generating code.

## Related Documentation

- [docs/namespaces.md](namespaces.md) - Complete namespace reference
- [docs/grammar.md](grammar.md) - NAMESPACE and USING grammar
- [docs/basic-language.md](../basic-language.md) - Tutorial section on namespaces
- [examples/basic/namespace_demo.bas](../../examples/basic/namespace_demo.bas) - Runnable example

## Summary

The namespace CI infrastructure provides:

✅ **Comprehensive coverage** - 23 tests across 4 categories
✅ **Policy enforcement** - Reserved namespace checks
✅ **Deterministic UX** - Golden tests lock error messages
✅ **Memory safety** - ASan/UBSan validation
✅ **Fast feedback** - Sub-second test execution
✅ **Easy integration** - Single-script CI entry points

Run `./scripts/ci_namespace_tests.sh` before every commit touching namespace code!
