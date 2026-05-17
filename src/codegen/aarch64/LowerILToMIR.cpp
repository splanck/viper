//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/LowerILToMIR.cpp
// Purpose: IL→MIR lowering orchestrator for AArch64.
//          Coordinates the full IL-to-MIR conversion: fast-path probe, frame
//          setup, phi parameter slot allocation, cross-block liveness analysis,
//          per-block instruction dispatch, and terminator lowering.
// Key invariants:
//   - Fast paths are tried first; generic lowering is used on miss.
//   - Cross-block temps are spilled at definition and reloaded at use.
//   - Phi slots are allocated as stack spills before instruction lowering.
// Ownership/Lifetime:
//   - All state is local to lowerFunction(); the LowerILToMIR object is stateless.
// Links: codegen/aarch64/LowerILToMIR.hpp,
//        codegen/aarch64/InstrLowering.hpp,
//        codegen/aarch64/OpcodeDispatch.hpp,
//        codegen/aarch64/TerminatorLowering.hpp,
//        codegen/aarch64/LivenessAnalysis.hpp,
//        codegen/aarch64/FastPaths.hpp
//
//===----------------------------------------------------------------------===//

#include "LowerILToMIR.hpp"

#include "FastPaths.hpp"
#include "FpCompareLowering.hpp"
#include "FrameBuilder.hpp"
#include "InstrLowering.hpp"
#include "LivenessAnalysis.hpp"
#include "LoweringContext.hpp"
#include "OpcodeDispatch.hpp"
#include "OpcodeMappings.hpp"
#include "TargetAArch64.hpp"
#include "TerminatorLowering.hpp"
#include "codegen/common/CallArgLayout.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"

#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::aarch64 {
namespace {
using il::core::Opcode;

/// @brief Return the AArch64 condition-code string for an IL comparison opcode.
static const char *condForOpcode(Opcode op) {
    return lookupAnyCondition(op);
}

/// @brief Return the bit-width for an IL integer type (I1→1, I16→16, I32→32, else 64).
static int integerTypeBits(il::core::Type::Kind kind) {
    switch (kind) {
        case il::core::Type::Kind::I1:
            return 1;
        case il::core::Type::Kind::I16:
            return 16;
        case il::core::Type::Kind::I32:
            return 32;
        default:
            return 64;
    }
}

/// @brief Emit LSL/ASR to sign-extend @p src from @p bits to 64 bits; return the result vreg.
/// @details No-op (returns @p src) when @p bits >= 64.
static uint16_t signExtendVRegToWidth(MBasicBlock &out,
                                      uint16_t src,
                                      int bits,
                                      uint16_t &nextVRegId) {
    if (bits >= 64)
        return src;
    const int shift = 64 - bits;
    const uint16_t dst = allocateNextVReg(nextVRegId);
    out.instrs.push_back(MInstr{MOpcode::LslRI,
                                {MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::vregOp(RegClass::GPR, src),
                                 MOperand::immOp(shift)}});
    out.instrs.push_back(MInstr{MOpcode::AsrRI,
                                {MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::immOp(shift)}});
    return dst;
}

/// @brief Emit a sub-64-bit checked overflow binary operation (IAddOvf/ISubOvf/IMulOvf).
/// @details Sign-extends both operands to the target width, performs the op, then sign-extends
///          the result and compares it to the full-width result. If they differ the value
///          overflowed — a trap block calling rt_trap_ovf is appended to @p mf.
/// @return true if the instruction was handled as a sub-width checked op; false if bits >= 64
///         or the opcode is not a checked overflow op (caller should use the generic path).
static bool emitSubWidthCheckedBinary(MFunction &mf,
                                      MBasicBlock &out,
                                      const il::core::Instr &ins,
                                      uint16_t dst,
                                      uint16_t lhs,
                                      uint16_t rhs,
                                      uint16_t &nextVRegId) {
    const int bits = integerTypeBits(ins.type.kind);
    if (bits >= 64)
        return false;

    MOpcode op = MOpcode::AddRRR;
    switch (ins.op) {
        case Opcode::IAddOvf:
            op = MOpcode::AddRRR;
            break;
        case Opcode::ISubOvf:
            op = MOpcode::SubRRR;
            break;
        case Opcode::IMulOvf:
            op = MOpcode::MulRRR;
            break;
        default:
            return false;
    }

    lhs = signExtendVRegToWidth(out, lhs, bits, nextVRegId);
    rhs = signExtendVRegToWidth(out, rhs, bits, nextVRegId);
    out.instrs.push_back(MInstr{op,
                                {MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::vregOp(RegClass::GPR, lhs),
                                 MOperand::vregOp(RegClass::GPR, rhs)}});

    const uint16_t narrowed = signExtendVRegToWidth(out, dst, bits, nextVRegId);
    const std::string trapLabel = ".Ltrap_subwidth_ovf_" + mf.name + "_" +
                                  std::to_string(mf.blocks.size()) + "_" +
                                  std::to_string(out.instrs.size());
    out.instrs.push_back(MInstr{
        MOpcode::CmpRR,
        {MOperand::vregOp(RegClass::GPR, narrowed), MOperand::vregOp(RegClass::GPR, dst)}});
    out.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trapLabel)}});

    mf.blocks.emplace_back();
    mf.blocks.back().name = trapLabel;
    mf.blocks.back().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_ovf")}});
    return true;
}

/// @brief Build a use-count table indexed by temp ID for the entire function.
/// @details Scans all operands and branch-arg lists. Returns a vector of length
///          (max_temp_id + 1) where each entry is the total use count for that temp.
///          Used to detect single-use temps that can be inlined without a MOV.
static std::vector<std::size_t> countTempUses(const il::core::Function &fn) {
    using il::core::Value;

    unsigned maxId = 0;
    for (const auto &param : fn.params)
        maxId = std::max(maxId, param.id);
    for (const auto &block : fn.blocks) {
        for (const auto &param : block.params)
            maxId = std::max(maxId, param.id);
        for (const auto &instr : block.instructions)
            if (instr.result)
                maxId = std::max(maxId, static_cast<unsigned>(*instr.result));
    }

    std::vector<std::size_t> uses(static_cast<std::size_t>(maxId) + 1, 0);
    auto touch = [&](unsigned id) {
        if (id < uses.size())
            uses[id]++;
    };

    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            for (const auto &operand : instr.operands) {
                if (operand.kind == Value::Kind::Temp)
                    touch(operand.id);
            }
            for (const auto &argList : instr.brArgs) {
                for (const auto &arg : argList) {
                    if (arg.kind == Value::Kind::Temp)
                        touch(arg.id);
                }
            }
        }
    }

    return uses;
}

/// @brief Return true if @p bb's terminator is a CBr that consumes @p tempId as its condition.
/// @details Used to skip materializing the condition into an extra vreg when the CBr
///          can consume the flag result directly from the preceding comparison.
static bool cbrConsumesTemp(const il::core::BasicBlock &bb, unsigned tempId) {
    using il::core::Opcode;
    using il::core::Value;

    if (bb.instructions.empty())
        return false;
    const auto &term = bb.instructions.back();
    return term.op == Opcode::CBr && !term.operands.empty() && term.operands[0].kind == Value::Kind::Temp &&
           term.operands[0].id == tempId;
}

/// @brief Counter for generating unique trap labels within a function.
/// @details Reset at the start of each lowerFunction() call to ensure unique
///          labels per function (combined with the function name prefix).
static thread_local unsigned trapLabelCounter;

} // namespace

std::optional<std::size_t> LowerILToMIR::knownVarArgNamedArgs(std::string_view callee) const {
    const auto it = knownVarArgNamedArgCounts_.find(std::string(callee));
    if (it == knownVarArgNamedArgCounts_.end())
        return std::nullopt;
    return it->second;
}

MFunction LowerILToMIR::lowerFunction(const il::core::Function &fn) const {
    MFunction mf{};
    mf.name = fn.name;
    // Reserve extra capacity for any helper/trap blocks we may append during lowering
    // to avoid std::vector reallocation which would invalidate references held by
    // in-flight lowering helpers. Over-reserving keeps references stable.
    mf.blocks.reserve(fn.blocks.size() + 1024);
    // Reset trap label counter for unique labels within this function
    trapLabelCounter = 0;

    // Pre-create MIR blocks with labels to mirror IL CFG shape.
    for (const auto &bb : fn.blocks) {
        mf.blocks.emplace_back();
        mf.blocks.back().name = bb.label;
    }

    // Helper to access a MIR block by IL block index
    auto bbOut = [&](std::size_t idx) -> MBasicBlock & { return mf.blocks[idx]; };

    // Support i64 and pointer-centric functions; arithmetic patterns remain i64-centric.

    const auto &argOrder = ti_->intArgOrder;

    // Build stack frame locals from allocas
    // Track which temps are allocas so we can exclude them from cross-block spilling
    std::unordered_set<unsigned> allocaTemps;
    FrameBuilder fb{mf};
    for (const auto &bb : fn.blocks) {
        for (const auto &instr : bb.instructions) {
            if (instr.op == il::core::Opcode::Alloca && instr.result && !instr.operands.empty()) {
                if (instr.operands[0].kind == il::core::Value::Kind::ConstInt) {
                    const long long rawSize = instr.operands[0].i64;
                    if (rawSize <= 0 || rawSize > std::numeric_limits<int>::max()) {
                        throw std::out_of_range("AArch64 codegen: alloca size must be in range "
                                                "1..INT_MAX bytes");
                    }
                    const int size = static_cast<int>(rawSize);
                    fb.addLocal(*instr.result, size, kSlotSizeBytes);
                    allocaTemps.insert(*instr.result);
                }
            }
        }
    }

    // Assign canonical vregs for block parameters (phi-elimination by edge moves).
    // We use spill slots to pass values across block boundaries since the register
    // allocator releases vreg→phys mappings at block ends.
    // NOTE: Skip the entry block (bi == 0) - its params are function args passed via ABI registers.
    std::unordered_map<std::string, std::vector<uint16_t>>
        phiVregId; // block label -> vreg per param
    std::unordered_map<std::string, std::vector<RegClass>>
        phiRegClass; // block label -> reg class per param
    std::unordered_map<std::string, std::vector<int>>
        phiSpillOffset;                 // block label -> spill offset per param
    uint16_t phiNextId = kPhiVRegStart; // reserve a distinct vreg range for phi temporaries
    for (std::size_t bi = 1; bi < fn.blocks.size(); ++bi) // Start at 1, skip entry block
    {
        const auto &bb = fn.blocks[bi];
        if (!bb.params.empty()) {
            std::vector<uint16_t> ids;
            std::vector<RegClass> classes;
            std::vector<int> spillOffsets;
            ids.reserve(bb.params.size());
            classes.reserve(bb.params.size());
            spillOffsets.reserve(bb.params.size());
            for (std::size_t pi = 0; pi < bb.params.size(); ++pi) {
                uint16_t id = allocatePhiVReg(phiNextId);
                ids.push_back(id);
                const auto &pt = bb.params[pi].type;
                const RegClass cls =
                    (pt.kind == il::core::Type::Kind::F64) ? RegClass::FPR : RegClass::GPR;
                classes.push_back(cls);
                // Allocate a dedicated spill slot for this phi value
                int offset = fb.ensureSpill(id);
                spillOffsets.push_back(offset);
            }
            phiVregId.emplace(bb.label, std::move(ids));
            phiRegClass.emplace(bb.label, std::move(classes));
            phiSpillOffset.emplace(bb.label, std::move(spillOffsets));
        }
    }

    // ===========================================================================
    // Global Liveness Analysis for Cross-Block Temps
    // ===========================================================================
    LivenessInfo liveness = analyzeCrossBlockLiveness(fn, allocaTemps, fb);

    // Try fast-paths for simple function patterns
    if (auto result =
            tryFastPaths(fn, *ti_, fb, mf, stringLiteralByteLengths_, &knownVarArgNamedArgCounts_))
        return *result;

    // Generic fallback: lower stack/local loads/stores and a simple return
    // This path handles arbitrary placement of alloca/load/store in a single block without
    // full-blown selection for other ops yet.

    // Use a single function-wide tempVReg map so values materialized in one block
    // are visible to other blocks. This handles cross-block value references that
    // the BASIC frontend generates (e.g., array operations using values from predecessor blocks).
    const auto tempUseCounts = countTempUses(fn);
    std::unordered_map<unsigned, uint16_t> tempVReg;
    // Track register class (GPR vs FPR) for each temp within this function
    std::unordered_map<unsigned, RegClass> tempRegClass;
    uint16_t nextVRegId = kFirstVirtualRegId; // vreg ids start at 1

    // Map function parameter IDs to their spill offsets (for entry block params)
    std::unordered_map<unsigned, int> funcParamSpillOffset;

    // Save per-block tempVReg snapshots so terminator loop can use the correct vreg mappings.
    // This is needed because cross-block temp reloading in later blocks can overwrite tempVReg
    // entries, but the terminator loop for the DEFINING block needs the original vreg.
    std::vector<std::unordered_map<unsigned, uint16_t>> blockTempVRegSnapshot(fn.blocks.size());

    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        const auto &bbIn = fn.blocks[bi];
        // NOTE: We use index bi to access mf.blocks[bi] instead of a reference because
        // instruction lowering can add new trap blocks via emplace_back(), which may
        // reallocate the vector and invalidate references.
        // NOTE: Do NOT clear tempRegClass here - we need to preserve class info for
        // cross-block temps that are spilled/reloaded. It's already cleared at function start.
        // Helper lambda to get current output block (avoids dangling references)
        auto bbOutFn = [&]() -> MBasicBlock & { return mf.blocks[bi]; };
        std::unordered_set<unsigned> reloadedCrossBlockTemps;

        auto reloadCrossBlockTempAtBlockEntry = [&](unsigned tempId) {
            auto spillIt = liveness.crossBlockSpillOffset.find(tempId);
            auto defIt = liveness.tempDefBlock.find(tempId);
            if (spillIt == liveness.crossBlockSpillOffset.end() ||
                defIt == liveness.tempDefBlock.end() || defIt->second == bi) {
                return;
            }

            // Cross-block temps need a fresh vreg in each block because the register allocator
            // is free to assign unrelated physical registers once control leaves the defining
            // block. Reload exactly once per block and then reuse the rematerialized mapping.
            if (!reloadedCrossBlockTemps.insert(tempId).second)
                return;

            const uint16_t vid = allocateNextVReg(nextVRegId);
            tempVReg[tempId] = vid;
            const int offset = spillIt->second;
            auto clsIt = tempRegClass.find(tempId);
            const RegClass cls =
                (clsIt != tempRegClass.end()) ? clsIt->second : RegClass::GPR;
            if (cls == RegClass::FPR) {
                bbOutFn().instrs.push_back(
                    MInstr{MOpcode::LdrFprFpImm,
                           {MOperand::vregOp(RegClass::FPR, vid), MOperand::immOp(offset)}});
            } else {
                bbOutFn().instrs.push_back(
                    MInstr{MOpcode::LdrRegFpImm,
                           {MOperand::vregOp(RegClass::GPR, vid), MOperand::immOp(offset)}});
            }
        };

        // Entry block (bi == 0): Spill function parameters to stack slots immediately.
        // This ensures parameters are preserved across function calls within the entry block.
        // ABI registers (x0-x7, v0-v7) are caller-saved and will be clobbered by calls.
        if (bi == 0 && !bbIn.params.empty()) {
            std::vector<viper::codegen::common::CallArgClass> paramClasses;
            paramClasses.reserve(bbIn.params.size());
            for (const auto &param : bbIn.params) {
                paramClasses.push_back(param.type.kind == il::core::Type::Kind::F64
                                           ? viper::codegen::common::CallArgClass::FPR
                                           : viper::codegen::common::CallArgClass::GPR);
            }
            const auto layout = viper::codegen::common::planParamClasses(
                paramClasses,
                viper::codegen::common::CallArgLayoutConfig{
                    .maxGPRArgs = ti_->intArgOrder.size(),
                    .maxFPRArgs = ti_->f64ArgOrder.size(),
                    .slotModel = viper::codegen::common::CallSlotModel::IndependentRegisterBanks,
                    .variadicTailOnStack = fn.isVarArg && ti_->usesStackVariadicTail(),
                    .numNamedArgs = paramClasses.size()});

            for (std::size_t pi = 0; pi < bbIn.params.size(); ++pi) {
                const auto &param = bbIn.params[pi];
                const auto &loc = layout.locations[pi];
                const RegClass cls = (loc.cls == viper::codegen::common::CallArgClass::FPR)
                                         ? RegClass::FPR
                                         : RegClass::GPR;

                // Allocate spill slot for this parameter
                // IMPORTANT: Use param.id (not pi index) to match LivenessAnalysis.cpp
                // cross-block spill keys. Without this,
                // the entry block and other blocks would use different spill slots for the
                // same parameter, causing BUG-005 (BYREF parameters not working).
                const int spillOffset = fb.ensureSpill(spillKeyForCrossBlockTemp(param.id));
                funcParamSpillOffset[param.id] = spillOffset;

                bool isStackParam = false;
                PhysReg src{};
                if (loc.inRegister) {
                    if (cls == RegClass::FPR)
                        src = ti_->f64ArgOrder[loc.regIndex];
                    else
                        src = ti_->intArgOrder[loc.regIndex];
                } else {
                    isStackParam = true;
                }

                if (isStackParam) {
                    // Stack parameter: load from caller's outgoing arg area.
                    // After prologue (stp x29, x30, [sp, #-16]!; mov x29, sp),
                    // caller's stack args are at [FP + 16 + stackArgIdx * 8].
                    const int callerArgOffset = 16 + static_cast<int>(loc.stackSlotIndex) * 8;

                    // Load from caller's stack arg area into spill slot via a temporary
                    // vreg. IMPORTANT: We must NOT use a hardcoded physical register here
                    // because the register allocator may have assigned that register to a
                    // vreg for an earlier parameter that is still live. Using a vreg lets
                    // the allocator pick a non-conflicting physical register.
                    if (cls == RegClass::FPR) {
                        const uint16_t tmpVid = allocateNextVReg(nextVRegId);
                        bbOutFn().instrs.push_back(MInstr{MOpcode::LdrFprFpImm,
                                                          {MOperand::vregOp(RegClass::FPR, tmpVid),
                                                           MOperand::immOp(callerArgOffset)}});
                        bbOutFn().instrs.push_back(MInstr{MOpcode::StrFprFpImm,
                                                          {MOperand::vregOp(RegClass::FPR, tmpVid),
                                                           MOperand::immOp(spillOffset)}});
                    } else {
                        const uint16_t tmpVid = allocateNextVReg(nextVRegId);
                        bbOutFn().instrs.push_back(MInstr{MOpcode::LdrRegFpImm,
                                                          {MOperand::vregOp(RegClass::GPR, tmpVid),
                                                           MOperand::immOp(callerArgOffset)}});
                        bbOutFn().instrs.push_back(MInstr{MOpcode::StrRegFpImm,
                                                          {MOperand::vregOp(RegClass::GPR, tmpVid),
                                                           MOperand::immOp(spillOffset)}});
                    }
                } else {
                    // Register parameter: store ABI register to spill slot
                    if (cls == RegClass::FPR) {
                        bbOutFn().instrs.push_back(
                            MInstr{MOpcode::StrFprFpImm,
                                   {MOperand::regOp(src), MOperand::immOp(spillOffset)}});
                    } else {
                        bbOutFn().instrs.push_back(
                            MInstr{MOpcode::StrRegFpImm,
                                   {MOperand::regOp(src), MOperand::immOp(spillOffset)}});
                    }
                }

                // Create vreg for this param and load from spill slot
                const uint16_t vid = allocateNextVReg(nextVRegId);
                tempVReg[param.id] = vid;
                tempRegClass[param.id] = cls;

                if (cls == RegClass::FPR) {
                    bbOutFn().instrs.push_back(MInstr{
                        MOpcode::LdrFprFpImm,
                        {MOperand::vregOp(RegClass::FPR, vid), MOperand::immOp(spillOffset)}});
                } else {
                    bbOutFn().instrs.push_back(MInstr{
                        MOpcode::LdrRegFpImm,
                        {MOperand::vregOp(RegClass::GPR, vid), MOperand::immOp(spillOffset)}});
                }
            }
        }

        // Load block parameters from spill slots into fresh vregs at block entry.
        // The edge copies store values to these spill slots before branching here.
        auto itPhi = phiVregId.find(bbIn.label);
        auto itSpill = phiSpillOffset.find(bbIn.label);
        if (itPhi != phiVregId.end() && itSpill != phiSpillOffset.end()) {
            const auto &ids = itPhi->second;
            const auto &spillOffsets = itSpill->second;
            const auto &classes = phiRegClass[bbIn.label];
            for (std::size_t pi = 0; pi < bbIn.params.size() && pi < ids.size(); ++pi) {
                const uint16_t vid = allocateNextVReg(nextVRegId);
                const unsigned paramId = bbIn.params[pi].id;
                tempVReg[paramId] = vid;
                const auto &pt = bbIn.params[pi].type;
                const RegClass cls =
                    (pt.kind == il::core::Type::Kind::F64) ? RegClass::FPR : RegClass::GPR;
                tempRegClass[paramId] = cls;
                const int offset = spillOffsets[pi];
                // Load from spill slot into vreg
                if (cls == RegClass::FPR) {
                    bbOutFn().instrs.push_back(
                        MInstr{MOpcode::LdrFprFpImm,
                               {MOperand::vregOp(RegClass::FPR, vid), MOperand::immOp(offset)}});
                } else {
                    bbOutFn().instrs.push_back(
                        MInstr{MOpcode::LdrRegFpImm,
                               {MOperand::vregOp(RegClass::GPR, vid), MOperand::immOp(offset)}});
                }

                // A promoted block parameter can be used directly by dominated
                // successor blocks. Those uses are handled by the cross-block
                // temp reload path below, so publish the freshly loaded phi
                // value into its cross-block spill slot at the defining block.
                if (auto spillIt = liveness.crossBlockSpillOffset.find(paramId);
                    spillIt != liveness.crossBlockSpillOffset.end()) {
                    const int crossBlockOffset = spillIt->second;
                    if (cls == RegClass::FPR) {
                        bbOutFn().instrs.push_back(
                            MInstr{MOpcode::StrFprFpImm,
                                   {MOperand::vregOp(RegClass::FPR, vid),
                                    MOperand::immOp(crossBlockOffset)}});
                    } else {
                        bbOutFn().instrs.push_back(
                            MInstr{MOpcode::StrRegFpImm,
                                   {MOperand::vregOp(RegClass::GPR, vid),
                                    MOperand::immOp(crossBlockOffset)}});
                    }
                }
            }
        }

        // Reload cross-block temps that are used in this block but defined elsewhere.
        // We need to reload them at block entry because the register allocator may have
        // reused their physical registers in intervening blocks.
        for (const auto &ins : bbIn.instructions) {
            for (const auto &op : ins.operands) {
                if (op.kind == il::core::Value::Kind::Temp)
                    reloadCrossBlockTempAtBlockEntry(op.id);
            }
            for (const auto &edgeArgs : ins.brArgs) {
                for (const auto &arg : edgeArgs) {
                    if (arg.kind == il::core::Value::Kind::Temp)
                        reloadCrossBlockTempAtBlockEntry(arg.id);
                }
            }
        }
        // Also check terminator for cross-block temp uses (CBr condition)
        if (!bbIn.instructions.empty()) {
            const auto &term = bbIn.instructions.back();
            if (term.op == il::core::Opcode::CBr && !term.operands.empty()) {
                const auto &cond = term.operands[0];
                if (cond.kind == il::core::Value::Kind::Temp)
                    reloadCrossBlockTempAtBlockEntry(cond.id);
            }
        }

        // Create lowering context for dispatching to extracted handlers
        LoweringContext ctx{fn,
                            *ti_,
                            fb,
                            mf,
                            nextVRegId,
                            tempVReg,
                            tempRegClass,
                            phiVregId,
                            phiRegClass,
                            phiSpillOffset,
                            liveness.crossBlockSpillOffset,
                                      liveness.tempDefBlock,
                                      liveness.crossBlockTemps,
                                      stringLiteralByteLengths_,
                                      &knownVarArgNamedArgCounts_,
                                      trapLabelCounter};

        for (const auto &ins : bbIn.instructions) {
            // Record instruction count so we can stamp source loc on new MInstrs.
            const size_t mirCountBefore = bbOutFn().instrs.size();

            // When a compare result is consumed only by this block's cbr, defer the
            // lowering to TerminatorLowering so it can emit cmp+b.cond directly.
            if (ins.result && isCompareOp(ins.op) && *ins.result < tempUseCounts.size() &&
                tempUseCounts[*ins.result] == 1 && cbrConsumesTemp(bbIn, *ins.result)) {
                continue;
            }

            // Try extracted handlers first; they return true if they handled the opcode
            if (lowerInstruction(ins, bbIn, ctx, bi)) {
                // Spill cross-block temps immediately after they are defined.
                // This ensures the value is preserved in memory for use in other blocks.
                if (ins.result) {
                    auto spillIt = liveness.crossBlockSpillOffset.find(*ins.result);
                    if (spillIt != liveness.crossBlockSpillOffset.end()) {
                        auto vregIt = tempVReg.find(*ins.result);
                        if (vregIt != tempVReg.end()) {
                            const uint16_t srcVreg = vregIt->second;
                            const int offset = spillIt->second;
                            // Respect the producing register class when spilling
                            auto clsIt = tempRegClass.find(*ins.result);
                            const RegClass cls =
                                (clsIt != tempRegClass.end()) ? clsIt->second : RegClass::GPR;
                            if (cls == RegClass::FPR) {
                                bbOutFn().instrs.push_back(
                                    MInstr{MOpcode::StrFprFpImm,
                                           {MOperand::vregOp(RegClass::FPR, srcVreg),
                                            MOperand::immOp(offset)}});
                            } else {
                                bbOutFn().instrs.push_back(
                                    MInstr{MOpcode::StrRegFpImm,
                                           {MOperand::vregOp(RegClass::GPR, srcVreg),
                                            MOperand::immOp(offset)}});
                            }
                        }
                    }
                }
                continue;
            }

            switch (ins.op) {
                    // NOTE: Zext1, Trunc1, CastSiNarrowChk, CastUiNarrowChk, CastFpToSiRteChk,
                    // CastFpToUiRteChk, CastSiToFp, CastUiToFp, SRemChk0, SDivChk0, UDivChk0,
                    // URemChk0, FAdd, FSub, FMul, FDiv, FCmpEQ, FCmpNE, FCmpLT, FCmpLE, FCmpGT,
                    // FCmpGE, Sitofp, Fptosi are handled by lowerInstruction() above

                // NOTE: Br, CBr, Call, Store, GEP, Load, Ret, Alloca, FP ops,
                // and conversions are all handled by lowerInstruction() in OpcodeDispatch.cpp
                case il::core::Opcode::Count:
                default:
                    // Handle binary ops and comparisons that may be referenced cross-block.
                    // This ensures values are materialized and cached in tempVReg for later use.
                    if (ins.result && ins.operands.size() == 2) {
                        const auto *binOp = lookupBinaryOp(ins.op);
                        if (binOp || isCompareOp(ins.op)) {
                            uint16_t lhs = 0, rhs = 0;
                            RegClass lcls = RegClass::GPR, rcls = RegClass::GPR;
                            if (materializeValueToVReg(ins.operands[0],
                                                       bbIn,
                                                       *ti_,
                                                       fb,
                                                       bbOutFn(),
                                                       tempVReg,
                                                       tempRegClass,
                                                       nextVRegId,
                                                       lhs,
                                                       lcls) &&
                                materializeValueToVReg(ins.operands[1],
                                                       bbIn,
                                                       *ti_,
                                                       fb,
                                                       bbOutFn(),
                                                       tempVReg,
                                                       tempRegClass,
                                                       nextVRegId,
                                                       rhs,
                                                       rcls)) {
                                const bool fpBinary = binOp && isFloatingPointOp(ins.op);
                                const bool fpCompare =
                                    !binOp && isFloatingPointCompareOp(ins.op);
                                if (fpBinary || fpCompare) {
                                    if (lcls != RegClass::FPR) {
                                        const uint16_t converted = allocateNextVReg(nextVRegId);
                                        bbOutFn().instrs.push_back(
                                            MInstr{MOpcode::SCvtF,
                                                   {MOperand::vregOp(RegClass::FPR, converted),
                                                    MOperand::vregOp(RegClass::GPR, lhs)}});
                                        lhs = converted;
                                        lcls = RegClass::FPR;
                                    }
                                    if (rcls != RegClass::FPR) {
                                        const uint16_t converted = allocateNextVReg(nextVRegId);
                                        bbOutFn().instrs.push_back(
                                            MInstr{MOpcode::SCvtF,
                                                   {MOperand::vregOp(RegClass::FPR, converted),
                                                    MOperand::vregOp(RegClass::GPR, rhs)}});
                                        rhs = converted;
                                        rcls = RegClass::FPR;
                                    }
                                } else if (lcls != RegClass::GPR || rcls != RegClass::GPR) {
                                    throw std::runtime_error("AArch64 codegen: register class "
                                                             "mismatch in binary lowering");
                                }

                                const RegClass rc = fpBinary ? RegClass::FPR : RegClass::GPR;
                                const uint16_t dst = allocateNextVReg(nextVRegId);
                                tempVReg[*ins.result] = dst;
                                tempRegClass[*ins.result] = rc;
                                if (binOp) {
                                    if (emitSubWidthCheckedBinary(
                                            mf, bbOutFn(), ins, dst, lhs, rhs, nextVRegId)) {
                                        // Width-aware checked arithmetic emitted above.
                                    } else {
                                    // Check if we can use immediate form for this operation
                                    const bool hasConstRHS =
                                        ins.operands[1].kind == il::core::Value::Kind::ConstInt;
                                    const bool isShift = (ins.op == il::core::Opcode::Shl ||
                                                          ins.op == il::core::Opcode::LShr ||
                                                          ins.op == il::core::Opcode::AShr);
                                    const bool isAddSub = (ins.op == il::core::Opcode::Add ||
                                                           ins.op == il::core::Opcode::IAddOvf ||
                                                           ins.op == il::core::Opcode::Sub ||
                                                           ins.op == il::core::Opcode::ISubOvf);
                                    const bool isBitwise = (ins.op == il::core::Opcode::And ||
                                                            ins.op == il::core::Opcode::Or ||
                                                            ins.op == il::core::Opcode::Xor);

                                    // Use immediate form if:
                                    // 1. RHS is a constant AND
                                    // 2. Operation supports immediate AND
                                    // 3. Value fits in the instruction's immediate field
                                    bool useImmediate = false;
                                    if (hasConstRHS && binOp->supportsImmediate) {
                                        const auto immVal = ins.operands[1].i64;
                                        if (isShift && isValidShiftAmount(immVal))
                                            useImmediate = true;
                                        else if (isAddSub && isUImm12(immVal))
                                            useImmediate = true;
                                        else if (isBitwise &&
                                                 isLogicalImmediate(static_cast<uint64_t>(immVal)))
                                            useImmediate = true;
                                    }

                                    if (useImmediate) {
                                        // Emit with immediate operand - no need to materialize RHS
                                        // Note: FP ops have supportsImmediate=false, so rc is
                                        // always GPR here
                                        bbOutFn().instrs.push_back(
                                            MInstr{binOp->immOp,
                                                   {MOperand::vregOp(rc, dst),
                                                    MOperand::vregOp(rc, lhs),
                                                    MOperand::immOp(ins.operands[1].i64)}});
                                    } else {
                                        // Emit binary op with all register operands
                                        bbOutFn().instrs.push_back(
                                            MInstr{binOp->mirOp,
                                                   {MOperand::vregOp(rc, dst),
                                                    MOperand::vregOp(rc, lhs),
                                                    MOperand::vregOp(rc, rhs)}});
                                    }
                                    }
                                } else {
                                    // Emit comparison (cmp + cset)
                                    if (fpCompare) {
                                        bbOutFn().instrs.push_back(
                                            MInstr{MOpcode::FCmpRR,
                                                   {MOperand::vregOp(RegClass::FPR, lhs),
                                                    MOperand::vregOp(RegClass::FPR, rhs)}});
                                        emitFpCompareResult(bbOutFn(), ins.op, dst, nextVRegId);
                                        break;
                                    }
                                    // Check if RHS is a small constant for CmpRI form
                                    const bool rhsIsSmallConst =
                                        ins.operands[1].kind == il::core::Value::Kind::ConstInt &&
                                        isUImm12(ins.operands[1].i64);

                                    if (rhsIsSmallConst) {
                                        bbOutFn().instrs.push_back(
                                            MInstr{MOpcode::CmpRI,
                                                   {MOperand::vregOp(RegClass::GPR, lhs),
                                                    MOperand::immOp(ins.operands[1].i64)}});
                                    } else {
                                        bbOutFn().instrs.push_back(
                                            MInstr{MOpcode::CmpRR,
                                                   {MOperand::vregOp(RegClass::GPR, lhs),
                                                    MOperand::vregOp(RegClass::GPR, rhs)}});
                                    }
                                    bbOutFn().instrs.push_back(
                                        MInstr{MOpcode::Cset,
                                               {MOperand::vregOp(RegClass::GPR, dst),
                                                MOperand::condOp(condForOpcode(ins.op))}});
                                }
                            }
                        }
                    } else {
                        throw std::runtime_error("AArch64 codegen: unhandled IL opcode '" +
                                                 std::string(il::core::toString(ins.op)) +
                                                 "' in block '" + bbIn.label + "'");
                    }
                    break;
            }

            // Spill cross-block temps immediately after they are defined.
            // This ensures the value is preserved in memory for use in other blocks,
            // since the register allocator may reuse the physical register.
            if (ins.result) {
                auto spillIt = liveness.crossBlockSpillOffset.find(*ins.result);
                if (spillIt != liveness.crossBlockSpillOffset.end()) {
                    // This temp is used in another block - spill it now
                    auto vregIt = tempVReg.find(*ins.result);
                    if (vregIt != tempVReg.end()) {
                        const uint16_t srcVreg = vregIt->second;
                        const int offset = spillIt->second;
                        // Check register class for this temp
                        auto clsIt = tempRegClass.find(*ins.result);
                        const RegClass cls =
                            (clsIt != tempRegClass.end()) ? clsIt->second : RegClass::GPR;
                        if (cls == RegClass::FPR) {
                            bbOutFn().instrs.push_back(
                                MInstr{MOpcode::StrFprFpImm,
                                       {MOperand::vregOp(RegClass::FPR, srcVreg),
                                        MOperand::immOp(offset)}});
                        } else {
                            bbOutFn().instrs.push_back(
                                MInstr{MOpcode::StrRegFpImm,
                                       {MOperand::vregOp(RegClass::GPR, srcVreg),
                                        MOperand::immOp(offset)}});
                        }
                    }
                }
            }

            // Stamp source location on all MInstrs emitted by this IL instruction.
            for (size_t mi = mirCountBefore; mi < bbOutFn().instrs.size(); ++mi)
                bbOutFn().instrs[mi].loc = ins.loc;
        }

        // Save tempVReg snapshot for this block before processing next block.
        // The terminator loop will use this snapshot to get correct vreg mappings
        // for temps defined in this block, since later blocks may overwrite tempVReg.
        blockTempVRegSnapshot[bi] = tempVReg;
    }

    // Lower control-flow terminators: br, cbr, trap AFTER all other instructions
    // This ensures branches appear after the values they depend on are computed.
    lowerTerminators(fn,
                     mf,
                     *ti_,
                     fb,
                     phiVregId,
                     phiRegClass,
                     phiSpillOffset,
                     blockTempVRegSnapshot,
                     tempRegClass,
                     nextVRegId);

    fb.finalize();
    return mf;
}

} // namespace viper::codegen::aarch64
