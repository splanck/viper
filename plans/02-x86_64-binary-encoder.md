# Phase 2: x86_64 Binary Encoder

## Purpose

Encode all 49 x86_64 MIR instruction forms into raw machine code bytes, emitting them into a
CodeSection. This replaces the text AsmEmitter for the binary path.

## Directory

```
src/codegen/x86_64/binenc/
    X64Encoding.hpp         # Register hardware encoding, opcode byte tables
    X64BinaryEncoder.hpp    # Public interface
    X64BinaryEncoder.cpp    # Implementation
```

---

## 1. Register Hardware Encoding (X64Encoding.hpp)

The existing `PhysReg` enum in `TargetX64.hpp` uses an arbitrary ordering that does NOT match
the x86_64 hardware encoding. The binary encoder needs a mapping from `PhysReg` to the
3-bit register number + REX extension bit.

### PhysReg → Hardware Encoding Map

| PhysReg enum | Enum value | Hardware 3-bit | REX ext bit | Notes |
|-------------|-----------|----------------|-------------|-------|
| RAX | 0  | 0 | 0 | |
| RBX | 1  | 3 | 0 | |
| RCX | 2  | 1 | 0 | |
| RDX | 3  | 2 | 0 | |
| RSI | 4  | 6 | 0 | |
| RDI | 5  | 7 | 0 | |
| R8  | 6  | 0 | 1 | |
| R9  | 7  | 1 | 1 | |
| R10 | 8  | 2 | 1 | |
| R11 | 9  | 3 | 1 | |
| R12 | 10 | 4 | 1 | SIB needed as base |
| R13 | 11 | 5 | 1 | Like RBP — mod=00 means disp32 |
| R14 | 12 | 6 | 1 | |
| R15 | 13 | 7 | 1 | |
| RBP | 14 | 5 | 0 | mod=00 means RIP+disp32 |
| RSP | 15 | 4 | 0 | Always needs SIB byte |
| XMM0-7 | 16-23 | 0-7 | 0 | |
| XMM8-15 | 24-31 | 0-7 | 1 | |

### Implementation

```cpp
struct HwReg {
    uint8_t bits3;    // 3-bit register number (0-7)
    uint8_t rexBit;   // 0 or 1, contributes to REX.R, REX.B, or REX.X
};

constexpr HwReg hwEncode(PhysReg reg);  // Lookup table
```

This is a simple `constexpr` array lookup — 32 entries, one per PhysReg.

---

## 2. REX Prefix Computation

The REX prefix byte format: `0100 WRXB`

| Bit | Name | Meaning |
|-----|------|---------|
| W | REX.W | 64-bit operand size |
| R | REX.R | Extends ModR/M reg field to 4 bits |
| X | REX.X | Extends SIB index field to 4 bits |
| B | REX.B | Extends ModR/M r/m field or SIB base to 4 bits |

### Rules

- **Always emit REX** when any of W/R/X/B is needed.
- REX.W is set for all instructions flagged `REXW` in EncodingTable.inc (most GPR instructions).
- REX.R is set when the `reg` field register (destination for loads, source for stores) is R8-R15 or XMM8-15.
- REX.B is set when the `r/m` field register (or SIB base) is R8-R15 or XMM8-15.
- REX.X is set when the SIB index register is R8-R15.
- **Special case:** If none of W/R/X/B is needed but the instruction uses SPL/BPL/SIL/DIL (8-bit), REX is still needed. Not applicable for Viper (we only use 64-bit GPRs).
- **SSE instructions with mandatory prefix (F2/66):** The mandatory prefix comes BEFORE REX.

### Implementation

```cpp
uint8_t computeRex(bool w, bool r, bool x, bool b) {
    uint8_t rex = 0x40;
    if (w) rex |= 0x08;
    if (r) rex |= 0x04;
    if (x) rex |= 0x02;
    if (b) rex |= 0x01;
    return rex;
}

bool needsRex(bool w, bool r, bool x, bool b) {
    return w || r || x || b;
}
```

---

## 3. ModR/M Byte Computation

Format: `[mod(2)][reg(3)][r/m(3)]`

### Mod Values

| mod | Meaning |
|-----|---------|
| 00 | Indirect `[r/m]` (no displacement) |
| 01 | Indirect `[r/m + disp8]` |
| 10 | Indirect `[r/m + disp32]` |
| 11 | Register direct (both operands are registers) |

### Special Cases (Critical for Correctness)

1. **r/m = 100 (RSP/R12 encoding) with mod ≠ 11:** SIB byte follows.
   Even for `[RSP]` with no index, you must emit SIB = `0x24` (scale=0, index=RSP=100, base=RSP=100).

2. **r/m = 101 (RBP/R13 encoding) with mod = 00:** Means `[RIP + disp32]`, NOT `[RBP]`.
   To encode `[RBP]`, use mod=01 with disp8=0.
   To encode `[R13]`, same rule: mod=01 with disp8=0.

3. **mod=00, r/m=101:** RIP-relative addressing. 32-bit displacement follows.
   This is how `OpRipLabel` is encoded.

### Implementation

```cpp
uint8_t makeModRM(uint8_t mod, uint8_t reg, uint8_t rm) {
    return (mod << 6) | ((reg & 7) << 3) | (rm & 7);
}
```

### Memory Operand Encoding Algorithm

Given `OpMem { base, index, scale, disp, hasIndex }`:

```
1. Determine mod:
   - If disp == 0 AND base is not RBP/R13: mod = 00
   - If disp fits in int8_t: mod = 01
   - Otherwise: mod = 10

   EXCEPTION: If base is RBP/R13 and disp == 0, use mod=01 with disp8=0.

2. Determine if SIB is needed:
   - If hasIndex: always SIB
   - If base hardware encoding bits3 == 4 (RSP/R12): always SIB

3. Emit ModR/M:
   - If SIB needed: r/m = 100 (SIB escape)
   - Otherwise: r/m = base.bits3

4. If SIB needed, emit SIB byte:
   - If hasIndex: scale|index.bits3|base.bits3
   - If no index (just RSP/R12 as base): 0x24 (scale=0, index=RSP=100, base=RSP=100)

5. Emit displacement:
   - mod=00: no displacement (except RIP-relative has 4 bytes)
   - mod=01: 1 byte (sign-extended int8_t)
   - mod=10: 4 bytes (int32_t, little-endian)
```

---

## 4. Instruction Encoding Table

Each MIR opcode maps to a binary encoding specification. The table can be extended from the
existing `x86_64_encodings.json` with actual opcode bytes.

### Opcode Byte Table (All 49 Encoding Rows)

#### Nullary Instructions (no operands)

| MOpcode | Bytes | Notes |
|---------|-------|-------|
| RET | `C3` | Near return |
| CQO | `48 99` | REX.W + CQO (sign-extend RAX→RDX:RAX) |
| UD2 | `0F 0B` | Undefined instruction (trap) |

#### Reg-Reg ALU (REX.W + opcode + ModR/M[11,src,dst])

| MOpcode | Opcode byte(s) | Direction | Notes |
|---------|----------------|-----------|-------|
| MOVrr   | `89` | reg=src, r/m=dst | Or `8B` reversed |
| ADDrr   | `01` | reg=src, r/m=dst | |
| SUBrr   | `29` | reg=src, r/m=dst | |
| ANDrr   | `21` | reg=src, r/m=dst | |
| ORrr    | `09` | reg=src, r/m=dst | |
| XORrr   | `31` | reg=src, r/m=dst | |
| CMPrr   | `39` | reg=src, r/m=dst | |
| TESTrr  | `85` | reg=src, r/m=dst | |
| IMULrr  | `0F AF` | reg=dst, r/m=src | **Reversed direction** |
| CMOVNErr | `0F 45` | reg=dst, r/m=src | Condition code 5 (NE) |

#### Reg-Imm ALU (REX.W + opcode + ModR/M[11, /ext, reg] + imm)

These use opcode `81` (imm32) or `83` (imm8) with a `/ext` field in the ModR/M reg bits:

| MOpcode | /ext | Opcode (imm32) | Opcode (imm8) | Notes |
|---------|------|----------------|---------------|-------|
| ADDri | /0 | `81` | `83` | |
| ANDri | /4 | `81` | `83` | |
| ORri  | /1 | `81` | `83` | |
| XORri | /6 | `81` | `83` | |
| CMPri | /7 | `81` | `83` | |

**Optimization:** When the immediate fits in `int8_t` (-128..127), use the shorter `83` form
(saves 3 bytes per instruction).

#### Shift Instructions

| MOpcode | Opcode | /ext | Operand | Notes |
|---------|--------|------|---------|-------|
| SHLri | `C1` | /4 | + imm8 | Shift left by immediate |
| SHRri | `C1` | /5 | + imm8 | Logical shift right |
| SARri | `C1` | /7 | + imm8 | Arithmetic shift right |
| SHLrc | `D3` | /4 | (implicit CL) | Shift left by CL |
| SHRrc | `D3` | /5 | (implicit CL) | Logical shift right by CL |
| SARrc | `D3` | /7 | (implicit CL) | Arithmetic shift right by CL |

#### Division (Unary with REX.W)

| MOpcode | Opcode | /ext | Notes |
|---------|--------|------|-------|
| IDIVrm | `F7` | /7 | Signed divide RDX:RAX by operand |
| DIVrm  | `F7` | /6 | Unsigned divide RDX:RAX by operand |

#### 64-bit Move Immediate

| MOpcode | Encoding | Notes |
|---------|----------|-------|
| MOVri | REX.W + `B8+rd` + imm64 | No ModR/M! `rd` is low 3 bits of reg, REX.B for extension |

This is the only instruction that takes a full 64-bit immediate. The opcode byte is
`B8` + the 3-bit register number (e.g., RAX=B8, RCX=B9, RDX=BA, ...).

#### 32-bit Operations

| MOpcode | Opcode | Notes |
|---------|--------|-------|
| XORrr32 | `31` | Same as XORrr but WITHOUT REX.W prefix (32-bit operation) |
| MOVZXrr32 | `0F B6` | movzbq: zero-extend byte to 64-bit (needs REX.W) |

#### Conditional Branches

| MOpcode | Encoding | Notes |
|---------|----------|-------|
| JMP (label) | `E9` + rel32 | Near jump, 32-bit relative offset |
| JMP (reg) | `FF` + ModR/M(11, /4, reg) | Indirect jump through register |
| JMP (mem) | `FF` + ModR/M(mod, /4, rm) + [SIB] + [disp] | Indirect jump through memory |
| JCC | `0F 8x` + rel32 | `x` is condition code (low nibble). E.g., JE = `0F 84`, JNE = `0F 85` |
| CALL (label) | `E8` + rel32 | Direct call, 32-bit relative |
| CALL (reg) | `FF` + ModR/M(11, /2, reg) | Indirect call through register |
| CALL (mem) | `FF` + ModR/M(mod, /2, rm) + [SIB] + [disp] | Indirect call through memory |

**Indirect variants via memory** are used for vtable dispatch and function pointer calls.
The emitter dispatches on operand type (AsmEmitter.cpp:1069-1080): `OpLabel` → direct,
`OpReg` → indirect reg, `OpMem` → indirect mem. The binary encoder must handle all three.

**Condition Code → Opcode Nibble Mapping:**

| Viper Code | Suffix | x86 CC | JCC opcode (0F 8x) | SETcc opcode (0F 9x) |
|-----------|--------|--------|--------------------|--------------------|
| 0 | e (equal) | 4 | 0F 84 | 0F 94 |
| 1 | ne (not equal) | 5 | 0F 85 | 0F 95 |
| 2 | l (less, signed) | C | 0F 8C | 0F 9C |
| 3 | le (less/equal) | E | 0F 8E | 0F 9E |
| 4 | g (greater, signed) | F | 0F 8F | 0F 9F |
| 5 | ge (greater/equal) | D | 0F 8D | 0F 9D |
| 6 | a (above, unsigned) | 7 | 0F 87 | 0F 97 |
| 7 | ae (above/equal) | 3 | 0F 83 | 0F 93 |
| 8 | b (below, unsigned) | 2 | 0F 82 | 0F 92 |
| 9 | be (below/equal) | 6 | 0F 86 | 0F 96 |
| 10 | p (parity) | A | 0F 8A | 0F 9A |
| 11 | np (no parity) | B | 0F 8B | 0F 9B |
| 12 | o (overflow) | 0 | 0F 80 | 0F 90 |
| 13 | no (no overflow) | 1 | 0F 81 | 0F 91 |

#### SETcc

| MOpcode | Encoding | Notes |
|---------|----------|-------|
| SETcc | `0F 9x` + ModR/M(11, 0, reg8) | `x` from condition mapping above. Uses 8-bit register names (al, bl, cl, etc. — same hardware encoding as 64-bit but mod=11) |

#### Memory Operations (Load/Store)

| MOpcode | Opcode | Direction | Notes |
|---------|--------|-----------|-------|
| MOVrm (store) | `89` | reg=src, r/m=mem | REX.W + `89` + ModR/M + [SIB] + [disp] |
| MOVmr (load) | `8B` | reg=dst, r/m=mem | REX.W + `8B` + ModR/M + [SIB] + [disp] |
| LEA | `8D` | reg=dst, r/m=mem | REX.W + `8D` + ModR/M + [SIB] + [disp] |

These use the full memory encoding algorithm from section 3.

#### SSE Scalar Double Instructions

All SSE scalar double instructions have mandatory prefix `F2` (or `66`), then `0F`, then opcode byte.

| MOpcode | Prefix | Opcode bytes | Direction | Notes |
|---------|--------|-------------|-----------|-------|
| FADD (addsd) | F2 | 0F 58 | reg=dst, r/m=src | |
| FSUB (subsd) | F2 | 0F 5C | reg=dst, r/m=src | |
| FMUL (mulsd) | F2 | 0F 59 | reg=dst, r/m=src | |
| FDIV (divsd) | F2 | 0F 5E | reg=dst, r/m=src | |
| UCOMIS (ucomisd) | 66 | 0F 2E | reg=src1, r/m=src2 | Sets EFLAGS |
| CVTSI2SD | F2 | 0F 2A | reg=dst(xmm), r/m=src(gpr) | REX.W for 64-bit int |
| CVTTSD2SI | F2 | 0F 2C | reg=dst(gpr), r/m=src(xmm) | REX.W for 64-bit int |
| MOVQrx | 66 | 0F 6E | reg=dst(xmm), r/m=src(gpr) | REX.W for 64-bit |
| MOVSDrr | F2 | 0F 10 | reg=dst, r/m=src | xmm-to-xmm |
| MOVSDrm (load) | F2 | 0F 10 | reg=dst, r/m=mem | Load from memory |
| MOVSDmr (store) | F2 | 0F 11 | reg=src, r/m=mem | Store to memory |
| MOVUPSrm (load) | (none) | 0F 10 | reg=dst, r/m=mem | Unaligned packed load |
| MOVUPSmr (store) | (none) | 0F 11 | reg=src, r/m=mem | Unaligned packed store |

**Prefix ordering for SSE+REX:** `[mandatory prefix] [REX] [0F] [opcode]`

Example: `cvtsi2sdq %rax, %xmm0` → `F2 48 0F 2A C0`
- `F2` = mandatory SSE prefix
- `48` = REX.W (64-bit integer operand)
- `0F 2A` = CVTSI2SD opcode
- `C0` = ModR/M(11, xmm0=0, rax=0)

---

## 5. Encoder Architecture

### Public Interface

```cpp
class X64BinaryEncoder {
public:
    /// Encode a complete MIR function into the text CodeSection.
    /// Defines function symbol, encodes all blocks, resolves internal branches.
    void encodeFunction(const MFunction& fn, CodeSection& text,
                        CodeSection& rodata, bool isDarwin);

private:
    // Internal helpers
    void encodeInstruction(const MInstr& instr, CodeSection& cs);
    void encodeRegReg(MOpcode op, PhysReg dst, PhysReg src, CodeSection& cs);
    void encodeRegImm(MOpcode op, PhysReg dst, int64_t imm, CodeSection& cs);
    void encodeRegMem(MOpcode op, PhysReg reg, const OpMem& mem, CodeSection& cs);
    void encodeMemReg(MOpcode op, const OpMem& mem, PhysReg reg, CodeSection& cs);
    void encodeBranch(MOpcode op, const std::string& label, int condCode, CodeSection& cs);
    void encodeCall(const Operand& target, CodeSection& cs);

    // Low-level emission
    void emitRexIfNeeded(bool w, HwReg reg, HwReg rm, CodeSection& cs);
    void emitRexIfNeeded(bool w, HwReg reg, HwReg index, HwReg base, CodeSection& cs);
    void emitModRM(uint8_t mod, HwReg reg, HwReg rm, CodeSection& cs);
    void emitSIB(uint8_t scale, HwReg index, HwReg base, CodeSection& cs);
    void emitMemOperand(HwReg reg, const OpMem& mem, CodeSection& cs);

    // Label tracking for internal branch resolution
    std::unordered_map<std::string, size_t> labelOffsets_;
    struct PendingBranch { size_t patchOffset; std::string target; int instrLen; };
    std::vector<PendingBranch> pendingBranches_;
};
```

### Encoding Flow

For each function:

1. **First pass (optional): Compute label offsets.**
   Since x86_64 uses variable-length encoding, we could either:
   - (a) Emit all instructions, leave branch offsets as 0, then patch in a second pass
   - (b) Use a single pass with forward-reference tracking

   **Recommendation: Single pass with patching.** All internal branches use rel32 (4-byte
   offset), so the instruction length is known even before the target offset. Record
   `PendingBranch` entries during emission, then resolve them after all blocks are emitted.

2. **Emit blocks in order.** For each block:
   - Record label offset in `labelOffsets_`
   - Encode each instruction into the CodeSection

3. **Resolve pending branches.** For each `PendingBranch`:
   - Compute `rel32 = labelOffsets_[target] - (patchOffset + 4)` (offset from end of branch instruction)
   - `cs.patch32LE(patchOffset, rel32)`

4. **External calls and RIP-relative references** generate Relocation entries (not patches).

### RIP-Relative Addend: The Critical -4

When encoding `RIP-relative` references (e.g., `LEA .LC_str_0(%rip), %rdi`), the addend
must account for the 4-byte displacement field itself. The system assembler (GAS) handles
this implicitly when given `symbol(%rip)` syntax, but the binary encoder must compute it
explicitly.

The relocation is placed at the displacement field offset. The CPU computes the effective
address as: `RIP_at_next_instruction + disp32`. Since `RIP_at_next_instruction` points past
the displacement, the addend embedded by the linker already accounts for this. However, **our
Relocation struct must use `addend = -4`** for `PCRel32` relocations from RIP-relative
instructions, because the relocation offset points to the start of the disp32 field, and
the PC value used by the CPU is 4 bytes later (at the end of the disp32 field).

```cpp
// When encoding LEA symbol(%rip), %reg:
cs.emit8(0x48);              // REX.W
cs.emit8(0x8D);              // LEA opcode
cs.emitModRM(0b00, dstReg, 0b101);  // mod=00, rm=101 = RIP-relative
size_t dispOffset = cs.currentOffset();
cs.emit32LE(0);              // placeholder disp32
cs.addRelocation(RelocKind::PCRel32, symbolIndex, /*addend=*/-4);
```

This -4 addend is standard for x86_64 RIP-relative relocations and matches the ELF
`R_X86_64_PC32` and Mach-O `X86_64_RELOC_SIGNED` conventions.

The same applies to `CALL rel32` (`Branch32` relocations): the CPU adds the 32-bit offset
to the address of the byte *after* the call instruction, which is 4 bytes past the start
of the rel32 field. The addend = -4 is baked into the relocation.

---

## 6. Prologue / Epilogue Handling

**Validated:** On x86_64, prologue and epilogue are synthesized by `FrameLowering.cpp` and
inserted as **real MIR instructions** (ADDri, MOVrr, MOVrm, MOVmr, MOVUPSrm/mr) before
emission. They reach the encoder as standard encoding-table instructions.

**Prologue sequence** (FrameLowering.cpp:415-481):
```
ADDri RSP, -8           # push (decrement SP)
MOVrm [RSP+0], RBP      # save old frame pointer
MOVrr RBP, RSP           # establish frame pointer
ADDri RSP, -frameSize    # allocate local frame
[optional: CALL __chkstk for Windows large frames > 4096]
[optional: page probing loop for large frames]
MOVrm [RBP-offset], callee_saved_GPR    # save callee-saved registers
MOVUPSmr [RBP-offset], callee_saved_XMM # save callee-saved XMM registers
```

**Epilogue sequence** (FrameLowering.cpp:524-563):
```
MOVUPSrm callee_saved_XMM, [RBP-offset]  # restore XMMs
MOVmr callee_saved_GPR, [RBP-offset]     # restore GPRs
MOVrr RSP, RBP           # tear down frame
MOVmr RBP, [RSP+0]       # restore old frame pointer
ADDri RSP, 8             # pop
```

**No special handling needed** — the binary encoder treats these as regular instructions
since they use standard encoding table entries.

---

## 7. RoData Emission

The existing `RoDataPool` (in the text AsmEmitter path) accumulates string literals and f64
constants with unique labels. **Validated:** Labels use `.LC_str_N` for strings and `.LC_f64_N`
for doubles (x86_64 naming convention — differs from AArch64's `L.str.N`). Literals are
deduplicated by hash. Emitted via `AsmEmitter::emitRoData()` (AsmEmitter.cpp:248-295).

**Section directives per platform:**
- macOS: `.section __TEXT,__const`
- Windows: `.section .rdata,"dr"`
- Linux: `.section .rodata`

For the binary path:

1. String literals → emitted as raw bytes + NUL terminator into the `rodata` CodeSection
2. Float constants → emitted as 8 raw bytes (IEEE 754 double) into `rodata` CodeSection
3. Each entry gets a symbol defined in `rodata` CodeSection (`.LC_str_N` / `.LC_f64_N` names)
4. References from `.text` to `.rodata` entries generate:
   - x86_64: `PCRel32` relocation (for RIP-relative LEA/MOV)
5. The object file writer places these in the appropriate platform section

---

## 7. Pseudo-Instruction Handling

The following pseudo-instructions are expanded before reaching the binary encoder.
**Validated:** All expansion happens in `Backend.cpp` which calls `LowerDiv` then `LowerOvf`
after ISel, before register allocation. No pseudo reaches EmitPass.

| Pseudo | Expanded by | Real instructions |
|--------|------------|-------------------|
| DIVS64rr | `LowerDiv.cpp` (Backend.cpp:159) | TESTrr + JCC + MOVrr→RAX + CQO + IDIVrm + MOVrr result + JMP. Fast path: detects power-of-2 divisors, replaces with SHR/AND. Creates `.Ltrap_div0_<funcname>` trap block. |
| REMS64rr | `LowerDiv.cpp` | Same as DIVS64rr but result from RDX instead of RAX |
| DIVU64rr | `LowerDiv.cpp` | TESTrr + JCC + XORrr32(RDX) + MOVrr→RAX + DIVrm + MOVrr result + JMP |
| REMU64rr | `LowerDiv.cpp` | Same as DIVU64rr but result from RDX |
| ADDOvfrr | `LowerOvf.cpp` (Backend.cpp:160) | ADDrr + JCC(cond=12/overflow) → `.Ltrap_ovf_<funcname>` containing CALL rt_trap |
| SUBOvfrr | `LowerOvf.cpp` | SUBrr + JCC(overflow) → trap block |
| IMULOvfrr | `LowerOvf.cpp` | IMULrr + JCC(overflow) → trap block |
| PX_COPY | **Not expanded** | Emitted as a comment (`# px_copy`) by AsmEmitter. Not a real instruction. Binary encoder should skip it (no bytes emitted). |
| LABEL | **Not an instruction** | Block label marker. AsmEmitter emits `label_name:\n`. Binary encoder defines symbol at current offset. |

**The binary encoder should:**
- For PX_COPY: skip (no bytes emitted, it's a parallel-copy annotation for debugging)
- For LABEL: define a symbol/label at the current CodeSection offset
- For all others: assert they are absent (indicates pipeline bug if present)

---

## 8. Windows x64 ABI Considerations

The x86_64 backend supports both SysV (Linux/macOS) and Win64 (Windows) calling conventions.
Key differences the binary encoder inherits from the lowering pipeline:

- **Shadow space:** Win64 allocates 32-byte shadow space above the return address for callees
  to save parameters. This is already handled in `CallLowering.cpp` (lines 167-199) and
  `FrameLowering.cpp`. The encoder sees regular SUBri/ADDri instructions.
- **Parameter registers:** SysV uses RDI,RSI,RDX,RCX,R8,R9; Win64 uses RCX,RDX,R8,R9.
  This is handled at lowering time; the encoder sees concrete register operands.
- **Varargs %al:** SysV sets AL to number of XMM args before variadic calls. Win64 skips
  this. Already handled in `CallLowering.cpp:378`.
- **Large stack frames (>4096):** Windows requires page probing (__chkstk). Already lowered
  as concrete MIR instructions. The encoder sees CALL __chkstk + arithmetic.

**No special encoder changes needed** — ABI differences are resolved at lowering time.

---

## 9. Absent Features (Documented Non-Support)

The following features are NOT generated by the current pipeline and thus NOT needed in the
binary encoder. Documenting them here prevents confusion during implementation:

- **CFI directives** (.cfi_startproc, .cfi_endproc, .cfi_def_cfa_offset): NOT emitted by
  either backend. Debugger stack traces will not work with binary-emitted .o files. This is
  a pre-existing limitation shared with the text assembly path.
- **DWARF debug info** (.debug_info, .debug_line): NOT generated. No source-level debugging.
- **BSS/Data sections** (.bss, .data): NOT used. All data is in .rodata or on the stack.
- **Weak symbols** (.weak): NOT used. All symbols are strong.
- **Thread-local storage**: NOT supported in IL.
- **GOT indirection**: NOT used. All references are direct (PC-relative or absolute).
- **Jump tables**: Switch statements use chained JCC, not indexed JMP through .rodata table.

---

## 10. Testing Strategy (Phase 2 Specific)

### Byte-Comparison Tests

For each of the 49 encoding rows, write a test that:
1. Constructs an `MInstr` with known operands
2. Encodes it via `X64BinaryEncoder`
3. Also generates the text assembly string
4. Assembles the text with `cc -c` (system assembler)
5. Extracts `.text` bytes via `objdump -d` or by reading the .o file
6. Compares our bytes against the system assembler's bytes

### Edge Case Tests

- RSP as base register (must emit SIB)
- RBP as base with zero displacement (must use mod=01 + disp8=0)
- R12 as base (needs SIB like RSP)
- R13 as base with zero displacement (like RBP)
- Immediate that fits in int8 (short form) vs int32 (long form)
- XMM8-15 (need REX.R or REX.B for extension)
- RIP-relative addressing with -4 addend (generates relocation)
- Forward and backward branch targets
- All 14 condition codes for JCC and SETcc
- CALL/JMP indirect via register (FF /2, FF /4 with mod=11)
- CALL/JMP indirect via memory (FF /2, FF /4 with mod≠11, full mem encoding)
- SSE prefix ordering: mandatory prefix (F2/66) BEFORE REX, then 0F+opcode
- MOVri with 64-bit immediate (B8+rd encoding, no ModR/M)

---

## Estimated Line Counts

| File | LOC |
|------|-----|
| X64Encoding.hpp | ~150 (register map, opcode tables, condition code map) |
| X64BinaryEncoder.hpp | ~80 (interface) |
| X64BinaryEncoder.cpp | ~550 (encoding logic) |
| **Total** | **~780** |

Plus tests (~400 LOC):
- 49 instruction encoding tests (one per encoding row)
- ~15 edge case tests (special register encodings, imm size selection)
- Branch resolution tests (forward/backward, internal/external)
