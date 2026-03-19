# Phase 3: AArch64 Binary Encoder

## Purpose

Encode all ~42 non-pseudo AArch64 MIR opcodes into 32-bit instruction words, emitting them
into a CodeSection. AArch64's fixed-width encoding makes this significantly simpler than x86_64.

## Directory

```
src/codegen/aarch64/binenc/
    A64Encoding.hpp         # Instruction templates, condition code encoding
    A64BinaryEncoder.hpp    # Public interface
    A64BinaryEncoder.cpp    # Implementation
```

---

## 1. AArch64 Encoding Fundamentals

Every AArch64 instruction is exactly **32 bits (4 bytes)**, written in little-endian order.
Encoding is pure bit-field construction:

```cpp
uint32_t word = TEMPLATE | (Rd << 0) | (Rn << 5) | (Rm << 16) | (imm << shift);
```

### Register Encoding

AArch64 registers use a clean 5-bit encoding:

| Register | 5-bit encoding | Notes |
|----------|---------------|-------|
| X0–X30 | 0–30 | Direct mapping |
| SP | 31 | Stack pointer (context-dependent with XZR) |
| XZR | 31 | Zero register (context-dependent with SP) |
| V0–V31 | 0–31 | FPR, same 5-bit field, differentiated by opcode |

The existing `PhysReg` enum maps directly:
- `X0` (enum 0) → hardware 0
- `X1` (enum 1) → hardware 1
- ...
- `X30` (enum 30) → hardware 30
- `SP` (enum 31) → hardware 31
- `V0` (enum 32) → hardware 0 (FPR context)
- `V1` (enum 33) → hardware 1
- ...
- `V31` (enum 63) → hardware 31

```cpp
constexpr uint32_t hwGPR(PhysReg r) { return static_cast<uint32_t>(r); }
constexpr uint32_t hwFPR(PhysReg r) { return static_cast<uint32_t>(r) - 32; }
```

### Condition Code Encoding

AArch64 condition codes are 4-bit values:

| String | 4-bit code | Meaning |
|--------|-----------|---------|
| "eq" | 0x0 | Equal (Z=1) |
| "ne" | 0x1 | Not equal (Z=0) |
| "hs"/"cs" | 0x2 | Unsigned higher/same (C=1) |
| "lo"/"cc" | 0x3 | Unsigned lower (C=0) |
| "mi" | 0x4 | Negative (N=1) |
| "pl" | 0x5 | Positive/zero (N=0) |
| "vs" | 0x6 | Overflow (V=1) |
| "vc" | 0x7 | No overflow (V=0) |
| "hi" | 0x8 | Unsigned higher (C=1, Z=0) |
| "ls" | 0x9 | Unsigned lower/same (C=0 or Z=1) |
| "ge" | 0xA | Signed ≥ (N=V) |
| "lt" | 0xB | Signed < (N≠V) |
| "gt" | 0xC | Signed > (Z=0, N=V) |
| "le" | 0xD | Signed ≤ (Z=1 or N≠V) |
| "al" | 0xE | Always |
| "nv" | 0xF | Never (architectural) |

Inversion: `invertCond(cc) = cc ^ 1` (flip least significant bit).

---

## 2. Instruction Encoding Templates

Each instruction class has a fixed bit template. The encoder OR's in register fields and
immediate values. Below are the exact encodings for every MIR opcode Viper uses.

### 2A. Data Processing — Three-Register

Format: `sf[1] opc[2] 01011 shift[2] 0 Rm[5] imm6[6] Rn[5] Rd[5]`

For 64-bit (sf=1), no shift (shift=00), no extend (imm6=000000):

| MOpcode | Template (hex) | opc bits | Notes |
|---------|---------------|----------|-------|
| AddRRR | `0x8B000000` | sf=1, op=0, S=0 | `add Xd, Xn, Xm` |
| SubRRR | `0xCB000000` | sf=1, op=1, S=0 | `sub Xd, Xn, Xm` |
| AndRRR | `0x8A000000` | sf=1, opc=00 | `and Xd, Xn, Xm` (logical shifted) |
| OrrRRR | `0xAA000000` | sf=1, opc=01 | `orr Xd, Xn, Xm` |
| EorRRR | `0xCA000000` | sf=1, opc=10 | `eor Xd, Xn, Xm` |
| AddsRRR | `0xAB000000` | sf=1, op=0, S=1 | `adds Xd, Xn, Xm` (sets flags) |
| SubsRRR | `0xEB000000` | sf=1, op=1, S=1 | `subs Xd, Xn, Xm` (sets flags) |

Encoding: `template | (Rm << 16) | (Rn << 5) | Rd`

### 2B. Data Processing — Register (Variable Shift)

Format: `sf[1] 0 0 11010110 Rm[5] 0010 op2[2] Rn[5] Rd[5]`

| MOpcode | Template (hex) | op2 | Notes |
|---------|---------------|-----|-------|
| LslvRRR | `0x9AC02000` | 00 | `lslv Xd, Xn, Xm` |
| LsrvRRR | `0x9AC02400` | 01 | `lsrv Xd, Xn, Xm` |
| AsrvRRR | `0x9AC02800` | 10 | `asrv Xd, Xn, Xm` |

Encoding: `template | (Rm << 16) | (Rn << 5) | Rd`

### 2C. Multiply / Divide

| MOpcode | Template (hex) | Format | Notes |
|---------|---------------|--------|-------|
| MulRRR | `0x9B007C00` | `madd Xd, Xn, Xm, XZR` | mul is alias for madd with Ra=XZR(31) |
| SmulhRRR | `0x9B407C00` | `smulh Xd, Xn, Xm` | Upper 64 bits of 128-bit signed product |
| SDivRRR | `0x9AC00C00` | `sdiv Xd, Xn, Xm` | |
| UDivRRR | `0x9AC00800` | `udiv Xd, Xn, Xm` | |
| MSubRRRR | `0x9B008000` | `msub Xd, Xn, Xm, Xa` | `Xd = Xa - Xn * Xm` |
| MAddRRRR | `0x9B000000` | `madd Xd, Xn, Xm, Xa` | `Xd = Xa + Xn * Xm` |

MSubRRRR/MAddRRRR encoding: `template | (Rm << 16) | (Ra << 10) | (Rn << 5) | Rd`
Others: `template | (Rm << 16) | (Rn << 5) | Rd`

### 2D. Data Processing — Immediate (Add/Sub)

Format: `sf[1] op[1] S[1] 10001 shift[1] imm12[12] Rn[5] Rd[5]`

| MOpcode | Template (hex) | Notes |
|---------|---------------|-------|
| AddRI | `0x91000000` | `add Xd, Xn, #imm12` (sf=1, op=0, S=0, shift=0) |
| SubRI | `0xD1000000` | `sub Xd, Xn, #imm12` (sf=1, op=1, S=0, shift=0) |
| AddsRI | `0xB1000000` | `adds Xd, Xn, #imm12` (sf=1, op=0, S=1, shift=0) |
| SubsRI | `0xF1000000` | `subs Xd, Xn, #imm12` (sf=1, op=1, S=1, shift=0) |

Encoding: `template | (imm12 << 10) | (Rn << 5) | Rd`

**CmpRR:** Alias for `subs XZR, Xn, Xm` → `SubsRRR` template with Rd=31
**CmpRI:** Alias for `subs XZR, Xn, #imm` → `SubsRI` template with Rd=31
**TstRR:** Alias for `ands XZR, Xn, Xm` → `0xEA000000 | (Rm << 16) | (Rn << 5) | 31`

### 2E. Move Instructions

| MOpcode | Encoding | Notes |
|---------|----------|-------|
| MovRR | `0xAA0003E0 \| (Rm << 16) \| Rd` | Alias for `orr Xd, XZR, Xm` |
| MovRI (small) | `0xD2800000 \| (imm16 << 5) \| Rd` | `movz Xd, #imm16` |

**64-bit immediate materialization (emitMovImm64):**
Break value into 4 × 16-bit chunks. Emit `movz` for first non-zero chunk, `movk` for remaining non-zero chunks.

```
movz Xd, #chunk0                      → 0xD2800000 | (chunk0 << 5) | Rd
movk Xd, #chunk1, lsl #16             → 0xF2A00000 | (chunk1 << 5) | Rd
movk Xd, #chunk2, lsl #32             → 0xF2C00000 | (chunk2 << 5) | Rd
movk Xd, #chunk3, lsl #48             → 0xF2E00000 | (chunk3 << 5) | Rd
```

Only emit `movk` for non-zero chunks (optimization: skip zero chunks).

### 2F. Shift by Immediate

These are aliases for bitfield operations:

| MOpcode | Real instruction | Encoding |
|---------|-----------------|----------|
| LslRI | `ubfm Xd, Xn, #(-shift mod 64), #(63-shift)` | `0xD3400000 \| (immr << 16) \| (imms << 10) \| (Rn << 5) \| Rd` |
| LsrRI | `ubfm Xd, Xn, #shift, #63` | Same template, different immr/imms |
| AsrRI | `sbfm Xd, Xn, #shift, #63` | `0x93400000 \| ...` |

Where for `lsl #n`: `immr = (64-n) & 63`, `imms = 63-n`
For `lsr #n`: `immr = n`, `imms = 63`
For `asr #n`: `immr = n`, `imms = 63`

### 2G. Conditional Instructions

| MOpcode | Encoding | Notes |
|---------|----------|-------|
| Cset | `0x9A9F07E0 \| (invertCond(cc) << 12) \| Rd` | Alias: `csinc Xd, XZR, XZR, invert(cond)` |
| Csel | `0x9A800000 \| (Rm << 16) \| (cc << 12) \| (Rn << 5) \| Rd` | `csel Xd, Xn, Xm, cond` |

### 2H. Load/Store — Unsigned Offset

Format: `size[2] 111 00 1 opc[1] imm12[12] Rn[5] Rt[5]`

For 64-bit GPR loads/stores (size=11, V=0):

| MOpcode | Template (hex) | Notes |
|---------|---------------|-------|
| LdrRegFpImm | `0xF9400000` | `ldr Xt, [Xn, #imm12*8]` — scaled unsigned offset |
| StrRegFpImm | `0xF9000000` | `str Xt, [Xn, #imm12*8]` |
| LdrRegBaseImm | `0xF9400000` | Same encoding, different base register |
| StrRegBaseImm | `0xF9000000` | Same encoding |

For 64-bit FPR loads/stores (size=11, V=1):

| MOpcode | Template (hex) | Notes |
|---------|---------------|-------|
| LdrFprFpImm | `0xFD400000` | `ldr Dt, [Xn, #imm12*8]` |
| StrFprFpImm | `0xFD000000` | `str Dt, [Xn, #imm12*8]` |

Encoding: `template | ((offset/8) << 10) | (Rn << 5) | Rt`

**Unscaled variant** (for offsets not divisible by 8 or negative):
`ldur`/`stur`: `0xF8400000 | (imm9 << 12) | (Rn << 5) | Rt` (GPR)

**Large offset handling:** The AsmEmitter uses scratch X9 for large offsets. The binary
encoder must replicate this: `movz x9, #offset` + `add x9, base, x9` + `ldr/str rt, [x9]`.

### 2I. Load/Store Pair

Format: `opc[2] 101 0 type[2] 1 imm7[7] Rt2[5] Rn[5] Rt[5]`

For 64-bit GPR pairs (opc=10):

| MOpcode | Template (hex) | Notes |
|---------|---------------|-------|
| LdpRegFpImm | `0xA9400000` | `ldp Xt1, Xt2, [Xn, #imm7*8]` |
| StpRegFpImm | `0xA9000000` | `stp Xt1, Xt2, [Xn, #imm7*8]` |

For 64-bit FPR pairs (opc=01, V=1):

| MOpcode | Template (hex) | Notes |
|---------|---------------|-------|
| LdpFprFpImm | `0x6D400000` | `ldp Dt1, Dt2, [Xn, #imm7*8]` |
| StpFprFpImm | `0x6D000000` | `stp Dt1, Dt2, [Xn, #imm7*8]` |

Encoding: `template | ((offset/8 & 0x7F) << 15) | (Rt2 << 10) | (Rn << 5) | Rt`

**Pre-index variant** (for prologue `stp x29, x30, [sp, #-16]!`):
`0xA9800000` (pre-index bit set) with writeback.

### 2J. Stack Pointer Operations

| MOpcode | Encoding | Notes |
|---------|----------|-------|
| SubSpImm | AddRI/SubRI template with Rd=SP(31), Rn=SP(31) | Multiple instructions if >4080 |
| AddSpImm | Same pattern | |
| StrRegSpImm | `0xF9000000 \| ((offset/8) << 10) \| (SP << 5) \| Rt` | Base = SP |
| StrFprSpImm | `0xFD000000 \| ((offset/8) << 10) \| (SP << 5) \| Rt` | |

### 2K. Floating-Point Instructions

Format: `0 0 0 11110 01 1 Rm[5] opcode[4] 10 Rn[5] Rd[5]` (for FP data-processing)

| MOpcode | Template (hex) | Notes |
|---------|---------------|-------|
| FAddRRR | `0x1E602800` | `fadd Dd, Dn, Dm` |
| FSubRRR | `0x1E603800` | `fsub Dd, Dn, Dm` |
| FMulRRR | `0x1E600800` | `fmul Dd, Dn, Dm` |
| FDivRRR | `0x1E601800` | `fdiv Dd, Dn, Dm` |
| FCmpRR | `0x1E602000` | `fcmp Dn, Dm` (Rd=0, sets flags) |
| FMovRR | `0x1E604000` | `fmov Dd, Dn` |
| FRintN | `0x1E644000` | `frintn Dd, Dn` |

FP encoding: `template | (Rm << 16) | (Rn << 5) | Rd`
(FCmpRR: `template | (Rm << 16) | (Rn << 5) | 0`)

### 2L. Integer ↔ Float Conversions

| MOpcode | Template (hex) | Notes |
|---------|---------------|-------|
| SCvtF | `0x9E620000` | `scvtf Dd, Xn` (sf=1, type=01, rmode=00, opcode=010) |
| FCvtZS | `0x9E780000` | `fcvtzs Xd, Dn` (sf=1, type=01, rmode=11, opcode=000) |
| UCvtF | `0x9E630000` | `ucvtf Dd, Xn` |
| FCvtZU | `0x9E790000` | `fcvtzu Xd, Dn` |
| FMovGR | `0x9E670000` | `fmov Dd, Xn` (GPR to FPR bit transfer) |

Encoding: `template | (Rn << 5) | Rd`

### 2M. Branch Instructions

| MOpcode | Template | Encoding |
|---------|----------|----------|
| Br | `0x14000000` | `b label` → `template \| (imm26 & 0x3FFFFFF)` |
| Bl | `0x94000000` | `bl label` → `template \| (imm26 & 0x3FFFFFF)` |
| BCond | `0x54000000` | `b.cond label` → `template \| ((imm19 & 0x7FFFF) << 5) \| cc` |
| Cbz | `0xB4000000` | `cbz Xt, label` → `template \| ((imm19 & 0x7FFFF) << 5) \| Rt` |
| Cbnz | `0xB5000000` | `cbnz Xt, label` → same pattern |
| Blr | `0xD63F0000` | `blr Xn` → `template \| (Rn << 5)` |
| Ret | `0xD65F03C0` | Fixed encoding (`ret x30`) |

Branch offsets: `imm26 = (targetOffset - branchOffset) / 4` for B/BL
`imm19 = (targetOffset - branchOffset) / 4` for B.cond/CBZ/CBNZ

### 2N. Address Materialization (Mach-O/ELF)

| MOpcode | Template | Notes |
|---------|----------|-------|
| AdrPage | `0x90000000` | `adrp Xd, label` → generates A64AdrpPage21 relocation |
| AddPageOff | AddRI template | `add Xd, Xn, label@PAGEOFF` → generates A64AddPageOff12 relocation |

For ADRP, the immediate is split: `immlo[2]` at bits 29-30, `immhi[19]` at bits 5-23.
The actual value is filled by the linker via relocation.

---

## 3. Encoder Architecture

### CRITICAL: Prologue/Epilogue Are Synthesized at Emission Time

**Unlike x86_64** (where FrameLowering inserts real MIR instructions), the AArch64 backend
synthesizes prologue and epilogue **at emission time** from the `MFunction::FrameLayout` and
saved register lists. The binary encoder MUST replicate this logic from
`AsmEmitter::emitPrologue(FramePlan)` and `AsmEmitter::emitEpilogue(FramePlan)`.

**Prologue sequence** (AsmEmitter.cpp:192-246):
```
stp x29, x30, [sp, #-16]!          # Pre-indexed: save FP+LR, decrement SP
mov x29, sp                         # Set frame pointer
sub sp, sp, #localFrameSize         # Allocate locals (chunked if >4080)
stp x19, x20, [sp, #-16]!          # Save callee-saved GPR pairs (pre-indexed)
str x21, [sp, #-16]!               # Save odd GPR (if count is odd)
stp d8, d9, [sp, #-16]!            # Save callee-saved FPR pairs
str d10, [sp, #-16]!               # Save odd FPR (if count is odd)
```

**Epilogue sequence** (AsmEmitter.cpp:248-293 — reverse order):
```
ldr d10, [sp], #16                  # Restore odd FPR (post-indexed)
ldp d8, d9, [sp], #16              # Restore FPR pairs
ldr x21, [sp], #16                 # Restore odd GPR
ldp x19, x20, [sp], #16            # Restore GPR pairs
add sp, sp, #localFrameSize        # Deallocate locals (chunked if >4080)
ldp x29, x30, [sp], #16            # Restore FP+LR (post-indexed)
ret
```

**Leaf function optimization:** If `MFunction::isLeaf` is true AND no callee-saved registers,
the prologue/epilogue can be skipped entirely.

### CRITICAL: Main Function Runtime Init Injection

**For the `main` function specifically**, the AsmEmitter injects two `bl` calls before user
code (AsmEmitter.cpp:887-895):
```
main:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  bl rt_legacy_context               # Injected — NOT in MIR
  bl rt_set_current_context           # Injected — NOT in MIR
  ; ... user code follows ...
```

The binary encoder must detect when encoding `main` and inject these two call instructions
with `A64Call26` relocations to the runtime symbols.

### Public Interface

```cpp
class A64BinaryEncoder {
public:
    void encodeFunction(const MFunction& fn, CodeSection& text,
                        CodeSection& rodata, ABIFormat abi);

private:
    void encodeInstruction(const MInstr& mi, CodeSection& cs, ABIFormat abi);

    // Prologue/epilogue synthesis (from FrameLayout, NOT MIR instructions)
    void encodePrologue(const MFunction& fn, CodeSection& cs);
    void encodeEpilogue(const MFunction& fn, CodeSection& cs);
    void encodeMainInit(CodeSection& cs);  // rt_legacy_context + rt_set_current_context

    // Multi-instruction sequences
    void encodeMovImm64(uint32_t rd, uint64_t imm, CodeSection& cs);
    void encodeSubSp(int64_t bytes, CodeSection& cs);     // Chunked SP adjust
    void encodeAddSp(int64_t bytes, CodeSection& cs);
    void encodeLargeOffset(uint32_t rt, uint32_t base, int64_t offset,
                           bool isLoad, bool isFPR, CodeSection& cs);

    // Emit a single 32-bit instruction word
    void emit32(uint32_t word, CodeSection& cs) { cs.emit32LE(word); }

    // Label tracking
    std::unordered_map<std::string, size_t> labelOffsets_;
    struct PendingBranch { size_t offset; std::string target; MOpcode kind; };
    std::vector<PendingBranch> pendingBranches_;
};
```

### Branch Resolution

Same two-phase approach as x86_64:
1. Emit all instructions, recording `PendingBranch` for internal labels
2. After all blocks emitted, compute offsets and patch: `cs.patch32LE(offset, word_with_offset)`

For AArch64, the patch must preserve the non-offset bits of the instruction word:
```cpp
uint32_t existing = readLE32(cs.bytes().data() + offset);
uint32_t imm26 = ((targetOffset - offset) / 4) & 0x3FFFFFF;
cs.patch32LE(offset, existing | imm26);  // OR in the offset bits
```

### External Symbols

All `Bl` (call) instructions targeting external symbols (runtime calls like `rt_print_i64`)
generate `A64Call26` relocations with an offset of 0 in the instruction word. The linker fills
in the actual branch offset.

### Symbol Mangling

The binary encoder itself does NOT mangle symbols. Platform-specific mangling (Darwin underscore
prefix) is handled by the ObjectFileWriter during serialization. The encoder stores canonical
names (e.g., `main`, `rt_print_i64`).

---

## 4. Large Offset / Multi-Instruction Sequences

The AsmEmitter handles large offsets by emitting multi-instruction sequences using scratch
register X9. The binary encoder must replicate this:

**Large frame offset (>4095 or negative):**
```
movz x9, #abs(offset)          (possibly movz + movk for >16-bit)
add x9, x29, x9                (or sub for negative)
ldr/str Rt, [x9]
```

**Large SP adjustment (>4080):**
Chunked into multiple `sub sp, sp, #4080` + final `sub sp, sp, #remainder`.

**Large CmpRI (>4095):**
```
movz x16, #imm                 (or movz + movk)
cmp Rn, x16
```

---

## 5. Pseudo-Instruction Handling

### Overflow Pseudos (expanded by LowerOvf.cpp before emission)

| Pseudo | Expansion | Notes |
|--------|-----------|-------|
| AddOvfRRR | `adds Xd, Xn, Xm` + `b.vs .Ltrap_ovf_<fn>` | Trap block contains `bl rt_trap` (noreturn) |
| SubOvfRRR | `subs Xd, Xn, Xm` + `b.vs .Ltrap_ovf_<fn>` | |
| AddOvfRI | `adds Xd, Xn, #imm` + `b.vs .Ltrap_ovf_<fn>` | |
| SubOvfRI | `subs Xd, Xn, #imm` + `b.vs .Ltrap_ovf_<fn>` | |
| MulOvfRRR | `mul Xd, Xn, Xm` + `smulh Xtmp1, Xn, Xm` + `asr Xtmp2, Xd, #63` + `cmp Xtmp1, Xtmp2` + `b.ne .Ltrap_ovf_<fn>` | 5 instructions total. Uses 2 temp virtual registers allocated by LowerOvf. |

### Phi Edge Pseudos (expanded by RegAlloc)

| Pseudo | Expansion |
|--------|-----------|
| PhiStoreGPR | `str Xt, [x29, #offset]` |
| PhiStoreFPR | `str Dt, [x29, #offset]` |

### FMovRI — Requires Special Handling

**Validated finding:** The current text AsmEmitter naively emits `fmov d0, #1.5` and relies
on the system assembler to either accept or reject the value. AArch64 `fmov` with immediate
only supports a **very limited set** of FP8-representable values (about 256 distinct values
with specific exponent/mantissa patterns: `±(1 + b/16) × 2^(n)` where b∈[0,15], n∈[-3,4]).

The binary encoder must handle this properly:
1. Check if the value is an ARM64 FP8 immediate using `isArmFP8Immediate(double val)`
2. If yes: encode as `fmov Dd, #imm8` — the encoding is `0x1E601000 | (imm8 << 13) | Rd`
3. If no: emit the 8-byte IEEE 754 double into `.rodata` CodeSection, then encode:
   - `adrp Xscratch, rodata_label` + `ldr Dd, [Xscratch, rodata_label@PAGEOFF]`
   - Or for same-section reference: `ldr Dd, [PC, #offset]` (PC-relative literal load)

### Scratch Register Usage (Summary)

| Context | Scratch Reg | Notes |
|---------|------------|-------|
| Large CmpRI (>4095) | **X16** (IP0) | Hardcoded in AsmEmitter.cpp:436-456 |
| Large frame offsets | **X9** (kScratchGPR) | Hardcoded in TargetAArch64.hpp |
| Large FPR base offsets | **X9** | Same scratch |
| FMovRI literal pool load | **X9** | For ADRP+LDR sequence |

---

## 6. Validated Design Decisions

### ADRP+ADD Only (No ADRP+LDR for address materialization)

**Validated:** The AArch64 backend CONSISTENTLY uses `ADRP+ADD` for ALL rodata/global address
materialization. Never `ADRP+LDR`. This means:
- Relocations are always `A64AdrpPage21` + `A64AddPageOff12` for rodata references
- `A64LdSt64Off12` is NOT used for initial rodata address loads — it would only apply if
  future code used `LDR Xt, [Xbase, symbol@PAGEOFF]` (direct load from rodata) instead of
  first loading the address then dereferencing

### Large Stack Frame Chunking (>4080 bytes)

**Validated:** The emitter correctly chunks SUB SP into 4080-byte steps (not 4095) to
maintain 16-byte SP alignment between intermediate sub-instructions. This matters because
an interrupt between sub instructions could see a misaligned SP. The binary encoder's
`encodeSubSp()` / `encodeAddSp()` must replicate this: `kMaxImm = 4080`.

### No Branch Range Relaxation

**Documented limitation:** The backend does NOT check branch ranges or implement relaxation
(trampolines/branch islands). Since Viper functions are typically small (well within ±1MB
for conditional branches and ±128MB for unconditional), this is acceptable. If a conditional
branch exceeds ±1MB, the binary encoder should emit a diagnostic error rather than silently
producing invalid code.

### Register X18 Reserved on Apple Platforms

**Validated:** X18 is NEVER allocated by the register allocator (excluded from RegPools).
It is the platform reserved register on Apple Silicon (Darwin). The binary encoder does not
need to handle X18 in any generated instruction.

---

## 7. Absent Features (Documented Non-Support)

Same as x86_64 (Plan 02 §9) — no CFI, no DWARF, no BSS/data, no weak symbols, no TLS,
no GOT, no jump tables. Additionally:

- **No atomic instructions** (DMB, DSB, ISB, CAS): Not generated by IL.
- **No struct returns**: IL only returns single values in X0 or V0.
- **No CSINC/CSINV/CSNEG**: Only CSEL is used for conditional select.
- **No stack canaries**: No buffer overflow protection generated.

---

## Estimated Line Counts

| File | LOC |
|------|-----|
| A64Encoding.hpp | ~120 (templates, condition codes, register helpers) |
| A64BinaryEncoder.hpp | ~60 (interface) |
| A64BinaryEncoder.cpp | ~350 (encoding logic, branch resolution, large-offset sequences) |
| **Total** | **~530** |

Plus tests (~300 LOC):
- One test per instruction category (not per-opcode — AArch64 categories share encoding patterns)
- Branch resolution tests
- Large offset sequence tests
- FP immediate encoding tests
