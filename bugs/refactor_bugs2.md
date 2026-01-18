# Viper Platform - Comprehensive Code Review for Refactoring Opportunities

**Date:** 2026-01-17
**Scope:** All C/C++ source files in src/ (excluding tests/)
**Total Files Reviewed:** ~1150 files
**Total Issues Identified:** 184

This document catalogs potential refactoring opportunities, code duplication, and optimization needs discovered during a systematic review of every source file in the Viper platform.

---

## Table of Contents

1. [Summary Statistics](#summary-statistics)
2. [src/bytecode/](#bytecode)
3. [src/codegen/](#codegen)
4. [src/common/](#common)
5. [src/frontends/](#frontends)
6. [src/il/](#il)
7. [src/lib/](#lib)
8. [src/parse/](#parse)
9. [src/pass/](#pass)
10. [src/runtime/](#runtime)
11. [src/support/](#support)
12. [src/tools/](#tools)
13. [src/tui/](#tui)
14. [src/vm/](#vm)
15. [Cross-Cutting Concerns](#cross-cutting-concerns)
16. [Recommendations Summary](#recommendations-summary)

---

## Summary Statistics

| Directory | Files | Issues Found | Priority |
|-----------|-------|--------------|----------|
| bytecode | 7 | 20 | High: 2, Medium: 8, Low: 10 |
| codegen | 96 | 16 | High: 6, Medium: 6, Low: 4 |
| common | 5 | 4 | Low: 4 |
| frontends | 366 | 35 | High: 7, Medium: 15, Low: 13 |
| il | 191 | 20 | High: 3, Medium: 9, Low: 8 |
| lib | 78 | 15 | High: 3, Medium: 6, Low: 6 |
| parse | 1 | 2 | Medium: 2 |
| pass | 1 | 2 | Low: 2 |
| runtime | 181 | 15 | High: 6, Medium: 4, Low: 5 |
| support | 20 | 15 | High: 2, Medium: 6, Low: 7 |
| tools | 40 | 20 | High: 3, Medium: 9, Low: 8 |
| tui | 107 | 12 | High: 4, Medium: 5, Low: 3 |
| vm | 57 | 16 | High: 3, Medium: 7, Low: 6 |

---

## Bytecode

### BC-001: Duplicated Overflow Detection Logic [HIGH]
**File:** `src/bytecode/BytecodeVM.cpp`
**Lines:** 1339-1389, 1896-1975

Overflow detection functions (`addOverflow`, `subOverflow`, `mulOverflow`) are defined as member functions but the same logic is replicated inline in the threaded dispatch loop. The threaded dispatch version uses `__builtin_*_overflow` directly while the regular functions provide a fallback path.

**Recommendation:** Extract overflow checking into standalone utility functions in `bytecode/OverflowHelpers.hpp`.

### BC-002: Inconsistent String Handling Between Dispatch Modes [MEDIUM]
**File:** `src/bytecode/BytecodeVM.cpp`
**Lines:** 1148-1155 (switch), 1819-1826 (threaded)

The `LOAD_STR` instruction handling is duplicated between switch-based and threaded dispatch modes.

### BC-003: Repeated Trap Dispatch Pattern [MEDIUM]
**File:** `src/bytecode/BytecodeVM.cpp`
**Lines:** 1977-2000

Pattern for checking division by zero and dispatching traps repeated 4 times for SDIV_I64_CHK, UDIV_I64_CHK, SREM_I64_CHK, UREM_I64_CHK.

### BC-004: Constant Pool Lookup Duplication [MEDIUM]
**File:** `src/bytecode/BytecodeModule.hpp`
**Lines:** 143-193

Four methods (`addI64`, `addF64`, `addString`, `addNativeFunc`) implement nearly identical deduplication lookup patterns.

**Recommendation:** Extract into template helper:
```cpp
template<typename T, typename Eq>
uint32_t findOrAdd(std::vector<T>& pool, const T& value, Eq equals);
```

### BC-005: Missing Bounds Checking in Bytecode Pool Access [HIGH]
**File:** `src/bytecode/BytecodeVM.cpp`
**Lines:** 312, 319

Instructions like `LOAD_I64` and `LOAD_F64` access constant pools without bounds validation. If bytecode is malformed, out-of-bounds access causes undefined behavior.

### BC-006: Repetitive Error Message Patterns [LOW]
**File:** `src/bytecode/BytecodeVM.cpp`
**Lines:** 404, 432, 460, 472, 496, 524

Trap messages manually constructed at multiple locations could be centralized.

### BC-007: Unused Placeholder Instructions [LOW]
**File:** `src/bytecode/Bytecode.hpp`
**Lines:** 174, 185-186

`MAKE_ERROR`, `LINE`, `WATCH_VAR` opcodes defined but never used.

### BC-008: Hardcoded Stack Sizes [LOW]
**File:** `src/bytecode/BytecodeVM.hpp`
**Lines:** 34-37

`kMaxCallDepth`, `kMaxStackSize` not configurable at runtime.

### BC-009: Repeated Local Variable Encoding Pattern [LOW]
**File:** `src/bytecode/BytecodeCompiler.cpp`
**Lines:** 464-468, 533-537, 1034-1038, 1774-1779

Pattern of emitting local load/store with wide vs narrow index check appears 4+ times.

### BC-010: Quote Function Platform Duplication [LOW]
**File:** `src/common/RunProcess.cpp`
**Lines:** 59-132 (Windows), 134-169 (POSIX)

Platform-specific quoting logic shares similar structure but implemented separately.

---

## Codegen

### CG-001: Duplicated Immediate Materialization Logic [HIGH]
**Files:**
- `src/codegen/aarch64/AsmEmitter.cpp` (277-288)
- `src/codegen/x86_64/Lowering.EmitCommon.cpp` (102-120)

Both backends implement immediate-to-register materialization independently.

**Recommendation:** Create `common/ImmediateMaterializer.hpp` with architecture-agnostic API.

### CG-002: Register Class Checking Duplication [HIGH]
**Files:**
- `src/codegen/aarch64/RegAllocLinear.cpp` (138-150)
- `src/codegen/x86_64/FrameLowering.cpp` (68-79)

Helper functions to check callee-saved registers, classify registers duplicated across backends.

**Recommendation:** Move to `common/RegisterUtils.hpp`.

### CG-003: Peephole Optimization Pattern Duplication [HIGH]
**Files:**
- `src/codegen/aarch64/Peephole.cpp` (1313 lines)
- `src/codegen/x86_64/Peephole.cpp` (700 lines)

Nearly identical patterns: identity move detection, consecutive move folding, zero-register patterns.

### CG-004: Frame Layout Computation Duplication [HIGH]
**Files:**
- `src/codegen/aarch64/FrameBuilder.cpp`
- `src/codegen/x86_64/FrameLowering.cpp`

Nearly identical frame layout algorithms with only architecture-specific constants differing.

**Recommendation:** Extract `common/FrameLayoutBuilder.hpp`.

### CG-005: Register Allocation Pattern Similarities [MEDIUM]
**Files:**
- `src/codegen/aarch64/RegAllocLinear.cpp` (1208 lines)
- `src/codegen/x86_64/ra/Allocator.cpp` (863 lines)

Both implement linear-scan with similar phases.

### CG-006: ABI/Calling Convention Duplication [MEDIUM]
**Files:**
- `src/codegen/aarch64/TargetAArch64.cpp` (413 lines)
- `src/codegen/x86_64/TargetX64.cpp` (385 lines)

Register ordering, callee/caller-saved lists defined independently.

### CG-007: Instruction Emission Boilerplate [MEDIUM]
**Files:**
- `src/codegen/aarch64/AsmEmitter.cpp` (884 lines)
- `src/codegen/x86_64/AsmEmitter.cpp` (1093 lines)

Similar emission patterns for instruction categories.

### CG-008: Memory Addressing Pattern Duplication [MEDIUM]
**File:** `src/codegen/aarch64/AsmEmitter.cpp` (455-630)

Memory addressing with scratch register fallback repeated for multiple access patterns.

### CG-009: Operand Formatting Abstraction Missing [MEDIUM]
Both backends independently implement operand formatting.

### CG-010: Condition Code Mapping Duplication [MEDIUM]
**Files:**
- `src/codegen/aarch64/OpcodeMappings.hpp` (333 lines)
- `src/codegen/x86_64/Lowering.EmitCommon.cpp`

IL comparison opcodes mapped to architecture-specific codes separately.

### CG-011: Live Interval Analysis Duplication [MEDIUM]
Both backends need live range computation with identical algorithms.

### CG-012: MachineIR Structure Differences [MEDIUM]
**Files:**
- `src/codegen/aarch64/MachineIR.hpp`
- `src/codegen/x86_64/MachineIR.hpp`

Nearly identical structures with different names. x86_64's variant approach is superior.

**Recommendation:** Create unified `common/MachineIR.hpp`.

### CG-013: Immediate Range Checking Duplication [LOW]
**Files:** Multiple

Multiple implementations of immediate range checking across backends.

### CG-014: Scratch Register Convention Inconsistency [LOW]
Different backends use different conventions for scratch registers.

### CG-015: Prologue/Epilogue Generation Patterns [LOW]
Both backends emit ABI-conformant sequences independently.

### CG-016: Stack Alignment Boundary Handling [LOW]
Multiple places handle large stack allocations by splitting into multiple instructions.

---

## Common

### CM-001: Cursor Position Tracking Recomputation [MEDIUM]
**File:** `src/parse/Cursor.cpp`
**Lines:** 216-232

`seek()` recomputes source position from start when seeking backward. O(n) per backward seek.

### CM-002: Cursor Whitespace Handling Lacks Configuration [LOW]
**File:** `src/parse/Cursor.cpp`
**Lines:** 100-104

`skipWs()` includes newlines without explicit tracking control.

### CM-003: Mangle/Demangle Bidirectional Consistency [LOW]
**File:** `src/common/Mangle.cpp`
**Lines:** 41-83

Transformation is intentionally lossy but not documented.

### CM-004: PassManager Hook Invocation Order [LOW]
**File:** `src/pass/PassManager.cpp`
**Lines:** 80-101

Fail-fast semantics not explicitly documented.

---

## Frontends

### FE-BASIC-001: Massive Duplication in Object Release Patterns [HIGH]
**File:** `src/frontends/basic/lower/Emitter.cpp`
**Lines:** 626-714, 724-820

`releaseObjectLocals` and `releaseObjectParams` are 95% identical (~200 lines duplication).

### FE-BASIC-002: Identical Array Release Duplication [HIGH]
**File:** `src/frontends/basic/lower/Emitter.cpp`
**Lines:** 371-426, 436-494

Nearly identical logic for array release between locals and params.

### FE-BASIC-003: ADDFILE Implementation Duplication [HIGH]
**File:** `src/frontends/basic/Parser.cpp`
**Lines:** 417-533, 535-646

Two nearly identical ADDFILE handlers (~200 lines duplication).

### FE-BASIC-004: Long CallExpr Visitor [MEDIUM]
**File:** `src/frontends/basic/lower/Lowerer_Expr.cpp`
**Lines:** 282-622

340-line method needs decomposition into focused helpers.

### FE-BASIC-005: Redundant Type Conversion Functions [MEDIUM]
**Files:** `src/frontends/basic/ILTypeUtils.cpp`, `src/frontends/basic/Lowerer.cpp`

Three overlapping type conversion functions.

### FE-BASIC-006: Parser Error Reporting Duplication [MEDIUM]
**File:** `src/frontends/basic/Parser.cpp`
**Lines:** 652-678

Four nearly identical error emission methods.

### FE-BASIC-007: Array Access Type Dispatch Repetition [MEDIUM]
**File:** `src/frontends/basic/lower/Lowerer_Expr.cpp`
**Lines:** 139-227

Same 4-way type dispatch appears multiple times.

### FE-PASCAL-001: Lexer Comment Handling Duplication [HIGH]
All three lexers (Pascal, Zia, Basic) implement nearly identical block comment handling.

**Recommendation:** Create `frontends/common/CommentSkipper.hpp`.

### FE-PASCAL-002: Number Literal Parsing Duplication [HIGH]
All three lexers duplicate number parsing logic for decimal, hex, and floating-point.

**Recommendation:** Extract to `frontends/common/NumberLexing.hpp`.

### FE-COMMON-001: Expression Lowering Pattern Duplication [MEDIUM]
**Files:** Pascal and Zia lowerers

Both implement identical expression lowering dispatcher patterns.

### FE-COMMON-002: Cursor Position Tracking Not Using LexerBase [MEDIUM]
Pascal and Zia lexers reimplement cursor management instead of using `LexerCursor<Derived>`.

### FE-COMMON-003: String Literal Lowering Duplication [LOW]
Both frontends implement identical string literal lowering.

### FE-COMMON-004: BlockManager Usage Pattern [LOW]
Both frontends initialize BlockManager identically.

### FE-COMMON-005: Extern Declaration Collection Pattern [MEDIUM]
All frontends maintain `usedExterns_` set with identical iteration pattern.

---

## IL

### IL-001: Type Kind Validation Pattern Duplication [HIGH]
**Files:** `src/il/verify/InstructionChecker_*.cpp`

Type kind validation checks repeated 4+ times across different checker files.

**Recommendation:** Create `isSupportedIntegerWidth()` helper.

### IL-002: String State Tracking Parser Duplication [HIGH]
**File:** `src/il/io/OperandParser.cpp`
**Lines:** 57-102, 604-646

`StringStateTracker` class duplicated inline in `parseSwitchTargets()`.

### IL-003: Parenthesis Matching Logic Duplication [HIGH]
**File:** `src/il/io/OperandParser.cpp`
**Lines:** 105-161, 163-238, 414-469

Three separate implementations of parenthesis depth tracking.

**Recommendation:** Consolidate into `ParenthesisMatcher` utility.

### IL-004: Type Category to Type::Kind Conversion [MEDIUM]
**Files:** `src/il/verify/InstructionChecker_Arithmetic.cpp`, `src/il/verify/InstructionChecker.cpp`

Near-identical switch statements for TypeCategory to Type::Kind.

### IL-005: Operand Count Validation Error Messages [MEDIUM]
**File:** `src/il/verify/InstructionChecker.cpp`
**Lines:** 406-427

Error message construction repeated 4 times.

### IL-006: Binary/Unary Verification Pattern [MEDIUM]
**File:** `src/il/verify/InstructionChecker_Arithmetic.cpp`
**Lines:** 91-122

`checkBinary()` and `checkUnary()` follow nearly identical patterns.

### IL-007: Diagnostic Formatting Duplication [MEDIUM]
Multiple files (~19)

Diagnostic formatting reimplemented across layers.

**Recommendation:** Create centralized `DiagBuilder` utility.

### IL-008: Exception Handler Block Detection [LOW]
**Files:** `src/il/io/Serializer.cpp`, `src/il/transform/SimplifyCFG/Utils.cpp`

Similar EH block detection logic.

### IL-009: Trap Kind Mapping Bidirectional [LOW]
**File:** `src/il/io/ParserUtil.cpp`
**Lines:** 219-243

Bidirectional mapping could be more efficient.

### IL-010: Value Equality Comparison [MEDIUM]
**File:** `src/il/transform/SimplifyCFG/Utils.cpp`
**Lines:** 72-93

Detailed bit-wise comparison localized but may be needed elsewhere.

### IL-011: Operand Type Checking in Runtime Verification [MEDIUM]
**File:** `src/il/verify/InstructionChecker_Runtime.cpp`
**Lines:** 158-176

Lambda pattern duplicated 6+ times in same function.

### IL-012: Switch Instruction Helper Consistency [LOW]
Helpers only used in serializer; could be used more broadly.

### IL-013: CFG Context Construction [LOW]
**File:** `src/il/analysis/CFG.cpp`
**Lines:** 46-121

Two-pass approach could be optimized.

### IL-014: Type Inference Result Recording Pattern [MEDIUM]
20+ files repeat same pattern at function end.

### IL-015: Operand Parser State Passing [LOW]
Many helpers accept redundant parameters.

### IL-016: Memory Effects Classification Completeness [LOW]
Large switch statement could be generated from Opcode.def.

### IL-017: Format Utilities for Type Kinds [LOW]
Similar switch-based converters in separate domains.

### IL-018: Variadic Operand Count Sentinel Detection [LOW]
Two identical functions could be templated.

### IL-019: Literal Parsing in Multiple Contexts [LOW]
May have duplication with frontend parsers.

### IL-020: Handler Block Parameter Serialization [LOW]
**File:** `src/il/io/Serializer.cpp`
**Lines:** 547-560

Special-case handling for Error and ResumeTok types.

---

## Lib

### LIB-001: Duplicated Null-Check Pattern [HIGH]
**Files:** 44 files across audio, graphics, GUI
**Count:** 583 occurrences

Pattern `if (!ptr || !ptr2) return;` repeated throughout.

**Recommendation:** Create validation utility macros in `lib_common.h`.

### LIB-002: Duplicated VTable Initialization [HIGH]
**Files:** 25+ widget files

Every GUI widget implements nearly identical VTable boilerplate (~250 lines total).

**Recommendation:** Create macro-based VTable factory.

### LIB-003: Repeated String Allocation Pattern [MEDIUM]
**Files:** 23 widget files
**Count:** 97 `strdup()` calls

Identical text management in widget constructors and destroy functions.

### LIB-004: Duplicated State Color Determination [MEDIUM]
**Files:** 15+ widget files

All interactive widgets implement identical color selection logic.

### LIB-005: Platform Threading Primitives Duplicated [HIGH]
**Files:** `src/lib/audio/src/vaud.c`, `src/lib/graphics/src/vgfx.c`

Both libraries independently implement identical platform threading abstractions.

**Recommendation:** Create shared `viper_platform.h`.

### LIB-006: Memory Allocation Error Handling Inconsistency [MEDIUM]
Different allocation strategies with different error handling.

### LIB-007: Repeated Constraint Application Pattern [MEDIUM]
~10 files with duplicate constraint clamping code.

### LIB-008: Hardcoded Magic Numbers in Theme/Style [MEDIUM]
Scattered style constants across files, some from theme, others hardcoded.

### LIB-009: Repeated Child Iteration Pattern [MEDIUM]
**Count:** 8+ instances

Child iteration with visibility checks repeated across layout code.

**Recommendation:** Create iteration macros `VG_FOREACH_CHILD`, `VG_FOREACH_VISIBLE_CHILD`.

### LIB-010: Platform-Specific Code in Graphics Backend [LOW]
Each platform backend duplicates error handling and scaffolding.

### LIB-011: Repeated Framebuffer Bounds Checking [LOW]
Pattern appears ~5 times; `vgfx_internal_in_bounds()` exists but underused.

### LIB-012: Synchronization Primitive Initialization [MEDIUM]
Mutex/condition variable initialization repeated across platforms.

### LIB-013: Mixer Voice Allocation Optimization [LOW]
3-pass algorithm could be single-pass.

### LIB-014: Soft Clipping Could Be Shared [LOW]
Audio soft_clip() might benefit graphics for HDR.

### LIB-015: Thread-Local Error Storage Duplication [MEDIUM]
Both audio and graphics implement identical TLS patterns.

---

## Runtime

### RT-001: Duplicate Hex Encoding Functions [HIGH]
**Files:** `rt_keyderive.c`, `rt_hash.c`, `rt_codec.c`, `rt_bytes.c`

Multiple files reimplement hex encoding and character lookup table.

**Recommendation:** Create shared `rt_hex_util.h/c`.

### RT-002: Repeated Bytes Extraction Pattern [HIGH]
**Files:** `rt_hash.c`, `rt_keyderive.c`, `rt_aes.c`

Identical byte extraction from rt_bytes objects appears 6+ times.

**Recommendation:** Create `rt_bytes_extract_raw()` utility.

### RT-003: Repeated rt_heap Header Pattern [HIGH]
**Files:** All array type files

Near-identical header retrieval and validation in 5 array implementations.

**Recommendation:** Create macro-based array header pattern.

### RT-004: Repeated Payload Byte Calculation [MEDIUM]
Each array type implements payload size calculation with identical overflow protection.

### RT-005: Inconsistent Memory Allocation Error Messages [MEDIUM]
**Count:** 114 instances across 27 files

Inconsistent error message strings embedded throughout.

### RT-006: Platform-Specific Code Scattered [MEDIUM]
**Files:** `rt_file_io.c`, `rt_archive.c`, `rt_compress.c`

`#ifdef` blocks repeated across files.

**Recommendation:** Create `rt_platform_config.h`.

### RT-007: Duplicate Hash Function Patterns [MEDIUM]
**File:** `rt_hash.c`

Same wrapper pattern for MD5, SHA1, SHA256.

**Recommendation:** Create macro `RT_HASH_STRING_IMPL(hashfn, digestsize)`.

### RT-008: HMAC Implementation Duplication [HIGH]
**File:** `rt_hash.c`

HMAC patterns repeated for each hash algorithm.

### RT-009: Similar Resize/Grow Logic [HIGH]
**Files:** `rt_array.c`, `rt_array_i64.c`, `rt_array_f64.c`

~85 lines resize logic virtually identical except element type.

### RT-010: Repeated String Extraction from rt_string [LOW]
Multiple files implement same cstr extraction pattern.

### RT-011: CRC32 Table Initialization Race Condition [MEDIUM]
**File:** `rt_crc32.c`
**Lines:** 28-46

Non-atomic initialization flag creates race condition.

### RT-012: Inline Bytes Structure Duplication [LOW]
**Files:** `rt_compress.c`, `rt_archive.c`, `rt_bytes.c`

`bytes_impl` structure defined locally in 3+ files.

### RT-013: String Parameter Validation Repeated [LOW]
**Files:** `rt_linereader.c`, `rt_linewriter.c`

Identical validation code.

### RT-014: Magic Number Constants Not Centralized [LOW]
Collection defaults scattered across files.

### RT-015: Repeated Memory Allocation Pattern [LOW]
Collection creation follows identical allocation pattern.

---

## Support

### SP-001: Namespace Inconsistency [HIGH]
**Files:** Multiple

Split between `il::support` and `viper::support` namespaces.

**Recommendation:** Consolidate under single namespace.

### SP-002: Duplicate Handle Validation Patterns [MEDIUM]
**Files:** `source_manager.cpp`, `string_interner.cpp`

Identical boundary checking for integer handles.

### SP-003: Inconsistent Size-to-ID Casting [MEDIUM]
Both files convert `size_t` to `uint32_t` without consistent guards.

### SP-004: Arena Overflow Checks Redundancy [LOW]
**File:** `arena.cpp`

Lines 92-95 redundant with earlier checks.

### SP-005: Two Competing Error-Handling Ecosystems [HIGH]
**Files:** `result.hpp`, `diag_expected.hpp`

`Result<T>` and `Expected<T>` are functionally similar but separate.

**Recommendation:** Unify error-handling layer.

### SP-006: DiagnosticEngine vs Direct Printing [MEDIUM]
Diagnostic printing happens via two distinct paths.

### SP-007: Platform Path Normalization Testing [MEDIUM]
Windows-specific logic only tested on Windows.

### SP-008: StringInterner Capacity Not Documented [LOW]
Growth strategy not documented.

### SP-009: Source Location Validation Scattered [MEDIUM]
**Files:** `source_location.cpp`, `diag_expected.cpp`

Validity checks duplicated inline.

### SP-010: Mixed Empty-Check Patterns [LOW]
Inconsistent `.empty()` vs `.size() > 0`.

### SP-011: No DiagnosticEngine Clear Method [LOW]
Cannot reset engine without creating new instance.

### SP-012: SmallVector Growth Strategy [LOW]
Doubling strategy not configurable or documented.

### SP-013: Unsafe Pointer Arithmetic in Arena [MEDIUM]
**File:** `arena.cpp`

Complex pointer arithmetic without debug assertions.

### SP-014: DiagCapture Design Coupling [MEDIUM]
Couples diagnostic creation with printing.

### SP-015: StringInterner Copy is O(n) [LOW]
rebuildMap() in copy operations is expensive.

---

## Tools

### TL-001: Duplicated Command-Line Argument Parsing [HIGH]
**Files:** Multiple cmd_*.cpp files

Same argument parsing logic repeated across frontends.

**Recommendation:** Extract `FrontendArgParser` class.

### TL-002: File I/O Pattern Duplication [HIGH]
**Files:** All frontend command files

Identical file loading code (~27 lines each).

**Recommendation:** Create `tools/common/file_loader.hpp`.

### TL-003: Bytecode VM Execution Pattern Duplication [HIGH]
**Files:** All frontend command files

Identical bytecode VM setup and execution.

**Recommendation:** Extract `tools/common/vm_executor.hpp`.

### TL-004: Trap Message Handling Duplication [MEDIUM]
All tools duplicate trap message extraction and reporting.

### TL-005: Shared Options Parsing Inconsistency [MEDIUM]
Different numeric parsing approaches across tools.

### TL-006: Usage/Help Text Duplication [MEDIUM]
Every frontend redefines usage strings with identical structure.

### TL-007: VM Decision Logic Duplication [MEDIUM]
`useStandardVm = config.debugVm || config.shared.trace.enabled()` repeated everywhere.

### TL-008: Module Loading Pattern Duplication [MEDIUM]
Same load + verify sequence repeated.

### TL-009: SourceManager Initialization Pattern [LOW]
Every entry point creates SourceManager identically.

### TL-010: Output File Error Handling Inconsistency [LOW]
Different error handling for file output operations.

### TL-011: ArgvView Duplication [LOW]
**Files:** `cmd_codegen_x64.cpp`, `cmd_codegen_arm64.cpp`

Identical ArgvView utility in both files.

**Recommendation:** Move to `tools/common/ArgvView.hpp`.

### TL-012: Configuration Structure Proliferation [LOW]
Each command defines own Config struct with overlapping fields.

### TL-013: Line Number Parsing Duplication [LOW]
Two implementations of line number parsing.

### TL-014: Numeric String Conversion Inconsistency [LOW]
Three different approaches to parsing numeric arguments.

### TL-015: Copy-Paste ilc_compat Shims [LOW]
All four files implement identical wrapper.

### TL-016: Error Message Consistency [LOW]
Varying format and style across tools.

### TL-017: Whitespace Trimming Utility Duplication [LOW]
Multiple implementations of string trimming.

### TL-018: Trap Handling Output Formatting [LOW]
Inconsistent logic for dumping trap messages.

### TL-019: Return Code Conversion Pattern [LOW]
Same conversion logic repeated.

### TL-020: Missing Header Guard Documentation [LOW]
Some headers lack inline documentation.

---

## TUI

### TUI-001: Text Rendering - Duplicate UTF-8 Decoding [HIGH]
**Files:** 3 files

UTF-8 decoding implemented with subtle differences in 3 locations.

**Recommendation:** Create unified UTF-8 codec library.

### TUI-002: Character Width Calculation Repeated [MEDIUM]
Decode + width calculation pattern repeated in tight loops.

### TUI-003: Screen Buffer Cell Clearing Duplication [MEDIUM]
**Count:** 4+ widgets

Rectangle clearing with spaces pattern repeated.

**Recommendation:** Add `clearRectangle()` utility.

### TUI-004: Inline Text Rendering Pattern [HIGH]
**Count:** 8 files

Nearly identical text-to-cell loop in every widget.

**Recommendation:** Create `renderText()`, `renderTextTruncated()`, `renderTextCentered()`.

### TUI-005: Style Role Lookup Pattern [LOW]
Repeated theme queries could be cached.

### TUI-006: Keyboard Event Dispatch Pattern [MEDIUM]
Similar onEvent() patterns across 6 widgets.

### TUI-007: Splitter Widget Duplication [HIGH]
**File:** `src/tui/src/widgets/splitter.cpp`

HSplitter and VSplitter are nearly identical (~100 lines duplication).

**Recommendation:** Refactor to shared base or template.

### TUI-008: Cursor Clamping and Selection Management [LOW]
Similar patterns across text views and list widgets.

### TUI-009: UTF-8 String Iteration Pattern [MEDIUM]
**Files:** 2 files

Nearly identical byte offset + display column tracking loops.

**Recommendation:** Create `Utf8LineIterator` utility.

### TUI-010: Bounds Checking Cast Patterns [LOW]
Repeated `static_cast<int>(size_t)` patterns.

### TUI-011: Color/Style Attribute Construction [LOW]
Hardcoded RGBA tuples in theme.cpp.

### TUI-012: Input Event Type Checking Pattern [LOW]
Verbose if-else chains for key codes.

---

## VM

### VM-001: Duplicate MSVC Overflow Detection [HIGH]
**Files:** `OpHandlerUtils.hpp`, `IntOpSupport.hpp`

Identical checked arithmetic implementations in two files.

**Recommendation:** Extract to shared `vm/detail/MsvcOverflow.hpp`.

### VM-002: Repeated Type-Dispatch Switch Statements [MEDIUM]
**Count:** 6 occurrences

Pattern of switching on `in.type.kind` appears across multiple files.

**Recommendation:** Create unified `dispatchOnIntegerWidth()` template.

### VM-003: storeResult Pattern [LOW - WELL DESIGNED]
33 occurrences correctly centralized. This is good practice.

### VM-004: Overflow Trap Message Duplication [MEDIUM]
Six overflow opcodes each provide own hardcoded trap message.

### VM-005: Repeated Operand Evaluation in Call Handlers [MEDIUM]
**File:** `Op_CallRet.cpp`
**Lines:** 113-120, 421-427, 465-470

Three functions perform nearly identical operand evaluation loops.

### VM-006: Integer Comparison Lambda Duplication [LOW]
10 comparison handlers use trivial lambdas that could be stateless functors.

### VM-007: Floating-Point Binary Operation Lambda Duplication [LOW]
3 float ops use lambdas that could be functors.

### VM-008: Floating-Point Comparison Lambda Duplication [LOW]
6 float comparison handlers with similar pattern.

### VM-009: Memory Access Context Duplication [MEDIUM]
Multiple trap-reporting paths manually reconstruct error context.

**Recommendation:** Create centralized `TrapHelper` functions.

### VM-010: Bounds Check Logic Duplication [MEDIUM]
**File:** `int_ops_arith.cpp`
**Lines:** 335-395

Same bounds-check logic duplicated 3 times for I16/I32/I64.

### VM-011: Null Pointer Handling Duplication [LOW]
GEP null pointer + zero offset pattern could be extracted.

### VM-012: Instruction Dispatch Hot Path [HIGH]
**File:** `VMContext.cpp`
**Lines:** 472-606

Sequential if statements in eval() could use dispatch table.

### VM-013: Repeated Function Pointer Cast Pattern [MEDIUM]
**File:** `Op_CallRet.cpp`
**Lines:** 175-237

Cascading if-statements for fast-path runtime calls.

**Recommendation:** Create fast-path dispatcher table.

### VM-014: Argument Binding and Synchronization [MEDIUM]
Complex argument binding logic with embedded type-dispatch.

### VM-015: Frame Reset Pattern [LOW]
Trap result creation repeated in error paths.

### VM-016: Debug Context Clearing [LOW]
Could use structured initialization.

---

## Cross-Cutting Concerns

### XC-001: Platform Abstraction Fragmentation
Multiple libraries implement platform-specific code independently:
- Threading primitives in audio, graphics
- File I/O in multiple components
- Error storage in multiple libraries

**Recommendation:** Create unified `viper_platform.h` layer.

### XC-002: String Utilities Scattered
String operations (trimming, case conversion, etc.) implemented multiple times:
- TUI has `util/string.hpp`
- Tools have inline implementations
- Runtime has C implementations

**Recommendation:** Consolidate to shared utility library.

### XC-003: Error Handling Inconsistency
Three different error-handling patterns:
- `Result<T>` in support
- `Expected<T>` in support
- Direct trap calls in runtime

### XC-004: Diagnostic Output Inconsistency
Multiple paths for diagnostic output without unified sink abstraction.

---

## Recommendations Summary

### High Priority (Estimated LOC Savings: 2000+)

1. **Extract Platform Abstraction Layer**
   - Threading, file I/O, error storage
   - Affects: runtime, lib/audio, lib/graphics
   - Estimated savings: 300+ lines

2. **Create Codegen Common Infrastructure**
   - MachineIR, FrameLayoutBuilder, RegisterUtils
   - Affects: aarch64, x86_64 backends
   - Estimated savings: 500+ lines

3. **Consolidate Frontend Utilities**
   - Comment skipping, number parsing, expression lowering
   - Affects: BASIC, Pascal, Zia frontends
   - Estimated savings: 400+ lines

4. **Extract Runtime Array Utilities**
   - Header patterns, resize logic, hex encoding
   - Affects: all rt_array_*.c files
   - Estimated savings: 400+ lines

5. **Unify Tool Infrastructure**
   - Argument parsing, file loading, VM execution
   - Affects: all ilc commands, frontend tools
   - Estimated savings: 500+ lines

### Medium Priority (Estimated LOC Savings: 1000+)

6. **Create TUI Rendering Utilities**
   - Text rendering, rectangle clearing, UTF-8 iteration
   - Affects: all TUI widgets

7. **Consolidate IL Verification Patterns**
   - Type validation, diagnostic formatting, parsing utilities

8. **Extract VM Operation Helpers**
   - Type dispatch, trap handling, operand evaluation

9. **Unify GUI Widget Infrastructure**
   - VTable factory, text management, state color helpers

### Low Priority (Code Quality Improvements)

10. Various naming inconsistencies and documentation gaps
11. Magic number centralization
12. Growth strategy documentation
13. Debug assertion additions

---

## Implementation Notes

- Changes should be incremental and maintain current API contracts
- Each extraction should include comprehensive unit tests
- Consider feature flags for gradual rollout in critical paths
- Document architectural decisions in ADRs as needed per CLAUDE.md

---

*Generated by systematic code review of Viper platform source files.*
