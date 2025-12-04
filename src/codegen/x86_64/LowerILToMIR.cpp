//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/LowerILToMIR.cpp
// Purpose: Bridge IL produced by the compiler front-end into Machine IR consumed
//          by the x86-64 backend using declarative lowering rules.
// Key invariants: Lowering must preserve SSA identities, emit deterministic
//                 basic-block ordering, and leave the adapter's internal caches
//                 consistent for subsequent functions.  Helper routines manage
//                 literal materialisation and virtual-register allocation so rule
//                 implementations remain stateless.
// Ownership/Lifetime: The adapter owns transient lowering state per function
//                     and writes into caller-owned Machine IR structures.
// Links: docs/architecture.md#codegen, src/codegen/x86_64/LowerILToMIR.hpp
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
#include "OperandUtils.hpp"
#include "Unsupported.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace viper::codegen::x64
{

namespace
{

/// @brief Report an error when no lowering rule matches an instruction.
/// @details Throws an exception to signal the unsupported instruction in both
///          debug and release builds. This ensures invalid IL is never silently
///          ignored.
/// @param instr Instruction that failed to match any rule.
[[noreturn]] void reportNoRule(const ILInstr &instr)
{
    std::string msg = "No lowering rule matched instruction: ";
    msg += instr.opcode;
    phaseAUnsupported(msg.c_str());
}

} // namespace

// -----------------------------------------------------------------------------
// MIRBuilder façade
// -----------------------------------------------------------------------------

/// @brief Construct a builder facade that emits into a specific MIR block.
/// @param lower Adapter that provides lowering utilities and state.
/// @param block Machine IR block that will receive emitted instructions.
MIRBuilder::MIRBuilder(LowerILToMIR &lower, MBasicBlock &block) noexcept
    : lower_{&lower}, block_{&block}
{
}

/// @brief Access the mutable machine block being populated.
/// @return Reference to the target machine block.
MBasicBlock &MIRBuilder::block() noexcept
{
    assert(block_ && "MIRBuilder missing block");
    return *block_;
}

/// @brief Access the machine block being populated (const view).
/// @return Const reference to the target machine block.
const MBasicBlock &MIRBuilder::block() const noexcept
{
    assert(block_ && "MIRBuilder missing block");
    return *block_;
}

/// @brief Access the owning adapter for helper services.
/// @return Reference to the lowering adapter.
LowerILToMIR &MIRBuilder::lower() noexcept
{
    assert(lower_ && "MIRBuilder missing adapter");
    return *lower_;
}

/// @brief Access the owning adapter (const view).
/// @return Const reference to the lowering adapter.
const LowerILToMIR &MIRBuilder::lower() const noexcept
{
    assert(lower_ && "MIRBuilder missing adapter");
    return *lower_;
}

/// @brief Retrieve target information describing registers and calling
///        conventions.
/// @return Const reference to the target description.
const TargetInfo &MIRBuilder::target() const noexcept
{
    assert(lower_ && lower_->target_ && "Target info unavailable");
    return *lower_->target_;
}

/// @brief Access the read-only data pool used to materialise literals.
/// @return Reference to the shared rodata pool.
AsmEmitter::RoDataPool &MIRBuilder::roData() const noexcept
{
    assert(lower_ && lower_->roDataPool_ && "RoData pool unavailable");
    return *lower_->roDataPool_;
}

/// @brief Map an IL value kind to a machine register class.
/// @param kind IL value classification (integer, float, pointer, etc.).
/// @return Register class that should represent the value.
RegClass MIRBuilder::regClassFor(ILValue::Kind kind) const noexcept
{
    assert(lower_);
    return LowerILToMIR::regClassFor(kind);
}

/// @brief Ensure an SSA identifier has a materialised virtual register.
/// @details Delegates to the adapter to allocate or reuse the register mapping.
/// @param id SSA identifier from the IL.
/// @param kind IL value kind associated with the identifier.
/// @return Virtual register assigned to the identifier.
VReg MIRBuilder::ensureVReg(int id, ILValue::Kind kind)
{
    assert(lower_);
    return lower_->ensureVReg(id, kind);
}

/// @brief Allocate a fresh temporary virtual register.
/// @param cls Register class to assign to the new temporary.
/// @return Newly allocated virtual register.
VReg MIRBuilder::makeTempVReg(RegClass cls)
{
    assert(lower_);
    return lower_->makeTempVReg(cls);
}

/// @brief Convert an IL value into a machine operand, materialising literals as needed.
/// @param value IL value to translate.
/// @param cls Preferred register class when a temporary must be created.
/// @return Machine operand referencing the value.
Operand MIRBuilder::makeOperandForValue(const ILValue &value, RegClass cls)
{
    assert(lower_ && block_);
    return lower_->makeOperandForValue(*block_, value, cls);
}

/// @brief Translate a label IL value into a machine operand.
/// @param value IL label value.
/// @return Machine operand referencing the label.
Operand MIRBuilder::makeLabelOperand(const ILValue &value) const
{
    assert(lower_);
    return lower_->makeLabelOperand(value);
}

/// @brief Determine whether an IL value encodes a literal immediate.
/// @param value IL value to inspect.
/// @return True when the value should be emitted as an immediate operand.
bool MIRBuilder::isImmediate(const ILValue &value) const noexcept
{
    assert(lower_);
    return lower_->isImmediate(value);
}

/// @brief Append a machine instruction to the current block.
/// @param instr Machine instruction to insert.
void MIRBuilder::append(MInstr instr)
{
    assert(block_);
    block_->append(std::move(instr));
}

/// @brief Record a call-lowering plan produced by a rule.
/// @param plan Plan describing register shuffles and call conventions.
void MIRBuilder::recordCallPlan(CallLoweringPlan plan)
{
    assert(lower_);
    lower_->callPlans_.push_back(std::move(plan));
}

// -----------------------------------------------------------------------------
// Adapter core
// -----------------------------------------------------------------------------

/// @brief Construct the lowering adapter with target configuration and rodata pool.
/// @param target Target description including register assignment details.
/// @param roData Read-only data pool used for literal materialisation.
LowerILToMIR::LowerILToMIR(const TargetInfo &target, AsmEmitter::RoDataPool &roData) noexcept
    : target_{&target}, roDataPool_{&roData}
{
}

/// @brief Expose the call-lowering plans accumulated during lowering.
/// @return Reference to the vector of plans emitted by call rules.
const std::vector<CallLoweringPlan> &LowerILToMIR::callPlans() const noexcept
{
    return callPlans_;
}

/// @brief Reset per-function caches before lowering a new function.
/// @details Clears virtual register assignments, block metadata, and pending
///          call plans so state from the previous function does not leak.
void LowerILToMIR::resetFunctionState()
{
    nextVReg_ = 1U;
    valueToVReg_.clear();
    blockInfo_.clear();
    callPlans_.clear();
}

/// @brief Map an IL value kind to a machine register class for the target.
/// @param kind IL value classification.
/// @return Register class capable of representing the value.
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

/// @brief Ensure an SSA identifier has an assigned virtual register.
/// @details Allocates a new register when necessary and verifies that repeated
///          requests agree on the register class.
/// @param id SSA identifier number.
/// @param kind IL value kind associated with @p id.
/// @return Virtual register descriptor for the identifier.
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

/// @brief Allocate a fresh temporary virtual register owned by the adapter.
/// @param cls Register class assigned to the temporary.
/// @return Newly allocated virtual register descriptor.
VReg LowerILToMIR::makeTempVReg(RegClass cls)
{
    return VReg{nextVReg_++, cls};
}

/// @brief Determine whether an IL value should be emitted as an immediate.
/// @param value IL value candidate.
/// @return True when the value encodes a literal rather than an SSA id.
bool LowerILToMIR::isImmediate(const ILValue &value) const noexcept
{
    return value.id < 0;
}

/// @brief Translate an IL label into a machine operand.
/// @param value Label-valued IL operand.
/// @return Machine operand referencing the label target.
Operand LowerILToMIR::makeLabelOperand(const ILValue &value) const
{
    assert(value.kind == ILValue::Kind::LABEL && "label operand expected");
    return x64::makeLabelOperand(value.label);
}

/// @brief Convert an IL value into an operand for the current machine block.
/// @details Handles literals by materialising loads into the provided block and
///          delegates register mapping for SSA identifiers.
/// @param block Machine block receiving any helper instructions.
/// @param value IL value to translate.
/// @param cls Preferred register class when literals must be materialised.
/// @return Machine operand representing the value.
Operand LowerILToMIR::makeOperandForValue(MBasicBlock &block, const ILValue &value, RegClass cls)
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

/// @brief Emit PX_COPY instructions to satisfy block parameter semantics.
/// @details Inserts copies on outgoing edges so successor block parameters have
///          the expected values.  Operates after each block's instructions are
///          lowered to ensure value mappings exist.
/// @param source IL block providing edge metadata.
/// @param block Machine block receiving the copies.
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

/// @brief Lower an IL function into machine IR using declarative rules.
/// @details Resets per-function state, creates machine blocks, invokes the rule
///          dispatcher for every instruction, and emits block-parameter copies.
/// @param func IL function to lower.
/// @return Machine function containing the lowered instructions.
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

        info.paramVRegs.reserve(ilBlock.paramIds.size());
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
