//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// File: src/codegen/x86_64/LowerILToMIR.cpp
// Purpose: Translate IL functions into the Machine IR representation consumed by
//          the x86-64 backend while recording auxiliary call lowering plans.
// Key invariants: Each IL SSA identifier maps to a single virtual register of a
//                 fixed class, block parameters become PX_COPY pairs on outgoing
//                 edges, and label operands are preserved verbatim.
// Ownership/Lifetime: Lowering borrows IL structures, constructs Machine IR by
//                     value, and caches call plan metadata scoped to the current
//                     function.
// Perf/Threading notes: Designed for single-threaded compilation passes; no
//                       shared global state is mutated.
// Links: docs/architecture.md#codegen, docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "LowerILToMIR.hpp"

#include <cassert>
#include <string_view>

namespace viper::codegen::x64
{

namespace
{
/// @brief Create a shallow copy of a Machine IR operand.
/// @details Machine IR operands are lightweight value types so copying preserves
///          all metadata.  Lowering frequently needs duplicate operands when
///          emitting multi-instruction sequences (for example MOV followed by ADD).
[[nodiscard]] Operand cloneOperand(const Operand &operand)
{
    return operand;
}
} // namespace

/// @brief Construct the lowering adapter with the target description.
/// @details Stores a pointer to @p target so lowering steps can reference calling
///          convention details without copying the descriptor.
LowerILToMIR::LowerILToMIR(const TargetInfo &target) noexcept : target_{&target} {}

/// @brief Retrieve the call-lowering plans accumulated during the last lowering run.
/// @details The returned reference remains valid until the adapter lowers another
///          function, at which point the plans are reset.
const std::vector<CallLoweringPlan> &LowerILToMIR::callPlans() const noexcept
{
    return callPlans_;
}

/// @brief Clear per-function state prior to lowering a new IL function.
/// @details Resets the virtual register counter, clears SSA-to-register maps, and
///          discards any previously recorded call-lowering metadata.
void LowerILToMIR::resetFunctionState()
{
    nextVReg_ = 1U;
    valueToVReg_.clear();
    blockInfo_.clear();
    callPlans_.clear();
}

/// @brief Map an IL value kind to a Machine IR register class.
/// @details Label values reuse the general-purpose register class because they are
///          lowered to addresses.  The default case returns GPR to keep the function
///          total even when new kinds are introduced.
RegClass LowerILToMIR::regClassFor(ILValue::Kind kind) noexcept
{
    switch (kind)
    {
        case ILValue::Kind::I64:
        case ILValue::Kind::I1:
        case ILValue::Kind::PTR:
            return RegClass::GPR;
        case ILValue::Kind::F64:
            return RegClass::XMM;
        case ILValue::Kind::LABEL:
            return RegClass::GPR;
    }
    return RegClass::GPR;
}

/// @brief Ensure the specified SSA id owns a Machine IR virtual register.
/// @details Creates a new @ref VReg on first use and verifies subsequent accesses use
///          the same register class.
VReg LowerILToMIR::ensureVReg(int id, ILValue::Kind kind)
{
    assert(id >= 0 && "SSA value without identifier");
    const auto it = valueToVReg_.find(id);
    if (it != valueToVReg_.end())
    {
        assert(it->second.cls == regClassFor(kind) && "SSA id reused with new type");
        return it->second;
    }
    const VReg vreg{nextVReg_++, regClassFor(kind)};
    valueToVReg_.emplace(id, vreg);
    return vreg;
}

/// @brief Check whether an IL value is encoded as an immediate constant.
/// @details Immediate values carry negative identifiers; all non-negative ids refer
///          to SSA values and must be mapped to virtual registers.
bool LowerILToMIR::isImmediate(const ILValue &value) const noexcept
{
    return value.id < 0;
}

/// @brief Convert an IL value into a Machine IR operand for the given block.
/// @details Produces label, immediate, or register operands depending on the IL kind.
///          When a virtual register is required the helper calls @ref ensureVReg.  A
///          placeholder conversion is used for f64 immediates until a constant pool
///          exists.
Operand LowerILToMIR::makeOperandForValue(MBasicBlock &block, const ILValue &value, RegClass cls)
{
    (void)block;
    if (value.kind == ILValue::Kind::LABEL)
    {
        return makeLabelOperand(value);
    }

    (void)cls;

    if (!isImmediate(value))
    {
        const VReg vreg = ensureVReg(value.id, value.kind);
        return makeVRegOperand(vreg.cls, vreg.id);
    }

    switch (value.kind)
    {
        case ILValue::Kind::I64:
        case ILValue::Kind::I1:
        case ILValue::Kind::PTR:
            return makeImmOperand(value.i64);
        case ILValue::Kind::F64:
        {
            // TODO: Materialise f64 immediates using a constant pool.
            const auto approx = static_cast<int64_t>(value.f64);
            return makeImmOperand(approx);
        }
        case ILValue::Kind::LABEL:
            break;
    }
    return makeImmOperand(0);
}

/// @brief Form a Machine IR label operand from an IL label value.
/// @details Validates that the input is a label and forwards its name to the Machine
///          IR factory.
Operand LowerILToMIR::makeLabelOperand(const ILValue &value) const
{
    assert(value.kind == ILValue::Kind::LABEL && "label operand expected");
    return x64::makeLabelOperand(value.label);
}

/// @brief Lower a binary arithmetic IL instruction into Machine IR.
/// @details Emits a MOV to seed the destination virtual register followed by either
///          the register-register opcode or its immediate variant when available.
void LowerILToMIR::lowerBinary(
    const ILInstr &instr, MBasicBlock &block, MOpcode opcRR, MOpcode opcRI, RegClass cls)
{
    if (instr.resultId < 0 || instr.ops.size() < 2)
    {
        return;
    }

    const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand lhs = makeOperandForValue(block, instr.ops[0], cls);
    const Operand rhs = makeOperandForValue(block, instr.ops[1], cls);

    if (std::holds_alternative<OpImm>(lhs))
    {
        block.append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{cloneOperand(dest), lhs}));
    }
    else
    {
        block.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), lhs}));
    }

    if (opcRI != opcRR && std::holds_alternative<OpImm>(rhs))
    {
        block.append(MInstr::make(opcRI, std::vector<Operand>{cloneOperand(dest), rhs}));
    }
    else
    {
        block.append(MInstr::make(opcRR, std::vector<Operand>{cloneOperand(dest), rhs}));
    }
}

/// @brief Lower a comparison instruction and optional boolean result.
/// @details Emits the compare opcode appropriate for the register class and, when
///          the IL instruction defines a result, zero-initialises the destination and
///          applies SETcc to materialise a 0/1 flag.
void LowerILToMIR::lowerCmp(const ILInstr &instr, MBasicBlock &block, RegClass cls)
{
    if (instr.ops.size() < 2)
    {
        return;
    }

    const Operand lhs = makeOperandForValue(block, instr.ops[0], cls);
    const Operand rhs = makeOperandForValue(block, instr.ops[1], cls);

    if (cls == RegClass::GPR)
    {
        block.append(MInstr::make(MOpcode::CMPrr, std::vector<Operand>{cloneOperand(lhs), rhs}));
    }
    else
    {
        block.append(MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{cloneOperand(lhs), rhs}));
    }

    if (instr.resultId >= 0)
    {
        const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
        const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
        block.append(
            MInstr::make(MOpcode::XORrr32, std::vector<Operand>{cloneOperand(dest), dest}));
        block.append(MInstr::make(MOpcode::SETcc,
                                  std::vector<Operand>{makeImmOperand(1), cloneOperand(dest)}));
    }
}

/// @brief Lower a ternary select into MOV/CMOV style sequences.
/// @details Handles general-purpose and floating-point cases, materialising
///          immediates into temporaries when required and emitting placeholder logic
///          for floating-point paths until richer instruction selection lands.
void LowerILToMIR::lowerSelect(const ILInstr &instr, MBasicBlock &block)
{
    if (instr.resultId < 0 || instr.ops.size() < 3)
    {
        return;
    }

    const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand cond = makeOperandForValue(block, instr.ops[0], RegClass::GPR);
    const Operand trueVal = makeOperandForValue(block, instr.ops[1], destReg.cls);
    const Operand falseVal = makeOperandForValue(block, instr.ops[2], destReg.cls);

    if (destReg.cls == RegClass::GPR)
    {
        Operand cmovSource = trueVal;
        if (std::holds_alternative<OpImm>(cmovSource))
        {
            const VReg tmpVReg{nextVReg_++, destReg.cls};
            cmovSource = makeVRegOperand(tmpVReg.cls, tmpVReg.id);
            block.append(MInstr::make(
                MOpcode::MOVri, std::vector<Operand>{cloneOperand(cmovSource), trueVal}));
        }

        const bool falseIsImm = std::holds_alternative<OpImm>(falseVal);
        std::vector<Operand> movOperands{};
        movOperands.push_back(cloneOperand(dest));
        movOperands.push_back(cloneOperand(falseVal));
        movOperands.push_back(cloneOperand(cmovSource));
        block.append(
            MInstr::make(falseIsImm ? MOpcode::MOVri : MOpcode::MOVrr, std::move(movOperands)));

        block.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
        block.append(MInstr::make(
            MOpcode::SETcc, std::vector<Operand>{makeImmOperand(1), cloneOperand(dest)}));
        return;
    }

    if (std::holds_alternative<OpImm>(falseVal))
    {
        block.append(
            MInstr::make(MOpcode::MOVri, std::vector<Operand>{cloneOperand(dest), falseVal}));
    }
    else
    {
        block.append(
            MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), falseVal}));
    }

    block.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
    block.append(
        MInstr::make(MOpcode::SETcc, std::vector<Operand>{makeImmOperand(1), cloneOperand(dest)}));
    (void)trueVal;
}

/// @brief Lower an unconditional IL branch to a Machine IR jump.
void LowerILToMIR::lowerBranch(const ILInstr &instr, MBasicBlock &block)
{
    if (instr.ops.empty())
    {
        return;
    }
    block.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(instr.ops[0])}));
}

/// @brief Lower a conditional branch testing a boolean guard.
/// @details Emits a TEST instruction followed by a conditional jump to the "true"
///          label and a final JMP to the fallthrough label.
void LowerILToMIR::lowerCondBranch(const ILInstr &instr, MBasicBlock &block)
{
    if (instr.ops.size() < 3)
    {
        return;
    }

    const Operand cond = makeOperandForValue(block, instr.ops[0], RegClass::GPR);
    const Operand trueLabel = makeLabelOperand(instr.ops[1]);
    const Operand falseLabel = makeLabelOperand(instr.ops[2]);

    block.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
    block.append(MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(1), trueLabel}));
    block.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{falseLabel}));
}

/// @brief Lower a return instruction, routing the value to the correct register.
/// @details Normalises boolean results to 0/1, materialises immediates into
///          temporaries when required, and finally moves the value into the ABI
///          mandated return register before emitting RET.
void LowerILToMIR::lowerReturn(const ILInstr &instr, MBasicBlock &block)
{
    if (instr.ops.empty())
    {
        block.append(MInstr::make(MOpcode::RET, {}));
        return;
    }

    assert(target_ != nullptr && "target info must be initialised");

    const ILValue &retVal = instr.ops.front();
    const RegClass cls = regClassFor(retVal.kind);

    Operand src = makeOperandForValue(block, retVal, cls);

    if (retVal.kind == ILValue::Kind::I1)
    {
        if (const auto *imm = std::get_if<OpImm>(&src))
        {
            src = makeImmOperand(imm->val != 0 ? 1 : 0);
        }
    }

    const auto materialiseToReg = [this, &block](Operand operand, RegClass expectedCls) -> Operand {
        if (std::holds_alternative<OpReg>(operand))
        {
            return operand;
        }

        const VReg tmp{nextVReg_++, expectedCls};
        const Operand tmpOp = makeVRegOperand(tmp.cls, tmp.id);

        if (std::holds_alternative<OpImm>(operand))
        {
            block.append(MInstr::make(
                MOpcode::MOVri, {cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else if (std::holds_alternative<OpMem>(operand))
        {
            const MOpcode loadOpc = expectedCls == RegClass::XMM ? MOpcode::MOVSDmr : MOpcode::MOVrr;
            block.append(MInstr::make(loadOpc, {cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else if (std::holds_alternative<OpLabel>(operand))
        {
            block.append(MInstr::make(MOpcode::LEA, {cloneOperand(tmpOp), cloneOperand(operand)}));
        }

        return tmpOp;
    };

    Operand srcReg = materialiseToReg(std::move(src), cls);

    if (retVal.kind == ILValue::Kind::I1 && std::holds_alternative<OpReg>(srcReg))
    {
        const auto &reg = std::get<OpReg>(srcReg);
        if (!reg.isPhys)
        {
            const VReg zx{nextVReg_++, RegClass::GPR};
            const Operand zxOp = makeVRegOperand(zx.cls, zx.id);
            block.append(MInstr::make(MOpcode::MOVZXrr32, {cloneOperand(zxOp), cloneOperand(srcReg)}));
            srcReg = zxOp;
        }
    }

    if (cls == RegClass::XMM)
    {
        const Operand retReg = makePhysRegOperand(
            RegClass::XMM, static_cast<uint16_t>(target_->f64ReturnReg));
        block.append(
            MInstr::make(MOpcode::MOVSDrr, {retReg, cloneOperand(srcReg)}));
    }
    else
    {
        const Operand retReg = makePhysRegOperand(
            RegClass::GPR, static_cast<uint16_t>(target_->intReturnReg));
        block.append(MInstr::make(MOpcode::MOVrr, {retReg, cloneOperand(srcReg)}));
    }

    block.append(MInstr::make(MOpcode::RET, {}));
}

/// @brief Lower a direct function call and record a call-lowering plan.
void LowerILToMIR::lowerCall(const ILInstr &instr, MBasicBlock &block)
{
    if (instr.ops.empty())
    {
        return;
    }

    CallLoweringPlan plan{};
    plan.calleeLabel = instr.ops.front().label;

    for (std::size_t idx = 1; idx < instr.ops.size(); ++idx)
    {
        const auto &argVal = instr.ops[idx];
        CallArg arg{};
        arg.kind = regClassFor(argVal.kind) == RegClass::GPR ? CallArg::GPR : CallArg::XMM;

        if (isImmediate(argVal))
        {
            arg.isImm = true;
            arg.imm = argVal.i64;
        }
        else
        {
            const Operand operand = makeOperandForValue(block, argVal, regClassFor(argVal.kind));
            if (const auto *reg = std::get_if<OpReg>(&operand))
            {
                arg.vreg = reg->idOrPhys;
            }
            else if (const auto *imm = std::get_if<OpImm>(&operand))
            {
                arg.isImm = true;
                arg.imm = imm->val;
            }
        }

        plan.args.push_back(arg);
    }

    if (instr.resultId >= 0)
    {
        [[maybe_unused]] const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
        if (instr.resultKind == ILValue::Kind::F64)
        {
            plan.returnsF64 = true;
        }
    }

    callPlans_.push_back(plan);
    block.append(MInstr::make(MOpcode::CALL, std::vector<Operand>{makeLabelOperand(instr.ops[0])}));
}

/// @brief Lower a load instruction into Machine IR memory access.
/// @details Materialises the base register, applies the displacement, and emits the
///          opcode matching the requested register class.
void LowerILToMIR::lowerLoad(const ILInstr &instr, MBasicBlock &block, RegClass cls)
{
    if (instr.resultId < 0 || instr.ops.empty())
    {
        return;
    }

    Operand baseOp = makeOperandForValue(block, instr.ops[0], RegClass::GPR);
    const auto *baseReg = std::get_if<OpReg>(&baseOp);
    if (!baseReg)
    {
        return;
    }

    const int32_t disp = instr.ops.size() > 1 ? static_cast<int32_t>(instr.ops[1].i64) : 0;
    const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand mem = makeMemOperand(*baseReg, disp);

    if (cls == RegClass::GPR)
    {
        block.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), mem}));
    }
    else
    {
        block.append(MInstr::make(MOpcode::MOVSDmr, std::vector<Operand>{cloneOperand(dest), mem}));
    }
}

/// @brief Lower a store instruction into the appropriate Machine IR form.
/// @details Computes the destination address and chooses the MOV variant based on
///          whether the stored value is integer, floating point, or immediate.
void LowerILToMIR::lowerStore(const ILInstr &instr, MBasicBlock &block)
{
    if (instr.ops.size() < 2)
    {
        return;
    }

    const Operand value = makeOperandForValue(block, instr.ops[0], regClassFor(instr.ops[0].kind));
    Operand baseOp = makeOperandForValue(block, instr.ops[1], RegClass::GPR);
    const auto *baseReg = std::get_if<OpReg>(&baseOp);
    if (!baseReg)
    {
        return;
    }
    const int32_t disp = instr.ops.size() > 2 ? static_cast<int32_t>(instr.ops[2].i64) : 0;
    const Operand mem = makeMemOperand(*baseReg, disp);

    if (std::holds_alternative<OpReg>(value))
    {
        const auto cls = std::get<OpReg>(value).cls;
        if (cls == RegClass::XMM)
        {
            block.append(MInstr::make(MOpcode::MOVSDrm, std::vector<Operand>{mem, value}));
        }
        else
        {
            block.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{mem, value}));
        }
    }
    else
    {
        block.append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{mem, value}));
    }
}

/// @brief Lower an IL cast between register classes.
/// @details Emits a MOV when no semantic change is required or uses the supplied
///          opcode to perform conversions such as integer-to-float.
void LowerILToMIR::lowerCast(
    const ILInstr &instr, MBasicBlock &block, MOpcode opc, RegClass dstCls, RegClass srcCls)
{
    if (instr.resultId < 0 || instr.ops.empty())
    {
        return;
    }

    const Operand src = makeOperandForValue(block, instr.ops[0], srcCls);
    const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    if (opc == MOpcode::MOVrr || std::holds_alternative<OpImm>(src))
    {
        block.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), src}));
    }
    else
    {
        block.append(MInstr::make(opc, std::vector<Operand>{cloneOperand(dest), src}));
    }

    (void)dstCls;
}

/// @brief Dispatch IL opcodes to the relevant lowering helper.
/// @details Covers the subset of operations implemented for Phase A and leaves
///          unsupported instructions untouched until later phases expand coverage.
void LowerILToMIR::lowerInstruction(const ILInstr &instr, MBasicBlock &block)
{
    const std::string_view opc{instr.opcode};

    if (opc == "add")
    {
        const RegClass cls = regClassFor(instr.resultKind);
        const MOpcode opRR = cls == RegClass::GPR ? MOpcode::ADDrr : MOpcode::FADD;
        const MOpcode opRI = cls == RegClass::GPR ? MOpcode::ADDri : opRR;
        lowerBinary(instr, block, opRR, opRI, cls);
        return;
    }
    if (opc == "sub")
    {
        const RegClass cls = regClassFor(instr.resultKind);
        const MOpcode opRR = cls == RegClass::GPR ? MOpcode::SUBrr : MOpcode::FSUB;
        lowerBinary(instr, block, opRR, opRR, cls);
        return;
    }
    if (opc == "mul")
    {
        const RegClass cls = regClassFor(instr.resultKind);
        const MOpcode opRR = cls == RegClass::GPR ? MOpcode::IMULrr : MOpcode::FMUL;
        lowerBinary(instr, block, opRR, opRR, cls);
        return;
    }
    if (opc == "cmp")
    {
        lowerCmp(instr,
                 block,
                 regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind));
        return;
    }
    if (opc == "select")
    {
        lowerSelect(instr, block);
        return;
    }
    if (opc == "br")
    {
        lowerBranch(instr, block);
        return;
    }
    if (opc == "cbr")
    {
        lowerCondBranch(instr, block);
        return;
    }
    if (opc == "ret")
    {
        lowerReturn(instr, block);
        return;
    }
    if (opc == "call")
    {
        lowerCall(instr, block);
        return;
    }
    if (opc == "load")
    {
        lowerLoad(instr, block, regClassFor(instr.resultKind));
        return;
    }
    if (opc == "store")
    {
        lowerStore(instr, block);
        return;
    }
    if (opc == "zext" || opc == "sext" || opc == "trunc")
    {
        lowerCast(instr,
                  block,
                  MOpcode::MOVrr,
                  regClassFor(instr.resultKind),
                  regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind));
        return;
    }
    if (opc == "sitofp")
    {
        lowerCast(instr, block, MOpcode::CVTSI2SD, RegClass::XMM, RegClass::GPR);
        return;
    }
    if (opc == "fptosi")
    {
        lowerCast(instr, block, MOpcode::CVTTSD2SI, RegClass::GPR, RegClass::XMM);
        return;
    }
    // TODO: handle division and additional opcodes.
}

/// @brief Emit PX_COPY instructions for block parameter transfers.
/// @details Uses precomputed destination parameter virtual registers to pair source
///          SSA ids with their Machine IR counterparts when lowering control-flow
///          edges.
void LowerILToMIR::emitEdgeCopies(const ILBlock &source, MBasicBlock &block)
{
    for (const auto &edge : source.terminatorEdges)
    {
        const auto destIt = blockInfo_.find(edge.to);
        if (destIt == blockInfo_.end() || destIt->second.paramVRegs.empty())
        {
            continue;
        }
        const auto &params = destIt->second.paramVRegs;
        if (edge.argIds.empty())
        {
            continue;
        }

        MInstr px = MInstr::make(MOpcode::PX_COPY, {});
        for (std::size_t idx = 0; idx < params.size() && idx < edge.argIds.size(); ++idx)
        {
            const auto valIt = valueToVReg_.find(edge.argIds[idx]);
            if (valIt == valueToVReg_.end())
            {
                continue;
            }
            px.operands.push_back(makeVRegOperand(params[idx].cls, params[idx].id));
            px.operands.push_back(makeVRegOperand(valIt->second.cls, valIt->second.id));
        }

        if (!px.operands.empty())
        {
            block.append(std::move(px));
        }
    }
}

/// @brief Lower an entire IL function into Machine IR.
/// @details Resets adapter state, constructs Machine IR blocks mirroring the IL
///          structure, lowers instructions within each block, and emits PX_COPY
///          transfers for block parameters.  The resulting @ref MFunction owns the
///          lowered instructions by value.
MFunction LowerILToMIR::lower(const ILFunction &func)
{
    resetFunctionState();

    MFunction result{};
    result.name = func.name;

    result.blocks.reserve(func.blocks.size());

    for (std::size_t idx = 0; idx < func.blocks.size(); ++idx)
    {
        const auto &ilBlock = func.blocks[idx];
        BlockInfo info{};
        info.index = idx;
        info.paramVRegs.reserve(ilBlock.paramIds.size());

        MBasicBlock block{};
        block.label = ilBlock.name;

        for (std::size_t p = 0; p < ilBlock.paramIds.size() && p < ilBlock.paramKinds.size(); ++p)
        {
            const int paramId = ilBlock.paramIds[p];
            const auto kind = ilBlock.paramKinds[p];
            if (paramId >= 0)
            {
                info.paramVRegs.push_back(ensureVReg(paramId, kind));
            }
        }

        blockInfo_[ilBlock.name] = info;
        result.addBlock(std::move(block));
    }

    for (std::size_t idx = 0; idx < func.blocks.size(); ++idx)
    {
        const auto &ilBlock = func.blocks[idx];
        auto &mirBlock = result.blocks[idx];

        for (const auto &instr : ilBlock.instrs)
        {
            lowerInstruction(instr, mirBlock);
        }

        emitEdgeCopies(ilBlock, mirBlock);
    }

    return result;
}

} // namespace viper::codegen::x64
