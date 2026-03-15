//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/binenc/X64BinaryEncoder.cpp
// Purpose: Encode x86_64 MIR instructions into raw machine code bytes.
//          Implements the full encoding for all 49 instruction forms in the
//          EncodingTable, handling REX prefix computation, ModR/M + SIB
//          generation, and relocation emission for external symbols.
// Key invariants:
//   - REX prefix is emitted only when needed (W/R/X/B set)
//   - RSP/R12 as base always emits SIB byte (hardware requirement)
//   - RBP/R13 with disp=0 uses mod=01 + disp8=0 (mod=00 means RIP-relative)
//   - SSE mandatory prefix (F2/66) comes BEFORE REX in byte stream
//   - All external branch/data references use addend = -4
// Ownership/Lifetime:
//   - Encoder state (labelOffsets_, pendingBranches_) is reset per function
//   - CodeSection is borrowed, not owned
// Links: codegen/x86_64/binenc/X64Encoding.hpp
//        codegen/common/objfile/CodeSection.hpp
//
//===----------------------------------------------------------------------===//

#include "X64BinaryEncoder.hpp"
#include "X64Encoding.hpp"

#include "codegen/common/objfile/DebugLineTable.hpp"

#include <cassert>
#include <cstring>

namespace viper::codegen::x64::binenc
{

// === Helper: extract PhysReg from an OpReg operand ===

static PhysReg toPhys(const OpReg &reg)
{
    assert(reg.isPhys && "binary encoder expects all registers to be physical");
    return static_cast<PhysReg>(reg.idOrPhys);
}

static PhysReg regFromOperand(const Operand &op)
{
    return toPhys(std::get<OpReg>(op));
}

static const OpMem &memFromOperand(const Operand &op)
{
    return std::get<OpMem>(op);
}

static int64_t immFromOperand(const Operand &op)
{
    return std::get<OpImm>(op).val;
}

static const OpLabel &labelFromOperand(const Operand &op)
{
    return std::get<OpLabel>(op);
}

static const OpRipLabel &ripFromOperand(const Operand &op)
{
    return std::get<OpRipLabel>(op);
}

// === Public entry point ===

void X64BinaryEncoder::encodeFunction(const MFunction &fn,
                                      objfile::CodeSection &text,
                                      objfile::CodeSection &rodata,
                                      bool isDarwin)
{
    // Reset per-function state.
    labelOffsets_.clear();
    pendingBranches_.clear();

    // Define the function symbol at the current text offset.
    std::string symName = isDarwin ? ("_" + fn.name) : fn.name;
    text.defineSymbol(symName, objfile::SymbolBinding::Global, objfile::SymbolSection::Text);

    // Encode all blocks.
    for (const auto &block : fn.blocks)
    {
        // Record label offset for internal branch resolution.
        labelOffsets_[block.label] = text.currentOffset();

        for (const auto &instr : block.instructions)
        {
            if (debugLines_ && instr.loc.hasLine())
                debugLines_->addEntry(text.currentOffset(), instr.loc.file_id, instr.loc.line, instr.loc.column);
            encodeInstruction(instr, text, rodata, isDarwin);
        }
    }

    // Resolve pending internal branches.
    for (const auto &pb : pendingBranches_)
    {
        auto it = labelOffsets_.find(pb.target);
        assert(it != labelOffsets_.end() && "unresolved internal branch target");
        // rel32 = target - (patchOffset + 4)
        auto rel = static_cast<int32_t>(static_cast<int64_t>(it->second) -
                                        static_cast<int64_t>(pb.patchOffset + 4));
        text.patch32LE(pb.patchOffset, static_cast<uint32_t>(rel));
    }
}

// === Main instruction dispatch ===

void X64BinaryEncoder::encodeInstruction(const MInstr &instr,
                                         objfile::CodeSection &text,
                                         objfile::CodeSection &rodata,
                                         bool isDarwin)
{
    const auto &ops = instr.operands;
    const auto op = instr.opcode;

    switch (op)
    {
        // --- Nullary ---
        case MOpcode::RET:
        case MOpcode::CQO:
        case MOpcode::UD2:
            encodeNullary(op, text);
            return;

        // --- Pseudo: skip ---
        case MOpcode::PX_COPY:
            return; // No bytes emitted.

        // --- Label definition ---
        case MOpcode::LABEL:
        {
            assert(!ops.empty());
            const auto &label = labelFromOperand(ops[0]);
            labelOffsets_[label.name] = text.currentOffset();
            return;
        }

        // --- Pseudo-instructions that should have been expanded ---
        case MOpcode::DIVS64rr:
        case MOpcode::REMS64rr:
        case MOpcode::DIVU64rr:
        case MOpcode::REMU64rr:
        case MOpcode::ADDOvfrr:
        case MOpcode::SUBOvfrr:
        case MOpcode::IMULOvfrr:
            assert(false && "pseudo-instruction reached binary encoder; pipeline bug");
            return;

        // --- MOVri (64-bit immediate) ---
        case MOpcode::MOVri:
        {
            PhysReg dst = regFromOperand(ops[0]);
            int64_t imm = immFromOperand(ops[1]);
            encodeMovRI(dst, imm, text);
            return;
        }

        // --- Reg-Reg GPR ---
        case MOpcode::MOVrr:
        case MOpcode::ADDrr:
        case MOpcode::SUBrr:
        case MOpcode::ANDrr:
        case MOpcode::ORrr:
        case MOpcode::XORrr:
        case MOpcode::CMPrr:
        case MOpcode::TESTrr:
        case MOpcode::IMULrr:
        case MOpcode::CMOVNErr:
        case MOpcode::XORrr32:
        {
            PhysReg dst = regFromOperand(ops[0]);
            PhysReg src = regFromOperand(ops[1]);
            encodeRegReg(op, dst, src, text);
            return;
        }

        // --- Reg-Imm ALU ---
        case MOpcode::ADDri:
        case MOpcode::ANDri:
        case MOpcode::ORri:
        case MOpcode::XORri:
        case MOpcode::CMPri:
        {
            PhysReg dst = regFromOperand(ops[0]);
            int64_t imm = immFromOperand(ops[1]);
            encodeRegImm(op, dst, imm, text);
            return;
        }

        // --- Shifts (immediate count) ---
        case MOpcode::SHLri:
        case MOpcode::SHRri:
        case MOpcode::SARri:
        {
            PhysReg dst = regFromOperand(ops[0]);
            int64_t count = immFromOperand(ops[1]);
            encodeShiftImm(op, dst, count, text);
            return;
        }

        // --- Shifts (CL count) ---
        case MOpcode::SHLrc:
        case MOpcode::SHRrc:
        case MOpcode::SARrc:
        {
            PhysReg dst = regFromOperand(ops[0]);
            // ops[1] is CL register (implicit, not encoded)
            encodeShiftCL(op, dst, text);
            return;
        }

        // --- Division ---
        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
        {
            // Operand can be reg or mem. Pipeline always uses reg after expansion.
            if (std::holds_alternative<OpReg>(ops[0]))
            {
                encodeDiv(op, regFromOperand(ops[0]), text);
            }
            else
            {
                // Memory operand for div — encode as unary with memory.
                const auto &mem = memFromOperand(ops[0]);
                uint8_t ext = (op == MOpcode::IDIVrm) ? 7 : 6;
                emitWithMemOperand(ext,
                                   0,
                                   mem,
                                   text,
                                   /*rexW=*/true,
                                   /*mandatoryPrefix=*/0,
                                   0xF7,
                                   0);
            }
            return;
        }

        // --- Memory operations ---
        case MOpcode::MOVrm: // store: MOVrm [mem], reg
        {
            PhysReg src = regFromOperand(ops[1]);
            const auto &mem = memFromOperand(ops[0]);
            encodeMemOp(op, src, mem, text);
            return;
        }
        case MOpcode::MOVmr: // load: MOVmr reg, [mem]
        {
            PhysReg dst = regFromOperand(ops[0]);
            const auto &mem = memFromOperand(ops[1]);
            encodeMemOp(op, dst, mem, text);
            return;
        }

        // --- LEA ---
        case MOpcode::LEA:
        {
            PhysReg dst = regFromOperand(ops[0]);
            if (std::holds_alternative<OpRipLabel>(ops[1]))
            {
                encodeLEARip(dst, ripFromOperand(ops[1]), text, rodata, isDarwin);
            }
            else if (std::holds_alternative<OpMem>(ops[1]))
            {
                encodeLEA(dst, memFromOperand(ops[1]), text);
            }
            else
            {
                assert(false && "LEA with unexpected operand type");
            }
            return;
        }

        // --- SETcc ---
        case MOpcode::SETcc:
        {
            int cc = static_cast<int>(immFromOperand(ops[0]));
            PhysReg dst = regFromOperand(ops[1]);
            encodeSETcc(cc, dst, text);
            return;
        }

        // --- MOVZXrr32 (movzbq) ---
        case MOpcode::MOVZXrr32:
        {
            PhysReg dst = regFromOperand(ops[0]);
            PhysReg src = regFromOperand(ops[1]);
            encodeMOVZX(dst, src, text);
            return;
        }

        // --- SSE reg-reg ---
        case MOpcode::FADD:
        case MOpcode::FSUB:
        case MOpcode::FMUL:
        case MOpcode::FDIV:
        case MOpcode::UCOMIS:
        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
        case MOpcode::MOVQrx:
        case MOpcode::MOVSDrr:
        {
            PhysReg dst = regFromOperand(ops[0]);
            PhysReg src = regFromOperand(ops[1]);
            encodeSseRegReg(op, dst, src, text);
            return;
        }

        // --- SSE memory ops ---
        case MOpcode::MOVSDrm: // store: [mem], xmm
        {
            PhysReg src = regFromOperand(ops[1]);
            const auto &mem = memFromOperand(ops[0]);
            encodeSseMem(op, src, mem, text);
            return;
        }
        case MOpcode::MOVSDmr: // load: xmm, [mem]
        {
            PhysReg dst = regFromOperand(ops[0]);
            const auto &mem = memFromOperand(ops[1]);
            encodeSseMem(op, dst, mem, text);
            return;
        }
        case MOpcode::MOVUPSrm: // store: [mem], xmm
        {
            PhysReg src = regFromOperand(ops[1]);
            const auto &mem = memFromOperand(ops[0]);
            encodeSseMem(op, src, mem, text);
            return;
        }
        case MOpcode::MOVUPSmr: // load: xmm, [mem]
        {
            PhysReg dst = regFromOperand(ops[0]);
            const auto &mem = memFromOperand(ops[1]);
            encodeSseMem(op, dst, mem, text);
            return;
        }

        // --- Branches and calls ---
        case MOpcode::JMP:
        {
            if (std::holds_alternative<OpLabel>(ops[0]))
            {
                encodeBranchLabel(op, labelFromOperand(ops[0]).name, 0, text);
            }
            else if (std::holds_alternative<OpReg>(ops[0]))
            {
                encodeBranchReg(op, regFromOperand(ops[0]), text);
            }
            else if (std::holds_alternative<OpMem>(ops[0]))
            {
                encodeBranchMem(op, memFromOperand(ops[0]), text);
            }
            else
            {
                assert(false && "JMP with unexpected operand type");
            }
            return;
        }

        case MOpcode::JCC:
        {
            int cc = static_cast<int>(immFromOperand(ops[0]));
            if (std::holds_alternative<OpLabel>(ops[1]))
            {
                encodeBranchLabel(op, labelFromOperand(ops[1]).name, cc, text);
            }
            else
            {
                assert(false && "JCC with non-label target");
            }
            return;
        }

        case MOpcode::CALL:
        {
            if (std::holds_alternative<OpLabel>(ops[0]))
            {
                const auto &label = labelFromOperand(ops[0]);
                // Check if this is an internal function label.
                auto it = labelOffsets_.find(label.name);
                if (it != labelOffsets_.end())
                {
                    // Internal call — use direct encoding with patch.
                    encodeBranchLabel(op, label.name, 0, text);
                }
                else
                {
                    // External call — generate relocation.
                    encodeCallExternal(label.name, text, isDarwin);
                }
            }
            else if (std::holds_alternative<OpReg>(ops[0]))
            {
                encodeBranchReg(op, regFromOperand(ops[0]), text);
            }
            else if (std::holds_alternative<OpMem>(ops[0]))
            {
                encodeBranchMem(op, memFromOperand(ops[0]), text);
            }
            else
            {
                assert(false && "CALL with unexpected operand type");
            }
            return;
        }
    }

    assert(false && "unhandled MOpcode in binary encoder");
}

// === Nullary instructions ===

void X64BinaryEncoder::encodeNullary(MOpcode op, objfile::CodeSection &cs)
{
    switch (op)
    {
        case MOpcode::RET:
            cs.emit8(0xC3);
            return;
        case MOpcode::CQO:
            cs.emit8(0x48); // REX.W
            cs.emit8(0x99); // CQO
            return;
        case MOpcode::UD2:
            cs.emit8(0x0F);
            cs.emit8(0x0B);
            return;
        default:
            assert(false && "not a nullary opcode");
    }
}

// === Reg-Reg GPR ===

void X64BinaryEncoder::encodeRegReg(MOpcode op, PhysReg dst, PhysReg src, objfile::CodeSection &cs)
{
    const auto info = regRegOpcode(op);
    const auto hwDst = hwEncode(dst);
    const auto hwSrc = hwEncode(src);

    // Determine which register goes in the ModR/M reg vs r/m field.
    const auto &regField = info.regIsDst ? hwDst : hwSrc;
    const auto &rmField = info.regIsDst ? hwSrc : hwDst;

    // REX.W for 64-bit; XORrr32 is the only 32-bit reg-reg opcode.
    bool rexW = (op != MOpcode::XORrr32);

    if (needsRex(rexW, regField.rexBit != 0, false, rmField.rexBit != 0))
    {
        cs.emit8(computeRex(rexW, regField.rexBit != 0, false, rmField.rexBit != 0));
    }

    // Opcode byte(s).
    cs.emit8(info.primary);
    if (info.secondary != 0)
    {
        cs.emit8(info.secondary);
    }

    // ModR/M with mod=11 (register direct).
    cs.emit8(makeModRM(0b11, regField.bits3, rmField.bits3));
}

// === Reg-Imm ALU ===

void X64BinaryEncoder::encodeRegImm(MOpcode op, PhysReg dst, int64_t imm, objfile::CodeSection &cs)
{
    const auto hw = hwEncode(dst);
    uint8_t ext = regImmExt(op);

    // REX.W prefix.
    if (needsRex(true, false, false, hw.rexBit != 0))
    {
        cs.emit8(computeRex(true, false, false, hw.rexBit != 0));
    }

    // Choose short (83 + imm8) or long (81 + imm32) form.
    if (imm >= -128 && imm <= 127)
    {
        cs.emit8(0x83);
        cs.emit8(makeModRM(0b11, ext, hw.bits3));
        cs.emit8(static_cast<uint8_t>(static_cast<int8_t>(imm)));
    }
    else
    {
        assert(imm >= -2147483648LL && imm <= 2147483647LL &&
               "encodeRegImm: immediate exceeds 32-bit range");
        cs.emit8(0x81);
        cs.emit8(makeModRM(0b11, ext, hw.bits3));
        cs.emit32LE(static_cast<uint32_t>(static_cast<int32_t>(imm)));
    }
}

// === Shift instructions ===

void X64BinaryEncoder::encodeShiftImm(MOpcode op,
                                      PhysReg dst,
                                      int64_t count,
                                      objfile::CodeSection &cs)
{
    const auto hw = hwEncode(dst);
    uint8_t ext = shiftExt(op);

    if (needsRex(true, false, false, hw.rexBit != 0))
    {
        cs.emit8(computeRex(true, false, false, hw.rexBit != 0));
    }

    cs.emit8(0xC1); // Shift by imm8.
    cs.emit8(makeModRM(0b11, ext, hw.bits3));
    cs.emit8(static_cast<uint8_t>(count & 0x3F)); // Mask to 6 bits for 64-bit mode.
}

void X64BinaryEncoder::encodeShiftCL(MOpcode op, PhysReg dst, objfile::CodeSection &cs)
{
    const auto hw = hwEncode(dst);
    uint8_t ext = shiftExt(op);

    if (needsRex(true, false, false, hw.rexBit != 0))
    {
        cs.emit8(computeRex(true, false, false, hw.rexBit != 0));
    }

    cs.emit8(0xD3); // Shift by CL.
    cs.emit8(makeModRM(0b11, ext, hw.bits3));
}

// === Division ===

void X64BinaryEncoder::encodeDiv(MOpcode op, PhysReg src, objfile::CodeSection &cs)
{
    const auto hw = hwEncode(src);
    uint8_t ext = (op == MOpcode::IDIVrm) ? 7 : 6;

    if (needsRex(true, false, false, hw.rexBit != 0))
    {
        cs.emit8(computeRex(true, false, false, hw.rexBit != 0));
    }

    cs.emit8(0xF7);
    cs.emit8(makeModRM(0b11, ext, hw.bits3));
}

// === MOVri (64-bit immediate move) ===

void X64BinaryEncoder::encodeMovRI(PhysReg dst, int64_t imm, objfile::CodeSection &cs)
{
    const auto hw = hwEncode(dst);

    if (imm >= 0 && imm <= 0x7FFFFFFF)
    {
        // 5-byte form (or 6 with REX.B): B8+rd + imm32 — zero-extends to 64 bits.
        if (hw.rexBit)
            cs.emit8(computeRex(false, false, false, true));
        cs.emit8(static_cast<uint8_t>(0xB8 + hw.bits3));
        cs.emit32LE(static_cast<uint32_t>(imm));
    }
    else if (imm >= INT32_MIN && imm < 0)
    {
        // 7-byte form: REX.W + C7 /0 + imm32 — sign-extends to 64 bits.
        cs.emit8(computeRex(true, false, false, hw.rexBit != 0));
        cs.emit8(0xC7);
        cs.emit8(makeModRM(0b11, 0, hw.bits3));
        cs.emit32LE(static_cast<uint32_t>(imm));
    }
    else
    {
        // 10-byte form: REX.W + B8+rd + imm64 — full 64-bit.
        cs.emit8(computeRex(true, false, false, hw.rexBit != 0));
        cs.emit8(static_cast<uint8_t>(0xB8 + hw.bits3));
        cs.emit64LE(static_cast<uint64_t>(imm));
    }
}

// === Memory operand encoding ===

void X64BinaryEncoder::emitWithMemOperand(uint8_t reg3,
                                          uint8_t regRex,
                                          const OpMem &mem,
                                          objfile::CodeSection &cs,
                                          bool rexW,
                                          uint8_t mandatoryPrefix,
                                          uint8_t opByte1,
                                          uint8_t opByte2)
{
    const auto hwBase = hwEncode(toPhys(mem.base));
    bool hasSIB = mem.hasIndex || hwBase.bits3 == 4; // RSP/R12 encoding needs SIB

    // Determine mod bits.
    uint8_t mod;
    if (mem.disp == 0 && hwBase.bits3 != 5)
    {
        // mod=00: no displacement. (bits3=5 is RBP/R13 — must use mod=01)
        mod = 0b00;
    }
    else if (mem.disp >= -128 && mem.disp <= 127)
    {
        mod = 0b01; // disp8
    }
    else
    {
        mod = 0b10; // disp32
    }

    // Compute REX bits.
    uint8_t indexRex = 0;
    if (mem.hasIndex)
    {
        indexRex = hwEncode(toPhys(mem.index)).rexBit;
    }

    // Emit mandatory prefix (SSE).
    if (mandatoryPrefix != 0)
    {
        cs.emit8(mandatoryPrefix);
    }

    // Emit REX.
    if (needsRex(rexW, regRex != 0, indexRex != 0, hwBase.rexBit != 0))
    {
        cs.emit8(computeRex(rexW, regRex != 0, indexRex != 0, hwBase.rexBit != 0));
    }

    // Emit opcode.
    cs.emit8(opByte1);
    if (opByte2 != 0)
    {
        cs.emit8(opByte2);
    }

    // Emit ModR/M.
    uint8_t rm3 = hasSIB ? 0b100 : hwBase.bits3;
    cs.emit8(makeModRM(mod, reg3, rm3));

    // Emit SIB if needed.
    if (hasSIB)
    {
        if (mem.hasIndex)
        {
            auto hwIdx = hwEncode(toPhys(mem.index));
            cs.emit8(makeSIB(scaleLog2(mem.scale), hwIdx.bits3, hwBase.bits3));
        }
        else
        {
            // No index — SIB for RSP/R12 base: scale=0, index=RSP(100), base=base.
            cs.emit8(makeSIB(0, 0b100, hwBase.bits3));
        }
    }

    // Emit displacement.
    if (mod == 0b01)
    {
        cs.emit8(static_cast<uint8_t>(static_cast<int8_t>(mem.disp)));
    }
    else if (mod == 0b10)
    {
        cs.emit32LE(static_cast<uint32_t>(mem.disp));
    }
}

// === Memory store/load ===

void X64BinaryEncoder::encodeMemOp(MOpcode op,
                                   PhysReg reg,
                                   const OpMem &mem,
                                   objfile::CodeSection &cs)
{
    const auto hwReg = hwEncode(reg);
    uint8_t opByte;

    switch (op)
    {
        case MOpcode::MOVrm:
            opByte = 0x89;
            break; // store
        case MOpcode::MOVmr:
            opByte = 0x8B;
            break; // load
        default:
            assert(false && "not a memory GPR opcode");
            return;
    }

    emitWithMemOperand(hwReg.bits3,
                       hwReg.rexBit,
                       mem,
                       cs,
                       /*rexW=*/true,
                       /*mandatoryPrefix=*/0,
                       opByte,
                       0);
}

// === LEA ===

void X64BinaryEncoder::encodeLEA(PhysReg dst, const OpMem &mem, objfile::CodeSection &cs)
{
    const auto hwDst = hwEncode(dst);
    emitWithMemOperand(hwDst.bits3,
                       hwDst.rexBit,
                       mem,
                       cs,
                       /*rexW=*/true,
                       /*mandatoryPrefix=*/0,
                       0x8D,
                       0);
}

void X64BinaryEncoder::encodeLEARip(PhysReg dst,
                                    const OpRipLabel &rip,
                                    objfile::CodeSection &text,
                                    objfile::CodeSection &rodata,
                                    bool isDarwin)
{
    const auto hwDst = hwEncode(dst);

    // REX.W + LEA opcode.
    // REX.W prefix.
    if (needsRex(true, hwDst.rexBit != 0, false, false))
    {
        text.emit8(computeRex(true, hwDst.rexBit != 0, false, false));
    }

    // LEA opcode.
    text.emit8(0x8D);

    // ModR/M: mod=00, reg=dst, r/m=101 (RIP-relative).
    text.emit8(makeModRM(0b00, hwDst.bits3, 0b101));

    // Emit placeholder disp32 and record relocation.
    std::string symName = isDarwin ? ("_" + rip.name) : rip.name;
    uint32_t symIdx = text.findOrDeclareSymbol(symName);
    size_t dispOffset = text.currentOffset();
    text.emit32LE(0); // Placeholder.
    text.addRelocationAt(dispOffset, objfile::RelocKind::PCRel32, symIdx, -4);
}

// === SSE reg-reg ===

void X64BinaryEncoder::encodeSseRegReg(MOpcode op,
                                       PhysReg dst,
                                       PhysReg src,
                                       objfile::CodeSection &cs)
{
    const auto info = sseOpcode(op);
    const auto hwDst = hwEncode(dst);
    const auto hwSrc = hwEncode(src);

    const auto &regField = info.regIsDst ? hwDst : hwSrc;
    const auto &rmField = info.regIsDst ? hwSrc : hwDst;

    // Mandatory prefix BEFORE REX.
    if (info.prefix != 0)
    {
        cs.emit8(info.prefix);
    }

    // REX (if needed).
    if (needsRex(info.needsRexW, regField.rexBit != 0, false, rmField.rexBit != 0))
    {
        cs.emit8(computeRex(info.needsRexW, regField.rexBit != 0, false, rmField.rexBit != 0));
    }

    // 0F escape + opcode.
    cs.emit8(0x0F);
    cs.emit8(info.opcode);

    // ModR/M mod=11.
    cs.emit8(makeModRM(0b11, regField.bits3, rmField.bits3));
}

// === SSE memory operations ===

void X64BinaryEncoder::encodeSseMem(MOpcode op,
                                    PhysReg reg,
                                    const OpMem &mem,
                                    objfile::CodeSection &cs)
{
    const auto info = sseOpcode(op);
    const auto hwReg = hwEncode(reg);

    // For SSE memory ops, opByte1=0x0F, opByte2=info.opcode.
    emitWithMemOperand(
        hwReg.bits3, hwReg.rexBit, mem, cs, info.needsRexW, info.prefix, 0x0F, info.opcode);
}

// === SETcc ===

void X64BinaryEncoder::encodeSETcc(int condCode, PhysReg dst, objfile::CodeSection &cs)
{
    const auto hw = hwEncode(dst);
    uint8_t cc = x86CC(condCode);

    // SETcc needs REX only if dst is R8-R15 (to access r/m field extension).
    // SETcc does NOT use REX.W.
    if (needsRex(false, false, false, hw.rexBit != 0))
    {
        cs.emit8(computeRex(false, false, false, hw.rexBit != 0));
    }

    cs.emit8(0x0F);
    cs.emit8(static_cast<uint8_t>(0x90 + cc));
    // ModR/M: mod=11, reg=0, r/m=dst (8-bit register, same hardware encoding).
    cs.emit8(makeModRM(0b11, 0, hw.bits3));
}

// === MOVZXrr32 (movzbq) ===

void X64BinaryEncoder::encodeMOVZX(PhysReg dst, PhysReg src, objfile::CodeSection &cs)
{
    const auto hwDst = hwEncode(dst);
    const auto hwSrc = hwEncode(src);

    // REX.W + 0F B6 + ModR/M(11, dst, src).
    if (needsRex(true, hwDst.rexBit != 0, false, hwSrc.rexBit != 0))
    {
        cs.emit8(computeRex(true, hwDst.rexBit != 0, false, hwSrc.rexBit != 0));
    }

    cs.emit8(0x0F);
    cs.emit8(0xB6);
    cs.emit8(makeModRM(0b11, hwDst.bits3, hwSrc.bits3));
}

// === Branch/Jump/Call with label ===

void X64BinaryEncoder::encodeBranchLabel(MOpcode op,
                                         const std::string &label,
                                         int condCode,
                                         objfile::CodeSection &cs)
{
    // --- Short-form relaxation (backward JMP/JCC only) ---
    // Short JMP  = 0xEB + rel8 (2 bytes, saves 3 over near form)
    // Short JCC  = 0x7x + rel8 (2 bytes, saves 4 over near form)
    // Only for backward branches where the target offset is already known and
    // the displacement fits in a signed byte [-128, 127].
    if (op == MOpcode::JMP || op == MOpcode::JCC)
    {
        auto it = labelOffsets_.find(label);
        if (it != labelOffsets_.end())
        {
            // Short-form instruction is 2 bytes total: opcode + rel8.
            // IP after instruction = currentOffset + 2.
            auto disp =
                static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset() + 2);
            if (disp >= -128 && disp <= 127)
            {
                if (op == MOpcode::JMP)
                    cs.emit8(0xEB); // JMP rel8
                else
                    cs.emit8(static_cast<uint8_t>(0x70 + x86CC(condCode))); // JCC rel8
                cs.emit8(static_cast<uint8_t>(static_cast<int8_t>(disp)));
                return;
            }
        }
    }

    // --- Near-form encoding (JMP rel32 / JCC rel32 / CALL rel32) ---
    size_t patchOffset;

    switch (op)
    {
        case MOpcode::JMP:
            cs.emit8(0xE9); // JMP rel32
            patchOffset = cs.currentOffset();
            cs.emit32LE(0); // Placeholder.
            break;

        case MOpcode::JCC:
            cs.emit8(0x0F);
            cs.emit8(static_cast<uint8_t>(0x80 + x86CC(condCode)));
            patchOffset = cs.currentOffset();
            cs.emit32LE(0);
            break;

        case MOpcode::CALL:
            cs.emit8(0xE8); // CALL rel32
            patchOffset = cs.currentOffset();
            cs.emit32LE(0);
            break;

        default:
            assert(false && "not a branch opcode");
            return;
    }

    // Check if target is already known (backward branch, near form).
    auto it = labelOffsets_.find(label);
    if (it != labelOffsets_.end())
    {
        auto rel = static_cast<int32_t>(static_cast<int64_t>(it->second) -
                                        static_cast<int64_t>(patchOffset + 4));
        cs.patch32LE(patchOffset, static_cast<uint32_t>(rel));
    }
    else
    {
        // Forward branch — record for later patching.
        pendingBranches_.push_back({patchOffset, label});
    }
}

// === Branch/Call indirect via register ===

void X64BinaryEncoder::encodeBranchReg(MOpcode op, PhysReg target, objfile::CodeSection &cs)
{
    const auto hw = hwEncode(target);
    uint8_t ext = (op == MOpcode::CALL) ? 2 : 4; // /2 for CALL, /4 for JMP

    // REX only needed for R8-R15 targets. No REX.W for indirect branch.
    if (needsRex(false, false, false, hw.rexBit != 0))
    {
        cs.emit8(computeRex(false, false, false, hw.rexBit != 0));
    }

    cs.emit8(0xFF);
    cs.emit8(makeModRM(0b11, ext, hw.bits3));
}

// === Branch/Call indirect via memory ===

void X64BinaryEncoder::encodeBranchMem(MOpcode op, const OpMem &mem, objfile::CodeSection &cs)
{
    uint8_t ext = (op == MOpcode::CALL) ? 2 : 4; // /2 for CALL, /4 for JMP
    emitWithMemOperand(ext,
                       0,
                       mem,
                       cs,
                       /*rexW=*/false,
                       /*mandatoryPrefix=*/0,
                       0xFF,
                       0);
}

// === External CALL (generates relocation) ===

void X64BinaryEncoder::encodeCallExternal(const std::string &name,
                                          objfile::CodeSection &cs,
                                          bool isDarwin)
{
    std::string symName = isDarwin ? ("_" + name) : name;
    uint32_t symIdx = cs.findOrDeclareSymbol(symName);

    cs.emit8(0xE8); // CALL rel32
    size_t dispOffset = cs.currentOffset();
    cs.emit32LE(0); // Placeholder.
    cs.addRelocationAt(dispOffset, objfile::RelocKind::Branch32, symIdx, -4);
}

} // namespace viper::codegen::x64::binenc
