# Lisp Interpreter - Viper/Zia Bug Tracker

Bugs, limitations, and unexpected behaviors encountered in the Viper compiler,
runtime, or Zia language while building the Lisp interpreter.

---

## ReadLine returns String, not String?

- **Date**: 2026-01-27
- **Status**: FIXED
- **Component**: runtime
- **Severity**: minor
- **Description**: `Viper.Terminal.ReadLine()` returns `String` rather than `String?`. There is no way to detect EOF (Ctrl+D) from a ReadLine call — the return type is non-optional.
- **Expected**: ReadLine should return `String?` with null indicating EOF, matching standard POSIX semantics where read() returns 0 at EOF.
- **Workaround**: For REPL usage, rely on explicit `(exit)` command rather than Ctrl+D detection.
- **Repro**:
```zia
module test;
func start() {
    var line: String? = Viper.Terminal.ReadLine(); // error: type mismatch
}
```
- **Fix**: Extended runtime signature system to support optional return types (`str?`). Changes span the full compiler pipeline:
  - **runtime.def**: Updated ReadLine/Ask signatures to `str?()` / `str?(str)`.
  - **RuntimeClasses.hpp/cpp**: Added `isOptionalReturn` flag to `ParsedSignature`, detect `?` suffix in `parseRuntimeSignature()`.
  - **Sema_Runtime.cpp**: Wrap optional returns with `types::optional()` for Zia type system.
  - **rtgen.cpp**: Updated `ilTypeToZiaType()` and `ilTypeToSigType()` to handle `?` suffix. Optional reference types recurse to inner type for IL-level signatures (e.g. `str?` → `"string"`, not `"ptr"`).
  - **Types.cpp** `toILType()`: Made `Optional[T]` map to the inner IL type when `T` is a nullable reference type (`Str` or `Ptr`). This avoids str/ptr type mismatches at the IL level.
  - **Lowerer_Stmt.cpp** `lowerVarStmt()`: Changed hardcoded `Type::Kind::Ptr` to `mapType(varType)` for Optional variable IL types.
  - **Lowerer_Emit.cpp**: Updated `emitOptionalWrap()` and `emitOptionalUnwrap()` to treat `Str` as a no-op (same as `Ptr`) — strings are already nullable pointers.
  - **Lowerer_Expr_Binary.cpp** `extendOperandForComparison()`: Added `Str` to the pointer-to-i64 conversion path for null comparisons.
  - **Lowerer_Expr.cpp** `lowerCoalesce()`: Use `left.type` instead of hardcoded `Ptr` for null-check store type.
  - **RuntimeSignatureParser.cpp**: Added `?` → `Kind::Ptr` fallback in `parseKindToken()`.
  - **Call sites**: Updated with `?? ""` null coalescing; Lisp REPL uses explicit null check for proper EOF detection.

## Non-zero exit codes from successful programs

- **Date**: 2026-01-27
- **Status**: FIXED
- **Component**: runtime
- **Severity**: cosmetic
- **Description**: Programs that run successfully and produce correct output sometimes return non-zero exit codes (observed: 48, 64, 240). The exit code varies between runs and program complexity.
- **Expected**: A program that completes without error should exit with code 0.
- **Workaround**: None needed for correctness; ignore exit codes in shell scripts.
- **Repro**:
```zia
module test;
func start() {
    Viper.Terminal.Say("hello");
}
// Exits with non-zero code despite success
```
- **Fix**: `vm_executor.cpp` was using garbage stack value (`bcResult.i64`) as exit code for void-returning `main()`. Changed non-trapped execution path to always return exit code 0. The trapped branch already correctly sets exit code 1.

## Zia compiler does not support passing arguments to programs

- **Date**: 2026-01-27
- **Status**: FIXED
- **Component**: compiler
- **Severity**: minor
- **Description**: The `zia` CLI does not support passing command-line arguments to the running Zia program. All arguments after the .zia file are parsed by the compiler itself, and `Viper.Environment.GetArgumentCount()` / `GetArgument()` are not populated with user-supplied values.
- **Expected**: Support `zia script.zia -- arg1 arg2` syntax to pass arguments through to the program.
- **Workaround**: Use environment variables (e.g., `LISP_FILE=path zia lisp.zia`) to pass configuration to programs.
- **Repro**:
```bash
zia hello.zia myarg  # error: unknown argument or file type
```
- **Fix**: Added `--` separator handling to `frontend_tool.hpp` `parseArgs()`. Arguments after `--` are collected into `programArgs` and forwarded through `buildIlcArgs()` to the underlying frontend command, which already supports `--` via `cmd_front_zia.cpp`. Updated `usage.cpp` help text with usage example. All standalone frontend tools (`zia`, `vbasic`, `vpascal`) benefit from this fix.

---
