# Audit Finding #6: Mach-O Unwind Metadata Generation

## Problem
No Viper-compiled function has unwind metadata. MachOReader skips `__compact_unwind` and `__eh_frame`. Writer never generates them. Stack traces truncate at Viper functions.

## Implementation Plan

### Phase 1: Always Emit Frame Pointer on Apple (1 hour)
In `A64BinaryEncoder.cpp:95-103`, gate leaf frame skipping on non-Apple:
```cpp
bool isDarwin = /* from pipeline options */;
skipFrame_ = !isDarwin && fn.isLeaf && fn.savedGPRs.empty() &&
             fn.savedFPRs.empty() && fn.localFrameSize == 0 && fn.name != "main";
```
This ensures every function on macOS has X29/X30 saved, enabling frame-pointer-based unwinding.

### Phase 2: Generate Compact Unwind Entries (3-4 days)

Apple's `__compact_unwind` format uses 32-byte entries per function:
```
struct compact_unwind_entry {
    uint64_t functionStart;     // Function's start address
    uint32_t functionLength;    // Function's code length
    uint32_t compactEncoding;   // Encoding of unwind info
    uint64_t personality;       // 0 for simple functions
    uint64_t lsda;              // 0 for no language-specific data
};
```

**Compact encoding for AArch64 with frame pointer:**
- Bits 31-28: `UNWIND_ARM64_MODE_FRAME` (0x04)
- Bits 20-12: Saved register pair mask (which pairs saved after X29/X30)
- Bits 11-0: Stack size / 16

**Steps:**
1. During `A64BinaryEncoder::encodeFunction()`, compute compact encoding:
   - Start with `0x04000000` (frame-based)
   - Set register pair bits based on `fn.savedGPRs`
   - Set stack bits based on `fn.localFrameSize`
   - Store as `MFunction.compactUnwindEncoding`

2. In `MachOExeWriter.cpp`, create `__TEXT,__compact_unwind` section:
   - Follow DWARF section creation pattern (lines 513-550)
   - Allocate 32 bytes per function
   - Write entries with function address, length, and compact encoding
   - Add relocations for functionStart addresses

3. In `MachOReader.cpp:226-227`, stop skipping `__compact_unwind` from input objects (allow passthrough for mixed Viper/external code)

### Phase 3: x86-64 Compact Unwind (1-2 days)
Same pattern but with x86-64 compact encoding:
- `UNWIND_X86_64_MODE_RBP_FRAME` (0x01)
- Register save offsets encoded in bits

### Files to Modify
- `src/codegen/aarch64/binenc/A64BinaryEncoder.cpp:95-103` — gate leaf frame on Darwin
- `src/codegen/aarch64/MachineIR.hpp` — add compactUnwindEncoding to MFunction
- `src/codegen/common/linker/MachOExeWriter.cpp` — generate __compact_unwind section
- `src/codegen/common/linker/MachOReader.cpp:226-227` — stop stripping unwind sections

### Verification
1. `./scripts/build_viper.sh` — all tests pass
2. `lldb` test: set breakpoint in Viper function, `bt` shows full stack trace
3. `dwarfdump --compact-unwind` on output binary — verify entries present
4. Crash test: trigger SIGABRT in Viper function — verify crash report shows correct frames
