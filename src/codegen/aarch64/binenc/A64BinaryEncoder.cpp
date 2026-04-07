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

#include "codegen/aarch64/A64ImmediateUtils.hpp"
#include "codegen/aarch64/FrameCodegen.hpp"
#include "codegen/aarch64/binenc/A64Encoding.hpp"
#include "codegen/common/LabelUtil.hpp"
#include "codegen/common/objfile/DebugLineTable.hpp"
#include "il/runtime/RuntimeNameMap.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace viper::codegen::aarch64::binenc {

// === Helpers ===

/// Map IL extern names to C runtime symbol names.
static std::string mapRuntimeSymbol(const std::string &name) {
    if (auto mapped = il::runtime::mapCanonicalRuntimeName(name))
        return std::string(*mapped);
    return name;
}

/// Sanitize a label for internal use (replace hyphens, etc.).
static std::string sanitizeLabel(const std::string &name) {
    return viper::codegen::common::sanitizeLabel(name);
}

/// Extract PhysReg from a register operand.
static PhysReg getReg(const MOperand &op) {
    if (op.kind != MOperand::Kind::Reg)
        throw std::runtime_error("AArch64 binary encoder expected register operand, got kind=" +
                                 std::to_string(static_cast<int>(op.kind)));
    if (!op.reg.isPhys)
        throw std::runtime_error("AArch64 binary encoder cannot encode virtual register v" +
                                 std::to_string(op.reg.idOrPhys));
    return static_cast<PhysReg>(op.reg.idOrPhys);
}

/// Extract immediate value from an operand.
static long long getImm(const MOperand &op) {
    if (op.kind != MOperand::Kind::Imm)
        throw std::runtime_error("AArch64 binary encoder expected immediate operand, got kind=" +
                                 std::to_string(static_cast<int>(op.kind)));
    return op.imm;
}

/// Check if offset fits in signed 9-bit range for ldur/stur.
static bool isInSignedImmRange(long long offset) {
    return offset >= -256 && offset <= 255;
}

static bool isLegalScaledUImm64(long long offset) {
    return offset >= 0 && (offset % 8) == 0 && (offset / 8) <= 4095;
}

static int32_t checkedBranchDispWords(int64_t deltaBytes,
                                      int immBits,
                                      const char *kind,
                                      const char *rangeDesc,
                                      const std::string &target,
                                      const std::string &fnName) {
    if ((deltaBytes & 0x3) != 0) {
        throw std::runtime_error("AArch64 binary encoder: " + std::string(kind) + " target '" +
                                 target + "' in function '" + fnName + "' is not 4-byte aligned");
    }

    const int64_t deltaWords = deltaBytes / 4;
    const int64_t min = -(int64_t{1} << (immBits - 1));
    const int64_t max = (int64_t{1} << (immBits - 1)) - 1;
    if (deltaWords < min || deltaWords > max) {
        throw std::runtime_error("AArch64 binary encoder: " + std::string(kind) + " target '" +
                                 target + "' in function '" + fnName + "' exceeds " + rangeDesc);
    }

    return static_cast<int32_t>(deltaWords);
}

static bool fitsBranchDispWords(int64_t deltaBytes, int immBits) {
    if ((deltaBytes & 0x3) != 0)
        return false;

    const int64_t deltaWords = deltaBytes / 4;
    const int64_t min = -(int64_t{1} << (immBits - 1));
    const int64_t max = (int64_t{1} << (immBits - 1)) - 1;
    return deltaWords >= min && deltaWords <= max;
}

size_t A64BinaryEncoder::measurePreludeSize(const MFunction &fn) {
    objfile::CodeSection text;
    text.reserveBytes(32);
    emit32(kBtiC, text);
    if (!skipFrame_)
        encodePrologue(fn, text);
    return text.currentOffset();
}

size_t A64BinaryEncoder::measureInstructionSize(const MInstr &mi,
                                                size_t currentOffset,
                                                const LabelOffsetMap &knownLabelOffsets) {
    A64BinaryEncoder measureEncoder;
    measureEncoder.labelOffsets_ = knownLabelOffsets;
    measureEncoder.currentFn_ = currentFn_;
    measureEncoder.currentRodata_ = currentRodata_;
    measureEncoder.usePlan_ = usePlan_;
    measureEncoder.skipFrame_ = skipFrame_;

    objfile::CodeSection text;
    text.reserveBytes(currentOffset + 32);
    text.emitZeros(currentOffset);
    measureEncoder.encodeInstruction(mi, text);
    return text.currentOffset() - currentOffset;
}

A64BinaryEncoder::LabelOffsetMap A64BinaryEncoder::computeFunctionLabelOffsets(const MFunction &fn) {
    LabelOffsetMap estimated;

    size_t relaxCandidates = 0;
    for (const auto &bb : fn.blocks) {
        for (const auto &mi : bb.instrs) {
            if (mi.opc == MOpcode::BCond || mi.opc == MOpcode::Cbz || mi.opc == MOpcode::Cbnz)
                ++relaxCandidates;
        }
    }

    const size_t maxIterations = std::max<size_t>(2, relaxCandidates + 1);
    for (size_t iter = 0; iter < maxIterations; ++iter) {
        bool changed = false;
        LabelOffsetMap known = estimated;
        LabelOffsetMap next;
        next.reserve(estimated.size() + fn.blocks.size());

        auto assignLabel = [&](const std::string &name, size_t offset) {
            const std::string sanitized = sanitizeLabel(name);
            auto prevIt = estimated.find(sanitized);
            if (prevIt == estimated.end() || prevIt->second != offset)
                changed = true;
            known[sanitized] = offset;
            next[sanitized] = offset;
        };

        size_t offset = measurePreludeSize(fn);
        for (const auto &bb : fn.blocks) {
            if (!bb.name.empty())
                assignLabel(bb.name, offset);
            for (const auto &mi : bb.instrs)
                offset += measureInstructionSize(mi, offset, known);
        }

        if (!changed && next.size() == estimated.size())
            return next;
        estimated = std::move(next);
    }

    return estimated;
}

size_t A64BinaryEncoder::estimateFunctionSize(const MFunction &fn,
                                              const LabelOffsetMap &knownLabelOffsets) {
    size_t size = measurePreludeSize(fn);
    for (const auto &bb : fn.blocks) {
        for (const auto &mi : bb.instrs)
            size += measureInstructionSize(mi, size, knownLabelOffsets);
    }
    return size;
}

void A64BinaryEncoder::verifyPredictedLabelOffset(const std::string &label, size_t actualOffset) const {
    auto it = labelOffsets_.find(label);
    if (it == labelOffsets_.end())
        return;
    if (it->second != actualOffset) {
        throw std::runtime_error("AArch64 binary encoder: label offset drift for '" + label +
                                 "' (predicted=" + std::to_string(it->second) +
                                 ", actual=" + std::to_string(actualOffset) + ")");
    }
}

// =============================================================================
// encodeFunction
// =============================================================================

void A64BinaryEncoder::encodeFunction(const MFunction &fn,
                                      objfile::CodeSection &text,
                                      objfile::CodeSection &rodata,
                                      ABIFormat abi) {
    (void)abi;    // Symbol mangling deferred to ObjectFileWriter

    labelOffsets_.clear();
    pendingBranches_.clear();
    currentFn_ = &fn;
    currentRodata_ = &rodata;

    try {
        // Leaf function optimization: skip frame when no calls, no callee-saved, no locals.
        skipFrame_ = fn.isLeaf && fn.savedGPRs.empty() && fn.savedFPRs.empty() &&
                     fn.localFrameSize == 0;
        usePlan_ = !fn.savedGPRs.empty() || !fn.savedFPRs.empty() || fn.localFrameSize > 0;

        // Define function symbol at current offset.
        const size_t funcStartOffset = text.currentOffset();
        const auto relativeLabelOffsets = computeFunctionLabelOffsets(fn);
        text.reserveAdditionalBytes(estimateFunctionSize(fn, relativeLabelOffsets));
        labelOffsets_.clear();
        labelOffsets_.reserve(relativeLabelOffsets.size());
        for (const auto &[label, offset] : relativeLabelOffsets)
            labelOffsets_[label] = funcStartOffset + offset;

        size_t branchCount = 0;
        for (const auto &bb : fn.blocks) {
            for (const auto &mi : bb.instrs) {
                if (mi.opc == MOpcode::Br || mi.opc == MOpcode::BCond || mi.opc == MOpcode::Cbz ||
                    mi.opc == MOpcode::Cbnz || mi.opc == MOpcode::Bl)
                    ++branchCount;
            }
        }
        pendingBranches_.reserve(branchCount);

        const uint32_t funcSymIdx = text.defineSymbol(
            fn.name, objfile::SymbolBinding::Global, objfile::SymbolSection::Text);

        // Emit BTI landing pad for indirect call targets (safe NOP on pre-ARMv8.5).
        emit32(kBtiC, text);

        // Emit prologue.
        if (!skipFrame_)
            encodePrologue(fn, text);

        // Encode all blocks.
        for (const auto &bb : fn.blocks) {
            if (!bb.name.empty()) {
                const std::string label = sanitizeLabel(bb.name);
                verifyPredictedLabelOffset(label, text.currentOffset());
                labelOffsets_[label] = text.currentOffset();
            }

            for (const auto &mi : bb.instrs) {
                if (debugLines_ && mi.loc.hasLine())
                    debugLines_->addEntry(
                        text.currentOffset(), mi.loc.file_id, mi.loc.line, mi.loc.column);
                encodeInstruction(mi, text);
            }
        }

        // Resolve pending internal branches.
        for (const auto &pb : pendingBranches_) {
            auto it = labelOffsets_.find(pb.target);
            if (it == labelOffsets_.end()) {
                throw std::runtime_error(
                    "AArch64 binary encoder: unresolved internal branch target '" + pb.target +
                    "' in function '" + fn.name + "'");
            }

            const size_t targetOff = it->second;
            const int64_t delta = static_cast<int64_t>(targetOff) - static_cast<int64_t>(pb.offset);

            // Read existing instruction word.
            const uint8_t *p = text.bytes().data() + pb.offset;
            uint32_t word = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                            (static_cast<uint32_t>(p[2]) << 16) |
                            (static_cast<uint32_t>(p[3]) << 24);

            if (pb.kind == MOpcode::Br || pb.kind == MOpcode::Bl) {
                const int32_t imm26 = checkedBranchDispWords(
                    delta, 26, "branch", "the +/-128MB B/BL range", pb.target, fn.name);
                word |= (static_cast<uint32_t>(imm26) & 0x3FFFFFF);
            } else {
                const int32_t imm19 = checkedBranchDispWords(delta,
                                                             19,
                                                             "conditional branch",
                                                             "the +/-1MB conditional-branch range",
                                                             pb.target,
                                                             fn.name);
                word |= ((static_cast<uint32_t>(imm19) & 0x7FFFF) << 5);
            }

            text.patch32LE(pb.offset, word);
        }

        // Record compact unwind entry for this function.
        // ARM64 frame-based encoding: bits [31:28] = 0x4 (UNWIND_ARM64_MODE_FRAME)
        if (!skipFrame_) {
            uint32_t encoding = 0x04000000u; // UNWIND_ARM64_MODE_FRAME

            // Encode callee-saved GPR pair count (bits [23:20], max 5 pairs: X19-X28)
            uint32_t gprPairs = static_cast<uint32_t>(fn.savedGPRs.size() + 1) / 2;
            if (gprPairs > 5)
                gprPairs = 5;
            encoding |= (gprPairs << 20);

            // Encode callee-saved FPR pair count (bits [27:24], max 4 pairs: D8-D15)
            uint32_t fprPairs = static_cast<uint32_t>(fn.savedFPRs.size() + 1) / 2;
            if (fprPairs > 4)
                fprPairs = 4;
            encoding |= (fprPairs << 24);

            const uint32_t funcLen = static_cast<uint32_t>(text.currentOffset() - funcStartOffset);

            objfile::CompactUnwindEntry entry{};
            entry.symbolIndex = funcSymIdx;
            entry.functionLength = funcLen;
            entry.encoding = encoding;
            text.addUnwindEntry(entry);
        } else {
            // Frameless leaf function — UNWIND_ARM64_MODE_FRAMELESS with zero encoding.
            // Still record an entry so the unwinder knows this function exists.
            const uint32_t funcLen = static_cast<uint32_t>(text.currentOffset() - funcStartOffset);

            objfile::CompactUnwindEntry entry{};
            entry.symbolIndex = funcSymIdx;
            entry.functionLength = funcLen;
            entry.encoding = 0x02000000u; // UNWIND_ARM64_MODE_FRAMELESS, stack size 0
            text.addUnwindEntry(entry);
        }

        currentFn_ = nullptr;
        currentRodata_ = nullptr;
    } catch (...) {
        currentFn_ = nullptr;
        currentRodata_ = nullptr;
        throw;
    }
}

// =============================================================================
// Prologue/Epilogue Synthesis
// =============================================================================

void A64BinaryEncoder::encodePrologue(const MFunction &fn, objfile::CodeSection &cs) {
    const uint32_t sp = hwGPR(PhysReg::SP);
    const uint32_t fp = hwGPR(PhysReg::X29);
    const uint32_t lr = hwGPR(PhysReg::X30);

    // Sign LR with SP before saving (Pointer Authentication, ARMv8.3+).
    // Executes as NOP on older hardware.
    emit32(kPaciasp, cs);

    // stp x29, x30, [sp, #-16]!  (pre-indexed, -16/8 = -2)
    emit32(encodePair(kStpGprPre, fp, lr, sp, static_cast<int32_t>(-16 / 8)), cs);

    // mov x29, sp  →  add x29, sp, #0
    emit32(encodeAddSubImm(kAddRI, fp, sp, 0), cs);

    // Allocate local frame.
    if (fn.localFrameSize > 0)
        encodeSubSp(fn.localFrameSize, cs);

    // Save callee-saved GPRs (shared iteration logic).
    forEachSaveReg(
        fn.savedGPRs,
        [&](PhysReg r0, PhysReg r1) {
            emit32(encodePair(kStpGprPre, hwGPR(r0), hwGPR(r1), sp, static_cast<int32_t>(-16 / 8)),
                   cs);
        },
        [&](PhysReg r0) {
            emit32(kStrGprPre | ((static_cast<uint32_t>(-16) & 0x1FF) << 12) | (sp << 5) |
                       hwGPR(r0),
                   cs);
        });

    // Save callee-saved FPRs.
    forEachSaveReg(
        fn.savedFPRs,
        [&](PhysReg r0, PhysReg r1) {
            emit32(encodePair(kStpFprPre, hwFPR(r0), hwFPR(r1), sp, static_cast<int32_t>(-16 / 8)),
                   cs);
        },
        [&](PhysReg r0) {
            emit32(kStrFprPre | ((static_cast<uint32_t>(-16) & 0x1FF) << 12) | (sp << 5) |
                       hwFPR(r0),
                   cs);
        });
}

void A64BinaryEncoder::encodeEpilogue(const MFunction &fn, objfile::CodeSection &cs) {
    const uint32_t sp = hwGPR(PhysReg::SP);
    const uint32_t fp = hwGPR(PhysReg::X29);
    const uint32_t lr = hwGPR(PhysReg::X30);

    // Restore callee-saved FPRs (reverse order, shared iteration logic).
    forEachRestoreReg(
        fn.savedFPRs,
        [&](PhysReg r0, PhysReg r1) {
            emit32(encodePair(kLdpFprPost, hwFPR(r0), hwFPR(r1), sp, static_cast<int32_t>(16 / 8)),
                   cs);
        },
        [&](PhysReg r0) {
            emit32(kLdrFprPost | ((16u & 0x1FF) << 12) | (sp << 5) | hwFPR(r0), cs);
        });

    // Restore callee-saved GPRs (reverse order).
    forEachRestoreReg(
        fn.savedGPRs,
        [&](PhysReg r0, PhysReg r1) {
            emit32(encodePair(kLdpGprPost, hwGPR(r0), hwGPR(r1), sp, static_cast<int32_t>(16 / 8)),
                   cs);
        },
        [&](PhysReg r0) {
            emit32(kLdrGprPost | ((16u & 0x1FF) << 12) | (sp << 5) | hwGPR(r0), cs);
        });

    // Deallocate local frame.
    if (fn.localFrameSize > 0)
        encodeAddSp(fn.localFrameSize, cs);

    // ldp x29, x30, [sp], #16  → post-indexed, imm7 = 16/8 = 2
    emit32(encodePair(kLdpGprPost, fp, lr, sp, static_cast<int32_t>(16 / 8)), cs);

    // Verify LR signature before return (Pointer Authentication, ARMv8.3+).
    // Executes as NOP on older hardware.
    emit32(kAutiasp, cs);

    // ret
    emit32(kRet, cs);
}

// =============================================================================
// Multi-instruction sequences
// =============================================================================

void A64BinaryEncoder::encodeMovImm64(uint32_t rd, uint64_t imm, objfile::CodeSection &cs) {
    // Templates indexed by halfword position (0=lsl #0, 1=lsl #16, 2=lsl #32, 3=lsl #48).
    static constexpr uint32_t movzTmpl[4] = {kMovZ, kMovZ16, kMovZ32, kMovZ48};
    static constexpr uint32_t movnTmpl[4] = {kMovN, kMovN16, kMovN32, kMovN48};
    static constexpr uint32_t movkTmpl[4] = {kMovK, kMovK16, kMovK32, kMovK48};

    forEachMoveWideInst(imm, [&](const MoveWideInst &inst) {
        const unsigned lane = inst.shift / 16;
        switch (inst.opcode) {
            case MoveWideOpcode::MovZ:
                emit32(movzTmpl[lane] | (static_cast<uint32_t>(inst.imm16) << 5) | rd, cs);
                break;
            case MoveWideOpcode::MovN:
                emit32(movnTmpl[lane] | (static_cast<uint32_t>(inst.imm16) << 5) | rd, cs);
                break;
            case MoveWideOpcode::MovK:
                emit32(movkTmpl[lane] | (static_cast<uint32_t>(inst.imm16) << 5) | rd, cs);
                break;
        }
    });
}

/// Emit a single ADD or SUB immediate, using lsl #12 when possible.
static void emitAddSubImmSmart(
    uint32_t tmpl, uint32_t rd, uint32_t rn, uint32_t val, objfile::CodeSection &cs) {
    if (val <= 4095) {
        cs.emit32LE(encodeAddSubImm(tmpl, rd, rn, val));
    } else if ((val & 0xFFF) == 0 && (val >> 12) <= 4095) {
        cs.emit32LE(encodeAddSubImmShift(tmpl, rd, rn, val >> 12));
    } else {
        // Split into shifted + unshifted parts.
        uint32_t hi = val >> 12;
        uint32_t lo = val & 0xFFF;
        if (hi > 0 && hi <= 4095) {
            cs.emit32LE(encodeAddSubImmShift(tmpl, rd, rn, hi));
            if (lo > 0)
                cs.emit32LE(encodeAddSubImm(tmpl, rd, rd, lo));
        } else {
            // Fallback: loop with max immediate.
            while (val > 4095) {
                cs.emit32LE(encodeAddSubImm(tmpl, rd, rn, 4080));
                val -= 4080;
                rn = rd; // subsequent iterations operate on rd
            }
            if (val > 0)
                cs.emit32LE(encodeAddSubImm(tmpl, rd, rn, val));
        }
    }
}

void A64BinaryEncoder::encodeSubSp(int64_t bytes, objfile::CodeSection &cs) {
    const uint32_t sp = hwGPR(PhysReg::SP);
    emitAddSubImmSmart(kSubRI, sp, sp, static_cast<uint32_t>(bytes), cs);
}

void A64BinaryEncoder::encodeAddSp(int64_t bytes, objfile::CodeSection &cs) {
    const uint32_t sp = hwGPR(PhysReg::SP);
    emitAddSubImmSmart(kAddRI, sp, sp, static_cast<uint32_t>(bytes), cs);
}

void A64BinaryEncoder::encodeLargeOffsetLdSt(
    uint32_t rt, uint32_t base, int64_t offset, bool isLoad, bool isFPR, objfile::CodeSection &cs) {
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

void A64BinaryEncoder::encodeSpOffsetStore(uint32_t rt,
                                           int64_t offset,
                                           bool isFPR,
                                           objfile::CodeSection &cs) {
    const uint32_t sp = hwGPR(PhysReg::SP);
    if (isLegalScaledUImm64(offset)) {
        const uint32_t scaled = static_cast<uint32_t>(offset / 8);
        emit32((isFPR ? kStrFpr : kStrGpr) | (scaled << 10) | (sp << 5) | rt, cs);
        return;
    }

    const uint32_t scratch = hwGPR(kScratchGPR2);
    emit32(encodeAddSubImm(kAddRI, scratch, sp, 0), cs);
    if (offset > 0)
        emitAddSubImmSmart(kAddRI, scratch, scratch, static_cast<uint32_t>(offset), cs);
    else if (offset < 0)
        emitAddSubImmSmart(kSubRI, scratch, scratch, static_cast<uint32_t>(-offset), cs);

    emit32((isFPR ? kStrFpr : kStrGpr) | (0 << 10) | (scratch << 5) | rt, cs);
}

// =============================================================================
// encodeInstruction — main dispatch
// =============================================================================

void A64BinaryEncoder::encodeInstruction(const MInstr &mi, objfile::CodeSection &cs) {
    switch (mi.opc) {
        // ─── Ret (triggers epilogue synthesis) ───
        case MOpcode::Ret:
            if (skipFrame_)
                emit32(kRet, cs);
            else
                encodeEpilogue(*currentFn_, cs);
            return;

        // ─── Data Processing — Three-Register ───
        case MOpcode::AddRRR:
            emit32(encode3Reg(kAddRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::SubRRR:
            emit32(encode3Reg(kSubRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::AndRRR:
            emit32(encode3Reg(kAndRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::OrrRRR:
            emit32(encode3Reg(kOrrRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::EorRRR:
            emit32(encode3Reg(kEorRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::AddsRRR:
            emit32(encode3Reg(kAddsRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::SubsRRR:
            emit32(encode3Reg(kSubsRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;

        // ─── Variable Shift ───
        case MOpcode::LslvRRR:
            emit32(encode3Reg(kLslvRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::LsrvRRR:
            emit32(encode3Reg(kLsrvRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::AsrvRRR:
            emit32(encode3Reg(kAsrvRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;

        // ─── Multiply / Divide ───
        case MOpcode::MulRRR:
            emit32(encode3Reg(kMulRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::SmulhRRR:
            emit32(encode3Reg(kSmulhRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::SDivRRR:
            emit32(encode3Reg(kSDivRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::UDivRRR:
            emit32(encode3Reg(kUDivRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::MSubRRRR:
            emit32(encode4Reg(kMSubRRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2])),
                              hwGPR(getReg(mi.ops[3]))),
                   cs);
            return;
        case MOpcode::MAddRRRR:
            emit32(encode4Reg(kMAddRRRR,
                              hwGPR(getReg(mi.ops[0])),
                              hwGPR(getReg(mi.ops[1])),
                              hwGPR(getReg(mi.ops[2])),
                              hwGPR(getReg(mi.ops[3]))),
                   cs);
            return;

        // ─── Add/Sub Immediate ───
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::AddsRI:
        case MOpcode::SubsRI: {
            const uint32_t rd = hwGPR(getReg(mi.ops[0]));
            const uint32_t rn = hwGPR(getReg(mi.ops[1]));
            const long long immValue = getImm(mi.ops[2]);
            if (immValue < 0) {
                throw std::runtime_error(
                    "AArch64 binary encoder: " + std::string(opcodeName(mi.opc)) +
                    " immediate reached encoder without sign normalization");
            }
            const uint64_t imm = static_cast<uint64_t>(immValue);
            const auto enc = classifyAddSubImmEncoding(imm);
            if (!enc.has_value()) {
                throw std::runtime_error(
                    "AArch64 binary encoder: " + std::string(opcodeName(mi.opc)) +
                    " immediate reached encoder without legalization");
            }
            uint32_t tmpl = kAddRI;
            if (mi.opc == MOpcode::SubRI)
                tmpl = kSubRI;
            else if (mi.opc == MOpcode::AddsRI)
                tmpl = kAddsRI;
            else if (mi.opc == MOpcode::SubsRI)
                tmpl = kSubsRI;
            emit32(enc->shift12 ? encodeAddSubImmShift(tmpl, rd, rn, enc->imm12)
                                : encodeAddSubImm(tmpl, rd, rn, enc->imm12),
                   cs);
            return;
        }

        // ─── Move ───
        case MOpcode::MovRR:
            // orr Xd, XZR, Xm
            emit32(kMovRR | (hwGPR(getReg(mi.ops[1])) << 16) | hwGPR(getReg(mi.ops[0])), cs);
            return;
        case MOpcode::MovRI: {
            long long imm = getImm(mi.ops[1]);
            uint32_t rd = hwGPR(getReg(mi.ops[0]));
            if (!needsWideImmSequence(imm)) {
                if (imm < 0) {
                    // MOVN Xd, #(~imm & 0xFFFF) — inverts all 64 bits to produce the negative
                    // value.
                    emit32(kMovN | (static_cast<uint32_t>(~imm & 0xFFFF) << 5) | rd, cs);
                } else {
                    // MOVZ Xd, #imm16 — zero-extends the 16-bit immediate.
                    emit32(kMovZ | (static_cast<uint32_t>(imm & 0xFFFF) << 5) | rd, cs);
                }
            } else {
                encodeMovImm64(rd, static_cast<uint64_t>(imm), cs);
            }
            return;
        }

        // ─── Shift by Immediate ───
        case MOpcode::LslRI: {
            uint32_t rd = hwGPR(getReg(mi.ops[0]));
            uint32_t rn = hwGPR(getReg(mi.ops[1]));
            auto sh = static_cast<uint32_t>(getImm(mi.ops[2]));
            // lsl is ubfm Xd, Xn, #(64-n)&63, #(63-n)
            uint32_t immr = (64 - sh) & 63;
            uint32_t imms = 63 - sh;
            emit32(kUbfm | (immr << 16) | (imms << 10) | (rn << 5) | rd, cs);
            return;
        }
        case MOpcode::LsrRI: {
            uint32_t rd = hwGPR(getReg(mi.ops[0]));
            uint32_t rn = hwGPR(getReg(mi.ops[1]));
            auto sh = static_cast<uint32_t>(getImm(mi.ops[2]));
            // lsr is ubfm Xd, Xn, #n, #63
            emit32(kUbfm | (sh << 16) | (63 << 10) | (rn << 5) | rd, cs);
            return;
        }
        case MOpcode::AsrRI: {
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
            emit32(encode3Reg(kSubsRRR, 31, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1]))),
                   cs);
            return;
        case MOpcode::CmpRI: {
            long long imm = getImm(mi.ops[1]);
            uint32_t rn = hwGPR(getReg(mi.ops[0]));
            if (imm >= 0 && imm <= 4095) {
                // subs XZR, Xn, #imm
                emit32(encodeAddSubImm(kSubsRI, 31, rn, static_cast<uint32_t>(imm)), cs);
            } else if (imm >= -4095 && imm < 0) {
                // cmn = adds XZR, Xn, #(-imm)
                emit32(encodeAddSubImm(kAddsRI, 31, rn, static_cast<uint32_t>(-imm)), cs);
            } else {
                // Large: materialize the immediate into the reserved secondary scratch.
                uint32_t scratch = hwGPR(kScratchGPR2);
                encodeMovImm64(scratch, static_cast<uint64_t>(imm), cs);
                emit32(encode3Reg(kSubsRRR, 31, rn, scratch), cs);
            }
            return;
        }
        case MOpcode::TstRR:
            // ands XZR, Xn, Xm
            emit32(encode3Reg(kAndsRRR, 31, hwGPR(getReg(mi.ops[0])), hwGPR(getReg(mi.ops[1]))),
                   cs);
            return;

        // ─── Conditional ───
        case MOpcode::Cset: {
            uint32_t rd = hwGPR(getReg(mi.ops[0]));
            uint32_t cc = condCode(mi.ops[1].cond);
            // csinc Xd, XZR, XZR, invert(cond)
            emit32(kCset | (invertCond(cc) << 12) | rd, cs);
            return;
        }
        case MOpcode::Csel: {
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

            if (isInSignedImmRange(offset)) {
                uint32_t tmpl = isLoad ? kLdurGpr : kSturGpr;
                emit32(tmpl | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (fp << 5) | rt, cs);
            } else {
                encodeLargeOffsetLdSt(rt, fp, offset, isLoad, false, cs);
            }
            return;
        }
        case MOpcode::StrRegFpImm: {
            uint32_t rt = hwGPR(getReg(mi.ops[0]));
            long long offset = getImm(mi.ops[1]);
            uint32_t fp = hwGPR(PhysReg::X29);

            if (isInSignedImmRange(offset))
                emit32(kSturGpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (fp << 5) | rt,
                       cs);
            else
                encodeLargeOffsetLdSt(rt, fp, offset, false, false, cs);
            return;
        }
        case MOpcode::LdrFprFpImm:
        case MOpcode::PhiStoreFPR: {
            uint32_t rt = hwFPR(getReg(mi.ops[0]));
            long long offset = getImm(mi.ops[1]);
            uint32_t fp = hwGPR(PhysReg::X29);
            bool isLoad = (mi.opc == MOpcode::LdrFprFpImm);

            if (isInSignedImmRange(offset)) {
                uint32_t tmpl = isLoad ? kLdurFpr : kSturFpr;
                emit32(tmpl | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (fp << 5) | rt, cs);
            } else {
                encodeLargeOffsetLdSt(rt, fp, offset, isLoad, true, cs);
            }
            return;
        }
        case MOpcode::StrFprFpImm: {
            uint32_t rt = hwFPR(getReg(mi.ops[0]));
            long long offset = getImm(mi.ops[1]);
            uint32_t fp = hwGPR(PhysReg::X29);

            if (isInSignedImmRange(offset))
                emit32(kSturFpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (fp << 5) | rt,
                       cs);
            else
                encodeLargeOffsetLdSt(rt, fp, offset, false, true, cs);
            return;
        }

        // ─── Load/Store (Base-relative) ───
        case MOpcode::LdrRegBaseImm: {
            uint32_t rt = hwGPR(getReg(mi.ops[0]));
            uint32_t base = hwGPR(getReg(mi.ops[1]));
            long long offset = getImm(mi.ops[2]);

            if (isInSignedImmRange(offset))
                emit32(kLdurGpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (base << 5) |
                           rt,
                       cs);
            else
                encodeLargeOffsetLdSt(rt, base, offset, true, false, cs);
            return;
        }
        case MOpcode::StrRegBaseImm: {
            uint32_t rt = hwGPR(getReg(mi.ops[0]));
            uint32_t base = hwGPR(getReg(mi.ops[1]));
            long long offset = getImm(mi.ops[2]);

            if (isInSignedImmRange(offset))
                emit32(kSturGpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (base << 5) |
                           rt,
                       cs);
            else
                encodeLargeOffsetLdSt(rt, base, offset, false, false, cs);
            return;
        }
        case MOpcode::LdrFprBaseImm: {
            uint32_t rt = hwFPR(getReg(mi.ops[0]));
            uint32_t base = hwGPR(getReg(mi.ops[1]));
            long long offset = getImm(mi.ops[2]);

            if (isInSignedImmRange(offset))
                emit32(kLdurFpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (base << 5) |
                           rt,
                       cs);
            else
                encodeLargeOffsetLdSt(rt, base, offset, true, true, cs);
            return;
        }
        case MOpcode::StrFprBaseImm: {
            uint32_t rt = hwFPR(getReg(mi.ops[0]));
            uint32_t base = hwGPR(getReg(mi.ops[1]));
            long long offset = getImm(mi.ops[2]);

            if (isInSignedImmRange(offset))
                emit32(kSturFpr | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (base << 5) |
                           rt,
                       cs);
            else
                encodeLargeOffsetLdSt(rt, base, offset, false, true, cs);
            return;
        }

        // ─── Load/Store Pair (FP-relative) ───
        case MOpcode::LdpRegFpImm: {
            uint32_t rt = hwGPR(getReg(mi.ops[0]));
            uint32_t rt2 = hwGPR(getReg(mi.ops[1]));
            auto offset = static_cast<int32_t>(getImm(mi.ops[2]));
            uint32_t fp = hwGPR(PhysReg::X29);
            emit32(encodePair(kLdpGpr, rt, rt2, fp, offset / 8), cs);
            return;
        }
        case MOpcode::StpRegFpImm: {
            uint32_t rt = hwGPR(getReg(mi.ops[0]));
            uint32_t rt2 = hwGPR(getReg(mi.ops[1]));
            auto offset = static_cast<int32_t>(getImm(mi.ops[2]));
            uint32_t fp = hwGPR(PhysReg::X29);
            emit32(encodePair(kStpGpr, rt, rt2, fp, offset / 8), cs);
            return;
        }
        case MOpcode::LdpFprFpImm: {
            uint32_t rt = hwFPR(getReg(mi.ops[0]));
            uint32_t rt2 = hwFPR(getReg(mi.ops[1]));
            auto offset = static_cast<int32_t>(getImm(mi.ops[2]));
            uint32_t fp = hwGPR(PhysReg::X29);
            emit32(encodePair(kLdpFpr, rt, rt2, fp, offset / 8), cs);
            return;
        }
        case MOpcode::StpFprFpImm: {
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
        case MOpcode::StrRegSpImm: {
            uint32_t rt = hwGPR(getReg(mi.ops[0]));
            const auto offset = static_cast<int64_t>(getImm(mi.ops[1]));
            encodeSpOffsetStore(rt, offset, false, cs);
            return;
        }
        case MOpcode::StrFprSpImm: {
            uint32_t rt = hwFPR(getReg(mi.ops[0]));
            const auto offset = static_cast<int64_t>(getImm(mi.ops[1]));
            encodeSpOffsetStore(rt, offset, true, cs);
            return;
        }

        // ─── AddFpImm (address computation: dst = x29 + offset) ───
        case MOpcode::AddFpImm: {
            uint32_t rd = hwGPR(getReg(mi.ops[0]));
            long long offset = getImm(mi.ops[1]);
            uint32_t fp = hwGPR(PhysReg::X29);
            const uint64_t magnitude = absImmUnsigned(offset);
            uint32_t tmpl = (offset >= 0) ? kAddRI : kSubRI;
            if (const auto enc = classifyAddSubImmEncoding(magnitude)) {
                emit32(enc->shift12 ? encodeAddSubImmShift(tmpl, rd, fp, enc->imm12)
                                    : encodeAddSubImm(tmpl, rd, fp, enc->imm12),
                       cs);
            } else {
                // Large offset: movz x9, #abs(offset); add/sub rd, x29, x9
                uint32_t scratch = hwGPR(PhysReg::X9);
                encodeMovImm64(scratch, magnitude, cs);
                if (offset >= 0)
                    emit32(encode3Reg(kAddRRR, rd, fp, scratch), cs);
                else
                    emit32(encode3Reg(kSubRRR, rd, fp, scratch), cs);
            }
            return;
        }

        // ─── Logical Immediate ───
        case MOpcode::AndRI: {
            uint32_t rd = hwGPR(getReg(mi.ops[0]));
            uint32_t rn = hwGPR(getReg(mi.ops[1]));
            auto imm = static_cast<uint64_t>(getImm(mi.ops[2]));
            int32_t enc = encodeLogicalImmediate(imm);
            if (enc >= 0) {
                emit32(encodeLogImm(kAndImm, rd, rn, enc), cs);
            } else {
                uint32_t scratch = hwGPR(PhysReg::X9);
                encodeMovImm64(scratch, imm, cs);
                emit32(encode3Reg(kAndRRR, rd, rn, scratch), cs);
            }
            return;
        }
        case MOpcode::OrrRI: {
            uint32_t rd = hwGPR(getReg(mi.ops[0]));
            uint32_t rn = hwGPR(getReg(mi.ops[1]));
            auto imm = static_cast<uint64_t>(getImm(mi.ops[2]));
            int32_t enc = encodeLogicalImmediate(imm);
            if (enc >= 0) {
                emit32(encodeLogImm(kOrrImm, rd, rn, enc), cs);
            } else {
                uint32_t scratch = hwGPR(PhysReg::X9);
                encodeMovImm64(scratch, imm, cs);
                emit32(encode3Reg(kOrrRRR, rd, rn, scratch), cs);
            }
            return;
        }
        case MOpcode::EorRI: {
            uint32_t rd = hwGPR(getReg(mi.ops[0]));
            uint32_t rn = hwGPR(getReg(mi.ops[1]));
            auto imm = static_cast<uint64_t>(getImm(mi.ops[2]));
            int32_t enc = encodeLogicalImmediate(imm);
            if (enc >= 0) {
                emit32(encodeLogImm(kEorImm, rd, rn, enc), cs);
            } else {
                uint32_t scratch = hwGPR(PhysReg::X9);
                encodeMovImm64(scratch, imm, cs);
                emit32(encode3Reg(kEorRRR, rd, rn, scratch), cs);
            }
            return;
        }

        // ─── Floating Point — Three-Register ───
        case MOpcode::FAddRRR:
            emit32(encode3Reg(kFAddRRR,
                              hwFPR(getReg(mi.ops[0])),
                              hwFPR(getReg(mi.ops[1])),
                              hwFPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::FSubRRR:
            emit32(encode3Reg(kFSubRRR,
                              hwFPR(getReg(mi.ops[0])),
                              hwFPR(getReg(mi.ops[1])),
                              hwFPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::FMulRRR:
            emit32(encode3Reg(kFMulRRR,
                              hwFPR(getReg(mi.ops[0])),
                              hwFPR(getReg(mi.ops[1])),
                              hwFPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::FDivRRR:
            emit32(encode3Reg(kFDivRRR,
                              hwFPR(getReg(mi.ops[0])),
                              hwFPR(getReg(mi.ops[1])),
                              hwFPR(getReg(mi.ops[2]))),
                   cs);
            return;
        case MOpcode::FCmpRR:
            // fcmp Dn, Dm (Rd field = 0)
            emit32(kFCmpRR | (hwFPR(getReg(mi.ops[1])) << 16) | (hwFPR(getReg(mi.ops[0])) << 5),
                   cs);
            return;
        case MOpcode::FMovRR: {
            PhysReg src = getReg(mi.ops[1]);
            if (static_cast<uint32_t>(src) <= static_cast<uint32_t>(PhysReg::SP)) {
                // Source is a GPR — emit fmov Dd, Xn (GPR→FPR bit transfer) instead.
                emit32(encode2Reg(kFMovGR, hwFPR(getReg(mi.ops[0])), hwGPR(src)), cs);
            } else {
                emit32(encode2Reg(kFMovRR, hwFPR(getReg(mi.ops[0])), hwFPR(src)), cs);
            }
            return;
        }
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
        case MOpcode::FMovRI: {
            uint32_t rd = hwFPR(getReg(mi.ops[0]));
            double val;
            std::memcpy(&val, &mi.ops[1].imm, sizeof(val));

            // Try FP8 immediate encoding first (single instruction).
            int32_t fp8 = encodeFP8Immediate(val);
            if (fp8 >= 0) {
                // FMOV Dd, #imm8 — imm8 at bits [20:13].
                emit32(kFMovDImm | (static_cast<uint32_t>(fp8) << 13) | rd, cs);
                return;
            }

            // Fallback: materialise 64-bit IEEE 754 bits in a GPR, then FMOV Dd, Xn.
            uint64_t bits;
            std::memcpy(&bits, &val, sizeof(bits));
            uint32_t scratch = hwGPR(PhysReg::X9);
            encodeMovImm64(scratch, bits, cs);
            emit32(encode2Reg(kFMovGR, rd, scratch), cs);
            return;
        }

        // ─── Branch Instructions ───
        case MOpcode::Br: {
            std::string target = sanitizeLabel(mi.ops[0].label);
            auto it = labelOffsets_.find(target);
            if (it != labelOffsets_.end()) {
                // Backward branch — resolve immediately.
                int64_t delta =
                    static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset());
                const int32_t imm26 =
                    checkedBranchDispWords(delta,
                                           26,
                                           "branch",
                                           "the +/-128MB B/BL range",
                                           target,
                                           currentFn_ ? currentFn_->name : "<unknown>");
                emit32(kBr | (static_cast<uint32_t>(imm26) & 0x3FFFFFF), cs);
            } else {
                pendingBranches_.push_back({cs.currentOffset(), target, MOpcode::Br});
                emit32(kBr, cs); // placeholder
            }
            return;
        }
        case MOpcode::BCond: {
            uint32_t cc = condCode(mi.ops[0].cond);
            std::string target = sanitizeLabel(mi.ops[1].label);
            auto it = labelOffsets_.find(target);
            if (it != labelOffsets_.end()) {
                const int64_t delta =
                    static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset());
                if (fitsBranchDispWords(delta, 19)) {
                    const int32_t imm19 =
                        checkedBranchDispWords(delta,
                                               19,
                                               "conditional branch",
                                               "the +/-1MB conditional-branch range",
                                               target,
                                               currentFn_ ? currentFn_->name : "<unknown>");
                    emit32(kBCond | ((static_cast<uint32_t>(imm19) & 0x7FFFF) << 5) | cc, cs);
                } else {
                    emit32(kBCond | (2u << 5) | invertCond(cc), cs);
                    const int64_t farDelta = static_cast<int64_t>(it->second) -
                                             static_cast<int64_t>(cs.currentOffset());
                    const int32_t imm26 =
                        checkedBranchDispWords(farDelta,
                                               26,
                                               "branch",
                                               "the +/-128MB B/BL range",
                                               target,
                                               currentFn_ ? currentFn_->name : "<unknown>");
                    emit32(kBr | (static_cast<uint32_t>(imm26) & 0x3FFFFFF), cs);
                }
            } else {
                pendingBranches_.push_back({cs.currentOffset(), target, MOpcode::BCond});
                emit32(kBCond | cc, cs); // placeholder with cond code set
            }
            return;
        }
        case MOpcode::Cbz: {
            uint32_t rt = hwGPR(getReg(mi.ops[0]));
            std::string target = sanitizeLabel(mi.ops[1].label);
            auto it = labelOffsets_.find(target);
            if (it != labelOffsets_.end()) {
                const int64_t delta =
                    static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset());
                if (fitsBranchDispWords(delta, 19)) {
                    const int32_t imm19 =
                        checkedBranchDispWords(delta,
                                               19,
                                               "conditional branch",
                                               "the +/-1MB conditional-branch range",
                                               target,
                                               currentFn_ ? currentFn_->name : "<unknown>");
                    emit32(kCbz | ((static_cast<uint32_t>(imm19) & 0x7FFFF) << 5) | rt, cs);
                } else {
                    emit32(kCbnz | (2u << 5) | rt, cs);
                    const int64_t farDelta = static_cast<int64_t>(it->second) -
                                             static_cast<int64_t>(cs.currentOffset());
                    const int32_t imm26 =
                        checkedBranchDispWords(farDelta,
                                               26,
                                               "branch",
                                               "the +/-128MB B/BL range",
                                               target,
                                               currentFn_ ? currentFn_->name : "<unknown>");
                    emit32(kBr | (static_cast<uint32_t>(imm26) & 0x3FFFFFF), cs);
                }
            } else {
                pendingBranches_.push_back({cs.currentOffset(), target, MOpcode::Cbz});
                emit32(kCbz | rt, cs); // placeholder with Rt set
            }
            return;
        }
        case MOpcode::Cbnz: {
            uint32_t rt = hwGPR(getReg(mi.ops[0]));
            std::string target = sanitizeLabel(mi.ops[1].label);
            auto it = labelOffsets_.find(target);
            if (it != labelOffsets_.end()) {
                const int64_t delta =
                    static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset());
                if (fitsBranchDispWords(delta, 19)) {
                    const int32_t imm19 =
                        checkedBranchDispWords(delta,
                                               19,
                                               "conditional branch",
                                               "the +/-1MB conditional-branch range",
                                               target,
                                               currentFn_ ? currentFn_->name : "<unknown>");
                    emit32(kCbnz | ((static_cast<uint32_t>(imm19) & 0x7FFFF) << 5) | rt, cs);
                } else {
                    emit32(kCbz | (2u << 5) | rt, cs);
                    const int64_t farDelta = static_cast<int64_t>(it->second) -
                                             static_cast<int64_t>(cs.currentOffset());
                    const int32_t imm26 =
                        checkedBranchDispWords(farDelta,
                                               26,
                                               "branch",
                                               "the +/-128MB B/BL range",
                                               target,
                                               currentFn_ ? currentFn_->name : "<unknown>");
                    emit32(kBr | (static_cast<uint32_t>(imm26) & 0x3FFFFFF), cs);
                }
            } else {
                pendingBranches_.push_back({cs.currentOffset(), target, MOpcode::Cbnz});
                emit32(kCbnz | rt, cs);
            }
            return;
        }
        case MOpcode::Bl: {
            // Direct call — always external (generates relocation).
            std::string sym = mapRuntimeSymbol(mi.ops[0].label);
            auto it = labelOffsets_.find(sanitizeLabel(mi.ops[0].label));
            if (it != labelOffsets_.end()) {
                // Internal call (rare but possible for local functions).
                int64_t delta =
                    static_cast<int64_t>(it->second) - static_cast<int64_t>(cs.currentOffset());
                const int32_t imm26 =
                    checkedBranchDispWords(delta,
                                           26,
                                           "call",
                                           "the +/-128MB B/BL range",
                                           mi.ops[0].label,
                                           currentFn_ ? currentFn_->name : "<unknown>");
                emit32(kBl | (static_cast<uint32_t>(imm26) & 0x3FFFFFF), cs);
            } else {
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
        case MOpcode::AdrPage: {
            uint32_t rd = hwGPR(getReg(mi.ops[0]));
            std::string sym = mi.ops[1].label;
            uint32_t symIdx = cs.findOrDeclareSymbol(sym);
            const auto targetSection = (currentRodata_ != nullptr &&
                                        currentRodata_->symbols().find(sym) != 0)
                                           ? objfile::SymbolSection::Rodata
                                           : objfile::SymbolSection::Undefined;
            cs.addRelocation(objfile::RelocKind::A64AdrpPage21, symIdx, 0, targetSection);
            emit32(kAdrp | rd, cs); // immediate filled by linker
            return;
        }
        case MOpcode::AddPageOff: {
            uint32_t rd = hwGPR(getReg(mi.ops[0]));
            uint32_t rn = hwGPR(getReg(mi.ops[1]));
            std::string sym = mi.ops[2].label;
            uint32_t symIdx = cs.findOrDeclareSymbol(sym);
            const auto targetSection = (currentRodata_ != nullptr &&
                                        currentRodata_->symbols().find(sym) != 0)
                                           ? objfile::SymbolSection::Rodata
                                           : objfile::SymbolSection::Undefined;
            cs.addRelocation(objfile::RelocKind::A64AddPageOff12, symIdx, 0, targetSection);
            emit32(encodeAddSubImm(kAddRI, rd, rn, 0), cs); // imm12 filled by linker
            return;
        }

        // ─── Pseudo-instructions that should have been expanded ───
        case MOpcode::AddOvfRRR:
        case MOpcode::SubOvfRRR:
        case MOpcode::AddOvfRI:
        case MOpcode::SubOvfRI:
        case MOpcode::MulOvfRRR:
            throw std::runtime_error("AArch64 binary encoder: overflow pseudo-op '" +
                                     std::string(opcodeName(mi.opc)) +
                                     "' reached binary emission before LowerOvf");

    } // end switch

    // If we reach here, the opcode was not handled.
    throw std::runtime_error("AArch64 binary encoder: unhandled opcode '" +
                             std::string(opcodeName(mi.opc)) + "'");
}

} // namespace viper::codegen::aarch64::binenc
