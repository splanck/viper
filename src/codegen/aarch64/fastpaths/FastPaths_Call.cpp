//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: codegen/aarch64/fastpaths/FastPaths_Call.cpp
// Purpose: Fast-path pattern matching for call operations.
//
// Summary:
//   Handles fast-path lowering for call patterns:
//   - call @callee(args...) feeding ret
//   - Register argument marshalling
//   - Stack argument handling
//   - Temporary computation into scratch registers
//
// Invariants:
//   - Call result must flow directly to a ret instruction
//   - Arguments must be entry params, constants, or simple computations
//   - Uses scratch registers for intermediate computations
//   - Cycle detection and breaking for register moves
//
//===----------------------------------------------------------------------===//

#include "FastPathsInternal.hpp"
#include "codegen/aarch64/LoweringContext.hpp"

namespace viper::codegen::aarch64::fastpaths
{

using il::core::Opcode;

namespace
{

/// @brief Move descriptor for register-to-register marshalling.
struct Move
{
    PhysReg dst;
    PhysReg src;
};

/// @brief Scratch register pool for temporary computations.
constexpr std::size_t kScratchPoolSize = 2;
const PhysReg scratchPool[kScratchPoolSize] = {kScratchGPR, PhysReg::X10};

/// @brief Check if a value is an entry parameter and get its index.
bool isParamTemp(const il::core::BasicBlock &bb,
                 const std::array<PhysReg, kMaxGPRArgs> &argOrder,
                 const il::core::Value &v,
                 unsigned &outIdx)
{
    if (v.kind != il::core::Value::Kind::Temp)
        return false;
    int p = indexOfParam(bb, v.id);
    if (p >= 0)
    {
        outIdx = static_cast<unsigned>(p);
        return true;
    }
    return false;
}

/// @brief Compute a temporary value into a destination register.
/// @returns true if the computation was successful
bool computeTempTo(const il::core::Instr &prod,
                   PhysReg dstReg,
                   const il::core::BasicBlock &bb,
                   const std::array<PhysReg, kMaxGPRArgs> &argOrder,
                   MBasicBlock &bbMir)
{
    // RR emit helper
    auto rr_emit = [&](MOpcode opc, unsigned p0, unsigned p1)
    {
        const PhysReg r0 = argOrder[p0];
        const PhysReg r1 = argOrder[p1];
        bbMir.instrs.push_back(MInstr{
            opc, {MOperand::regOp(dstReg), MOperand::regOp(r0), MOperand::regOp(r1)}});
    };

    // RI emit helper
    auto ri_emit = [&](MOpcode opc, unsigned p0, long long imm)
    {
        const PhysReg r0 = argOrder[p0];
        bbMir.instrs.push_back(MInstr{
            opc, {MOperand::regOp(dstReg), MOperand::regOp(r0), MOperand::immOp(imm)}});
    };

    // RR patterns: both operands are entry params
    if (prod.op == Opcode::Add || prod.op == Opcode::IAddOvf || prod.op == Opcode::Sub ||
        prod.op == Opcode::ISubOvf || prod.op == Opcode::Mul || prod.op == Opcode::IMulOvf ||
        prod.op == Opcode::And || prod.op == Opcode::Or || prod.op == Opcode::Xor)
    {
        if (prod.operands.size() != 2)
            return false;
        if (prod.operands[0].kind == il::core::Value::Kind::Temp &&
            prod.operands[1].kind == il::core::Value::Kind::Temp)
        {
            int i0 = indexOfParam(bb, prod.operands[0].id);
            int i1 = indexOfParam(bb, prod.operands[1].id);
            if (i0 >= 0 && i1 >= 0 && static_cast<std::size_t>(i0) < kMaxGPRArgs &&
                static_cast<std::size_t>(i1) < kMaxGPRArgs)
            {
                MOpcode opc = MOpcode::AddRRR;
                if (prod.op == Opcode::Add || prod.op == Opcode::IAddOvf)
                    opc = MOpcode::AddRRR;
                else if (prod.op == Opcode::Sub || prod.op == Opcode::ISubOvf)
                    opc = MOpcode::SubRRR;
                else if (prod.op == Opcode::Mul || prod.op == Opcode::IMulOvf)
                    opc = MOpcode::MulRRR;
                else if (prod.op == Opcode::And)
                    opc = MOpcode::AndRRR;
                else if (prod.op == Opcode::Or)
                    opc = MOpcode::OrrRRR;
                else if (prod.op == Opcode::Xor)
                    opc = MOpcode::EorRRR;
                rr_emit(opc, static_cast<unsigned>(i0), static_cast<unsigned>(i1));
                return true;
            }
        }
    }

    // RI patterns: param + imm for add/sub/shift
    if (prod.op == Opcode::Shl || prod.op == Opcode::LShr || prod.op == Opcode::AShr ||
        prod.op == Opcode::Add || prod.op == Opcode::IAddOvf || prod.op == Opcode::Sub ||
        prod.op == Opcode::ISubOvf)
    {
        if (prod.operands.size() != 2)
            return false;
        const auto &o0 = prod.operands[0];
        const auto &o1 = prod.operands[1];
        if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::ConstInt)
        {
            int ip = indexOfParam(bb, o0.id);
            if (ip >= 0 && static_cast<std::size_t>(ip) < kMaxGPRArgs)
            {
                if (prod.op == Opcode::Shl)
                    ri_emit(MOpcode::LslRI, static_cast<unsigned>(ip), o1.i64);
                else if (prod.op == Opcode::LShr)
                    ri_emit(MOpcode::LsrRI, static_cast<unsigned>(ip), o1.i64);
                else if (prod.op == Opcode::AShr)
                    ri_emit(MOpcode::AsrRI, static_cast<unsigned>(ip), o1.i64);
                else if (prod.op == Opcode::Add || prod.op == Opcode::IAddOvf)
                    ri_emit(MOpcode::AddRI, static_cast<unsigned>(ip), o1.i64);
                else if (prod.op == Opcode::Sub || prod.op == Opcode::ISubOvf)
                    ri_emit(MOpcode::SubRI, static_cast<unsigned>(ip), o1.i64);
                return true;
            }
        }
        else if (o1.kind == il::core::Value::Kind::Temp &&
                 o0.kind == il::core::Value::Kind::ConstInt)
        {
            int ip = indexOfParam(bb, o1.id);
            if (ip >= 0 && static_cast<std::size_t>(ip) < kMaxGPRArgs)
            {
                if (prod.op == Opcode::Shl)
                    ri_emit(MOpcode::LslRI, static_cast<unsigned>(ip), o0.i64);
                else if (prod.op == Opcode::LShr)
                    ri_emit(MOpcode::LsrRI, static_cast<unsigned>(ip), o0.i64);
                else if (prod.op == Opcode::AShr)
                    ri_emit(MOpcode::AsrRI, static_cast<unsigned>(ip), o0.i64);
                else if (prod.op == Opcode::Add || prod.op == Opcode::IAddOvf)
                    ri_emit(MOpcode::AddRI, static_cast<unsigned>(ip), o0.i64);
                // Sub with const first not supported
                return true;
            }
        }
    }

    // Compare patterns: produce 0/1 in dstReg via cmp + cset
    if (prod.op == Opcode::ICmpEq || prod.op == Opcode::ICmpNe || prod.op == Opcode::SCmpLT ||
        prod.op == Opcode::SCmpLE || prod.op == Opcode::SCmpGT || prod.op == Opcode::SCmpGE ||
        prod.op == Opcode::UCmpLT || prod.op == Opcode::UCmpLE || prod.op == Opcode::UCmpGT ||
        prod.op == Opcode::UCmpGE)
    {
        if (prod.operands.size() != 2)
            return false;
        const auto &o0 = prod.operands[0];
        const auto &o1 = prod.operands[1];
        const char *cc = lookupCondition(prod.op);
        if (!cc)
            return false;
        if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::Temp)
        {
            int i0 = indexOfParam(bb, o0.id);
            int i1 = indexOfParam(bb, o1.id);
            if (i0 >= 0 && i1 >= 0 && static_cast<std::size_t>(i0) < kMaxGPRArgs &&
                static_cast<std::size_t>(i1) < kMaxGPRArgs)
            {
                const PhysReg r0 = argOrder[i0];
                const PhysReg r1 = argOrder[i1];
                bbMir.instrs.push_back(
                    MInstr{MOpcode::CmpRR, {MOperand::regOp(r0), MOperand::regOp(r1)}});
                bbMir.instrs.push_back(
                    MInstr{MOpcode::Cset, {MOperand::regOp(dstReg), MOperand::condOp(cc)}});
                return true;
            }
        }
        if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::ConstInt)
        {
            int i0 = indexOfParam(bb, o0.id);
            if (i0 >= 0 && static_cast<std::size_t>(i0) < kMaxGPRArgs)
            {
                const PhysReg r0 = argOrder[i0];
                bbMir.instrs.push_back(
                    MInstr{MOpcode::CmpRI, {MOperand::regOp(r0), MOperand::immOp(o1.i64)}});
                bbMir.instrs.push_back(
                    MInstr{MOpcode::Cset, {MOperand::regOp(dstReg), MOperand::condOp(cc)}});
                return true;
            }
        }
    }
    return false;
}

} // namespace

std::optional<MFunction> tryCallFastPaths(FastPathContext &ctx)
{
    if (ctx.fn.blocks.empty())
        return std::nullopt;

    const auto &bb = ctx.fn.blocks.front();
    auto &bbMir = ctx.bbOut(0);

    // =========================================================================
    // call @callee(args...) feeding ret
    // =========================================================================
    // Pattern: call @callee(args...) -> %r; ret %r
    // Marshals arguments into ABI registers/stack, emits bl, then ret
    if (ctx.fn.blocks.size() != 1 || bb.instructions.size() < 2 || bb.params.empty())
        return std::nullopt;

    const auto &binI = bb.instructions[bb.instructions.size() - 2];
    const auto &retI = bb.instructions.back();

    if (binI.op != Opcode::Call || retI.op != Opcode::Ret || !binI.result ||
        retI.operands.empty())
        return std::nullopt;

    const auto &retV = retI.operands[0];
    if (retV.kind != il::core::Value::Kind::Temp || retV.id != *binI.result || binI.callee.empty())
        return std::nullopt;

    // Check for floating-point arguments (requires vreg-based lowering)
    bool hasFloatArg = false;
    for (const auto &arg : binI.operands)
    {
        if (arg.kind == il::core::Value::Kind::ConstFloat)
        {
            hasFloatArg = true;
            break;
        }
        if (arg.kind == il::core::Value::Kind::Temp)
        {
            int p = indexOfParam(bb, arg.id);
            if (p >= 0 && p < static_cast<int>(bb.params.size()) &&
                bb.params[static_cast<std::size_t>(p)].type.kind == il::core::Type::Kind::F64)
            {
                hasFloatArg = true;
                break;
            }
        }
    }

    // Use generalized vreg-based lowering when we exceed register args or have floats
    if (binI.operands.size() > ctx.ti.intArgOrder.size() || hasFloatArg)
    {
        LoweredCall seq{};
        std::unordered_map<unsigned, uint16_t> tempVReg;
        uint16_t nextVRegId = 1;
        if (lowerCallWithArgs(binI, bb, ctx.ti, ctx.fb, bbMir, seq, tempVReg, nextVRegId))
        {
            for (auto &mi : seq.prefix)
                bbMir.instrs.push_back(std::move(mi));
            bbMir.instrs.push_back(std::move(seq.call));
            for (auto &mi : seq.postfix)
                bbMir.instrs.push_back(std::move(mi));
            bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
            ctx.fb.finalize();
            return ctx.mf;
        }
    }

    // Single-block, marshal only entry params and const i64 to integer arg regs
    const std::size_t nargs = binI.operands.size();
    if (nargs > ctx.ti.intArgOrder.size())
        return std::nullopt;

    // Build move plan for reg->reg moves; immediates applied after
    std::vector<Move> moves;
    std::vector<std::pair<PhysReg, long long>> immLoads;
    std::vector<std::pair<std::size_t, PhysReg>> tempRegs;
    std::size_t scratchUsed = 0;
    bool supported = true;

    // Register args: plan moves/imm loads/temps for 0..nargs-1
    const std::size_t nReg = ctx.argOrder.size();
    const std::size_t nRegArgs = (nargs < nReg) ? nargs : nReg;
    const std::size_t nStackArgs = (nargs > nReg) ? (nargs - nReg) : 0;

    for (std::size_t i = 0; i < nRegArgs; ++i)
    {
        const PhysReg dst = ctx.argOrder[i];
        const auto &arg = binI.operands[i];
        if (arg.kind == il::core::Value::Kind::ConstInt)
        {
            immLoads.emplace_back(dst, arg.i64);
        }
        else
        {
            unsigned pIdx = 0;
            if (isParamTemp(bb, ctx.argOrder, arg, pIdx) && pIdx < ctx.argOrder.size())
            {
                const PhysReg src = ctx.argOrder[pIdx];
                if (src != dst)
                    moves.push_back(Move{dst, src});
            }
            else
            {
                // Attempt to compute temp into a scratch then marshal it
                if (arg.kind == il::core::Value::Kind::Temp && scratchUsed < kScratchPoolSize)
                {
                    auto it = std::find_if(bb.instructions.begin(),
                                           bb.instructions.end(),
                                           [&](const il::core::Instr &I)
                                           { return I.result && *I.result == arg.id; });
                    if (it != bb.instructions.end())
                    {
                        const PhysReg dstScratch = scratchPool[scratchUsed];
                        if (computeTempTo(*it, dstScratch, bb, ctx.argOrder, bbMir))
                        {
                            tempRegs.emplace_back(i, dstScratch);
                            ++scratchUsed;
                            continue;
                        }
                    }
                }
                supported = false;
                break;
            }
        }
    }

    if (!supported)
        return std::nullopt;

    // Include temp-reg moves into overall move list
    for (auto &tr : tempRegs)
    {
        const PhysReg dstArg = ctx.argOrder[tr.first];
        if (dstArg != tr.second)
            moves.push_back(Move{dstArg, tr.second});
    }

    // Resolve reg moves with scratch X9 to break cycles
    auto hasDst = [&](PhysReg r)
    {
        for (auto &m : moves)
            if (m.dst == r)
                return true;
        return false;
    };

    while (!moves.empty())
    {
        bool progressed = false;
        for (auto it = moves.begin(); it != moves.end();)
        {
            if (!hasDst(it->src))
            {
                bbMir.instrs.push_back(MInstr{
                    MOpcode::MovRR, {MOperand::regOp(it->dst), MOperand::regOp(it->src)}});
                it = moves.erase(it);
                progressed = true;
            }
            else
            {
                ++it;
            }
        }
        if (!progressed)
        {
            // Break cycle using scratch register
            const PhysReg cycleSrc = moves.front().src;
            bbMir.instrs.push_back(
                MInstr{MOpcode::MovRR, {MOperand::regOp(kScratchGPR), MOperand::regOp(cycleSrc)}});
            for (auto &m : moves)
                if (m.src == cycleSrc)
                    m.src = kScratchGPR;
        }
    }

    // Apply immediates
    for (auto &pr : immLoads)
        bbMir.instrs.push_back(
            MInstr{MOpcode::MovRI, {MOperand::regOp(pr.first), MOperand::immOp(pr.second)}});

    // Stack args: allocate area, materialize values, store at [sp, #offset]
    if (nStackArgs > 0)
    {
        long long frameBytes = static_cast<long long>(nStackArgs) * kSlotSizeBytes;
        if (frameBytes % kStackAlignment != 0LL)
            frameBytes += kSlotSizeBytes;
        ctx.fb.setMaxOutgoingBytes(static_cast<int>(frameBytes));

        for (std::size_t i = nReg; i < nargs; ++i)
        {
            const auto &arg = binI.operands[i];
            PhysReg valReg = kScratchGPR;
            if (arg.kind == il::core::Value::Kind::ConstInt)
            {
                if (scratchUsed >= kScratchPoolSize)
                {
                    supported = false;
                    break;
                }
                const PhysReg tmp = scratchPool[scratchUsed++];
                bbMir.instrs.push_back(
                    MInstr{MOpcode::MovRI, {MOperand::regOp(tmp), MOperand::immOp(arg.i64)}});
                valReg = tmp;
            }
            else if (arg.kind == il::core::Value::Kind::Temp)
            {
                unsigned pIdx = 0;
                if (isParamTemp(bb, ctx.argOrder, arg, pIdx) && pIdx < ctx.argOrder.size())
                {
                    valReg = ctx.argOrder[pIdx];
                }
                else
                {
                    if (scratchUsed >= kScratchPoolSize)
                    {
                        supported = false;
                        break;
                    }
                    valReg = scratchPool[scratchUsed++];
                    auto it = std::find_if(bb.instructions.begin(),
                                           bb.instructions.end(),
                                           [&](const il::core::Instr &I)
                                           { return I.result && *I.result == arg.id; });
                    if (it == bb.instructions.end() ||
                        !computeTempTo(*it, valReg, bb, ctx.argOrder, bbMir))
                    {
                        supported = false;
                        break;
                    }
                }
            }
            else
            {
                supported = false;
                break;
            }
            const long long off = static_cast<long long>((i - nReg) * 8ULL);
            bbMir.instrs.push_back(
                MInstr{MOpcode::StrRegSpImm, {MOperand::regOp(valReg), MOperand::immOp(off)}});
        }

        if (!supported)
        {
            bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
            ctx.fb.finalize();
            return ctx.mf;
        }

        bbMir.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp(binI.callee)}});
        bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
        ctx.fb.finalize();
        return ctx.mf;
    }

    // No stack args; emit call directly
    bbMir.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp(binI.callee)}});
    bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
    ctx.fb.finalize();
    return ctx.mf;
}

} // namespace viper::codegen::aarch64::fastpaths
