//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/binenc/A64BinaryEncoder.cpp
// Purpose: AArch64 MIR-to-machine-code binary encoder implementation.
//          Encodes all non-pseudo AArch64 MIR opcodes into 32-bit instruction
//          words, synthesizes prologue/epilogue from function metadata, and
//          resolves internal branches via deferred patching.
// Key invariants:
//   - Every instruction is exactly 4 bytes, emitted via emit32()
//   - Prologue/epilogue follow AsmEmitter.cpp logic exactly
//   - SP adjustments are chunked at 4080 (not 4095) for alignment safety
//   - External BL generates A64Call26 relocation; ADRP/ADD generate page relocs
// Ownership/Lifetime:
//   - State (labelOffsets_, pendingBranches_) is cleared per encodeFunction() call
// Links: codegen/aarch64/binenc/A64Encoding.hpp
//        codegen/aarch64/AsmEmitter.cpp (reference for prologue/epilogue logic)
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/binenc/A64BinaryEncoder.hpp"

#include "codegen/aarch64/binenc/A64Encoding.hpp"
#include "codegen/common/LabelUtil.hpp"
#include "il/runtime/RuntimeNameMap.hpp"

#include <cassert>
#include <cstring>

namespace viper::codegen::aarch64::binenc
{

// === Helpers ===

/// Map IL extern names to C runtime symbol names.
static std::string mapRuntimeSymbol(const std::string &name)
{
    if (auto mapped = il::runtime::mapCanonicalRuntimeName(name))
        return std::string(*mapped);
    return name;
}

/// Sanitize a label for internal use (replace hyphens, etc.).
static std::string sanitizeLabel(const std::string &name)
{
    return viper::codegen::common::sanitizeLabel(name);
}

/// Extract PhysReg from a register operand.
static PhysReg getReg(const MOperand &op)
{
    assert(op.kind == MOperand::Kind::Reg && "expected reg operand");
    assert(op.reg.isPhys && "unallocated vreg reached binary encoder");
    return static_cast<PhysReg>(op.reg.idOrPhys);
}

/// Extract immediate value from an operand.
static long long getImm(const MOperand &op)
{
    assert(op.kind == MOperand::Kind::Imm && "expected imm operand");
    return op.imm;
}

/// Check if offset fits in signed 9-bit range for ldur/stur.
static bool isInSignedImmRange(long long offset)
{
    return offset >= -256 && offset <= 255;
}

// =============================================================================
// encodeFunction
// =============================================================================

void A64BinaryEncoder::encodeFunction(const MFunction &fn,
                                      objfile::CodeSection &text,
                                      objfile::CodeSection &rodata,
                                      ABIFormat abi)
{
    (void)rodata; // Reserved for FMovRI literal pool (future)
    (void)abi;    // Symbol mangling deferred to ObjectFileWriter

    labelOffsets_.clear();
    pendingBranches_.clear();
    currentFn_ = &fn;

    // Define function symbol at current offset.
    text.defineSymbol(fn.name, objfile::SymbolBinding::Global, objfile::SymbolSection::Text);

    // Leaf function optimization: skip frame when no calls, no callee-saved, no locals.
    // Exclude main because we inject bl calls to runtime init.
    skipFrame_ = fn.isLeaf && fn.savedGPRs.empty() && fn.savedFPRs.empty() &&
                 fn.localFrameSize == 0 && fn.name != "main";
    usePlan_ = !fn.savedGPRs.empty() || fn.localFrameSize > 0;

    // Emit prologue.
    if (!skipFrame_)
        encodePrologue(fn, text);

    // For main, inject runtime context initialization.
    if (fn.name == "main")
        encodeMainInit(text);

    // Encode all blocks.
    for (const auto &bb : fn.blocks)
    {
        if (!bb.name.empty())
            labelOffsets_[sanitizeLabel(bb.name)] = text.currentOffset();

        for (const auto &mi : bb.instrs)
            encodeInstruction(mi, text);
    }

    // Resolve pending internal branches.
    for (const auto &pb : pendingBranches_)
    {
        auto it = labelOffsets_.find(pb.target);
        assert(it != labelOffsets_.end() && "unresolved internal branch target");

        size_t targetOff = it->second;
        int64_t delta = static_cast<int64_t>(targetOff) - static_cast<int64_t>(pb.offset);

        // Read existing instruction word.
        const uint8_t *p = text.bytes().data() + pb.offset;
        uint32_t word = static_cast<uint32_t>(p[0]) |
                        (static_cast<uint32_t>(p[1]) << 8) |
                        (static_cast<uint32_t>(p[2]) << 16) |
                        (static_cast<uint32_t>(p[3]) << 24);

        if (pb.kind == MOpcode::Br || pb.kind == MOpcode::Bl)
        {
            // B/BL: 26-bit signed offset in units of 4 bytes.
            int32_t imm26 = static_cast<int32_t>(delta / 4);
            word |= (static_cast<uint32_t>(imm26) & 0x3FFFFFF);
        }
        else
        {
            // B.cond/CBZ/CBNZ: 19-bit signed offset in units of 4 bytes.
            int32_t imm19 = static_cast<int32_t>(delta / 4);
            word |= ((static_cast<uint32_t>(imm19) & 0x7FFFF) << 5);
        }

        text.patch32LE(pb.offset, word);
    }

    currentFn_ = nullptr;
}

// =============================================================================
// Prologue/Epilogue Synthesis
// =============================================================================

void A64BinaryEncoder::encodePrologue(const MFunction &fn, objfile::CodeSection &cs)
{
    const uint32_t sp = hwGPR(PhysReg::SP);
    const uint32_t fp = hwGPR(PhysReg::X29);
    const uint32_t lr = hwGPR(PhysReg::X30);

    // stp x29, x30, [sp, #-16]!  (pre-indexed, -16/8 = -2)
    emit32(encodePair(kStpGprPre, fp, lr, sp, static_cast<int32_t>(-16 / 8)), cs);

    // mov x29, sp  →  add x29, sp, #0
    emit32(encodeAddSubImm(kAddRI, fp, sp, 0), cs);

    // Allocate local frame.
    if (fn.localFrameSize > 0)
        encodeSubSp(fn.localFrameSize, cs);

    // Save callee-saved GPRs.
    for (size_t i = 0; i < fn.savedGPRs.size();)
    {
        uint32_t r0 = hwGPR(fn.savedGPRs[i++]);
        if (i < fn.savedGPRs.size())
        {
            uint32_t r1 = hwGPR(fn.savedGPRs[i++]);
            // stp r0, r1, [sp, #-16]!  → pre-indexed, imm7 = -16/8 = -2
            emit32(encodePair(kStpGprPre, r0, r1, sp, static_cast<int32_t>(-16 / 8)), cs);
        }
        else
        {
            // str r0, [sp, #-16]!  → pre-indexed single
            emit32(kStrGprPre | ((static_cast<uint32_t>(-16) & 0x1FF) << 12) | (sp << 5) | r0, cs);
        }
    }

    // Save callee-saved FPRs.
    for (size_t i = 0; i < fn.savedFPRs.size();)
    {
        uint32_t r0 = hwFPR(fn.savedFPRs[i++]);
        if (i < fn.savedFPRs.size())
        {
            uint32_t r1 = hwFPR(fn.savedFPRs[i++]);
            emit32(encodePair(kStpFprPre, r0, r1, sp, static_cast<int32_t>(-16 / 8)), cs);
        }
        else
        {
            emit32(kStrFprPre | ((static_cast<uint32_t>(-16) & 0x1FF) << 12) | (sp << 5) | r0, cs);
        }
    }
}

void A64BinaryEncoder::encodeEpilogue(const MFunction &fn, objfile::CodeSection &cs)
{
    const uint32_t sp = hwGPR(PhysReg::SP);
    const uint32_t fp = hwGPR(PhysReg::X29);
    const uint32_t lr = hwGPR(PhysReg::X30);

    // Restore callee-saved GPRs (reverse order).
    size_t n = fn.savedGPRs.size();
    if (n % 2 == 1)
    {
        uint32_t r0 = hwGPR(fn.savedGPRs[n - 1]);
        // ldr r0, [sp], #16  → post-indexed
        emit32(kLdrGprPost | ((16u & 0x1FF) << 12) | (sp << 5) | r0, cs);
        --n;
    }
    while (n > 0)
    {
        uint32_t r1 = hwGPR(fn.savedGPRs[n - 1]);
        uint32_t r0 = hwGPR(fn.savedGPRs[n - 2]);
        // ldp r0, r1, [sp], #16  → post-indexed, imm7 = 16/8 = 2
        emit32(encodePair(kLdpGprPost, r0, r1, sp, static_cast<int32_t>(16 / 8)), cs);
        n -= 2;
    }

    // Restore callee-saved FPRs (reverse order).
    size_t nf = fn.savedFPRs.size();
    if (nf % 2 == 1)
    {
        uint32_t r0 = hwFPR(fn.savedFPRs[nf - 1]);
        emit32(kLdrFprPost | ((16u & 0x1FF) << 12) | (sp << 5) | r0, cs);
        --nf;
    }
    while (nf > 0)
    {
        uint32_t r1 = hwFPR(fn.savedFPRs[nf - 1]);
        uint32_t r0 = hwFPR(fn.savedFPRs[nf - 2]);
        emit32(encodePair(kLdpFprPost, r0, r1, sp, static_cast<int32_t>(16 / 8)), cs);
        nf -= 2;
    }

    // Deallocate local frame.
    if (fn.localFrameSize > 0)
        encodeAddSp(fn.localFrameSize, cs);

    // ldp x29, x30, [sp], #16  → post-indexed, imm7 = 16/8 = 2
    emit32(encodePair(kLdpGprPost, fp, lr, sp, static_cast<int32_t>(16 / 8)), cs);

    // ret
    emit32(kRet, cs);
}

void A64BinaryEncoder::encodeMainInit(objfile::CodeSection &cs)
{
    // bl rt_legacy_context
    {
        std::string sym = mapRuntimeSymbol("rt_legacy_context");
        uint32_t symIdx = cs.findOrDeclareSymbol(sym);
        cs.addRelocation(objfile::RelocKind::A64Call26, symIdx, 0);
        emit32(kBl, cs); // imm26 = 0, filled by linker
    }
    // bl rt_set_current_context
    {
        std::string sym = mapRuntimeSymbol("rt_set_current_context");
        uint32_t symIdx = cs.findOrDeclareSymbol(sym);
        cs.addRelocation(objfile::RelocKind::A64Call26, symIdx, 0);
        emit32(kBl, cs);
    }
}

// =============================================================================
// Multi-instruction sequences
// =============================================================================

void A64BinaryEncoder::encodeMovImm64(uint32_t rd, uint64_t imm, objfile::CodeSection &cs)
{
    uint16_t chunks[4] = {
        static_cast<uint16_t>(imm & 0xFFFF),
        static_cast<uint16_t>((imm >> 16) & 0xFFFF),
        static_cast<uint16_t>((imm >> 32) & 0xFFFF),
        static_cast<uint16_t>((imm >> 48) & 0xFFFF),
    };

    // movz Xd, #chunk0
    emit32(kMovZ | (static_cast<uint32_t>(chunks[0]) << 5) | rd, cs);

    // movk for non-zero higher chunks
    if (chunks[1])
        emit32(kMovK16 | (static_cast<uint32_t>(chunks[1]) << 5) | rd, cs);
    if (chunks[2])
        emit32(kMovK32 | (static_cast<uint32_t>(chunks[2]) << 5) | rd, cs);
    if (chunks[3])
        emit32(kMovK48 | (static_cast<uint32_t>(chunks[3]) << 5) | rd, cs);
}

void A64BinaryEncoder::encodeSubSp(int64_t bytes, objfile::CodeSection &cs)
{
    const uint32_t sp = hwGPR(PhysReg::SP);
    constexpr int64_t kMaxImm = 4080;
    while (bytes > kMaxImm)
    {
        emit32(encodeAddSubImm(kSubRI, sp, sp, static_cast<uint32_t>(kMaxImm)), cs);
        bytes -= kMaxImm;
    }
    if (bytes > 0)
        emit32(encodeAddSubImm(kSubRI, sp, sp, static_cast<uint32_t>(bytes)), cs);
}

void A64BinaryEncoder::encodeAddSp(int64_t bytes, objfile::CodeSection &cs)
{
    const uint32_t sp = hwGPR(PhysReg::SP);
    constexpr int64_t kMaxImm = 4080;
    while (bytes > kMaxImm)
    {
        emit32(encodeAddSubImm(kAddRI, sp, sp, static_cast<uint32_t>(kMaxImm)), cs);
        bytes -= kMaxImm;
    }
    if (bytes > 0)
        emit32(encodeAddSubImm(kAddRI, sp, sp, static_cast<uint32_t>(bytes)), cs);
}

void A64BinaryEncoder::encodeLargeOffsetLdSt(uint32_t rt, uint32_t base, int64_t offset,
                                              bool isLoad, bool isFPR, objfile::CodeSection &cs)
{
    // Use scratch X9 to materialise the effective address.
    const uint32_t scratch = hwGPR(PhysReg::X9);
    encodeMovImm64(scratch, static_cast<uint64_t>(offset), cs);
    // add x9, base, x9
    emit32(encode3Reg(kAddRRR, scratch, base, scratch), cs);
    // ldr/str rt, [x9]  (offset 0)
    if (isFPR)
        emit32((isLoad ? kLdrFpr : kStrFpr) | (0 << 10) | (scratch << 5) | rt, cs);
    else
        emit32((isLoad ? kLdrGpr : kStrGpr) | (0 << 10) | (scratch << 5) | rt, cs);
}

// =============================================================================
// encodeInstruction — main dispatch
// =============================================================================

void A64BinaryEncoder::encodeInstruction(const MInstr &mi, objfile::CodeSection &cs)
{
    switch (mi.opc)
    {
    // ─── Ret (triggers epilogue synthesis) ───
    case MOpcode::Ret:
        if (skipFrame_)
            emit32(kRet, cs);
        else
            encodeEpilogue(*currentFn_, cs);
        return;

    // ─── Data Processing — Three-Register ───
    case MOpcode::AddRRR:
        emit32(encode3Reg(kAddRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::SubRRR:
        emit32(encode3Reg(kSubRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::AndRRR:
        emit32(encode3Reg(kAndRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::OrrRRR:
        emit32(encode3Reg(kOrrRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::EorRRR:
        emit32(encode3Reg(kEorRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::AddsRRR:
        emit32(encode3Reg(kAddsRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::SubsRRR:
        emit32(encode3Reg(kSubsRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;

    // ─── Variable Shift ───
    case MOpcode::LslvRRR:
        emit32(encode3Reg(kLslvRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::LsrvRRR:
        emit32(encode3Reg(kLsrvRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::AsrvRRR:
        emit32(encode3Reg(kAsrvRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;

    // ─── Multiply / Divide ───
    case MOpcode::MulRRR:
        emit32(encode3Reg(kMulRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::SmulhRRR:
        emit32(encode3Reg(kSmulhRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::SDivRRR:
        emit32(encode3Reg(kSDivRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::UDivRRR:
        emit32(encode3Reg(kUDivRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::MSubRRRR:
        emit32(encode4Reg(kMSubRRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2])), hwGPR(getReg(mi.ops[3]))), cs);
        return;
    case MOpcode::MAddRRRR:
        emit32(encode4Reg(kMAddRRRR, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                          hwGPR(getReg(mi.ops[2])), hwGPR(getReg(mi.ops[3]))), cs);
        return;

    // ─── Add/Sub Immediate ───
    case MOpcode::AddRI:
        emit32(encodeAddSubImm(kAddRI, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                               static_cast<uint32_t>(getImm(mi.ops[2]))), cs);
        return;
    case MOpcode::SubRI:
        emit32(encodeAddSubImm(kSubRI, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                               static_cast<uint32_t>(getImm(mi.ops[2]))), cs);
        return;
    case MOpcode::AddsRI:
        emit32(encodeAddSubImm(kAddsRI, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                               static_cast<uint32_t>(getImm(mi.ops[2]))), cs);
        return;
    case MOpcode::SubsRI:
        emit32(encodeAddSubImm(kSubsRI, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1])),
                               static_cast<uint32_t>(getImm(mi.ops[2]))), cs);
        return;

    // ─── Move ───
    case MOpcode::MovRR:
        // orr Xd, XZR, Xm
        emit32(kMovRR | (hwGPR(getReg(mi.ops[1])) << 16) | hwGPR(getReg(mi.ops[0])), cs);
        return;
    case MOpcode::MovRI:
    {
        long long imm = getImm(mi.ops[1]);
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        if (!needsWideImmSequence(imm))
        {
            // movz Xd, #imm16 (or mov alias handled by assembler — we emit movz directly)
            emit32(kMovZ | (static_cast<uint32_t>(imm & 0xFFFF) << 5) | rd, cs);
        }
        else
        {
            encodeMovImm64(rd, static_cast<uint64_t>(imm), cs);
        }
        return;
    }

    // ─── Shift by Immediate ───
    case MOpcode::LslRI:
    {
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        uint32_t rn = hwGPR(getReg(mi.ops[1]));
        auto sh = static_cast<uint32_t>(getImm(mi.ops[2]));
        // lsl is ubfm Xd, Xn, #(64-n)&63, #(63-n)
        uint32_t immr = (64 - sh) & 63;
        uint32_t imms = 63 - sh;
        emit32(kUbfm | (immr << 16) | (imms << 10) | (rn << 5) | rd, cs);
        return;
    }
    case MOpcode::LsrRI:
    {
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        uint32_t rn = hwGPR(getReg(mi.ops[1]));
        auto sh = static_cast<uint32_t>(getImm(mi.ops[2]));
        // lsr is ubfm Xd, Xn, #n, #63
        emit32(kUbfm | (sh << 16) | (63 << 10) | (rn << 5) | rd, cs);
        return;
    }
    case MOpcode::AsrRI:
    {
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        uint32_t rn = hwGPR(getReg(mi.ops[1]));
        auto sh = static_cast<uint32_t>(getImm(mi.ops[2]));
        // asr is sbfm Xd, Xn, #n, #63
        emit32(kSbfm | (sh << 16) | (63 << 10) | (rn << 5) | rd, cs);
        return;
    }

    // ─── Compare / Test ───
    case MOpcode::CmpRR:
        // subs XZR, Xn, Xm
        emit32(encode3Reg(kSubsRRR, 31, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1]))), cs);
        return;
    case MOpcode::CmpRI:
    {
        long long imm = getImm(mi.ops[1]);
        uint32_t rn = hwGPR(getReg(mi.ops[0]));
        if (imm >= 0 && imm <= 4095)
        {
            // subs XZR, Xn, #imm
            emit32(encodeAddSubImm(kSubsRI, 31, rn, static_cast<uint32_t>(imm)), cs);
        }
        else if (imm >= -4095 && imm < 0)
        {
            // cmn = adds XZR, Xn, #(-imm)
            emit32(encodeAddSubImm(kAddsRI, 31, rn, static_cast<uint32_t>(-imm)), cs);
        }
        else
        {
            // Large: movz x16, #imm; cmp Xn, x16
            uint32_t scratch = hwGPR(PhysReg::X16);
            encodeMovImm64(scratch, static_cast<uint64_t>(imm), cs);
            emit32(encode3Reg(kSubsRRR, 31, rn, scratch), cs);
        }
        return;
    }
    case MOpcode::TstRR:
        // ands XZR, Xn, Xm
        emit32(encode3Reg(kAndsRRR, 31, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1]))), cs);
        return;

    // ─── Conditional ───
    case MOpcode::Cset:
    {
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        uint32_t cc = condCode(mi.ops[1].cond);
        // csinc Xd, XZR, XZR, invert(cond)
        emit32(kCset | (invertCond(cc) << 12) | rd, cs);
        return;
    }
    case MOpcode::Csel:
    {
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        uint32_t rn = hwGPR(getReg(mi.ops[1]));
        uint32_t rm = hwGPR(getReg(mi.ops[2]));
        uint32_t cc = condCode(mi.ops[3].cond);
        emit32(kCsel | (rm << 16) | (cc << 12) | (rn << 5) | rd, cs);
        return;
    }

    // ─── Load/Store (FP-relative) ───
    case MOpcode::LdrRegFpImm:
    case MOpcode::PhiStoreGPR: // After RA, same encoding as StrRegFpImm
    {
        uint32_t rt = hwGPR(getReg(mi.ops[0]));
        long long offset = getImm(mi.ops[1]);
        uint32_t fp = hwGPR(PhysReg::X29);
        bool isLoad = (mi.opc == MOpcode::LdrRegFpImm);

        if (isInSignedImmRange(offset))
        {
            uint32_t tmpl = isLoad ? kLdurGpr : kSturGpr;
            emit32(tmpl | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (fp << 5) | rt, cs);
        }
        else
        {
            encodeLargeOffsetLdSt(rt, fp, offset, isLoad, false, cs);
        }
        return;
    }
    case MOpcode::StrRegFpImm:
    {
        uint32_t rt = hwGPR(getReg(mi.ops[0]));
        long long offset = getImm(mi.ops[1]);
        uint32_t fp = hwGPR(PhysReg::X29);

        if (isInSignedImmRange(offset))
            emit32(kSturGpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (fp << 5) | rt, cs);
        else
            encodeLargeOffsetLdSt(rt, fp, offset, false, false, cs);
        return;
    }
    case MOpcode::LdrFprFpImm:
    case MOpcode::PhiStoreFPR:
    {
        uint32_t rt = hwFPR(getReg(mi.ops[0]));
        long long offset = getImm(mi.ops[1]);
        uint32_t fp = hwGPR(PhysReg::X29);
        bool isLoad = (mi.opc == MOpcode::LdrFprFpImm);

        if (isInSignedImmRange(offset))
        {
            uint32_t tmpl = isLoad ? kLdurFpr : kSturFpr;
            emit32(tmpl | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (fp << 5) | rt, cs);
        }
        else
        {
            encodeLargeOffsetLdSt(rt, fp, offset, isLoad, true, cs);
        }
        return;
    }
    case MOpcode::StrFprFpImm:
    {
        uint32_t rt = hwFPR(getReg(mi.ops[0]));
        long long offset = getImm(mi.ops[1]);
        uint32_t fp = hwGPR(PhysReg::X29);

        if (isInSignedImmRange(offset))
            emit32(kSturFpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (fp << 5) | rt, cs);
        else
            encodeLargeOffsetLdSt(rt, fp, offset, false, true, cs);
        return;
    }

    // ─── Load/Store (Base-relative) ───
    case MOpcode::LdrRegBaseImm:
    {
        uint32_t rt = hwGPR(getReg(mi.ops[0]));
        uint32_t base = hwGPR(getReg(mi.ops[1]));
        long long offset = getImm(mi.ops[2]);

        if (isInSignedImmRange(offset))
            emit32(kLdurGpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (base << 5) | rt, cs);
        else
            encodeLargeOffsetLdSt(rt, base, offset, true, false, cs);
        return;
    }
    case MOpcode::StrRegBaseImm:
    {
        uint32_t rt = hwGPR(getReg(mi.ops[0]));
        uint32_t base = hwGPR(getReg(mi.ops[1]));
        long long offset = getImm(mi.ops[2]);

        if (isInSignedImmRange(offset))
            emit32(kSturGpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (base << 5) | rt, cs);
        else
            encodeLargeOffsetLdSt(rt, base, offset, false, false, cs);
        return;
    }
    case MOpcode::LdrFprBaseImm:
    {
        uint32_t rt = hwFPR(getReg(mi.ops[0]));
        uint32_t base = hwGPR(getReg(mi.ops[1]));
        long long offset = getImm(mi.ops[2]);

        if (isInSignedImmRange(offset))
            emit32(kLdurFpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (base << 5) | rt, cs);
        else
            encodeLargeOffsetLdSt(rt, base, offset, true, true, cs);
        return;
    }
    case MOpcode::StrFprBaseImm:
    {
        uint32_t rt = hwFPR(getReg(mi.ops[0]));
        uint32_t base = hwGPR(getReg(mi.ops[1]));
        long long offset = getImm(mi.ops[2]);

        if (isInSignedImmRange(offset))
            emit32(kSturFpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (base << 5) | rt, cs);
        else
            encodeLargeOffsetLdSt(rt, base, offset, false, true, cs);
        return;
    }

    // ─── Load/Store Pair (FP-relative) ───
    case MOpcode::LdpRegFpImm:
    {
        uint32_t rt = hwGPR(getReg(mi.ops[0]));
        uint32_t rt2 = hwGPR(getReg(mi.ops[1]));
        auto offset = static_cast<int32_t>(getImm(mi.ops[2]));
        uint32_t fp = hwGPR(PhysReg::X29);
        emit32(encodePair(kLdpGpr, rt, rt2, fp, offset / 8), cs);
        return;
    }
    case MOpcode::StpRegFpImm:
    {
        uint32_t rt = hwGPR(getReg(mi.ops[0]));
        uint32_t rt2 = hwGPR(getReg(mi.ops[1]));
        auto offset = static_cast<int32_t>(getImm(mi.ops[2]));
        uint32_t fp = hwGPR(PhysReg::X29);
        emit32(encodePair(kStpGpr, rt, rt2, fp, offset / 8), cs);
        return;
    }
    case MOpcode::LdpFprFpImm:
    {
        uint32_t rt = hwFPR(getReg(mi.ops[0]));
        uint32_t rt2 = hwFPR(getReg(mi.ops[1]));
        auto offset = static_cast<int32_t>(getImm(mi.ops[2]));
        uint32_t fp = hwGPR(PhysReg::X29);
        emit32(encodePair(kLdpFpr, rt, rt2, fp, offset / 8), cs);
        return;
    }
    case MOpcode::StpFprFpImm:
    {
        uint32_t rt = hwFPR(getReg(mi.ops[0]));
        uint32_t rt2 = hwFPR(getReg(mi.ops[1]));
        auto offset = static_cast<int32_t>(getImm(mi.ops[2]));
        uint32_t fp = hwGPR(PhysReg::X29);
        emit32(encodePair(kStpFpr, rt, rt2, fp, offset / 8), cs);
        return;
    }

    // ─── Stack Pointer Operations ───
    case MOpcode::SubSpImm:
        encodeSubSp(getImm(mi.ops[0]), cs);
        return;
    case MOpcode::AddSpImm:
        encodeAddSp(getImm(mi.ops[0]), cs);
        return;
    case MOpcode::StrRegSpImm:
    {
        uint32_t rt = hwGPR(getReg(mi.ops[0]));
        auto offset = static_cast<uint32_t>(getImm(mi.ops[1]));
        uint32_t sp = hwGPR(PhysReg::SP);
        // str Xt, [sp, #offset] — scaled unsigned offset
        emit32(kStrGpr | ((offset / 8) << 10) | (sp << 5) | rt, cs);
        return;
    }
    case MOpcode::StrFprSpImm:
    {
        uint32_t rt = hwFPR(getReg(mi.ops[0]));
        auto offset = static_cast<uint32_t>(getImm(mi.ops[1]));
        uint32_t sp = hwGPR(PhysReg::SP);
        emit32(kStrFpr | ((offset / 8) << 10) | (sp << 5) | rt, cs);
        return;
    }

    // ─── AddFpImm (address computation: dst = x29 + offset) ───
    case MOpcode::AddFpImm:
    {
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        long long offset = getImm(mi.ops[1]);
        uint32_t fp = hwGPR(PhysReg::X29);
        if (offset >= 0 && offset <= 4095)
        {
            emit32(encodeAddSubImm(kAddRI, rd, fp, static_cast<uint32_t>(offset)), cs);
        }
        else if (offset < 0 && -offset <= 4095)
        {
            emit32(encodeAddSubImm(kSubRI, rd, fp, static_cast<uint32_t>(-offset)), cs);
        }
        else
        {
            // Large offset: movz x9, #abs(offset); add/sub rd, x29, x9
            uint32_t scratch = hwGPR(PhysReg::X9);
            encodeMovImm64(scratch, static_cast<uint64_t>(offset >= 0 ? offset : -offset), cs);
            if (offset >= 0)
                emit32(encode3Reg(kAddRRR, rd, fp, scratch), cs);
            else
                emit32(encode3Reg(kSubRRR, rd, fp, scratch), cs);
        }
        return;
    }

    // ─── Logical Immediate ───
    case MOpcode::AndRI:
    {
        // Logical immediate encoding is complex (N/immr/imms bit-fields).
        // For now, materialise the immediate in X9 and use AND Xd, Xn, X9.
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        uint32_t rn = hwGPR(getReg(mi.ops[1]));
        long long imm = getImm(mi.ops[2]);
        uint32_t scratch = hwGPR(PhysReg::X9);
        encodeMovImm64(scratch, static_cast<uint64_t>(imm), cs);
        emit32(encode3Reg(kAndRRR, rd, rn, scratch), cs);
        return;
    }
    case MOpcode::OrrRI:
    {
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        uint32_t rn = hwGPR(getReg(mi.ops[1]));
        long long imm = getImm(mi.ops[2]);
        uint32_t scratch = hwGPR(PhysReg::X9);
        encodeMovImm64(scratch, static_cast<uint64_t>(imm), cs);
        emit32(encode3Reg(kOrrRRR, rd, rn, scratch), cs);
        return;
    }
    case MOpcode::EorRI:
    {
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        uint32_t rn = hwGPR(getReg(mi.ops[1]));
        long long imm = getImm(mi.ops[2]);
        uint32_t scratch = hwGPR(PhysReg::X9);
        encodeMovImm64(scratch, static_cast<uint64_t>(imm), cs);
        emit32(encode3Reg(kEorRRR, rd, rn, scratch), cs);
        return;
    }

    // ─── Floating Point — Three-Register ───
    case MOpcode::FAddRRR:
        emit32(encode3Reg(kFAddRRR, hwFPR(getReg(mi.ops[0])), hwFPR(getReg(mi.ops[1])),
                          hwFPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::FSubRRR:
        emit32(encode3Reg(kFSubRRR, hwFPR(getReg(mi.ops[0])), hwFPR(getReg(mi.ops[1])),
                          hwFPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::FMulRRR:
        emit32(encode3Reg(kFMulRRR, hwFPR(getReg(mi.ops[0])), hwFPR(getReg(mi.ops[1])),
                          hwFPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::FDivRRR:
        emit32(encode3Reg(kFDivRRR, hwFPR(getReg(mi.ops[0])), hwFPR(getReg(mi.ops[1])),
                          hwFPR(getReg(mi.ops[2]))), cs);
        return;
    case MOpcode::FCmpRR:
        // fcmp Dn, Dm (Rd field = 0)
        emit32(kFCmpRR | (hwFPR(getReg(mi.ops[1])) << 16) | (hwFPR(getReg(mi.ops[0])) << 5), cs);
        return;
    case MOpcode::FMovRR:
        emit32(encode2Reg(kFMovRR, hwFPR(getReg(mi.ops[0])), hwFPR(getReg(mi.ops[1]))), cs);
        return;
    case MOpcode::FRintN:
        emit32(encode2Reg(kFRintN, hwFPR(getReg(mi.ops[0])), hwFPR(getReg(mi.ops[1]))), cs);
        return;

    // ─── Floating Point — Conversions ───
    case MOpcode::SCvtF:
        emit32(encode2Reg(kSCvtF, hwFPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1]))), cs);
        return;
    case MOpcode::FCvtZS:
        emit32(encode2Reg(kFCvtZS, hwGPR(getReg(mi.ops[0])), hwFPR(getReg(mi.ops[1]))), cs);
        return;
    case MOpcode::UCvtF:
        emit32(encode2Reg(kUCvtF, hwFPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1]))), cs);
        return;
    case MOpcode::FCvtZU:
        emit32(encode2Reg(kFCvtZU, hwGPR(getReg(mi.ops[0])), hwFPR(getReg(mi.ops[1]))), cs);
        return;
    case MOpcode::FMovGR:
        emit32(encode2Reg(kFMovGR, hwFPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1]))), cs);
        return;

    // ─── FMovRI (float immediate) ───
    case MOpcode::FMovRI:
    {
        // For now, materialise the 64-bit IEEE 754 double in a GPR via movz/movk,
        // then fmov Dd, Xn to transfer bits.
        uint32_t rd = hwFPR(getReg(mi.ops[0]));
        double val;
        std::memcpy(&val, &mi.ops[1].imm, sizeof(val));
        uint64_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        uint32_t scratch = hwGPR(PhysReg::X9);
        encodeMovImm64(scratch, bits, cs);
        emit32(encode2Reg(kFMovGR, rd, scratch), cs);
        return;
    }

    // ─── Branch Instructions ───
    case MOpcode::Br:
    {
        std::string target = sanitizeLabel(mi.ops[0].label);
        auto it = labelOffsets_.find(target);
        if (it != labelOffsets_.end())
        {
            // Backward branch — resolve immediately.
            int64_t delta = static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset());
            int32_t imm26 = static_cast<int32_t>(delta / 4);
            emit32(kBr | (static_cast<uint32_t>(imm26) & 0x3FFFFFF), cs);
        }
        else
        {
            pendingBranches_.push_back({cs.currentOffset(), target, MOpcode::Br});
            emit32(kBr, cs); // placeholder
        }
        return;
    }
    case MOpcode::BCond:
    {
        uint32_t cc = condCode(mi.ops[0].cond);
        std::string target = sanitizeLabel(mi.ops[1].label);
        auto it = labelOffsets_.find(target);
        if (it != labelOffsets_.end())
        {
            int64_t delta = static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset());
            int32_t imm19 = static_cast<int32_t>(delta / 4);
            emit32(kBCond | ((static_cast<uint32_t>(imm19) & 0x7FFFF) << 5) | cc, cs);
        }
        else
        {
            pendingBranches_.push_back({cs.currentOffset(), target, MOpcode::BCond});
            emit32(kBCond | cc, cs); // placeholder with cond code set
        }
        return;
    }
    case MOpcode::Cbz:
    {
        uint32_t rt = hwGPR(getReg(mi.ops[0]));
        std::string target = sanitizeLabel(mi.ops[1].label);
        auto it = labelOffsets_.find(target);
        if (it != labelOffsets_.end())
        {
            int64_t delta = static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset());
            int32_t imm19 = static_cast<int32_t>(delta / 4);
            emit32(kCbz | ((static_cast<uint32_t>(imm19) & 0x7FFFF) << 5) | rt, cs);
        }
        else
        {
            pendingBranches_.push_back({cs.currentOffset(), target, MOpcode::Cbz});
            emit32(kCbz | rt, cs); // placeholder with Rt set
        }
        return;
    }
    case MOpcode::Cbnz:
    {
        uint32_t rt = hwGPR(getReg(mi.ops[0]));
        std::string target = sanitizeLabel(mi.ops[1].label);
        auto it = labelOffsets_.find(target);
        if (it != labelOffsets_.end())
        {
            int64_t delta = static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset());
            int32_t imm19 = static_cast<int32_t>(delta / 4);
            emit32(kCbnz | ((static_cast<uint32_t>(imm19) & 0x7FFFF) << 5) | rt, cs);
        }
        else
        {
            pendingBranches_.push_back({cs.currentOffset(), target, MOpcode::Cbnz});
            emit32(kCbnz | rt, cs);
        }
        return;
    }
    case MOpcode::Bl:
    {
        // Direct call — always external (generates relocation).
        std::string sym = mapRuntimeSymbol(mi.ops[0].label);
        auto it = labelOffsets_.find(sanitizeLabel(mi.ops[0].label));
        if (it != labelOffsets_.end())
        {
            // Internal call (rare but possible for local functions).
            int64_t delta = static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset());
            int32_t imm26 = static_cast<int32_t>(delta / 4);
            emit32(kBl | (static_cast<uint32_t>(imm26) & 0x3FFFFFF), cs);
        }
        else
        {
            uint32_t symIdx = cs.findOrDeclareSymbol(sym);
            cs.addRelocation(objfile::RelocKind::A64Call26, symIdx, 0);
            emit32(kBl, cs); // imm26 = 0, filled by linker
        }
        return;
    }
    case MOpcode::Blr:
        emit32(kBlr | (hwGPR(getReg(mi.ops[0])) << 5), cs);
        return;

    // ─── Address Materialization ───
    case MOpcode::AdrPage:
    {
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        std::string sym = mi.ops[1].label;
        uint32_t symIdx = cs.findOrDeclareSymbol(sym);
        cs.addRelocation(objfile::RelocKind::A64AdrpPage21, symIdx, 0);
        emit32(kAdrp | rd, cs); // immediate filled by linker
        return;
    }
    case MOpcode::AddPageOff:
    {
        uint32_t rd = hwGPR(getReg(mi.ops[0]));
        uint32_t rn = hwGPR(getReg(mi.ops[1]));
        std::string sym = mi.ops[2].label;
        uint32_t symIdx = cs.findOrDeclareSymbol(sym);
        cs.addRelocation(objfile::RelocKind::A64AddPageOff12, symIdx, 0);
        emit32(encodeAddSubImm(kAddRI, rd, rn, 0), cs); // imm12 filled by linker
        return;
    }

    // ─── Pseudo-instructions that should have been expanded ───
    case MOpcode::AddOvfRRR:
    case MOpcode::SubOvfRRR:
    case MOpcode::AddOvfRI:
    case MOpcode::SubOvfRI:
    case MOpcode::MulOvfRRR:
        assert(false && "Overflow pseudo-instruction reached binary encoder — should be expanded by LowerOvf");
        return;

    } // end switch

    // If we reach here, the opcode was not handled.
    assert(false && "Unhandled MOpcode in A64BinaryEncoder");
}

} // namespace viper::codegen::aarch64::binenc
