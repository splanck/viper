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
// IL to MIR conversion. Individual opcode handlers are in InstrLowering.cpp.
//
//===----------------------------------------------------------------------===//

#include "LowerILToMIR.hpp"

#include "FastPaths.hpp"
#include "FrameBuilder.hpp"
#include "InstrLowering.hpp"
#include "LoweringContext.hpp"
#include "OpcodeDispatch.hpp"
#include "OpcodeMappings.hpp"
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

// Alias for the global temp registry defined in InstrLowering.cpp
static auto &tempRegClass = g_tempRegClass;

} // namespace

MFunction LowerILToMIR::lowerFunction(const il::core::Function &fn) const
{
    MFunction mf{};
    mf.name = fn.name;
    // Reserve extra capacity for any helper/trap blocks we may append during lowering
    // to avoid std::vector reallocation which would invalidate references held by
    // in-flight lowering helpers. Over-reserving keeps references stable.
    mf.blocks.reserve(fn.blocks.size() + 1024);
    // Clear any cross-function temp->class hints
    tempRegClass.clear();
    // Reset trap label counter for unique labels within this function
    trapLabelCounter = 0;
    // Debug: function has N instructions in first block
    if (!fn.blocks.empty())
    {
        // std::cerr << "[DEBUG] Lowering " << fn.name << " with " <<
        // fn.blocks.front().instructions.size() << " instructions\n";
    }
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
    // Detect temps that are defined in one block and used in a different block.
    // Such temps must be spilled at definition and reloaded at use, since the
    // register allocator processes blocks independently and may reuse registers.
    //
    // Step 1: Build map of tempId -> defining block index
    std::unordered_map<unsigned, std::size_t> tempDefBlock;
    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi)
    {
        const auto &bb = fn.blocks[bi];
        // Block parameters are "defined" by their block
        for (const auto &param : bb.params)
        {
            tempDefBlock[param.id] = bi;
        }
        // Instructions that produce a result
        for (const auto &instr : bb.instructions)
        {
            if (instr.result)
            {
                tempDefBlock[*instr.result] = bi;
            }
        }
    }

    // Step 2: Find temps used in different blocks than their definition
    // Exclude alloca temps since they don't hold values - they represent stack addresses
    std::unordered_set<unsigned> crossBlockTemps;
    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi)
    {
        const auto &bb = fn.blocks[bi];
        auto checkValue = [&](const il::core::Value &v)
        {
            if (v.kind == il::core::Value::Kind::Temp)
            {
                // Skip alloca temps - they don't need cross-block spilling
                // Their address is computed from the frame pointer when needed
                if (allocaTemps.contains(v.id))
                    return;
                auto it = tempDefBlock.find(v.id);
                if (it != tempDefBlock.end() && it->second != bi)
                {
                    // This temp is used in block bi but defined in a different block
                    crossBlockTemps.insert(v.id);
                }
            }
        };
        for (const auto &instr : bb.instructions)
        {
            for (const auto &op : instr.operands)
            {
                checkValue(op);
            }
        }
        // Check terminator operands (branch conditions and arguments)
        // The terminator is the last instruction in the block
        if (!bb.instructions.empty())
        {
            const auto &term = bb.instructions.back();
            // Check condition operand for CBr
            if (term.op == il::core::Opcode::CBr && !term.operands.empty())
            {
                checkValue(term.operands[0]); // condition
            }
            // Check return value for Ret
            if (term.op == il::core::Opcode::Ret && !term.operands.empty())
            {
                checkValue(term.operands[0]);
            }
            // Check branch arguments (phi values)
            for (const auto &argList : term.brArgs)
            {
                for (const auto &arg : argList)
                {
                    checkValue(arg);
                }
            }
        }
    }

    // Step 3: Allocate spill slots for cross-block temps
    std::unordered_map<unsigned, int> crossBlockSpillOffset;
    for (unsigned tempId : crossBlockTemps)
    {
        int offset = fb.ensureSpill(50000 + tempId); // Use high ID range to avoid conflicts
        crossBlockSpillOffset[tempId] = offset;
    }

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
            for (std::size_t pi = 0; pi < bbIn.params.size(); ++pi)
            {
                const auto &param = bbIn.params[pi];
                const RegClass cls =
                    (param.type.kind == il::core::Type::Kind::F64) ? RegClass::FPR : RegClass::GPR;

                // Allocate spill slot for this parameter
                const int spillOffset = fb.ensureSpill(static_cast<uint16_t>(50000 + pi));
                funcParamSpillOffset[param.id] = spillOffset;

                // Get the ABI register for this parameter
                PhysReg src;
                if (cls == RegClass::FPR)
                {
                    if (pi < ti_->f64ArgOrder.size())
                        src = ti_->f64ArgOrder[pi];
                    else
                        continue; // Stack param - not handled yet
                }
                else
                {
                    if (pi < ti_->intArgOrder.size())
                        src = ti_->intArgOrder[pi];
                    else
                        continue; // Stack param - not handled yet
                }

                // Emit store: str xN, [fp, #offset]
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
                    auto spillIt = crossBlockSpillOffset.find(op.id);
                    auto defIt = tempDefBlock.find(op.id);
                    if (spillIt != crossBlockSpillOffset.end() && defIt != tempDefBlock.end() &&
                        defIt->second != bi)
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
                    auto spillIt = crossBlockSpillOffset.find(cond.id);
                    auto defIt = tempDefBlock.find(cond.id);
                    if (spillIt != crossBlockSpillOffset.end() && defIt != tempDefBlock.end() &&
                        defIt->second != bi)
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

        // Debug: Processing all instructions generically
        // std::cerr << "[DEBUG] Generic loop for block " << bbIn.label << " with " <<
        // bbIn.instructions.size() << " instructions\n";

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
                            crossBlockSpillOffset,
                            tempDefBlock,
                            crossBlockTemps,
                            trapLabelCounter};

        for (const auto &ins : bbIn.instructions)
        {
            // std::cerr << "[DEBUG] Processing opcode: " << static_cast<int>(ins.op) << "\n";

            // Try extracted handlers first; they return true if they handled the opcode
            if (lowerInstruction(ins, bbIn, ctx, bi))
            {
                // Spill cross-block temps immediately after they are defined.
                // This ensures the value is preserved in memory for use in other blocks.
                if (ins.result)
                {
                    auto spillIt = crossBlockSpillOffset.find(*ins.result);
                    if (spillIt != crossBlockSpillOffset.end())
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
                                                       nextVRegId,
                                                       lhs,
                                                       lcls) &&
                                materializeValueToVReg(ins.operands[1],
                                                       bbIn,
                                                       *ti_,
                                                       fb,
                                                       bbOut(),
                                                       tempVReg,
                                                       nextVRegId,
                                                       rhs,
                                                       rcls))
                            {
                                const uint16_t dst = nextVRegId++;
                                tempVReg[*ins.result] = dst;
                                if (binOp)
                                {
                                    // Emit binary op
                                    bbOut().instrs.push_back(
                                        MInstr{binOp->mirOp,
                                               {MOperand::vregOp(RegClass::GPR, dst),
                                                MOperand::vregOp(RegClass::GPR, lhs),
                                                MOperand::vregOp(RegClass::GPR, rhs)}});
                                }
                                else
                                {
                                    // Emit comparison (cmp + cset)
                                    bbOut().instrs.push_back(
                                        MInstr{MOpcode::CmpRR,
                                               {MOperand::vregOp(RegClass::GPR, lhs),
                                                MOperand::vregOp(RegClass::GPR, rhs)}});
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
                auto spillIt = crossBlockSpillOffset.find(*ins.result);
                if (spillIt != crossBlockSpillOffset.end())
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
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
    {
        const auto &inBB = fn.blocks[i];
        if (inBB.instructions.empty())
            continue;
        const auto &term = inBB.instructions.back();
        auto &outBB = mf.blocks[i];
        // Use the block's tempVReg snapshot to get correct vreg mappings for temps
        // defined in this block. This avoids using overwritten values from later blocks.
        auto &blockTempVReg = blockTempVRegSnapshot[i];
        switch (term.op)
        {
            case il::core::Opcode::Br:
                if (!term.labels.empty())
                {
                    // Emit phi edge copies for target - store to spill slots
                    if (!term.brArgs.empty() && !term.brArgs[0].empty())
                    {
                        const std::string &dst = term.labels[0];
                        auto itIds = phiVregId.find(dst);
                        auto itSpill = phiSpillOffset.find(dst);
                        if (itIds != phiVregId.end() && itSpill != phiSpillOffset.end())
                        {
                            const auto &ids = itIds->second;
                            const auto &classes = phiRegClass[dst];
                            const auto &spillOffsets = itSpill->second;
                            std::unordered_map<unsigned, uint16_t> tmp2v;
                            uint16_t nvr = 1;
                            for (std::size_t ai = 0; ai < term.brArgs[0].size() && ai < ids.size();
                                 ++ai)
                            {
                                uint16_t sv = 0;
                                RegClass scls = RegClass::GPR;
                                if (!materializeValueToVReg(term.brArgs[0][ai],
                                                            inBB,
                                                            *ti_,
                                                            fb,
                                                            outBB,
                                                            tmp2v,
                                                            nvr,
                                                            sv,
                                                            scls))
                                    continue;
                                const RegClass dstCls = classes[ai];
                                const int offset = spillOffsets[ai];
                                if (dstCls == RegClass::FPR)
                                {
                                    if (scls != RegClass::FPR)
                                    {
                                        const uint16_t cvt = nvr++;
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::SCvtF,
                                                   {MOperand::vregOp(RegClass::FPR, cvt),
                                                    MOperand::vregOp(RegClass::GPR, sv)}});
                                        sv = cvt;
                                        scls = RegClass::FPR;
                                    }
                                    // Store FPR to spill slot
                                    outBB.instrs.push_back(
                                        MInstr{MOpcode::StrFprFpImm,
                                               {MOperand::vregOp(RegClass::FPR, sv),
                                                MOperand::immOp(offset)}});
                                }
                                else
                                {
                                    if (scls == RegClass::FPR)
                                    {
                                        const uint16_t cvt = nvr++;
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::FCvtZS,
                                                   {MOperand::vregOp(RegClass::GPR, cvt),
                                                    MOperand::vregOp(RegClass::FPR, sv)}});
                                        sv = cvt;
                                        scls = RegClass::GPR;
                                    }
                                    // Store GPR to spill slot
                                    outBB.instrs.push_back(
                                        MInstr{MOpcode::StrRegFpImm,
                                               {MOperand::vregOp(RegClass::GPR, sv),
                                                MOperand::immOp(offset)}});
                                }
                            }
                        }
                    }
                    outBB.instrs.push_back(
                        MInstr{MOpcode::Br, {MOperand::labelOp(term.labels[0])}});
                }
                break;
            case il::core::Opcode::Trap:
            {
                // Phase A: lower trap to a helper call for diagnostics.
                // Skip emitting rt_trap if the block already has a call to a noreturn function
                // like rt_arr_oob_panic (which will abort and never return).
                bool hasNoreturnCall = false;
                for (const auto &mi : outBB.instrs)
                {
                    if (mi.opc == MOpcode::Bl && !mi.ops.empty() &&
                        mi.ops[0].kind == MOperand::Kind::Label)
                    {
                        const std::string &callee = mi.ops[0].label;
                        if (callee == "rt_arr_oob_panic" || callee == "rt_trap")
                        {
                            hasNoreturnCall = true;
                            break;
                        }
                    }
                }
                if (!hasNoreturnCall)
                {
                    outBB.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
                }
                break;
            }
            case il::core::Opcode::TrapFromErr:
            {
                // Phase A: move optional error code into x0 (when available), then call rt_trap.
                if (!term.operands.empty())
                {
                    const auto &code = term.operands[0];
                    if (code.kind == il::core::Value::Kind::ConstInt)
                    {
                        outBB.instrs.push_back(
                            MInstr{MOpcode::MovRI,
                                   {MOperand::regOp(PhysReg::X0), MOperand::immOp(code.i64)}});
                    }
                    else if (code.kind == il::core::Value::Kind::Temp)
                    {
                        int pIdx = indexOfParam(inBB, code.id);
                        if (pIdx >= 0 && static_cast<std::size_t>(pIdx) < kMaxGPRArgs)
                        {
                            const PhysReg src = argOrder[static_cast<std::size_t>(pIdx)];
                            if (src != PhysReg::X0)
                                outBB.instrs.push_back(
                                    MInstr{MOpcode::MovRR,
                                           {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        }
                    }
                }
                outBB.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
                break;
            }
            case il::core::Opcode::CBr:
                if (term.operands.size() >= 1 && term.labels.size() == 2)
                {
                    // Emit phi copies for both edges unconditionally
                    const std::string &trueLbl = term.labels[0];
                    const std::string &falseLbl = term.labels[1];
                    auto emitEdgeCopies =
                        [&](const std::string &dst, const std::vector<il::core::Value> &args)
                    {
                        auto itIds = phiVregId.find(dst);
                        if (itIds == phiVregId.end())
                            return;
                        auto itSpill = phiSpillOffset.find(dst);
                        if (itSpill == phiSpillOffset.end())
                            return;
                        const auto &ids = itIds->second;
                        const auto &classes = phiRegClass[dst];
                        const auto &spillOffsets = itSpill->second;
                        // Store phi values to spill slots since register allocator
                        // releases vreg mappings at block boundaries
                        for (std::size_t ai = 0; ai < args.size() && ai < ids.size(); ++ai)
                        {
                            uint16_t sv = 0;
                            RegClass scls = RegClass::GPR;
                            if (!materializeValueToVReg(args[ai],
                                                        inBB,
                                                        *ti_,
                                                        fb,
                                                        outBB,
                                                        blockTempVReg,
                                                        nextVRegId,
                                                        sv,
                                                        scls))
                                continue;
                            const RegClass dstCls = classes[ai];
                            const int offset = spillOffsets[ai];
                            if (dstCls == RegClass::FPR)
                            {
                                if (scls != RegClass::FPR)
                                {
                                    const uint16_t cvt = nextVRegId++;
                                    outBB.instrs.push_back(
                                        MInstr{MOpcode::SCvtF,
                                               {MOperand::vregOp(RegClass::FPR, cvt),
                                                MOperand::vregOp(RegClass::GPR, sv)}});
                                    sv = cvt;
                                    scls = RegClass::FPR;
                                }
                                // Store FPR to spill slot
                                outBB.instrs.push_back(MInstr{MOpcode::StrFprFpImm,
                                                              {MOperand::vregOp(RegClass::FPR, sv),
                                                               MOperand::immOp(offset)}});
                            }
                            else
                            {
                                if (scls == RegClass::FPR)
                                {
                                    const uint16_t cvt = nextVRegId++;
                                    outBB.instrs.push_back(
                                        MInstr{MOpcode::FCvtZS,
                                               {MOperand::vregOp(RegClass::GPR, cvt),
                                                MOperand::vregOp(RegClass::FPR, sv)}});
                                    sv = cvt;
                                    scls = RegClass::GPR;
                                }
                                // Store GPR to spill slot
                                outBB.instrs.push_back(MInstr{MOpcode::StrRegFpImm,
                                                              {MOperand::vregOp(RegClass::GPR, sv),
                                                               MOperand::immOp(offset)}});
                            }
                        }
                    };
                    if (term.brArgs.size() > 0)
                        emitEdgeCopies(trueLbl, term.brArgs[0]);
                    if (term.brArgs.size() > 1)
                        emitEdgeCopies(falseLbl, term.brArgs[1]);
                    // Try to lower compares to cmp + b.<cond>
                    const auto &cond = term.operands[0];
                    bool loweredViaCompare = false;
                    if (cond.kind == il::core::Value::Kind::Temp)
                    {
                        const auto it = std::find_if(inBB.instructions.begin(),
                                                     inBB.instructions.end(),
                                                     [&](const il::core::Instr &I)
                                                     { return I.result && *I.result == cond.id; });
                        if (it != inBB.instructions.end())
                        {
                            const il::core::Instr &cmpI = *it;
                            const char *cc = condForOpcode(cmpI.op);
                            if (cc && cmpI.operands.size() == 2)
                            {
                                const auto &o0 = cmpI.operands[0];
                                const auto &o1 = cmpI.operands[1];
                                if (o0.kind == il::core::Value::Kind::Temp &&
                                    o1.kind == il::core::Value::Kind::Temp)
                                {
                                    int idx0 = indexOfParam(inBB, o0.id);
                                    int idx1 = indexOfParam(inBB, o1.id);
                                    if (idx0 >= 0 && idx1 >= 0 &&
                                        static_cast<std::size_t>(idx0) < kMaxGPRArgs &&
                                        static_cast<std::size_t>(idx1) < kMaxGPRArgs)
                                    {
                                        const PhysReg src0 = argOrder[static_cast<size_t>(idx0)];
                                        const PhysReg src1 = argOrder[static_cast<size_t>(idx1)];
                                        // cmp x0, x1
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::CmpRR,
                                                   {MOperand::regOp(src0), MOperand::regOp(src1)}});
                                        outBB.instrs.push_back(MInstr{
                                            MOpcode::BCond,
                                            {MOperand::condOp(cc), MOperand::labelOp(trueLbl)}});
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::Br, {MOperand::labelOp(falseLbl)}});
                                        loweredViaCompare = true;
                                    }
                                }
                                else if (o0.kind == il::core::Value::Kind::Temp &&
                                         o1.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int idx0 = indexOfParam(inBB, o0.id);
                                    if (idx0 >= 0 && static_cast<std::size_t>(idx0) < kMaxGPRArgs)
                                    {
                                        const PhysReg src0 = argOrder[static_cast<size_t>(idx0)];
                                        if (src0 != PhysReg::X0)
                                        {
                                            outBB.instrs.push_back(
                                                MInstr{MOpcode::MovRR,
                                                       {MOperand::regOp(PhysReg::X0),
                                                        MOperand::regOp(src0)}});
                                        }
                                        outBB.instrs.push_back(MInstr{MOpcode::CmpRI,
                                                                      {MOperand::regOp(PhysReg::X0),
                                                                       MOperand::immOp(o1.i64)}});
                                        outBB.instrs.push_back(MInstr{
                                            MOpcode::BCond,
                                            {MOperand::condOp(cc), MOperand::labelOp(trueLbl)}});
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::Br, {MOperand::labelOp(falseLbl)}});
                                        loweredViaCompare = true;
                                    }
                                }
                            }
                        }
                    }
                    if (!loweredViaCompare)
                    {
                        // Materialize boolean and branch on non-zero
                        // Use the block's tempVReg snapshot to get correct vreg mappings
                        uint16_t cv = 0;
                        RegClass cc = RegClass::GPR;
                        materializeValueToVReg(
                            cond, inBB, *ti_, fb, outBB, blockTempVReg, nextVRegId, cv, cc);
                        outBB.instrs.push_back(
                            MInstr{MOpcode::CmpRI,
                                   {MOperand::vregOp(RegClass::GPR, cv), MOperand::immOp(0)}});
                        outBB.instrs.push_back(MInstr{
                            MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trueLbl)}});
                        outBB.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(falseLbl)}});
                    }
                }
                break;
            default:
                break;
        }
    }

    fb.finalize();
    return mf;
}

} // namespace viper::codegen::aarch64
