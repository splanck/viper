# Audit Finding #9: Thread-Local Storage Support

## Problem
IL has no way to express thread-local semantics despite the Mach-O linker having partial TLS infrastructure (`kTlvpLoadPage21`, `kTlvpLoadPageOff12` in `RelocConstants.hpp:70-71`).

## Implementation Plan

### Phase 1: IL Thread-Local Attribute (1-2 days)

1. Add `ThreadLocal` to linkage or as a separate attribute in `Global.hpp`:
   ```cpp
   struct Global {
       std::string name;
       il::core::Type type;
       Linkage linkage;
       bool isThreadLocal{false};  // NEW
       // ...
   };
   ```

2. Update IL text format to support `thread_local` modifier:
   ```
   global @counter : i64 thread_local
   ```

3. Update `ILReader.cpp` to parse `thread_local` keyword
4. Update `ILWriter.cpp` to emit it
5. Update IL verifier to validate thread-local globals

### Phase 2: AArch64 TLS Code Generation (3-4 days)

**Mach-O (Darwin) TLS model:**
Darwin uses TLV (Thread Local Variable) descriptors. Access pattern:
```asm
adrp x0, _var@TLVPPAGE           ; Page of TLV descriptor
ldr  x0, [x0, _var@TLVPPAGEOFF]  ; Load descriptor pointer
blr  x0                          ; Call tlv_get_addr → returns address in x0
; x0 now points to thread-local storage
```

The Mach-O linker already handles `kTlvpLoadPage21` and `kTlvpLoadPageOff12` relocations (`RelocApplier.cpp:235-268`).

Steps:
1. In `LowerILToMIR.cpp`, detect `isThreadLocal` on global access
2. Emit ADRP+LDR+BLR sequence instead of ADRP+ADD
3. Emit new MIR opcodes: `AdrTlvPage`, `LdrTlvPageOff`
4. In `A64BinaryEncoder.cpp`, encode with TLV relocations

**ELF (Linux) TLS model:**
ELF uses `__tls_get_addr` with GOT entries. Initial-exec model:
```asm
mrs  x0, TPIDR_EL0              ; Thread pointer
ldr  x1, [x0, :tprel_lo12:var]  ; Load from TLS offset
```

### Phase 3: x86-64 TLS Code Generation (2-3 days)

**macOS x86-64:** Same TLV descriptor model as AArch64
**Linux x86-64:** FS-segment relative addressing:
```asm
mov rax, qword ptr fs:[var@TPOFF]  ; Initial-exec model
```

### Phase 4: Linker TLS Sections (2-3 days)

**Mach-O:** Already partially supported. Add:
- `__thread_vars` section for TLV descriptors
- `__thread_bss` section for zero-initialized TLS data

**ELF:** Add:
- `.tdata` section for initialized TLS
- `.tbss` section for zero-initialized TLS
- TLS relocations (`R_X86_64_TPOFF64`, `R_AARCH64_TLS_*`)

**PE/COFF:** Add:
- `.tls` section with TLS directory

### Phase 5: Frontend Support (1-2 days)
- Zia: `thread var x: Integer = 0`
- BASIC: `THREAD DIM x AS INTEGER`

### Files to Modify
- `src/il/core/Global.hpp` — add isThreadLocal
- `src/il/io/ILReader.cpp` / `ILWriter.cpp` — parse/emit
- `src/il/verify/GlobalVerifier.cpp` — validate
- `src/codegen/aarch64/LowerILToMIR.cpp` — TLS lowering
- `src/codegen/x86_64/LowerILToMIR.cpp` — TLS lowering
- `src/codegen/common/linker/ElfWriter.cpp` — .tdata/.tbss
- `src/codegen/common/linker/MachOExeWriter.cpp` — __thread_vars/__thread_bss
- `src/codegen/common/linker/PeWriter.cpp` — .tls section

### Verification
1. `./scripts/build_viper.sh` — all tests pass
2. Write IL test with thread_local global, compile to native, run with 2 threads
3. Each thread modifies its copy independently — verify no data race
4. Verify on macOS + Linux
