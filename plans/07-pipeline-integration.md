# Phase 7: Pipeline Integration

## Purpose

Wire the binary encoder and object file writer into the existing codegen pipeline, so that
`viper codegen x64` and `viper codegen arm64` can produce `.o` files directly without invoking
the system assembler.

---

## 1. Files to Modify

| File | Change |
|------|--------|
| `src/codegen/x86_64/CodegenPipeline.cpp` | Add binary emission path |
| `src/codegen/aarch64/CodegenPipeline.cpp` | Add binary emission path |
| `src/codegen/common/LinkerSupport.hpp` | Add symbol-table-based `prepareLinkContext` overload |
| `src/codegen/common/LinkerSupport.cpp` | Implement new overload |
| `src/tools/viper/cmd_codegen_x64.cpp` | Add `--native-asm` flag |
| `src/tools/viper/cmd_codegen_arm64.cpp` | Add `--native-asm` flag |

## 2. New Files

| File | Purpose |
|------|---------|
| `src/codegen/x86_64/passes/BinaryEmitPass.hpp` | MIR → binary .o for x86_64 |
| `src/codegen/x86_64/passes/BinaryEmitPass.cpp` | Implementation |
| `src/codegen/aarch64/passes/BinaryEmitPass.hpp` | MIR → binary .o for AArch64 |
| `src/codegen/aarch64/passes/BinaryEmitPass.cpp` | Implementation |

---

## 3. Pipeline Flow Change

### x86_64 Current Flow

The x86_64 backend uses a class-based `PassManager` with `CodegenPipeline::run()`:

```
CodegenPipeline::run()
  → PassManager: LoweringPass → LegalizePass → RegAllocPass → EmitPass
  → EmitPass writes MIR → .s text (stored in CodegenResult::asmText)
  → invokeAssembler("cc", "-c", ".s", ".o")    ← external tool (lines 189-219)
  → prepareLinkContext() scans asmText for rt_* symbols
  → invokeLinker("cc", ".o", archives, "-o", exe)
```

### AArch64 Current Flow

The AArch64 backend uses a **functional pipeline** (not class-based):

```
runCodegenPipeline()   [src/codegen/aarch64/CodegenPipeline.cpp]
  → LoweringPass.run(module, diags)
  → RegAllocPass.run(module, diags)
  → PeepholePass.run(module, diags)
  → SchedulerPass.run(module, diags)
  → EmitPass.run(module, diags)    → writes to module.assembly string
  → (caller writes .s file, calls invokeAssembler, then linker)
```

**Important difference:** AArch64 does NOT use a `PassManager<T>::addPass()` registration
pattern. Each pass is called sequentially in the function body. BinaryEmitPass integration
requires adding a conditional branch in the function, not pass registration.

### New Flow (Binary Emission)

```
x86_64:
CodegenPipeline::run()
  → PassManager: Lowering → Legalize → RegAlloc → [BinaryEmitPass OR EmitPass]
  → BinaryEmitPass:
      1. Emit rodata pool to CodeSection (strings + f64 constants)
      2. For each MFunction: encoder.encodeFunction(fn, text, rodata)
      3. Detect host platform → create ObjectFileWriter
      4. writer.write(objPath, text, rodata, err)
  → prepareLinkContext(text.symbols()) reads symbols directly
  → invokeLinker("cc", ".o", archives, "-o", exe)   ← unchanged

AArch64:
runCodegenPipeline()
  → LoweringPass → RegAllocPass → PeepholePass → SchedulerPass
  → if (binaryPath):
      BinaryEmitPass.run(module, diags)  → produces .o file directly
  → else:
      EmitPass.run(module, diags)        → produces .s text
  → prepareLinkContext() from symbol table or assembly text
  → invokeLinker (unchanged)
```

### Pass Selection Logic

**x86_64** (class-based PassManager):
```cpp
if (outputMode == OutputMode::Assembly || forceTextAsm) {
    passManager.addPass(EmitPass(outputStream));   // existing text path
} else {
    passManager.addPass(BinaryEmitPass(objPath, targetFormat));  // new binary path
}
```

**AArch64** (functional pipeline):
```cpp
// In runCodegenPipeline(), after SchedulerPass:
if (!forceTextAsm && outputMode != OutputMode::Assembly) {
    BinaryEmitPass binaryEmit(objPath, detectHostFormat(), ObjArch::AArch64);
    binaryEmit.run(module, diags);
} else {
    EmitPass emit;
    emit.run(module, diags);
}
```

The `-S` flag (assembly output) always uses the text path. Default: binary path.

---

## 4. BinaryEmitPass Design

### x86_64 BinaryEmitPass

```cpp
class BinaryEmitPass : public Pass {
public:
    BinaryEmitPass(const std::string& objPath, ObjFormat format);
    void run(PassContext& ctx) override;

private:
    std::string objPath_;
    ObjFormat format_;
};

void BinaryEmitPass::run(PassContext& ctx) {
    CodeSection text, rodata;
    X64BinaryEncoder encoder;

    // Emit rodata pool (string literals, float constants)
    emitRoDataPool(ctx.rodataPool(), rodata);

    // Encode each function
    for (auto& fn : ctx.functions()) {
        bool isDarwin = (format_ == ObjFormat::MachO);
        encoder.encodeFunction(fn, text, rodata, isDarwin);
    }

    // Write object file
    auto writer = createObjectFileWriter(format_, ObjArch::X86_64);
    if (!writer->write(objPath_, text, rodata, ctx.err())) {
        ctx.setError("Failed to write object file");
    }
}
```

### AArch64 BinaryEmitPass

Similar structure but with key differences:

```cpp
class BinaryEmitPass {
public:
    BinaryEmitPass(const std::string& objPath, ObjFormat format, ObjArch arch);
    bool run(AArch64Module& module, Diagnostics& diags);

private:
    std::string objPath_;
    ObjFormat format_;
};

bool BinaryEmitPass::run(AArch64Module& module, Diagnostics& diags) {
    CodeSection text, rodata;
    A64BinaryEncoder encoder;
    bool isDarwin = (format_ == ObjFormat::MachO);

    // Emit rodata pool (from module.rodataPool)
    emitRoDataPoolBinary(module.rodataPool, rodata, isDarwin);

    // Encode each function (includes prologue/epilogue synthesis)
    for (auto& fn : module.mir) {
        encoder.encodeFunction(fn, text, rodata, isDarwin);
    }

    // Write object file
    auto writer = createObjectFileWriter(format_, ObjArch::AArch64);
    if (!writer->write(objPath_, text, rodata, diags.err())) {
        diags.error("Failed to write object file");
        return false;
    }
    return true;
}
```

**Key difference from x86_64:** AArch64 prologue/epilogue are synthesized at emission time
(not from MIR instructions). The `A64BinaryEncoder::encodeFunction()` must:
1. Emit prologue bytes (STP, SUB SP) based on `fn.frameLayout`
2. If main function: emit `bl rt_legacy_context` + `bl rt_set_current_context`
3. Encode all MIR instructions
4. Emit epilogue bytes (ADD SP, LDP, RET) before return instructions

See `plans/03-aarch64-binary-encoder.md` §5 for full prologue/epilogue specification.

---

## 5. LinkerSupport Adaptation

### Current: Assembly Text Scanning

The current `prepareLinkContext()` receives assembly **text content** (not a file path) and
scans it using `parseRuntimeSymbols()`:

```cpp
// Current: scans assembly text for "rt_*" / "Viper.*" symbols
// Located in src/codegen/common/LinkerSupport.cpp lines 171-233
int prepareLinkContext(const std::string &asmText, LinkContext &ctx,
                       std::ostream &out, std::ostream &err);
```

`parseRuntimeSymbols()` (lines 78-139) uses string scanning to find:
- `rt_*` symbols (direct runtime function calls)
- `Viper.*` namespace-qualified symbols (mapped via `RuntimeNameMap`)

It then resolves these to `RtComponent` enum values and computes required archive paths.

### New: Symbol Table Direct Access

```cpp
// New overload: reads symbols directly from SymbolTable
int prepareLinkContext(const SymbolTable &symbols, LinkContext &ctx,
                       std::ostream &out, std::ostream &err);
```

This overload iterates the SymbolTable, looking for **External** (undefined) symbols that
match `rt_*` or `Viper.*` patterns. This is faster and more reliable than text scanning
because the symbol names are already resolved and mapped.

The existing text-scanning overload remains for the `--text-asm` / `-S` path.

### Integration with x86_64 Pipeline

The x86_64 pipeline has its **own** `invokeAssembler()` at lines 189-219 of
`CodegenPipeline.cpp`, in addition to the common one in `LinkerSupport.cpp`. When the
binary path is active, this per-pipeline assembler invocation is skipped entirely — the
BinaryEmitPass produces the `.o` file directly.

---

## 6. Command-Line Interface

### New Flags

| Flag | Meaning |
|------|---------|
| `--native-asm` | Use binary encoder + object file writer (default when available) |
| `--text-asm` | Force text assembly + system assembler (for debugging/comparison) |
| `-S <file>` | Emit text assembly to file (always uses text path, does not assemble) |

### Behavior Matrix

| Flags | Behavior |
|-------|----------|
| (none) | Binary path → .o → link |
| `-S out.s` | Text path → .s file only (no assemble, no link) |
| `--text-asm` | Text path → .s → cc -c → .o → link |
| `--text-asm -S out.s` | Text path → .s file only |
| `--native-asm` | Binary path → .o → link (explicit) |

---

## 7. Object File Path Convention

The pipeline always produces **one .o file per compilation** (1 IL module → 1 .o → 1 exe).

**Current path derivation** (CodegenPipeline.cpp lines 622-624, 94-107):
```cpp
// Assembly path: replace .il extension with .s
std::filesystem::path asmPath = deriveAssemblyPath(opts_);  // e.g., program.il → program.s

// Object path: derived similarly, replacing .s with .o
std::filesystem::path objPath = /* derived from output path */;
```

The binary path must use the **same directory convention** — no `/tmp` files. The .o file
is placed alongside the source or output path. After linking, the intermediate .o is deleted
(unless the user requests object-only output).

**Cleanup:** Assembly (.s) files are deleted after successful linking unless `-S` flag is
used (CodegenPipeline.cpp lines 704-708). The binary path skips .s file creation entirely.

---

## 8. Platform Detection

The `BinaryEmitPass` needs to select the correct object file format. Detection logic:

```cpp
ObjFormat detectHostFormat() {
#if defined(__APPLE__)
    return ObjFormat::MachO;
#elif defined(_WIN32)
    return ObjFormat::COFF;
#else
    return ObjFormat::ELF;
#endif
}
```

For cross-compilation (future), a `--target-format=elf|macho|coff` flag can override this.

---

## 9. RoData Pool Integration

The two backends have different RoDataPool implementations:

### AArch64 RoDataPool (`src/codegen/aarch64/RodataPool.hpp`)

- Standalone class, already separate from the emitter
- Content-based deduplication: identical strings share a label
- Label format: `L.str.0`, `L.str.1`, etc.
- Stores: `contentToLabel_`, `nameToLabel_`, `ordered_` (deterministic)
- Built from IL module via `buildFromModule()` scanning IL globals

The BinaryEmitPass can iterate `ordered_` directly to emit binary rodata.

### x86_64 RoDataPool (`src/codegen/x86_64/AsmEmitter.hpp`, inner class)

- **Nested inside AsmEmitter** — not directly accessible from BinaryEmitPass
- Manages strings and f64 constants separately
- Label format: `.LC_str_N` for strings, `.LC_f64_N` for floats
- Index-based lookup: `addStringLiteral()` returns index, `stringLabel(idx)` returns label

**Action needed:** Either extract the x86_64 RoDataPool into a standalone class (preferred),
or build a parallel implementation in BinaryEmitPass that reads the same IL globals.

### Binary RoData Emission

For both architectures, `emitRoDataPoolBinary()`:

1. Iterate pool entries in deterministic order
2. For each string: `rodata.alignTo(1)`, emit bytes + NUL, `defineSymbol(label, Local)`
3. For each f64: `rodata.alignTo(8)`, emit 8 bytes (IEEE 754), `defineSymbol(label, Local)`
4. The encoder generates cross-section relocations when `.text` accesses rodata symbols

**Raw byte emission advantage:** The binary path handles string literals as raw
`std::string` byte vectors directly — no text escaping needed. This is actually **more
correct** than the text path, which has a known issue where NUL bytes within strings may
not be properly escaped to `.byte 0` directives by the current `format_rodata_bytes()`
function (asmfmt/Format.cpp:141-192). The binary path simply calls
`rodata.emitBytes(str.data(), str.size() + 1)` to include the NUL terminator.

---

## 10. Estimated Line Counts

| File | LOC |
|------|-----|
| x86_64 BinaryEmitPass.hpp + .cpp | ~120 |
| AArch64 BinaryEmitPass.hpp + .cpp | ~120 |
| LinkerSupport new overload | ~50 |
| CLI flag handling (both cmd_codegen files) | ~50 |
| Platform detection utility (ABIFormat → ObjFormat) | ~20 |
| RoData pool binary emission helper | ~100 |
| x86_64 RoDataPool extraction (if needed) | ~60 |
| CMakeLists.txt updates | ~15 |
| **Total** | **~535** |

**Note:** Previous estimate of ~295 was too low. Major additions:
- AArch64 BinaryEmitPass is more complex (prologue/epilogue synthesis delegation)
- RoData pool binary emission needs architecture-specific label format handling
- x86_64 RoDataPool may need extraction from AsmEmitter inner class

---

## 11. Testing

- E2E: Compile a Zia program with `--native-asm`, verify output matches `--text-asm`
- Regression: Run full 1279-test suite with binary path as default
- Comparison: For each test, verify binary-path output == text-path output
