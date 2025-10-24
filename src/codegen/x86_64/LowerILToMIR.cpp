//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/LowerILToMIR.cpp
// Purpose: Provide the IL → Machine IR lowering pipeline that backs the
//          experimental x86-64 backend.
// Key invariants:
//   * Every SSA identifier in the IL function maps to a single virtual
//     register in the generated Machine IR.
//   * Terminator edges materialise PX_COPY pseudo instructions so block
//     arguments are transferred explicitly.
//   * Only the Phase-A opcode subset is implemented; helpers leave TODO notes
//     for unhandled forms.
// Ownership model: The lowering class borrows IL data structures owned by the
// caller and constructs Machine IR by value, retaining scratch tables that map
// SSA identifiers to virtual registers and describe per-block metadata.
// Links: docs/architecture.md#codegen
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the provisional IL-to-Machine-IR lowering adapter.
/// @details The lowering pass translates IL instructions into Machine IR
///          tailored for the x86-64 backend.  It caches SSA-to-virtual-register
///          mappings, creates PX_COPY bridges for block parameters, and records
///          call-lowering plans so later stages can honour ABI conventions.
///          The implementation purposefully favours clarity over completeness to
///          support early backend bring-up.

#include "LowerILToMIR.hpp"

#include <cassert>
#include <string_view>

namespace viper::codegen::x64
{

namespace
{
/// @brief Produce a defensive copy of a Machine IR operand.
/// @details Many lowering helpers need to forward the same operand into
///          multiple Machine IR instructions.  Returning the operand by value
///          ensures each consumer receives an independent copy, preventing
///          later mutations from aliasing previously emitted instructions.
[[nodiscard]] Operand cloneOperand(const Operand &operand)
{
    return operand;
}
} // namespace

/// @brief Construct a lowering adapter tied to a concrete target description.
/// @details The adapter stores a pointer to @p target so lowering helpers can
///          consult register-class assignments and ABI details without copying
///          bulky target tables.  The pointer remains valid for the adapter's
///          lifetime because the owner supplies a long-lived @ref TargetInfo.
LowerILToMIR::LowerILToMIR(const TargetInfo &target) noexcept : target_{&target} {}

/// @brief Access the call-lowering plans produced during the last translation.
/// @details Each IL call emits a @ref CallLoweringPlan describing the mapping of
///          arguments and results to physical resources.  Exposing the vector by
///          const reference lets later pipeline stages consume the plans without
///          copying large structures.
const std::vector<CallLoweringPlan> &LowerILToMIR::callPlans() const noexcept
{
    return callPlans_;
}

/// @brief Clear caches so a fresh IL function can be lowered deterministically.
/// @details The method resets the virtual-register counter and erases SSA
///          lookups, per-block metadata, and staged call plans.  This keeps
///          translations independent and prevents stale identifiers from
///          polluting the Machine IR of subsequent functions.
void LowerILToMIR::resetFunctionState()
{
    nextVReg_ = 1U;
    valueToVReg_.clear();
    blockInfo_.clear();
    callPlans_.clear();
}

/// @brief Translate an IL value kind into a machine register class.
/// @details The helper encapsulates the current Phase-A convention that maps
///          integers, pointers, and booleans onto the general-purpose register
///          file while floating-point values use XMM registers.  Labels are
///          treated as GPR targets because they lower into RIP-relative
///          immediates.
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

/// @brief Guarantee that an SSA value has a materialised virtual register.
/// @details If @p id has already been lowered, the cached virtual register is
///          returned after verifying its class matches the IL kind.  Otherwise a
///          fresh virtual register identifier is allocated, recorded in the
///          lookup table, and returned to the caller.  This keeps Machine IR
///          consistent even when multiple instructions reference the same SSA
///          value.
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

/// @brief Test whether an IL value can be represented as an immediate literal.
/// @details The lowering pipeline treats values with negative SSA identifiers as
///          immediates.  The helper hides that convention so opcode-specific
///          lowering routines can branch on the property without duplicating the
///          sentinel logic.
bool LowerILToMIR::isImmediate(const ILValue &value) const noexcept
{
    return value.id < 0;
}

/// @brief Build the Machine IR operand corresponding to an IL SSA value.
/// @details Values that already own virtual registers return those operands,
///          immediates are translated into @ref OpImm, and labels lower into
///          @ref OpLabel references.  The @p block argument exists for future
///          enhancements (such as materialising constants inside the block) and
///          is currently unused by the Phase-A implementation.
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

/// @brief Convert an IL label value into a Machine IR label operand.
/// @details The helper asserts that the IL value is indeed a label and delegates
///          to the common label-operand constructor so branch-related lowering
///          code stays concise.
Operand LowerILToMIR::makeLabelOperand(const ILValue &value) const
{
    assert(value.kind == ILValue::Kind::LABEL && "label operand expected");
    return x64::makeLabelOperand(value.label);
}

/// @brief Lower a binary arithmetic or logical IL instruction into Machine IR.
/// @details The routine materialises the destination register, copies the left
///          operand into it, and then selects either a register–register or
///          register–immediate form depending on the right operand.  This keeps
///          the generated code close to canonical two-address x86 idioms.
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

/// @brief Lower an IL comparison instruction.
/// @details The helper emits the appropriate compare/test instruction for the
///          operand class and, when the comparison produces a boolean result,
///          synthesises the zero-extension idiom used by subsequent code.  Phase
///          A models boolean results as 0/1 values stored in GPRs.
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

/// @brief Lower the IL select (ternary) instruction.
/// @details The current implementation materialises the false path into the
///          destination and then uses a SETcc mask driven by the condition to
///          overwrite the result when the true branch fires.  Although not
///          optimal, the strategy keeps code generation straightforward until
///          more advanced patterns are implemented.
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

    // Phase A placeholder: materialise false path then overwrite with true path using SETcc mask.
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

    // TODO: Replace with CMOV-based lowering once available.
    block.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
    block.append(
        MInstr::make(MOpcode::SETcc, std::vector<Operand>{makeImmOperand(1), cloneOperand(dest)}));
    (void)trueVal;
}

/// @brief Lower an unconditional branch instruction.
/// @details IL branches encode their destination as a label operand.  The
///          helper simply translates the operand into a Machine IR label and
///          appends a @c JMP instruction, guarding against malformed input that
///          omits the label.
void LowerILToMIR::lowerBranch(const ILInstr &instr, MBasicBlock &block)
{
    if (instr.ops.empty())
    {
        return;
    }
    block.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(instr.ops[0])}));
}

/// @brief Lower a conditional branch instruction.
/// @details The routine evaluates the condition into the GPR file, emits a
///          @c TEST to update flags, and then sequences a conditional jump
///          followed by a fall-through @c JMP.  This mirrors the structure
///          expected by the placeholder backend until dedicated condition-code
///          lowering is introduced.
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

/// @brief Lower a return terminator.
/// @details Phase A does not yet move return values into ABI locations, so the
///          helper ignores operands and simply appends a @c RET instruction.
///          Future iterations will copy the result into the correct register
///          before returning.
void LowerILToMIR::lowerReturn(const ILInstr &instr, MBasicBlock &block)
{
    // TODO: Materialise return value copies once ABI details are defined.
    (void)instr;
    block.append(MInstr::make(MOpcode::RET, {}));
}

/// @brief Lower a call instruction and record its ABI requirements.
/// @details The helper walks call operands, classifies each argument, and
///          builds a @ref CallLoweringPlan that later stages will use to
///          materialise the call sequence.  It also emits the Machine IR call
///          instruction pointing at the callee label.
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
        ensureVReg(instr.resultId, instr.resultKind);
        if (instr.resultKind == ILValue::Kind::F64)
        {
            plan.returnsF64 = true;
        }
    }

    callPlans_.push_back(plan);
    block.append(MInstr::make(MOpcode::CALL, std::vector<Operand>{makeLabelOperand(instr.ops[0])}));
}

/// @brief Lower a load instruction.
/// @details The routine forms a memory operand from the base pointer and
///          optional displacement, allocates a destination virtual register, and
///          emits either an integer or floating-point move depending on @p cls.
///          Labels and non-register bases are rejected until constant pools are
///          implemented.
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

/// @brief Lower a store instruction.
/// @details The helper resolves the source operand to either a register or
///          immediate and forms a base-plus-displacement memory operand for the
///          destination.  It then emits the appropriate move instruction,
///          choosing between integer and floating-point variants.
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

/// @brief Lower simple cast instructions such as zero/sign extend.
/// @details Casts that reduce to moves reuse @c MOVrr while more complex forms
///          emit the supplied opcode.  The helper ensures a destination virtual
///          register exists and copies immediates directly when possible.
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

/// @brief Dispatch lowering for a single IL instruction.
/// @details This front end examines the opcode string and routes to the
///          specialised lowering helper for each supported instruction.  The
///          design keeps opcode handling explicit so Phase A bring-up can focus
///          on a narrow subset before broadening coverage.
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

/// @brief Materialise PX_COPY instructions that ferry block arguments.
/// @details IL blocks may pass values to successors via parameter lists.  The
///          helper records the mapping between outgoing values and destination
///          virtual registers, constructing PX_COPY instructions that the
///          register allocator will later expand into moves.
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

/// @brief Translate an IL function into Machine IR.
/// @details The lowering pass first creates Machine IR blocks that mirror the
///          IL layout, establishes parameter virtual registers, and then walks
///          each instruction to emit Machine IR equivalents.  Terminator edges
///          receive PX_COPY records so block arguments remain explicit in the
///          resulting program.
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
