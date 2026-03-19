# Phase 8: Testing Strategy

## Purpose

Comprehensive testing plan to verify that the native assembler produces correct object files
that link and run identically to the system-assembler path. Testing must cover:
- Instruction encoding correctness (byte-level)
- Object file format validity (structural)
- End-to-end equivalence (behavioral)
- Demo compilation and linking (all 11 demos from `build_demos.sh`)
- Regression protection via ctest for every test

---

## 1. Test Categories

### 1A. Instruction Encoding Tests (Unit)

**Approach:** For each MIR opcode, encode via our binary encoder AND emit via the text
AsmEmitter, assemble the text with `cc -c`, extract the `.text` bytes, and compare.

**x86_64 — 49 encoding rows:**

```
For each encoding row in kEncodingTable:
  1. Construct MInstr with representative operands
  2. binaryEncode(instr) → vector<uint8_t> actual
  3. textEmit(instr) → string asmText
  4. Write asmText to temp.s
  5. Run: cc -c temp.s -o temp.o
  6. Extract .text bytes from temp.o (via our ELF/Mach-O reader or objdump)
  7. ASSERT actual == expected
```

**Test operand coverage per encoding row:**
- Reg-Reg: test with both low regs (RAX, RBX) and high regs (R8-R15, XMM8-15)
- Reg-Imm: test with imm8 range (-128..127) and imm32 range
- Memory: test with base-only, base+disp8, base+disp32, base+index*scale+disp
- Memory edge cases: RSP as base, RBP as base with disp=0, R12 as base, R13 as base
- Branches: forward reference, backward reference
- Condition codes: all 14 codes for JCC and SETcc

**AArch64 — ~42 dispatch cases:**

Same approach, but simpler since every instruction is exactly 4 bytes. The comparison is
a single `uint32_t` per instruction.

**AArch64-specific test cases:**
- Prologue/epilogue synthesis: verify STP/SUB SP/ADD SP/LDP/RET byte sequences match the
  text emitter's output for leaf vs non-leaf functions, various frame sizes
- Main function init: verify `bl rt_legacy_context` + `bl rt_set_current_context` injection
- Large immediate materialization: MOVZ+MOVK sequences for 64-bit constants
- FMovRI: verify FP8 immediate encoding vs literal pool fallback for non-FP8 values

**Estimated test count:** ~80 tests (49 x86_64 + ~30 AArch64 instruction groups)

### 1B. Object File Format Tests (Structural)

**Approach:** Write a `.o` file via our writer, then parse it back and verify all structures.

**ELF verification tests:**
- Header magic, class, data encoding, machine type
- Section count, section header string table index
- .text section: correct flags (ALLOC|EXECINSTR), data matches input bytes
- .rodata section: correct flags (ALLOC), data matches
- .symtab: null entry at index 0, correct binding/type for each symbol
- .strtab: correct name offsets, NUL-terminated strings
- .rela.text: correct offset, type, symbol index, addend for each relocation
- .note.GNU-stack: present, empty

**Mach-O verification tests:**
- Header magic (0xFEEDFACF), cputype, filetype
- LC_SEGMENT_64: correct nsects, section names, flags
- LC_BUILD_VERSION: present with valid platform/minos
- LC_SYMTAB/LC_DYSYMTAB: correct offsets and counts
- Symbol mangling: all global symbols have underscore prefix
- Relocations: correct r_type, r_pcrel, r_length, r_extern, descending order

**PE/COFF verification tests:**
- Header Machine field, NumberOfSections
- .text section: correct Characteristics (CODE|EXEC|READ)
- Symbol names: inline (≤8 chars) and string-table-offset (>8 chars)
- String table: 4-byte size prefix correct
- Relocations: correct Type, SymbolTableIndex

**Estimated test count:** ~20 tests (structural validation per format)

### 1C. End-to-End Equivalence Tests (Behavioral)

**Approach:** Compile the same IL program twice — once via text assembly path, once via
binary path — and verify the executables produce identical output.

```
For each test program:
  1. viper codegen x64 program.il --text-asm -o text_exe
  2. viper codegen x64 program.il --native-asm -o bin_exe
  3. Run text_exe → capture stdout + exit code
  4. Run bin_exe → capture stdout + exit code
  5. ASSERT outputs match
```

**Test programs (from existing test suite):**
- Arithmetic (integer, float, overflow)
- Control flow (branches, loops, function calls)
- String operations (rodata references)
- Runtime calls (external symbol relocations)
- Multi-function programs (cross-function branches)
- Programs using all runtime components

**Estimated test count:** Full 1279-test suite run through binary path

### 1D. Golden File Tests (Determinism)

**Approach:** For a fixed set of small programs, store the `.o` file contents as golden files
(hex dump). Any change to the encoder that alters output triggers a diff.

```
For each golden file:
  1. Compile program.il → output.o via binary path
  2. Compute SHA-256 of output.o
  3. Compare against stored golden hash
  4. If different: show diff of hex dumps
```

**Purpose:** Catches unintentional encoding changes, optimization regressions, or
serialization bugs that don't affect correctness but alter binary output.

**Golden file count:** ~10 carefully chosen programs (one per instruction category)

### 1E. Round-Trip Tests (AArch64 Only)

Since AArch64 has fixed-width encoding, we can write a simple disassembler for our instruction
set and verify round-trip fidelity:

```
For each instruction:
  1. Encode instruction → uint32_t word
  2. Decode word → extract opcode, registers, immediates
  3. Verify decoded fields match original operands
```

This is optional but provides very high confidence. ~100 LOC for the mini-disassembler.

### 1F. Demo Compilation & Linking Tests (E2E)

**Approach:** Compile every demo from `build_demos.sh` using the native assembler path and
verify that the resulting binaries link successfully. This is the highest-level test — it
exercises the entire pipeline from frontend through codegen through binary encoding through
object file writing through system linker.

**All 11 demos:**

| # | Demo | Language | Project Dir | Type |
|---|------|----------|-------------|------|
| 1 | chess | BASIC | `examples/games/chess-basic` | Console |
| 2 | vtris | BASIC | `examples/games/vtris` | Graphics |
| 3 | frogger | BASIC | `examples/games/frogger-basic` | Graphics |
| 4 | centipede | BASIC | `examples/games/centipede-basic` | Graphics |
| 5 | pacman | BASIC | `examples/games/pacman-basic` | Graphics |
| 6 | paint | Zia | `examples/apps/paint` | Graphics |
| 7 | viperide | Zia | `examples/apps/viperide` | Graphics |
| 8 | pacman-zia | Zia | `examples/games/pacman` | Graphics |
| 9 | sqldb | Zia | `examples/apps/sqldb` | Console |
| 10 | chess-zia | Zia | `examples/games/chess` | Graphics |
| 11 | sidescroller | Zia | `examples/games/sidescroller` | Graphics |

**Test CMake script: `test_native_asm_demo_build.cmake`**

Each demo test follows this pattern:

```cmake
cmake_minimum_required(VERSION 3.16)

# Required variables: ILC, PROJECT_DIR, DEMO_NAME
if (NOT DEFINED ILC OR NOT DEFINED PROJECT_DIR OR NOT DEFINED DEMO_NAME)
    message(FATAL_ERROR "ILC, PROJECT_DIR, and DEMO_NAME must be set")
endif ()

set(EXE_FILE "/tmp/test_nativeasm_${DEMO_NAME}")

# Build using native assembler path
message(STATUS "Building ${DEMO_NAME} with --native-asm...")
execute_process(
    COMMAND ${ILC} build ${PROJECT_DIR} --native-asm -o ${EXE_FILE}
    RESULT_VARIABLE RESULT
    ERROR_VARIABLE STDERR
    TIMEOUT 120
)
if (NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build ${DEMO_NAME} with --native-asm:\n${STDERR}")
endif ()

# Verify executable was created
if (NOT EXISTS ${EXE_FILE})
    message(FATAL_ERROR "Executable ${EXE_FILE} was not created")
endif ()

message(STATUS "${DEMO_NAME} built successfully with native assembler: ${EXE_FILE}")

# Cleanup
file(REMOVE ${EXE_FILE})
```

**Test CMake script: `test_native_asm_demo_equiv.cmake`**

For console demos (chess, sqldb), we can compare output between text-asm and native-asm paths:

```cmake
cmake_minimum_required(VERSION 3.16)

# Required variables: ILC, PROJECT_DIR, DEMO_NAME, GOLDEN (optional)
if (NOT DEFINED ILC OR NOT DEFINED PROJECT_DIR OR NOT DEFINED DEMO_NAME)
    message(FATAL_ERROR "ILC, PROJECT_DIR, and DEMO_NAME must be set")
endif ()

set(TEXT_EXE "/tmp/test_textasm_${DEMO_NAME}")
set(NATIVE_EXE "/tmp/test_nativeasm_${DEMO_NAME}")

# Build with text assembler
message(STATUS "Building ${DEMO_NAME} with --text-asm...")
execute_process(
    COMMAND ${ILC} build ${PROJECT_DIR} --text-asm -o ${TEXT_EXE}
    RESULT_VARIABLE RESULT
    ERROR_VARIABLE STDERR
    TIMEOUT 120
)
if (NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build ${DEMO_NAME} with --text-asm:\n${STDERR}")
endif ()

# Build with native assembler
message(STATUS "Building ${DEMO_NAME} with --native-asm...")
execute_process(
    COMMAND ${ILC} build ${PROJECT_DIR} --native-asm -o ${NATIVE_EXE}
    RESULT_VARIABLE RESULT
    ERROR_VARIABLE STDERR
    TIMEOUT 120
)
if (NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build ${DEMO_NAME} with --native-asm:\n${STDERR}")
endif ()

# Both executables should exist
if (NOT EXISTS ${TEXT_EXE})
    message(FATAL_ERROR "Text-asm executable was not created")
endif ()
if (NOT EXISTS ${NATIVE_EXE})
    message(FATAL_ERROR "Native-asm executable was not created")
endif ()

message(STATUS "${DEMO_NAME}: both text-asm and native-asm binaries built successfully")

# Cleanup
file(REMOVE ${TEXT_EXE} ${NATIVE_EXE})
```

**CMakeLists.txt registration (all 11 demos):**

```cmake
# --- Native Assembler Demo Tests ---
# Tests that all demos compile and link with the native assembler.
# Registered unconditionally — the cmake scripts detect host arch.
# Uses `viper build <project_dir> --native-asm -o <exe>` path.

set(_NASM_E2E_DIR ${CMAKE_CURRENT_LIST_DIR})
set(_NASM_BUILD_SCRIPT ${_NASM_E2E_DIR}/test_native_asm_demo_build.cmake)
set(_NASM_EQUIV_SCRIPT ${_NASM_E2E_DIR}/test_native_asm_demo_equiv.cmake)

# BASIC demos
viper_add_ctest(nativeasm_chess
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/games/chess-basic
    -DDEMO_NAME=chess
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_chess PROPERTIES LABELS "NativeAsm;BASIC")

viper_add_ctest(nativeasm_vtris
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/games/vtris
    -DDEMO_NAME=vtris
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_vtris PROPERTIES LABELS "NativeAsm;BASIC")

viper_add_ctest(nativeasm_frogger
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/games/frogger-basic
    -DDEMO_NAME=frogger
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_frogger PROPERTIES LABELS "NativeAsm;BASIC")

viper_add_ctest(nativeasm_centipede
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/games/centipede-basic
    -DDEMO_NAME=centipede
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_centipede PROPERTIES LABELS "NativeAsm;BASIC")

viper_add_ctest(nativeasm_pacman
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/games/pacman-basic
    -DDEMO_NAME=pacman
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_pacman PROPERTIES LABELS "NativeAsm;BASIC")

# Zia demos
viper_add_ctest(nativeasm_paint
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/apps/paint
    -DDEMO_NAME=paint
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_paint PROPERTIES LABELS "NativeAsm;Zia")

viper_add_ctest(nativeasm_viperide
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/apps/viperide
    -DDEMO_NAME=viperide
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_viperide PROPERTIES LABELS "NativeAsm;Zia")

viper_add_ctest(nativeasm_pacman_zia
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/games/pacman
    -DDEMO_NAME=pacman-zia
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_pacman_zia PROPERTIES LABELS "NativeAsm;Zia")

viper_add_ctest(nativeasm_sqldb
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/apps/sqldb
    -DDEMO_NAME=sqldb
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_sqldb PROPERTIES LABELS "NativeAsm;Zia")

viper_add_ctest(nativeasm_chess_zia
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/games/chess
    -DDEMO_NAME=chess-zia
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_chess_zia PROPERTIES LABELS "NativeAsm;Zia")

viper_add_ctest(nativeasm_sidescroller
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/games/sidescroller
    -DDEMO_NAME=sidescroller
    -P ${_NASM_BUILD_SCRIPT})
set_tests_properties(nativeasm_sidescroller PROPERTIES LABELS "NativeAsm;Zia")

# Dual-path equivalence tests (build both --text-asm and --native-asm, verify both link)
viper_add_ctest(nativeasm_equiv_chess
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/games/chess-basic
    -DDEMO_NAME=chess
    -P ${_NASM_EQUIV_SCRIPT})
set_tests_properties(nativeasm_equiv_chess PROPERTIES LABELS "NativeAsm;Equiv")

viper_add_ctest(nativeasm_equiv_sqldb
    ${CMAKE_COMMAND}
    -DILC=$<TARGET_FILE:viper>
    -DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/apps/sqldb
    -DDEMO_NAME=sqldb
    -P ${_NASM_EQUIV_SCRIPT})
set_tests_properties(nativeasm_equiv_sqldb PROPERTIES LABELS "NativeAsm;Equiv")
```

**Label convention:** All native assembler demo tests use label `NativeAsm` so they can be
run as a group: `ctest -L NativeAsm`.

**TestHelpers.cmake update:** Add a label pattern for native assembler tests:

```cmake
elseif (name MATCHES "^nativeasm_")
    set_tests_properties(${name} PROPERTIES LABELS NativeAsm)
```

**Estimated test count:** 11 build tests + 2 equivalence tests = 13 demo tests

### 1G. Full Test Suite via Native Assembler (Regression)

**Approach:** Run the entire existing test suite with `--native-asm` as the default path.
This is not a new test file — it's a CI/development validation mode.

Once the native assembler is feature-complete (after Phase 7), we verify:

```bash
# All existing tests must still pass when native assembler is the default
./scripts/build_viper.sh        # builds with --native-asm as default
ctest --test-dir build --output-on-failure

# Explicit dual-path validation for codegen tests
ctest --test-dir build -L NativeAsm --output-on-failure
```

The existing `codegen_native_run` test (which runs IL→codegen→assemble→link→execute) will
automatically exercise the native assembler once `--native-asm` becomes the default.

---

## 2. Test Infrastructure

### Helper: extractTextBytes()

A utility function that reads a `.o` file (ELF or Mach-O) and extracts the `.text` section
bytes. This is used by instruction encoding tests to get reference bytes from the system
assembler.

```cpp
/// Extract the .text section bytes from an object file produced by the system assembler.
/// Supports ELF and Mach-O formats (auto-detected by magic bytes).
std::vector<uint8_t> extractTextBytes(const std::string& objPath);
```

**Implementation:** Read the file header, find the .text section, return its raw bytes.
This is much simpler than a full object file parser — it only needs to find one section.

### Helper: assembleText()

A utility that writes assembly text to a temp file, invokes `cc -c`, and returns the object
file path.

```cpp
/// Assemble text assembly to a .o file using the system assembler.
/// Returns the path to the generated .o file.
std::string assembleText(const std::string& asmText, ObjArch arch);
```

### Helper: extractSectionBytes() (for COFF)

Same as `extractTextBytes()` but for PE/COFF `.obj` files on Windows:

```cpp
/// Extract a named section from a COFF .obj file.
/// Returns empty vector if section not found.
std::vector<uint8_t> extractCoffSectionBytes(const std::string& objPath,
                                              const std::string& sectionName);
```

### Test Registration

Following the existing pattern in `src/tests/CMakeLists.txt`, using `viper_add_test` and
`viper_add_ctest` macros from `src/tests/cmake/TestHelpers.cmake`:

```cmake
# Unit tests — C++ executables using viper_testing framework
viper_add_test(test_x64_binary_encoder ${DIR}/TestX64BinaryEncoder.cpp)
target_link_libraries(test_x64_binary_encoder PRIVATE codegen_x64 objfile viper_test_common)

viper_add_test(test_a64_binary_encoder ${DIR}/TestA64BinaryEncoder.cpp)
target_link_libraries(test_a64_binary_encoder PRIVATE codegen_aarch64 objfile viper_test_common)

viper_add_test(test_elf_writer ${DIR}/TestElfWriter.cpp)
target_link_libraries(test_elf_writer PRIVATE objfile viper_test_common)

viper_add_test(test_macho_writer ${DIR}/TestMachOWriter.cpp)
target_link_libraries(test_macho_writer PRIVATE objfile viper_test_common)

viper_add_test(test_coff_writer ${DIR}/TestCoffWriter.cpp)
target_link_libraries(test_coff_writer PRIVATE objfile viper_test_common)

viper_add_test(test_code_section ${DIR}/TestCodeSection.cpp)
target_link_libraries(test_code_section PRIVATE objfile viper_test_common)

viper_add_test(test_string_table ${DIR}/TestStringTable.cpp)
target_link_libraries(test_string_table PRIVATE objfile viper_test_common)

viper_add_test(test_symbol_table ${DIR}/TestSymbolTable.cpp)
target_link_libraries(test_symbol_table PRIVATE objfile viper_test_common)

# Demo build tests — cmake script-based (see 1F above)
# Registered via viper_add_ctest with -P <script>.cmake pattern
```

**Note:** The `viper_add_test` macro automatically registers with ctest (unless NO_CTEST
is specified), so all unit tests are automatically regression-protected.

### Test File Organization

```
src/tests/codegen/objfile/           # New test directory
    TestCodeSection.cpp               # CodeSection unit tests
    TestStringTable.cpp               # StringTable unit tests
    TestSymbolTable.cpp               # SymbolTable unit tests
    TestX64BinaryEncoder.cpp          # x86_64 instruction encoding tests
    TestA64BinaryEncoder.cpp          # AArch64 instruction encoding tests
    TestElfWriter.cpp                 # ELF format structural + E2E tests
    TestMachOWriter.cpp               # Mach-O format structural + E2E tests
    TestCoffWriter.cpp                # PE/COFF format structural + E2E tests
    TestHelpers.hpp                   # extractTextBytes(), assembleText() helpers
    TestHelpers.cpp

src/tests/e2e/
    test_native_asm_demo_build.cmake  # Demo compilation via --native-asm
    test_native_asm_demo_equiv.cmake  # Dual-path equivalence comparison
```

---

## 3. CI Integration

### Phase 1: Parallel Validation

During development, run both paths in CI and compare:
```
viper codegen x64 test.il --text-asm -o text.exe
viper codegen x64 test.il --native-asm -o bin.exe
diff <(./text.exe) <(./bin.exe)
```

### Phase 2: Default Switch

Once all tests pass with the binary path:
1. Make `--native-asm` the default
2. Keep `--text-asm` as fallback
3. CI runs both paths for continued validation
4. All demo tests run with both paths

### Phase 3: Deprecation (Future)

Eventually, the text assembly path becomes debug-only (`-S` flag).
The binary path is the default and only production path.

---

## 4. Estimated Test LOC

| Test File | Tests | LOC |
|-----------|-------|-----|
| TestCodeSection.cpp | ~12 (emit, align, patch, reloc) | ~150 |
| TestStringTable.cpp | ~8 (add, dedup, find, layout) | ~100 |
| TestSymbolTable.cpp | ~8 (add, findOrAdd, index) | ~100 |
| TestX64BinaryEncoder.cpp | ~55 (49 rows + edge cases) | ~500 |
| TestA64BinaryEncoder.cpp | ~35 (instruction groups + edge cases) | ~350 |
| TestElfWriter.cpp | ~8 (structural + E2E) | ~200 |
| TestMachOWriter.cpp | ~8 (structural + E2E) | ~200 |
| TestCoffWriter.cpp | ~8 (structural + E2E) | ~200 |
| Test helpers (extractTextBytes, assembleText) | — | ~200 |
| test_native_asm_demo_build.cmake | 11 demo tests | ~50 |
| test_native_asm_demo_equiv.cmake | 2 equivalence tests | ~60 |
| CMakeLists.txt additions | 13 registrations | ~80 |
| **Total** | **~145+** | **~2,190** |

---

## 5. Risk Mitigation Through Testing

| Risk | Test Coverage |
|------|--------------|
| x86_64 ModR/M/SIB edge cases | Dedicated tests for RSP/RBP/R12/R13 as base register |
| AArch64 branch offset calculation | Forward/backward branch tests with known offsets |
| Mach-O ld64 rejection | Link test on macOS (our .o → ld64 → executable → run) |
| ELF linker rejection | Link test on Linux (our .o → ld → executable → run) |
| PE/COFF link.exe rejection | Link test on Windows (our .obj → link.exe → .exe → run) |
| Relocation correctness | External call tests (rt_* functions) |
| Symbol mangling (Mach-O underscore) | Explicit name comparison tests |
| Deterministic output | Golden file tests with SHA-256 |
| Real-world code (demos) | All 11 demos from build_demos.sh |
| Frontend diversity (BASIC + Zia) | 5 BASIC + 6 Zia demo tests |
| Graphics subsystem integration | Graphics demos test canvas/sprite runtime calls |
| Dual-path parity | Equivalence tests compare --text-asm vs --native-asm |

---

## 6. Complete Test Inventory

### By ctest label:

| Label | Tests | Description |
|-------|-------|-------------|
| `codegen` | 8 | Unit tests for encoders + infrastructure |
| `NativeAsm` | 13 | Demo build + equivalence tests |
| `NativeX64Run` | 1 | Existing codegen native run (updated) |
| `ARM64;NativeLink` | 4 | Existing ARM64 demo link tests |

### By phase when tests are implemented:

| Phase | Tests Added | Running Total |
|-------|-------------|---------------|
| 1 (Infrastructure) | TestCodeSection, TestStringTable, TestSymbolTable | 3 files |
| 2 (x86_64 encoder) | TestX64BinaryEncoder | 4 files |
| 3 (AArch64 encoder) | TestA64BinaryEncoder | 5 files |
| 4 (ELF writer) | TestElfWriter | 6 files |
| 5 (Mach-O writer) | TestMachOWriter | 7 files |
| 6 (PE/COFF writer) | TestCoffWriter | 8 files |
| 7 (Pipeline integration) | 11 demo build + 2 equiv tests | 8 files + 13 ctest entries |

### All tests are ctest-registered:

Every test is registered via `viper_add_test` (C++ unit tests) or `viper_add_ctest` (cmake
script tests), ensuring they run as part of the default `ctest` invocation. This means:

- `./scripts/build_viper.sh` exercises all tests including native assembler
- `ctest -L NativeAsm` runs only the demo compilation tests
- `ctest -L codegen` includes the encoder unit tests
- Any regression in the native assembler path is caught automatically
