//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Bridges the temporary IL structures used by the bring-up harness into the
// provisional Machine IR form consumed by the x86-64 backend.  The adapter is
// now intentionally slim: it walks blocks, consults the rule registry, and lets
// each rule append instructions through MIRBuilder.  State management and
// literal materialisation helpers continue to live here so rules can remain
// focused on opcode semantics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Rule-driven IL-to-MIR lowering entry points for the x86-64 backend.
/// @details Provides the MIRBuilder façade used by individual rules as well as
///          the top-level LowerILToMIR::lower dispatch loop.  Helper routines
///          such as virtual register allocation and literal handling remain in
///          this translation unit to avoid coupling rules to adapter internals.

#include "LowerILToMIR.hpp"

#include "LoweringRules.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace viper::codegen::x64
{

namespace
{
[[nodiscard]] Operand cloneOperand(const Operand &operand)
{
    return operand;
}

void reportNoRule(const ILInstr &instr)
{
    (void)instr;
#ifndef NDEBUG
    assert(false && "No lowering rule matched instruction");
#endif
}

} // namespace

// -----------------------------------------------------------------------------
// MIRBuilder façade
// -----------------------------------------------------------------------------

MIRBuilder::MIRBuilder(LowerILToMIR &lower, MBasicBlock &block) noexcept
    : lower_{&lower}, block_{&block}
{
}

MBasicBlock &MIRBuilder::block() noexcept
{
    assert(block_ && "MIRBuilder missing block");
    return *block_;
}

const MBasicBlock &MIRBuilder::block() const noexcept
{
    assert(block_ && "MIRBuilder missing block");
    return *block_;
}

LowerILToMIR &MIRBuilder::lower() noexcept
{
    assert(lower_ && "MIRBuilder missing adapter");
    return *lower_;
}

const LowerILToMIR &MIRBuilder::lower() const noexcept
{
    assert(lower_ && "MIRBuilder missing adapter");
    return *lower_;
}

const TargetInfo &MIRBuilder::target() const noexcept
{
    assert(lower_ && lower_->target_ && "Target info unavailable");
    return *lower_->target_;
}

AsmEmitter::RoDataPool &MIRBuilder::roData() const noexcept
{
    assert(lower_ && lower_->roDataPool_ && "RoData pool unavailable");
    return *lower_->roDataPool_;
}

RegClass MIRBuilder::regClassFor(ILValue::Kind kind) const noexcept
{
    assert(lower_);
    return LowerILToMIR::regClassFor(kind);
}

VReg MIRBuilder::ensureVReg(int id, ILValue::Kind kind)
{
    assert(lower_);
    return lower_->ensureVReg(id, kind);
}

VReg MIRBuilder::makeTempVReg(RegClass cls)
{
    assert(lower_);
    return lower_->makeTempVReg(cls);
}

Operand MIRBuilder::makeOperandForValue(const ILValue &value, RegClass cls)
{
    assert(lower_ && block_);
    return lower_->makeOperandForValue(*block_, value, cls);
}

Operand MIRBuilder::makeLabelOperand(const ILValue &value) const
{
    assert(lower_);
    return lower_->makeLabelOperand(value);
}

bool MIRBuilder::isImmediate(const ILValue &value) const noexcept
{
    assert(lower_);
    return lower_->isImmediate(value);
}

void MIRBuilder::append(MInstr instr)
{
    assert(block_);
    block_->append(std::move(instr));
}

void MIRBuilder::recordCallPlan(CallLoweringPlan plan)
{
    assert(lower_);
    lower_->callPlans_.push_back(std::move(plan));
}

// -----------------------------------------------------------------------------
// Adapter core
// -----------------------------------------------------------------------------

LowerILToMIR::LowerILToMIR(const TargetInfo &target, AsmEmitter::RoDataPool &roData) noexcept
    : target_{&target}, roDataPool_{&roData}
{
}

const std::vector<CallLoweringPlan> &LowerILToMIR::callPlans() const noexcept
{
    return callPlans_;
}

void LowerILToMIR::resetFunctionState()
{
    nextVReg_ = 1U;
    valueToVReg_.clear();
    blockInfo_.clear();
    callPlans_.clear();
}

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
        case ILValue::Kind::STR:
            return RegClass::GPR;
    }
    return RegClass::GPR;
}

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

VReg LowerILToMIR::makeTempVReg(RegClass cls)
{
    return VReg{nextVReg_++, cls};
}

bool LowerILToMIR::isImmediate(const ILValue &value) const noexcept
{
    return value.id < 0;
}

Operand LowerILToMIR::makeLabelOperand(const ILValue &value) const
{
    assert(value.kind == ILValue::Kind::LABEL && "label operand expected");
    return x64::makeLabelOperand(value.label);
}

Operand LowerILToMIR::makeOperandForValue(MBasicBlock &block,
                                          const ILValue &value,
                                          RegClass cls)
{
    if (value.kind == ILValue::Kind::LABEL)
    {
        return makeLabelOperand(value);
    }

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
            assert(cls == RegClass::XMM && "f64 operands must target XMM registers");
            assert(roDataPool_ && "RoData pool unavailable for f64 literals");
            const int poolIndex = roDataPool_->addF64Literal(value.f64);
            const std::string label = roDataPool_->f64Label(poolIndex);
            const VReg temp = makeTempVReg(RegClass::XMM);
            Operand tempOperand = makeVRegOperand(temp.cls, temp.id);
            const Operand ripOperand = makeRipLabelOperand(label);
            block.append(MInstr::make(MOpcode::MOVSDrm,
                                      std::vector<Operand>{cloneOperand(tempOperand), ripOperand}));
            return tempOperand;
        }
        case ILValue::Kind::STR:
        {
            assert(cls == RegClass::GPR && "string literals expect GPR destinations");
            assert(roDataPool_ && "RoData pool unavailable for string literals");
            assert(target_ && "Target info unavailable for string literal lowering");

            std::string literalBytes = value.str;
            const auto requestedLen = static_cast<std::size_t>(value.strLen);
            if (literalBytes.size() != requestedLen)
            {
                literalBytes.resize(requestedLen);
            }

            const int poolIndex = roDataPool_->addStringLiteral(std::move(literalBytes));
            const std::string label = roDataPool_->stringLabel(poolIndex);
            const auto literalLen = roDataPool_->stringByteLength(poolIndex);
            assert(literalLen <=
                   static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()));

            const Operand ripOperand = makeRipLabelOperand(label);
            const VReg ptrTmp = makeTempVReg(RegClass::GPR);
            const Operand ptrTmpOp = makeVRegOperand(ptrTmp.cls, ptrTmp.id);
            block.append(MInstr::make(MOpcode::LEA,
                                      std::vector<Operand>{cloneOperand(ptrTmpOp), ripOperand}));

            const Operand ptrArg =
                makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(target_->intArgOrder[0]));
            const Operand lenArg =
                makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(target_->intArgOrder[1]));

            block.append(
                MInstr::make(MOpcode::MOVrr,
                             std::vector<Operand>{cloneOperand(ptrArg), cloneOperand(ptrTmpOp)}));

            const auto lenImm = static_cast<int64_t>(literalLen);
            block.append(
                MInstr::make(MOpcode::MOVri,
                             std::vector<Operand>{cloneOperand(lenArg), makeImmOperand(lenImm)}));

            const Operand callTarget = x64::makeLabelOperand(std::string{"rt_str_from_lit"});
            block.append(MInstr::make(MOpcode::CALL, std::vector<Operand>{callTarget}));

            const VReg result = makeTempVReg(RegClass::GPR);
            const Operand resultOp = makeVRegOperand(result.cls, result.id);
            const Operand retReg =
                makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(target_->intReturnReg));
            block.append(
                MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(resultOp), retReg}));
            return resultOp;
        }
        case ILValue::Kind::LABEL:
            break;
    }
    return makeImmOperand(0);
}

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

    const auto &rules = viper_get_lowering_rules();
    (void)rules; // keep static initialisation local to this TU.

    for (std::size_t idx = 0; idx < func.blocks.size(); ++idx)
    {
        const auto &ilBlock = func.blocks[idx];
        auto &mirBlock = result.blocks[idx];
        MIRBuilder builder{*this, mirBlock};

        for (const auto &instr : ilBlock.instrs)
        {
            const LoweringRule *rule = viper_select_rule(instr);
            if (!rule)
            {
                reportNoRule(instr);
                continue;
            }
            rule->emit(instr, builder);
        }

        emitEdgeCopies(ilBlock, mirBlock);
    }

    return result;
}

} // namespace viper::codegen::x64
