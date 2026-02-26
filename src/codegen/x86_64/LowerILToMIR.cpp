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
        // Entry block parameters are now handled at function entry via MOV instructions
        // that copy from physical argument registers to virtual registers. This ensures
        // the values are properly preserved across function calls.
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
            block.append(MInstr::make(MOpcode::MOVSDmr,
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

            // LEA string address into temp vreg
            const Operand ripOperand = makeRipLabelOperand(label);
            const VReg ptrTmp = makeTempVReg(RegClass::GPR);
            const Operand ptrTmpOp = makeVRegOperand(ptrTmp.cls, ptrTmp.id);
            block.append(MInstr::make(MOpcode::LEA,
                                      std::vector<Operand>{cloneOperand(ptrTmpOp), ripOperand}));

            // Create a CallLoweringPlan for rt_str_from_lit(ptr, len)
            // This ensures the call goes through the proper argument lowering mechanism
            CallLoweringPlan plan{};
            plan.calleeLabel = "rt_str_from_lit";

            // First arg: ptr vreg
            CallArg ptrArg{};
            ptrArg.kind = CallArg::GPR;
            ptrArg.vreg = ptrTmp.id;
            ptrArg.isImm = false;
            plan.args.push_back(ptrArg);

            // Second arg: length immediate
            CallArg lenArg{};
            lenArg.kind = CallArg::GPR;
            lenArg.isImm = true;
            lenArg.imm = static_cast<int64_t>(literalLen);
            plan.args.push_back(lenArg);

            // Record the plan so lowerPendingCalls will set up args properly
            callPlans_.push_back(std::move(plan));

            // Emit the CALL (argument setup will be inserted by lowerPendingCalls)
            const Operand callTarget = x64::makeLabelOperand(std::string{"rt_str_from_lit"});
            block.append(MInstr::make(MOpcode::CALL, std::vector<Operand>{callTarget}));

            // Move result from return register to temp vreg
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
        block.label = ".L_" + func.name + "_" + ilBlock.name;

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

    // Build a map from entry block parameter IDs to their ABI physical registers
    // or stack offsets for stack-passed parameters
    struct StackParam
    {
        int paramId;
        int32_t offset;
        ILValue::Kind kind;
    };

    std::unordered_map<int, Operand> entryParamToPhysReg{};
    std::vector<StackParam> stackParams{};
    if (!func.blocks.empty() && !func.blocks[0].paramIds.empty())
    {
        const auto &entryParams = func.blocks[0];
        std::size_t gprArgIdx = 0;
        std::size_t xmmArgIdx = 0;
        std::size_t stackArgIdx = 0;
        for (std::size_t p = 0;
             p < entryParams.paramIds.size() && p < entryParams.paramKinds.size();
             ++p)
        {
            const int paramId = entryParams.paramIds[p];
            const auto kind = entryParams.paramKinds[p];
            if (paramId < 0)
            {
                continue;
            }
            const RegClass cls = regClassFor(kind);
            if (cls == RegClass::XMM)
            {
                if (xmmArgIdx < target_->maxFPArgs)
                {
                    const PhysReg argReg = target_->f64ArgOrder[xmmArgIdx++];
                    entryParamToPhysReg[paramId] =
                        makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(argReg));
                }
                else
                {
                    // Stack-passed XMM argument.
                    // Offset from RBP after standard prologue:
                    //   SysV AMD64:  16 + stackArgIdx*8  (8 saved RBP + 8 return address)
                    //   Windows x64: shadowSpace + 16 + stackArgIdx*8  (= 48 + stackArgIdx*8)
                    //                The 32-byte shadow space lives between the return address
                    //                and the first stack-passed argument in the caller frame.
                    const int32_t offset = static_cast<int32_t>(target_->shadowSpace) + 16 +
                                           static_cast<int32_t>(stackArgIdx * 8);
                    stackParams.push_back({paramId, offset, kind});
                    ++stackArgIdx;
                }
            }
            else
            {
                if (gprArgIdx < target_->maxGPRArgs)
                {
                    const PhysReg argReg = target_->intArgOrder[gprArgIdx++];
                    entryParamToPhysReg[paramId] =
                        makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(argReg));
                }
                else
                {
                    // Stack-passed GPR argument.
                    // SysV AMD64:  16 + stackArgIdx*8
                    // Windows x64: shadowSpace + 16 + stackArgIdx*8
                    const int32_t offset = static_cast<int32_t>(target_->shadowSpace) + 16 +
                                           static_cast<int32_t>(stackArgIdx * 8);
                    stackParams.push_back({paramId, offset, kind});
                    ++stackArgIdx;
                }
            }
        }
    }
    // NOTE: We intentionally do NOT set entryParamToPhysReg_ here.
    // Entry parameters arrive in caller-saved registers (RCX, RDX, R8, R9 on Windows;
    // RDI, RSI, RDX, RCX, R8, R9 on SysV). If we return these physical registers
    // directly in makeOperandForValue(), they will be clobbered by any function
    // call before the parameter is used. Instead, we emit MOV instructions at
    // function entry to copy parameters from physical registers to virtual registers,
    // which the register allocator will properly manage (spilling across calls).

    // Emit moves for register-passed parameters at the start of the entry block
    if (!entryParamToPhysReg.empty() && !result.blocks.empty())
    {
        auto &entryBlock = result.blocks[0];
        for (const auto &[paramId, physOp] : entryParamToPhysReg)
        {
            // Find the kind for this parameter
            const auto &entryParams = func.blocks[0];
            ILValue::Kind kind = ILValue::Kind::I64; // default
            for (std::size_t p = 0; p < entryParams.paramIds.size(); ++p)
            {
                if (entryParams.paramIds[p] == paramId && p < entryParams.paramKinds.size())
                {
                    kind = entryParams.paramKinds[p];
                    break;
                }
            }

            // Create a vreg and emit MOV from physical register to vreg
            const VReg vreg = ensureVReg(paramId, kind);
            const Operand dest = makeVRegOperand(vreg.cls, vreg.id);
            if (vreg.cls == RegClass::XMM)
            {
                entryBlock.instructions.push_back(MInstr::make(MOpcode::MOVSDrr, {dest, physOp}));
            }
            else
            {
                entryBlock.instructions.push_back(MInstr::make(MOpcode::MOVrr, {dest, physOp}));
            }
        }
    }

    // Emit loads for stack-passed parameters at the start of the entry block
    if (!stackParams.empty() && !result.blocks.empty())
    {
        auto &entryBlock = result.blocks[0];
        for (const auto &sp : stackParams)
        {
            // Create a vreg for this parameter and emit a load from stack
            const VReg vreg = ensureVReg(sp.paramId, sp.kind);
            const Operand dest = makeVRegOperand(vreg.cls, vreg.id);
            const Operand src = makeMemOperand(makePhysBase(PhysReg::RBP), sp.offset);
            // Emit MOVmr to load from stack into vreg
            if (vreg.cls == RegClass::XMM)
            {
                entryBlock.instructions.push_back(MInstr::make(MOpcode::MOVSDmr, {dest, src}));
            }
            else
            {
                entryBlock.instructions.push_back(MInstr::make(MOpcode::MOVmr, {dest, src}));
            }
        }
    }

    for (std::size_t idx = 0; idx < func.blocks.size(); ++idx)
    {
        const auto &ilBlock = func.blocks[idx];
        auto &mirBlock = result.blocks[idx];
        MIRBuilder builder{*this, mirBlock};

        for (std::size_t instrIdx = 0; instrIdx < ilBlock.instrs.size(); ++instrIdx)
        {
            const auto &instr = ilBlock.instrs[instrIdx];

            // Detect terminator instructions and emit edge copies BEFORE the terminator.
            // This ensures block arguments are passed correctly before the branch.
            const bool isTerminator = instr.opcode == "br" || instr.opcode == "cbr" ||
                                      instr.opcode == "ret" || instr.opcode == "switch_i32";
            if (isTerminator)
            {
                emitEdgeCopies(ilBlock, mirBlock);
            }

            const LoweringRule *rule = viper_select_rule(instr);
            if (!rule)
            {
                reportNoRule(instr);
            }
            rule->emit(instr, builder);
        }
    }

    return result;
}

} // namespace viper::codegen::x64
