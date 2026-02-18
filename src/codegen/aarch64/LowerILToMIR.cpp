//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/LowerILToMIR.cpp
// Purpose: IL→MIR lowering orchestrator for AArch64.
//
// This file contains the main lowerFunction() method that coordinates the
// IL to MIR conversion. The lowering logic is organized into several modules:
//
// File Structure:
// ---------------
// LowerILToMIR.cpp      - Main orchestration: frame setup, phi allocation,
//                         block iteration, cross-block spill/reload
// LowerILToMIR.hpp      - Public interface (lowerFunction)
// LoweringContext.hpp   - Shared lowering state passed to handlers
//
// Instruction Handlers:
// ---------------------
// OpcodeDispatch.cpp    - Main instruction switch statement
// OpcodeDispatch.hpp    - lowerInstruction() declaration
// InstrLowering.cpp     - Individual opcode handlers (arithmetic, casts, etc.)
// InstrLowering.hpp     - Handler declarations and materializeValueToVReg
// OpcodeMappings.hpp    - Binary op and comparison tables
//
// Analysis & Control Flow:
// ------------------------
// LivenessAnalysis.cpp  - Cross-block temp liveness analysis
// LivenessAnalysis.hpp  - LivenessInfo struct and analyzeCrossBlockLiveness()
// TerminatorLowering.cpp - Control-flow terminator lowering (br, cbr, trap)
// TerminatorLowering.hpp - lowerTerminators() declaration
//
// Fast Paths:
// -----------
// FastPaths.cpp         - Fast-path dispatcher for common patterns
// fastpaths/*           - Category-specific fast-path handlers
//
//===----------------------------------------------------------------------===//

#include "LowerILToMIR.hpp"

#include "FastPaths.hpp"
#include "FrameBuilder.hpp"
#include "InstrLowering.hpp"
#include "LivenessAnalysis.hpp"
#include "LoweringContext.hpp"
#include "OpcodeDispatch.hpp"
#include "OpcodeMappings.hpp"
#include "TargetAArch64.hpp"
#include "TerminatorLowering.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"

#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::aarch64
{
namespace
{
using il::core::Opcode;

static const char *condForOpcode(Opcode op)
{
    return lookupCondition(op);
}

/// @brief Counter for generating unique trap labels within a function.
/// @details Reset at the start of each lowerFunction() call to ensure unique
///          labels per function (combined with the function name prefix).
static thread_local unsigned trapLabelCounter;

} // namespace

MFunction LowerILToMIR::lowerFunction(const il::core::Function &fn) const
{
    MFunction mf{};
    mf.name = fn.name;
    // Reserve extra capacity for any helper/trap blocks we may append during lowering
    // to avoid std::vector reallocation which would invalidate references held by
    // in-flight lowering helpers. Over-reserving keeps references stable.
    mf.blocks.reserve(fn.blocks.size() + 1024);
    // Reset trap label counter for unique labels within this function
    trapLabelCounter = 0;

    // Pre-create MIR blocks with labels to mirror IL CFG shape.
    for (const auto &bb : fn.blocks)
    {
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
    for (const auto &bb : fn.blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == il::core::Opcode::Alloca && instr.result && !instr.operands.empty())
            {
                if (instr.operands[0].kind == il::core::Value::Kind::ConstInt)
                {
                    const int size = static_cast<int>(instr.operands[0].i64);
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
        phiSpillOffset;         // block label -> spill offset per param
    uint16_t phiNextId = 40000; // reserve a high vreg id range for phis (fit in uint16)
    for (std::size_t bi = 1; bi < fn.blocks.size(); ++bi) // Start at 1, skip entry block
    {
        const auto &bb = fn.blocks[bi];
        if (!bb.params.empty())
        {
            std::vector<uint16_t> ids;
            std::vector<RegClass> classes;
            std::vector<int> spillOffsets;
            ids.reserve(bb.params.size());
            classes.reserve(bb.params.size());
            spillOffsets.reserve(bb.params.size());
            for (std::size_t pi = 0; pi < bb.params.size(); ++pi)
            {
                uint16_t id = phiNextId++;
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
    if (auto result = tryFastPaths(fn, *ti_, fb, mf))
        return *result;

    // Generic fallback: lower stack/local loads/stores and a simple return
    // This path handles arbitrary placement of alloca/load/store in a single block without
    // full-blown selection for other ops yet.

    // Use a single function-wide tempVReg map so values materialized in one block
    // are visible to other blocks. This handles cross-block value references that
    // the BASIC frontend generates (e.g., array operations using values from predecessor blocks).
    std::unordered_map<unsigned, uint16_t> tempVReg;
    // Track register class (GPR vs FPR) for each temp within this function
    std::unordered_map<unsigned, RegClass> tempRegClass;
    uint16_t nextVRegId = 1; // vreg ids start at 1

    // Map function parameter IDs to their spill offsets (for entry block params)
    std::unordered_map<unsigned, int> funcParamSpillOffset;

    // Save per-block tempVReg snapshots so terminator loop can use the correct vreg mappings.
    // This is needed because cross-block temp reloading in later blocks can overwrite tempVReg
    // entries, but the terminator loop for the DEFINING block needs the original vreg.
    std::vector<std::unordered_map<unsigned, uint16_t>> blockTempVRegSnapshot(fn.blocks.size());

    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi)
    {
        const auto &bbIn = fn.blocks[bi];
        // NOTE: We use index bi to access mf.blocks[bi] instead of a reference because
        // instruction lowering can add new trap blocks via emplace_back(), which may
        // reallocate the vector and invalidate references.
        // NOTE: Do NOT clear tempRegClass here - we need to preserve class info for
        // cross-block temps that are spilled/reloaded. It's already cleared at function start.
        // Helper lambda to get current output block (avoids dangling references)
        auto bbOut = [&]() -> MBasicBlock & { return mf.blocks[bi]; };

        // Entry block (bi == 0): Spill function parameters to stack slots immediately.
        // This ensures parameters are preserved across function calls within the entry block.
        // ABI registers (x0-x7, v0-v7) are caller-saved and will be clobbered by calls.
        if (bi == 0 && !bbIn.params.empty())
        {
            // ARM64 ABI: GPR and FPR parameters have independent indexing.
            // Integer/pointer params use x0, x1, x2...
            // Float params use d0, d1, d2... (independently numbered)
            std::size_t gprArgIdx = 0;
            std::size_t fprArgIdx = 0;
            // Track stack argument index for parameters that overflow registers.
            // AAPCS64: stack args are laid out in argument order regardless of class.
            std::size_t stackArgIdx = 0;

            for (std::size_t pi = 0; pi < bbIn.params.size(); ++pi)
            {
                const auto &param = bbIn.params[pi];
                const RegClass cls =
                    (param.type.kind == il::core::Type::Kind::F64) ? RegClass::FPR : RegClass::GPR;

                // Allocate spill slot for this parameter
                // IMPORTANT: Use param.id (not pi index) to match LivenessAnalysis.cpp line 113
                // which uses (50000 + tempId) for cross-block spill slots. Without this,
                // the entry block and other blocks would use different spill slots for the
                // same parameter, causing BUG-005 (BYREF parameters not working).
                const int spillOffset = fb.ensureSpill(static_cast<uint16_t>(50000 + param.id));
                funcParamSpillOffset[param.id] = spillOffset;

                // Get the ABI register for this parameter using independent GPR/FPR indexing
                bool isStackParam = false;
                PhysReg src{};
                if (cls == RegClass::FPR)
                {
                    if (fprArgIdx < ti_->f64ArgOrder.size())
                        src = ti_->f64ArgOrder[fprArgIdx++];
                    else
                        isStackParam = true;
                }
                else
                {
                    if (gprArgIdx < ti_->intArgOrder.size())
                        src = ti_->intArgOrder[gprArgIdx++];
                    else
                        isStackParam = true;
                }

                if (isStackParam)
                {
                    // Stack parameter: load from caller's outgoing arg area.
                    // After prologue (stp x29, x30, [sp, #-16]!; mov x29, sp),
                    // caller's stack args are at [FP + 16 + stackArgIdx * 8].
                    const int callerArgOffset = 16 + static_cast<int>(stackArgIdx) * 8;
                    ++stackArgIdx;

                    // Load from caller's stack arg area into spill slot via a temporary
                    // vreg. IMPORTANT: We must NOT use a hardcoded physical register here
                    // because the register allocator may have assigned that register to a
                    // vreg for an earlier parameter that is still live. Using a vreg lets
                    // the allocator pick a non-conflicting physical register.
                    if (cls == RegClass::FPR)
                    {
                        const uint16_t tmpVid = nextVRegId++;
                        bbOut().instrs.push_back(MInstr{MOpcode::LdrFprFpImm,
                                                        {MOperand::vregOp(RegClass::FPR, tmpVid),
                                                         MOperand::immOp(callerArgOffset)}});
                        bbOut().instrs.push_back(MInstr{MOpcode::StrFprFpImm,
                                                        {MOperand::vregOp(RegClass::FPR, tmpVid),
                                                         MOperand::immOp(spillOffset)}});
                    }
                    else
                    {
                        const uint16_t tmpVid = nextVRegId++;
                        bbOut().instrs.push_back(MInstr{MOpcode::LdrRegFpImm,
                                                        {MOperand::vregOp(RegClass::GPR, tmpVid),
                                                         MOperand::immOp(callerArgOffset)}});
                        bbOut().instrs.push_back(MInstr{MOpcode::StrRegFpImm,
                                                        {MOperand::vregOp(RegClass::GPR, tmpVid),
                                                         MOperand::immOp(spillOffset)}});
                    }
                }
                else
                {
                    // Register parameter: store ABI register to spill slot
                    if (cls == RegClass::FPR)
                    {
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::StrFprFpImm,
                                   {MOperand::regOp(src), MOperand::immOp(spillOffset)}});
                    }
                    else
                    {
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::StrRegFpImm,
                                   {MOperand::regOp(src), MOperand::immOp(spillOffset)}});
                    }
                }

                // Create vreg for this param and load from spill slot
                const uint16_t vid = nextVRegId++;
                tempVReg[param.id] = vid;
                tempRegClass[param.id] = cls;

                if (cls == RegClass::FPR)
                {
                    bbOut().instrs.push_back(MInstr{
                        MOpcode::LdrFprFpImm,
                        {MOperand::vregOp(RegClass::FPR, vid), MOperand::immOp(spillOffset)}});
                }
                else
                {
                    bbOut().instrs.push_back(MInstr{
                        MOpcode::LdrRegFpImm,
                        {MOperand::vregOp(RegClass::GPR, vid), MOperand::immOp(spillOffset)}});
                }
            }
        }

        // Load block parameters from spill slots into fresh vregs at block entry.
        // The edge copies store values to these spill slots before branching here.
        auto itPhi = phiVregId.find(bbIn.label);
        auto itSpill = phiSpillOffset.find(bbIn.label);
        if (itPhi != phiVregId.end() && itSpill != phiSpillOffset.end())
        {
            const auto &ids = itPhi->second;
            const auto &spillOffsets = itSpill->second;
            const auto &classes = phiRegClass[bbIn.label];
            for (std::size_t pi = 0; pi < bbIn.params.size() && pi < ids.size(); ++pi)
            {
                const uint16_t vid = nextVRegId++;
                tempVReg[bbIn.params[pi].id] = vid;
                const auto &pt = bbIn.params[pi].type;
                const RegClass cls =
                    (pt.kind == il::core::Type::Kind::F64) ? RegClass::FPR : RegClass::GPR;
                tempRegClass[bbIn.params[pi].id] = cls;
                const int offset = spillOffsets[pi];
                // Load from spill slot into vreg
                if (cls == RegClass::FPR)
                {
                    bbOut().instrs.push_back(
                        MInstr{MOpcode::LdrFprFpImm,
                               {MOperand::vregOp(RegClass::FPR, vid), MOperand::immOp(offset)}});
                }
                else
                {
                    bbOut().instrs.push_back(
                        MInstr{MOpcode::LdrRegFpImm,
                               {MOperand::vregOp(RegClass::GPR, vid), MOperand::immOp(offset)}});
                }
            }
        }

        // Reload cross-block temps that are used in this block but defined elsewhere.
        // We need to reload them at block entry because the register allocator may have
        // reused their physical registers in intervening blocks.
        for (const auto &ins : bbIn.instructions)
        {
            for (const auto &op : ins.operands)
            {
                if (op.kind == il::core::Value::Kind::Temp)
                {
                    auto spillIt = liveness.crossBlockSpillOffset.find(op.id);
                    auto defIt = liveness.tempDefBlock.find(op.id);
                    if (spillIt != liveness.crossBlockSpillOffset.end() &&
                        defIt != liveness.tempDefBlock.end() && defIt->second != bi)
                    {
                        // This temp is defined in another block and used here - reload it
                        // Only reload if we haven't already in this block
                        if (tempVReg.find(op.id) == tempVReg.end() ||
                            tempVReg[op.id] < 60000) // Use high range for reloaded values
                        {
                            const uint16_t vid = nextVRegId++;
                            tempVReg[op.id] = vid;
                            const int offset = spillIt->second;
                            // Check register class for this temp
                            auto clsIt = tempRegClass.find(op.id);
                            const RegClass cls =
                                (clsIt != tempRegClass.end()) ? clsIt->second : RegClass::GPR;
                            if (cls == RegClass::FPR)
                            {
                                bbOut().instrs.push_back(
                                    MInstr{MOpcode::LdrFprFpImm,
                                           {MOperand::vregOp(RegClass::FPR, vid),
                                            MOperand::immOp(offset)}});
                            }
                            else
                            {
                                bbOut().instrs.push_back(
                                    MInstr{MOpcode::LdrRegFpImm,
                                           {MOperand::vregOp(RegClass::GPR, vid),
                                            MOperand::immOp(offset)}});
                            }
                        }
                    }
                }
            }
        }
        // Also check terminator for cross-block temp uses (CBr condition)
        if (!bbIn.instructions.empty())
        {
            const auto &term = bbIn.instructions.back();
            if (term.op == il::core::Opcode::CBr && !term.operands.empty())
            {
                const auto &cond = term.operands[0];
                if (cond.kind == il::core::Value::Kind::Temp)
                {
                    auto spillIt = liveness.crossBlockSpillOffset.find(cond.id);
                    auto defIt = liveness.tempDefBlock.find(cond.id);
                    if (spillIt != liveness.crossBlockSpillOffset.end() &&
                        defIt != liveness.tempDefBlock.end() && defIt->second != bi)
                    {
                        if (tempVReg.find(cond.id) == tempVReg.end())
                        {
                            const uint16_t vid = nextVRegId++;
                            tempVReg[cond.id] = vid;
                            const int offset = spillIt->second;
                            bbOut().instrs.push_back(MInstr{
                                MOpcode::LdrRegFpImm,
                                {MOperand::vregOp(RegClass::GPR, vid), MOperand::immOp(offset)}});
                        }
                    }
                }
            }
        }

        // Create lowering context for dispatching to extracted handlers
        LoweringContext ctx{*ti_,
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
                            trapLabelCounter};

        for (const auto &ins : bbIn.instructions)
        {
            // Try extracted handlers first; they return true if they handled the opcode
            if (lowerInstruction(ins, bbIn, ctx, bi))
            {
                // Spill cross-block temps immediately after they are defined.
                // This ensures the value is preserved in memory for use in other blocks.
                if (ins.result)
                {
                    auto spillIt = liveness.crossBlockSpillOffset.find(*ins.result);
                    if (spillIt != liveness.crossBlockSpillOffset.end())
                    {
                        auto vregIt = tempVReg.find(*ins.result);
                        if (vregIt != tempVReg.end())
                        {
                            const uint16_t srcVreg = vregIt->second;
                            const int offset = spillIt->second;
                            // Respect the producing register class when spilling
                            auto clsIt = tempRegClass.find(*ins.result);
                            const RegClass cls =
                                (clsIt != tempRegClass.end()) ? clsIt->second : RegClass::GPR;
                            if (cls == RegClass::FPR)
                            {
                                bbOut().instrs.push_back(
                                    MInstr{MOpcode::StrFprFpImm,
                                           {MOperand::vregOp(RegClass::FPR, srcVreg),
                                            MOperand::immOp(offset)}});
                            }
                            else
                            {
                                bbOut().instrs.push_back(
                                    MInstr{MOpcode::StrRegFpImm,
                                           {MOperand::vregOp(RegClass::GPR, srcVreg),
                                            MOperand::immOp(offset)}});
                            }
                        }
                    }
                }
                continue;
            }

            switch (ins.op)
            {
                    // NOTE: Zext1, Trunc1, CastSiNarrowChk, CastUiNarrowChk, CastFpToSiRteChk,
                    // CastFpToUiRteChk, CastSiToFp, CastUiToFp, SRemChk0, SDivChk0, UDivChk0,
                    // URemChk0, FAdd, FSub, FMul, FDiv, FCmpEQ, FCmpNE, FCmpLT, FCmpLE, FCmpGT,
                    // FCmpGE, Sitofp, Fptosi are handled by lowerInstruction() above

                case il::core::Opcode::SwitchI32:
                {
                    using namespace il::core;
                    // Scrutinee
                    uint16_t sv = 0;
                    RegClass scls = RegClass::GPR;
                    if (ins.operands.empty() || !materializeValueToVReg(ins.operands[0],
                                                                        bbIn,
                                                                        *ti_,
                                                                        fb,
                                                                        bbOut(),
                                                                        tempVReg,
                                                                        tempRegClass,
                                                                        nextVRegId,
                                                                        sv,
                                                                        scls))
                    {
                        break;
                    }
                    const size_t ncases = switchCaseCount(ins);
                    for (size_t ci = 0; ci < ncases; ++ci)
                    {
                        const Value &cval = switchCaseValue(ins, ci);
                        const std::string &clabel = switchCaseLabel(ins, ci);
                        long long imm = 0;
                        if (cval.kind == Value::Kind::ConstInt)
                            imm = cval.i64;
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::CmpRI,
                                   {MOperand::vregOp(RegClass::GPR, sv), MOperand::immOp(imm)}});
                        // Phi copies for this case
                        auto itIds = phiVregId.find(clabel);
                        if (itIds != phiVregId.end())
                        {
                            const auto &classes = phiRegClass[clabel];
                            const auto &args = switchCaseArgs(ins, ci);
                            for (std::size_t ai = 0; ai < args.size() && ai < itIds->second.size();
                                 ++ai)
                            {
                                uint16_t pv = 0;
                                RegClass pcls = RegClass::GPR;
                                if (!materializeValueToVReg(args[ai],
                                                            bbIn,
                                                            *ti_,
                                                            fb,
                                                            bbOut(),
                                                            tempVReg,
                                                            tempRegClass,
                                                            nextVRegId,
                                                            pv,
                                                            pcls))
                                    continue;
                                const uint16_t dstV = itIds->second[ai];
                                const RegClass dstCls = classes[ai];
                                if (dstCls == RegClass::FPR)
                                {
                                    if (pcls != RegClass::FPR)
                                    {
                                        const uint16_t cvt = nextVRegId++;
                                        bbOut().instrs.push_back(
                                            MInstr{MOpcode::SCvtF,
                                                   {MOperand::vregOp(RegClass::FPR, cvt),
                                                    MOperand::vregOp(RegClass::GPR, pv)}});
                                        pv = cvt;
                                        pcls = RegClass::FPR;
                                    }
                                    bbOut().instrs.push_back(
                                        MInstr{MOpcode::FMovRR,
                                               {MOperand::vregOp(RegClass::FPR, dstV),
                                                MOperand::vregOp(RegClass::FPR, pv)}});
                                }
                                else
                                {
                                    if (pcls == RegClass::FPR)
                                    {
                                        const uint16_t cvt = nextVRegId++;
                                        bbOut().instrs.push_back(
                                            MInstr{MOpcode::FCvtZS,
                                                   {MOperand::vregOp(RegClass::GPR, cvt),
                                                    MOperand::vregOp(RegClass::FPR, pv)}});
                                        pv = cvt;
                                        pcls = RegClass::GPR;
                                    }
                                    bbOut().instrs.push_back(
                                        MInstr{MOpcode::MovRR,
                                               {MOperand::vregOp(RegClass::GPR, dstV),
                                                MOperand::vregOp(RegClass::GPR, pv)}});
                                }
                            }
                        }
                        bbOut().instrs.push_back(MInstr{
                            MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(clabel)}});
                    }
                    // Default
                    const std::string &defLbl = switchDefaultLabel(ins);
                    if (!defLbl.empty())
                    {
                        auto itIds = phiVregId.find(defLbl);
                        if (itIds != phiVregId.end())
                        {
                            const auto &classes = phiRegClass[defLbl];
                            const auto &dargs = switchDefaultArgs(ins);
                            for (std::size_t ai = 0; ai < dargs.size() && ai < itIds->second.size();
                                 ++ai)
                            {
                                uint16_t pv = 0;
                                RegClass pcls = RegClass::GPR;
                                if (!materializeValueToVReg(dargs[ai],
                                                            bbIn,
                                                            *ti_,
                                                            fb,
                                                            bbOut(),
                                                            tempVReg,
                                                            tempRegClass,
                                                            nextVRegId,
                                                            pv,
                                                            pcls))
                                    continue;
                                const uint16_t dstV = itIds->second[ai];
                                const RegClass dstCls = classes[ai];
                                if (dstCls == RegClass::FPR)
                                {
                                    if (pcls != RegClass::FPR)
                                    {
                                        const uint16_t cvt = nextVRegId++;
                                        bbOut().instrs.push_back(
                                            MInstr{MOpcode::SCvtF,
                                                   {MOperand::vregOp(RegClass::FPR, cvt),
                                                    MOperand::vregOp(RegClass::GPR, pv)}});
                                        pv = cvt;
                                        pcls = RegClass::FPR;
                                    }
                                    bbOut().instrs.push_back(
                                        MInstr{MOpcode::FMovRR,
                                               {MOperand::vregOp(RegClass::FPR, dstV),
                                                MOperand::vregOp(RegClass::FPR, pv)}});
                                }
                                else
                                {
                                    if (pcls == RegClass::FPR)
                                    {
                                        const uint16_t cvt = nextVRegId++;
                                        bbOut().instrs.push_back(
                                            MInstr{MOpcode::FCvtZS,
                                                   {MOperand::vregOp(RegClass::GPR, cvt),
                                                    MOperand::vregOp(RegClass::FPR, pv)}});
                                        pv = cvt;
                                        pcls = RegClass::GPR;
                                    }
                                    bbOut().instrs.push_back(
                                        MInstr{MOpcode::MovRR,
                                               {MOperand::vregOp(RegClass::GPR, dstV),
                                                MOperand::vregOp(RegClass::GPR, pv)}});
                                }
                            }
                        }
                        bbOut().instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(defLbl)}});
                    }
                    break;
                }
                // NOTE: Br, CBr, Call, Store, GEP, Load, Ret, Alloca, FP ops,
                // and conversions are all handled by lowerInstruction() in OpcodeDispatch.cpp
                default:
                    // Handle binary ops and comparisons that may be referenced cross-block.
                    // This ensures values are materialized and cached in tempVReg for later use.
                    if (ins.result && ins.operands.size() == 2)
                    {
                        const auto *binOp = lookupBinaryOp(ins.op);
                        if (binOp || isCompareOp(ins.op))
                        {
                            uint16_t lhs = 0, rhs = 0;
                            RegClass lcls = RegClass::GPR, rcls = RegClass::GPR;
                            if (materializeValueToVReg(ins.operands[0],
                                                       bbIn,
                                                       *ti_,
                                                       fb,
                                                       bbOut(),
                                                       tempVReg,
                                                       tempRegClass,
                                                       nextVRegId,
                                                       lhs,
                                                       lcls) &&
                                materializeValueToVReg(ins.operands[1],
                                                       bbIn,
                                                       *ti_,
                                                       fb,
                                                       bbOut(),
                                                       tempVReg,
                                                       tempRegClass,
                                                       nextVRegId,
                                                       rhs,
                                                       rcls))
                            {
                                const uint16_t dst = nextVRegId++;
                                tempVReg[*ins.result] = dst;
                                if (binOp)
                                {
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
                                    if (hasConstRHS && binOp->supportsImmediate)
                                    {
                                        const auto immVal = ins.operands[1].i64;
                                        if (isShift && isValidShiftAmount(immVal))
                                            useImmediate = true;
                                        else if (isAddSub && isUImm12(immVal))
                                            useImmediate = true;
                                        else if (isBitwise &&
                                                 isLogicalImmediate(static_cast<uint64_t>(immVal)))
                                            useImmediate = true;
                                    }

                                    if (useImmediate)
                                    {
                                        // Emit with immediate operand - no need to materialize RHS
                                        bbOut().instrs.push_back(
                                            MInstr{binOp->immOp,
                                                   {MOperand::vregOp(RegClass::GPR, dst),
                                                    MOperand::vregOp(RegClass::GPR, lhs),
                                                    MOperand::immOp(ins.operands[1].i64)}});
                                    }
                                    else
                                    {
                                        // Emit binary op with all register operands
                                        bbOut().instrs.push_back(
                                            MInstr{binOp->mirOp,
                                                   {MOperand::vregOp(RegClass::GPR, dst),
                                                    MOperand::vregOp(RegClass::GPR, lhs),
                                                    MOperand::vregOp(RegClass::GPR, rhs)}});
                                    }
                                }
                                else
                                {
                                    // Emit comparison (cmp + cset)
                                    // Check if RHS is a small constant for CmpRI form
                                    const bool rhsIsSmallConst =
                                        ins.operands[1].kind == il::core::Value::Kind::ConstInt &&
                                        isUImm12(ins.operands[1].i64);

                                    if (rhsIsSmallConst)
                                    {
                                        bbOut().instrs.push_back(
                                            MInstr{MOpcode::CmpRI,
                                                   {MOperand::vregOp(RegClass::GPR, lhs),
                                                    MOperand::immOp(ins.operands[1].i64)}});
                                    }
                                    else
                                    {
                                        bbOut().instrs.push_back(
                                            MInstr{MOpcode::CmpRR,
                                                   {MOperand::vregOp(RegClass::GPR, lhs),
                                                    MOperand::vregOp(RegClass::GPR, rhs)}});
                                    }
                                    bbOut().instrs.push_back(
                                        MInstr{MOpcode::Cset,
                                               {MOperand::vregOp(RegClass::GPR, dst),
                                                MOperand::condOp(condForOpcode(ins.op))}});
                                }
                            }
                        }
                    }
                    break;
            }

            // Spill cross-block temps immediately after they are defined.
            // This ensures the value is preserved in memory for use in other blocks,
            // since the register allocator may reuse the physical register.
            if (ins.result)
            {
                auto spillIt = liveness.crossBlockSpillOffset.find(*ins.result);
                if (spillIt != liveness.crossBlockSpillOffset.end())
                {
                    // This temp is used in another block - spill it now
                    auto vregIt = tempVReg.find(*ins.result);
                    if (vregIt != tempVReg.end())
                    {
                        const uint16_t srcVreg = vregIt->second;
                        const int offset = spillIt->second;
                        // Check register class for this temp
                        auto clsIt = tempRegClass.find(*ins.result);
                        const RegClass cls =
                            (clsIt != tempRegClass.end()) ? clsIt->second : RegClass::GPR;
                        if (cls == RegClass::FPR)
                        {
                            bbOut().instrs.push_back(
                                MInstr{MOpcode::StrFprFpImm,
                                       {MOperand::vregOp(RegClass::FPR, srcVreg),
                                        MOperand::immOp(offset)}});
                        }
                        else
                        {
                            bbOut().instrs.push_back(
                                MInstr{MOpcode::StrRegFpImm,
                                       {MOperand::vregOp(RegClass::GPR, srcVreg),
                                        MOperand::immOp(offset)}});
                        }
                    }
                }
            }
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
