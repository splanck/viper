//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Defines the IL→MIR adapter that lowers provisional IL structures into the
// Machine IR format consumed by the x86-64 backend.  The translation unit owns
// the state required to materialise virtual registers, emit machine blocks, and
// collect call-lowering metadata so later passes can finish code generation.
// The implementation focuses on clarity over cleverness so future opcode
// support can be added incrementally without unravelling intricate dispatch
// tables or register allocation hacks.
//
// Invariants:
//   • Each IL SSA identifier maps to exactly one virtual register whose class
//     mirrors the source value's kind.
//   • Block parameters are reified through PX_COPY pairs to keep control-flow
//     edges explicit in the emitted MIR.
//   • Literal operands are materialised deterministically to guarantee repeatable
//     code generation across runs and toolchains.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief IL-to-MIR lowering adapter for the x86-64 backend.
/// @details The lowering bridge walks IL functions, creates the corresponding
///          machine-level blocks, selects instruction patterns, and records the
///          auxiliary state (virtual registers, call plans, literal pools) that
///          the backend expects.  The helper operates in a single pass per IL
///          function and intentionally keeps side effects local to the adapter so
///          it can be reused by tools that need lightweight code generation.

#include "LowerILToMIR.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace viper::codegen::x64
{

namespace
{
/// @brief Produce an independent copy of a Machine IR operand.
/// @details Operands are lightweight value types, yet many helper routines store
///          them in vectors that expect to own their payloads.  Cloning via this
///          helper makes the intent explicit and keeps call sites symmetric so
///          future operand kinds can hook custom copy behaviour if needed.
[[nodiscard]] Operand cloneOperand(const Operand &operand)
{
    return operand;
}

/// @brief Translate an integer-compare opcode mnemonic into a MIR condition id.
/// @details The lowering pipeline emits an integer compare pseudo-op with an
///          explicit condition operand.  This helper recognises the textual IL
///          opcode (e.g., `icmp_eq`) and returns the backend's numeric condition
///          code so the caller can materialise the appropriate SETcc form.  When
///          the opcode is not an integer compare the function returns
///          `std::nullopt` so the caller can fall through to other lowering
///          strategies.
[[nodiscard]] std::optional<int> icmpConditionCode(std::string_view opcode) noexcept
{
    if (!opcode.starts_with("icmp_"))
    {
        return std::nullopt;
    }

    const std::string_view suffix = opcode.substr(5);
    if (suffix == "eq")
    {
        return 0;
    }
    if (suffix == "ne")
    {
        return 1;
    }
    if (suffix == "slt")
    {
        return 2;
    }
    if (suffix == "sle")
    {
        return 3;
    }
    if (suffix == "sgt")
    {
        return 4;
    }
    if (suffix == "sge")
    {
        return 5;
    }
    if (suffix == "ugt")
    {
        return 6;
    }
    if (suffix == "uge")
    {
        return 7;
    }
    if (suffix == "ult")
    {
        return 8;
    }
    if (suffix == "ule")
    {
        return 9;
    }
    return std::nullopt;
}

/// @brief Map a floating-point compare opcode to its MIR condition id.
/// @details Floating-point comparisons in the provisional MIR dialect also rely
///          on small integer condition codes.  This helper mirrors
///          @ref icmpConditionCode but only handles the `fcmp_*` family, allowing
///          the main lowering routine to reuse the same selection logic for both
///          integer and floating-point comparisons.
[[nodiscard]] std::optional<int> fcmpConditionCode(std::string_view opcode) noexcept
{
    if (!opcode.starts_with("fcmp_"))
    {
        return std::nullopt;
    }

    const std::string_view suffix = opcode.substr(5);
    if (suffix == "eq")
    {
        return 0;
    }
    if (suffix == "ne")
    {
        return 1;
    }
    if (suffix == "lt")
    {
        return 2;
    }
    if (suffix == "le")
    {
        return 3;
    }
    if (suffix == "gt")
    {
        return 4;
    }
    if (suffix == "ge")
    {
        return 5;
    }
    return std::nullopt;
}
} // namespace

/// @brief Construct a lowering adapter bound to the backend description.
/// @details The adapter keeps lightweight pointers to the target information and
///          read-only data pool so later lowering steps can materialise literals
///          or consult calling-convention metadata without repeated parameter
///          threading.  No heavy initialisation occurs here, keeping the
///          constructor cheap for tooling that instantiates transient adapters.
LowerILToMIR::LowerILToMIR(const TargetInfo &target, AsmEmitter::RoDataPool &roData) noexcept
    : target_{&target}, roDataPool_{&roData}
{
}

/// @brief Expose the call-lowering plans captured during the last lowering run.
/// @details Each lowered call records how its arguments map to registers or the
///          stack.  Back-end passes read this vector after @ref lower to emit ABI
///          compliant prologues/epilogues or to schedule call fixups.
const std::vector<CallLoweringPlan> &LowerILToMIR::callPlans() const noexcept
{
    return callPlans_;
}

/// @brief Reset per-function state before lowering a new IL function.
/// @details Clearing the register map, block metadata, and call plan cache keeps
///          the adapter reusable across many invocations without leaking stale
///          state from the previous function.
void LowerILToMIR::resetFunctionState()
{
    nextVReg_ = 1U;
    valueToVReg_.clear();
    blockInfo_.clear();
    callPlans_.clear();
}

/// @brief Determine the register class required for a given IL value kind.
/// @details Integer-like values and pointers map to general-purpose registers
///          while floating-point values map to XMM registers.  Other kinds are
///          conservatively routed through GPRs until specialised support lands.
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

/// @brief Ensure an SSA identifier owns a virtual register descriptor.
/// @details Looks up the id in @ref valueToVReg_ and allocates a new entry when
///          needed.  Reuses the existing mapping when present while asserting the
///          register class stays consistent across uses.
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

/// @brief Allocate a scratch virtual register of the requested class.
/// @details Temporaries are reserved for literal materialisation or complex
///          addressing modes.  The helper increments @ref nextVReg_ and returns a
///          descriptor tagged with the provided register class.
VReg LowerILToMIR::makeTempVReg(RegClass cls)
{
    return VReg{nextVReg_++, cls};
}

/// @brief Detect whether an IL value encodes an immediate literal.
/// @details The provisional IL format uses negative ids for literals.  Recognising
///          those values allows the lowering helpers to emit `OpImm` operands
///          without routing through the register map.
bool LowerILToMIR::isImmediate(const ILValue &value) const noexcept
{
    return value.id < 0;
}

/// @brief Convert an IL value into a MIR operand appropriate for @p block.
/// @details Handles label references, SSA identifiers, integer literals, floating
///          literals (materialised through the read-only data pool), and strings.
///          When the value type is unsupported the helper returns a zero
///          immediate so callers fail deterministically.
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

/// @brief Transform an IL label value into a MIR label operand.
/// @details Performs a defensive assertion on the value kind and then delegates
///          to the shared x86-64 operand builder so label formatting stays
///          consistent throughout the backend.
Operand LowerILToMIR::makeLabelOperand(const ILValue &value) const
{
    assert(value.kind == ILValue::Kind::LABEL && "label operand expected");
    return x64::makeLabelOperand(value.label);
}

/// @brief Lower a generic binary arithmetic or bitwise instruction.
/// @details Evaluates both operands, materialises them into registers when
///          necessary, and chooses between the register-register and
///          register-immediate forms depending on operand kinds and backend
///          constraints.  When immediates exceed 32 bits the helper falls back to
///          temporary registers to keep the generated MIR valid.
/// @param instr IL instruction describing the binary operation.
/// @param block MIR block receiving the generated instructions.
/// @param opcRR Register-register opcode emitted when both operands reside in
///        registers.
/// @param opcRI Register-immediate opcode used when @p rhs fits the immediate
///        encoding.
/// @param cls Register class expected for the operation.
/// @param requireImm32 Whether the immediate form is limited to 32-bit values.
void LowerILToMIR::lowerBinary(const ILInstr &instr,
                               MBasicBlock &block,
                               MOpcode opcRR,
                               MOpcode opcRI,
                               RegClass cls,
                               bool requireImm32)
{
    if (instr.resultId < 0 || instr.ops.size() < 2)
    {
        return;
    }

    const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand lhs = makeOperandForValue(block, instr.ops[0], cls);
    Operand rhs = makeOperandForValue(block, instr.ops[1], cls);

    if (std::holds_alternative<OpImm>(lhs))
    {
        block.append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{cloneOperand(dest), lhs}));
    }
    else
    {
        block.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), lhs}));
    }

    const auto canUseImm = [&]()
    {
        if (opcRI == opcRR)
        {
            return false;
        }
        const auto *imm = std::get_if<OpImm>(&rhs);
        if (!imm)
        {
            return false;
        }
        if (!requireImm32)
        {
            return true;
        }
        return imm->val >= static_cast<int64_t>(std::numeric_limits<int32_t>::min()) &&
               imm->val <= static_cast<int64_t>(std::numeric_limits<int32_t>::max());
    }();

    if (canUseImm)
    {
        block.append(MInstr::make(opcRI, std::vector<Operand>{cloneOperand(dest), rhs}));
        return;
    }

    const auto materialiseToReg = [this, &block, cls](Operand operand)
    {
        if (std::holds_alternative<OpReg>(operand))
        {
            return operand;
        }

        const VReg tmp = makeTempVReg(cls);
        const Operand tmpOp = makeVRegOperand(tmp.cls, tmp.id);

        if (std::holds_alternative<OpImm>(operand))
        {
            block.append(MInstr::make(
                MOpcode::MOVri, std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else if (std::holds_alternative<OpLabel>(operand) ||
                 std::holds_alternative<OpRipLabel>(operand))
        {
            block.append(MInstr::make(
                MOpcode::LEA, std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else
        {
            block.append(MInstr::make(
                MOpcode::MOVrr, std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
        }

        return tmpOp;
    };

    const Operand rhsReg = materialiseToReg(rhs);
    block.append(
        MInstr::make(opcRR, std::vector<Operand>{cloneOperand(dest), cloneOperand(rhsReg)}));
}

/// @brief Lower a shift instruction with either immediate or register counts.
/// @details Shifts materialise the destination register, then either emit an
///          immediate form (after masking to the architectural width) or move the
///          shift amount into RCX before emitting the register form.  The helper
///          normalises operands so the rest of the lowering pipeline can remain
///          oblivious to x86-64's special-case shift semantics.
void LowerILToMIR::lowerShift(const ILInstr &instr,
                              MBasicBlock &block,
                              MOpcode opcImm,
                              MOpcode opcReg)
{
    if (instr.resultId < 0 || instr.ops.size() < 2)
    {
        return;
    }

    const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    const Operand lhs = makeOperandForValue(block, instr.ops[0], destReg.cls);

    if (std::holds_alternative<OpImm>(lhs))
    {
        block.append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{cloneOperand(dest), lhs}));
    }
    else
    {
        block.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), lhs}));
    }

    Operand rhs = makeOperandForValue(block, instr.ops[1], destReg.cls);
    if (auto *imm = std::get_if<OpImm>(&rhs))
    {
        const auto masked = static_cast<int64_t>(static_cast<std::uint8_t>(imm->val));
        block.append(
            MInstr::make(opcImm, std::vector<Operand>{cloneOperand(dest), makeImmOperand(masked)}));
        return;
    }

    const Operand clOperand =
        makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RCX));

    bool alreadyCl = false;
    if (const auto *reg = std::get_if<OpReg>(&rhs))
    {
        alreadyCl = reg->isPhys && reg->cls == RegClass::GPR &&
                    reg->idOrPhys == static_cast<uint16_t>(PhysReg::RCX);
    }

    if (!alreadyCl)
    {
        block.append(MInstr::make(
            MOpcode::MOVrr, std::vector<Operand>{cloneOperand(clOperand), cloneOperand(rhs)}));
    }

    block.append(
        MInstr::make(opcReg, std::vector<Operand>{cloneOperand(dest), cloneOperand(clOperand)}));
}

/// @brief Lower a compare instruction and optionally materialise its boolean
///        result.
/// @details Emits the appropriate compare opcode for the register class, then
///          synthesises a SETcc into the destination vreg when the IL instruction
///          produces a result.  Condition codes default to @p defaultCond but can
///          be overridden by an immediate third operand on the IL instruction.
void LowerILToMIR::lowerCmp(const ILInstr &instr, MBasicBlock &block, RegClass cls, int defaultCond)
{
    if (instr.ops.size() < 2)
    {
        return;
    }

    int condCode = defaultCond;
    if (instr.ops.size() >= 3)
    {
        const ILValue &condOperand = instr.ops[2];
        if (condOperand.id < 0 &&
            (condOperand.kind == ILValue::Kind::I64 || condOperand.kind == ILValue::Kind::I1))
        {
            condCode = static_cast<int>(condOperand.i64);
        }
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
        block.append(MInstr::make(
            MOpcode::SETcc, std::vector<Operand>{makeImmOperand(condCode), cloneOperand(dest)}));
    }
}

/// @brief Lower a `select` instruction into conditional moves or scalar blends.
/// @details Evaluates all three operands, moves the false value into the
///          destination, and then conditionally overwrites it with the true value
///          based on the condition.  Integer selections leverage CMOV-like logic
///          while floating-point selections use MOVSD to preserve register class
///          semantics.
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
            block.append(MInstr::make(MOpcode::MOVri,
                                      std::vector<Operand>{cloneOperand(cmovSource), trueVal}));
        }

        const bool falseIsImm = std::holds_alternative<OpImm>(falseVal);
        std::vector<Operand> movOperands{};
        movOperands.push_back(cloneOperand(dest));
        movOperands.push_back(cloneOperand(falseVal));
        movOperands.push_back(cloneOperand(cmovSource));
        block.append(
            MInstr::make(falseIsImm ? MOpcode::MOVri : MOpcode::MOVrr, std::move(movOperands)));

        block.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
        block.append(MInstr::make(MOpcode::SETcc,
                                  std::vector<Operand>{makeImmOperand(1), cloneOperand(dest)}));
        return;
    }

    std::vector<Operand> movOperands{};
    movOperands.push_back(cloneOperand(dest));
    movOperands.push_back(cloneOperand(falseVal));
    movOperands.push_back(cloneOperand(trueVal));
    block.append(MInstr::make(MOpcode::MOVSDrr, std::move(movOperands)));

    block.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
    block.append(
        MInstr::make(MOpcode::SETcc, std::vector<Operand>{makeImmOperand(1), cloneOperand(dest)}));
}

/// @brief Lower an unconditional branch into a MIR jump.
/// @details Emits a single JMP instruction targeting the successor label when the
///          IL instruction supplies one.  Instructions without operands are
///          treated as no-ops so the caller can report diagnostics elsewhere.
void LowerILToMIR::lowerBranch(const ILInstr &instr, MBasicBlock &block)
{
    if (instr.ops.empty())
    {
        return;
    }
    block.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(instr.ops[0])}));
}

/// @brief Lower a conditional branch into TEST/JCC form.
/// @details The helper emits a TEST against the condition value, a conditional
///          jump to the true label, and a fall-through jump to the false label.
///          Operands are validated defensively to avoid generating malformed MIR
///          when the IL input is incomplete.
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

/// @brief Lower a return instruction, moving the value into the ABI result
///        register when necessary.
/// @details Materialises the return operand, applies the ABI-specific zero
///          extension for i1 values, and copies the value into the appropriate
///          physical register before emitting a RET.  Missing operands degrade to
///          a bare return.
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

    const auto materialiseToReg = [this, &block](Operand operand, RegClass expectedCls) -> Operand
    {
        if (std::holds_alternative<OpReg>(operand))
        {
            return operand;
        }

        const VReg tmp{nextVReg_++, expectedCls};
        const Operand tmpOp = makeVRegOperand(tmp.cls, tmp.id);

        if (std::holds_alternative<OpImm>(operand))
        {
            block.append(
                MInstr::make(MOpcode::MOVri, {cloneOperand(tmpOp), cloneOperand(operand)}));
        }
        else if (std::holds_alternative<OpMem>(operand))
        {
            const MOpcode loadOpc =
                expectedCls == RegClass::XMM ? MOpcode::MOVSDmr : MOpcode::MOVrr;
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
            block.append(
                MInstr::make(MOpcode::MOVZXrr32, {cloneOperand(zxOp), cloneOperand(srcReg)}));
            srcReg = zxOp;
        }
    }

    if (cls == RegClass::XMM)
    {
        const Operand retReg =
            makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(target_->f64ReturnReg));
        block.append(MInstr::make(MOpcode::MOVSDrr, {retReg, cloneOperand(srcReg)}));
    }
    else
    {
        const Operand retReg =
            makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(target_->intReturnReg));
        block.append(MInstr::make(MOpcode::MOVrr, {retReg, cloneOperand(srcReg)}));
    }

    block.append(MInstr::make(MOpcode::RET, {}));
}

/// @brief Record metadata for a call instruction and emit the CALL opcode.
/// @details Collects argument classes, immediate values, and result
///          expectations into a @ref CallLoweringPlan so later passes can apply
///          ABI lowering.  The MIR emitted here is intentionally minimal: just a
///          direct CALL to the callee label.
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
        [[maybe_unused]] const VReg retReg = ensureVReg(instr.resultId, instr.resultKind);
        if (instr.resultKind == ILValue::Kind::F64)
        {
            plan.returnsF64 = true;
        }
    }

    callPlans_.push_back(plan);
    block.append(MInstr::make(MOpcode::CALL, std::vector<Operand>{makeLabelOperand(instr.ops[0])}));
}

/// @brief Lower a load instruction that reads from a base+offset address.
/// @details Validates the base operand, materialises the destination register,
///          and emits either MOV or MOVSD depending on the register class.  The
///          helper currently assumes simple addressing modes and ignores scaling
///          for brevity.
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

/// @brief Lower a store instruction that writes to a base+offset address.
/// @details Materialises the value operand when necessary and emits the
///          appropriate MOV variant based on operand class.  Non-register values
///          fall back to MOV immediate forms.
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

/// @brief Lower a casting instruction between integer and floating-point types.
/// @details Converts the source operand into the requested register class and
///          emits either a MOV (for identity/constant cases) or the supplied
///          opcode when an actual conversion is required.  Destination classes
///          are currently unused because the MIR pseudo-ops encode the target
///          register file in the opcode itself.
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

/// @brief Dispatch a single IL instruction to the specialised lowering helper.
/// @details Pattern-matches the opcode string and forwards to the corresponding
///          lowering routine.  The set of supported opcodes is intentionally small
///          while the backend is in development; unsupported operations silently
///          fall through so future work can expand coverage incrementally.
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
    if (opc == "and")
    {
        const RegClass cls = regClassFor(instr.resultKind);
        if (cls == RegClass::GPR)
        {
            lowerBinary(instr, block, MOpcode::ANDrr, MOpcode::ANDri, cls, true);
        }
        return;
    }
    if (opc == "or")
    {
        const RegClass cls = regClassFor(instr.resultKind);
        if (cls == RegClass::GPR)
        {
            lowerBinary(instr, block, MOpcode::ORrr, MOpcode::ORri, cls, true);
        }
        return;
    }
    if (opc == "xor")
    {
        const RegClass cls = regClassFor(instr.resultKind);
        if (cls == RegClass::GPR)
        {
            lowerBinary(instr, block, MOpcode::XORrr, MOpcode::XORri, cls, true);
        }
        return;
    }
    if (const auto cond = icmpConditionCode(opc))
    {
        lowerCmp(instr, block, RegClass::GPR, *cond);
        return;
    }
    if (const auto cond = fcmpConditionCode(opc))
    {
        lowerCmp(instr, block, RegClass::XMM, *cond);
        return;
    }
    if (opc == "div" || opc == "sdiv" || opc == "srem" || opc == "udiv" || opc == "urem" ||
        opc == "rem")
    {
        if (instr.resultId < 0 || instr.ops.size() < 2)
        {
            return;
        }

        const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
        const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

        Operand dividend = makeOperandForValue(block, instr.ops[0], RegClass::GPR);
        Operand divisor = makeOperandForValue(block, instr.ops[1], RegClass::GPR);

        const auto materialiseGprReg = [this, &block](const Operand &operand) -> Operand
        {
            if (std::holds_alternative<OpReg>(operand))
            {
                return cloneOperand(operand);
            }

            const VReg tmp = makeTempVReg(RegClass::GPR);
            const Operand tmpOp = makeVRegOperand(tmp.cls, tmp.id);

            if (std::holds_alternative<OpImm>(operand))
            {
                block.append(
                    MInstr::make(MOpcode::MOVri,
                                 std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
            }
            else if (std::holds_alternative<OpLabel>(operand))
            {
                block.append(
                    MInstr::make(MOpcode::LEA,
                                 std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
            }
            else
            {
                block.append(
                    MInstr::make(MOpcode::MOVrr,
                                 std::vector<Operand>{cloneOperand(tmpOp), cloneOperand(operand)}));
            }

            return tmpOp;
        };

        if (!std::holds_alternative<OpReg>(dividend) && !std::holds_alternative<OpImm>(dividend))
        {
            dividend = materialiseGprReg(dividend);
        }

        divisor = materialiseGprReg(divisor);

        const MOpcode pseudo = [opc]()
        {
            if (opc == "div" || opc == "sdiv")
            {
                return MOpcode::DIVS64rr;
            }
            if (opc == "rem" || opc == "srem")
            {
                return MOpcode::REMS64rr;
            }
            if (opc == "udiv")
            {
                return MOpcode::DIVU64rr;
            }
            return MOpcode::REMU64rr;
        }();
        block.append(MInstr::make(pseudo,
                                  std::vector<Operand>{cloneOperand(dest),
                                                       cloneOperand(dividend),
                                                       cloneOperand(divisor)}));
        return;
    }
    if (opc == "shl")
    {
        lowerShift(instr, block, MOpcode::SHLri, MOpcode::SHLrc);
        return;
    }
    if (opc == "lshr")
    {
        lowerShift(instr, block, MOpcode::SHRri, MOpcode::SHRrc);
        return;
    }
    if (opc == "ashr")
    {
        lowerShift(instr, block, MOpcode::SARri, MOpcode::SARrc);
        return;
    }
    if (opc == "cmp")
    {
        const RegClass cls =
            regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind);
        lowerCmp(instr, block, cls, 1);
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
    // TODO: handle additional opcodes.
}

/// @brief Emit PX_COPY instructions for block-parameter edges leaving @p source.
/// @details The MIR representation expects predecessor edges to carry explicit
///          copy pairs for block parameters.  This helper walks the IL edge list,
///          looks up the destination parameter vregs, and appends PX_COPY
///          instructions containing alternating dest/src operands.  Missing value
///          mappings are skipped so partially constructed functions degrade
///          gracefully.
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

/// @brief Lower an entire IL function into the provisional MIR dialect.
/// @details Resets adapter state, creates MIR blocks that mirror the IL layout,
///          lowers each instruction sequentially, and finally emits parameter
///          copies for every outgoing edge.  The returned @ref MFunction owns all
///          emitted blocks and call plans can be queried via @ref callPlans.
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
