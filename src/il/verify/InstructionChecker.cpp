// File: src/il/verify/InstructionChecker.cpp
// Purpose: Implements helpers that validate non-control IL instructions.
// Key invariants: Relies on TypeInference to keep operand types consistent.
// Ownership/Lifetime: Functions operate on caller-provided structures.
// License: MIT (see LICENSE).
// Links: docs/il-spec.md

#include "il/verify/InstructionChecker.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/verify/TypeInference.hpp"
#include "support/diag_expected.hpp"

#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace il::core;

namespace il::verify
{
namespace
{
using il::support::Diag;
using il::support::Expected;
using il::support::Severity;
using il::support::makeError;

/// @brief Format a canonical diagnostic string for an instruction.
/// @param fn Function that owns the instruction.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction that triggered the diagnostic.
/// @param message Additional detail appended to the diagnostic message.
/// @return Fully formatted verifier diagnostic payload.
std::string formatInstrDiag(const Function &fn,
                            const BasicBlock &bb,
                            const Instr &instr,
                            std::string_view message)
{
    std::ostringstream oss;
    oss << fn.name << ":" << bb.label << ": " << makeSnippet(instr);
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

/// @brief Append a warning diagnostic associated with @p instr.
/// @param fn Function supplying context for the diagnostic message.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction that prompted the warning.
/// @param message Human-readable warning text.
/// @param warnings Collection receiving deferred warning diagnostics.
void emitWarning(const Function &fn,
                 const BasicBlock &bb,
                 const Instr &instr,
                 std::string_view message,
                 std::vector<Diag> &warnings)
{
    warnings.push_back(Diag{Severity::Warning, formatInstrDiag(fn, bb, instr, message), instr.loc});
}

/// @brief Validate operand/result arity constraints against opcode metadata.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose signature is being checked.
/// @return Empty on success; otherwise an error diagnostic describing the
///         structural mismatch.
Expected<void> verifyOpcodeSignature_E(const Function &fn,
                                        const BasicBlock &bb,
                                        const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);

    const bool hasResult = instr.result.has_value();
    switch (info.resultArity)
    {
        case ResultArity::None:
            if (hasResult)
                return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "unexpected result"))};
            break;
        case ResultArity::One:
            if (!hasResult)
                return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "missing result"))};
            break;
        case ResultArity::Optional:
            break;
    }

    const size_t operandCount = instr.operands.size();
    const bool variadic = isVariadicOperandCount(info.numOperandsMax);
    if (operandCount < info.numOperandsMin || (!variadic && operandCount > info.numOperandsMax))
    {
        std::ostringstream ss;
        if (info.numOperandsMin == info.numOperandsMax)
        {
            ss << "expected " << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                ss << 's';
        }
        else if (variadic)
        {
            ss << "expected at least " << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                ss << 's';
        }
        else
        {
            ss << "expected between " << static_cast<unsigned>(info.numOperandsMin) << " and "
               << static_cast<unsigned>(info.numOperandsMax) << " operands";
        }
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
    }

    if (instr.labels.size() != info.numSuccessors)
    {
        std::ostringstream ss;
        ss << "expected " << static_cast<unsigned>(info.numSuccessors) << " successor";
        if (info.numSuccessors != 1)
            ss << 's';
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
    }

    if (instr.brArgs.size() > info.numSuccessors)
    {
        std::ostringstream ss;
        ss << "expected at most " << static_cast<unsigned>(info.numSuccessors)
           << " branch argument bundle";
        if (info.numSuccessors != 1)
            ss << 's';
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
    }
    if (!instr.brArgs.empty() && instr.brArgs.size() != info.numSuccessors)
    {
        std::ostringstream ss;
        ss << "expected " << static_cast<unsigned>(info.numSuccessors) << " branch argument bundle";
        if (info.numSuccessors != 1)
            ss << 's';
        ss << ", or none";
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
    }

    return {};
}

/// @brief Require all operands of @p instr to resolve to the requested type
///        kind.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose operands are being validated.
/// @param types Type inference engine that answers operand queries.
/// @param kind Expected operand kind.
/// @return Empty on success; otherwise an error diagnostic when a mismatch is
///         observed.
Expected<void> expectAllOperandType(const Function &fn,
                                    const BasicBlock &bb,
                                    const Instr &instr,
                                    TypeInference &types,
                                    Type::Kind kind)
{
    for (const auto &op : instr.operands)
    {
        if (types.valueType(op).kind != kind)
        {
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "operand type mismatch"))};
        }
    }
    return {};
}

/// @brief Validate allocator instructions for operand and result correctness.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Alloca instruction being verified.
/// @param types Type inference engine for operand/result metadata.
/// @param warnings Warning sink for questionable but allowed patterns.
/// @return Empty on success; otherwise an error diagnostic describing the
///         violated constraint.
Expected<void> checkAlloca_E(const Function &fn,
                             const BasicBlock &bb,
                             const Instr &instr,
                             TypeInference &types,
                             std::vector<Diag> &warnings)
{
    if (instr.operands.empty())
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "missing size operand"))};

    if (types.valueType(instr.operands[0]).kind != Type::Kind::I64)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "size must be i64"))};

    if (instr.operands[0].kind == Value::Kind::ConstInt)
    {
        long long sz = instr.operands[0].i64;
        if (sz < 0)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "negative alloca size"))};
        if (sz > (1LL << 20))
            emitWarning(fn, bb, instr, "huge alloca", warnings);
    }

    types.recordResult(instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Verify binary arithmetic and comparison instructions.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose operands and result are validated.
/// @param types Type inference engine that answers operand queries.
/// @param operandKind Expected kind shared by both operands.
/// @param resultType Result type recorded when validation succeeds.
/// @return Empty on success; otherwise an error diagnostic describing arity or
///         type mismatches.
Expected<void> checkBinary_E(const Function &fn,
                             const BasicBlock &bb,
                             const Instr &instr,
                             TypeInference &types,
                             Type::Kind operandKind,
                             Type resultType)
{
    if (instr.operands.size() < 2)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    if (auto result = expectAllOperandType(fn, bb, instr, types, operandKind); !result)
        return result;

    types.recordResult(instr, resultType);
    return {};
}

/// @brief Verify unary conversions and casts.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose single operand is validated.
/// @param types Type inference engine that answers operand queries.
/// @param operandKind Required operand kind.
/// @param resultType Result type recorded when validation succeeds.
/// @return Empty on success; otherwise an error diagnostic describing arity or
///         type mismatches.
Expected<void> checkUnary_E(const Function &fn,
                            const BasicBlock &bb,
                            const Instr &instr,
                            TypeInference &types,
                            Type::Kind operandKind,
                            Type resultType)
{
    if (instr.operands.empty())
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    if (types.valueType(instr.operands[0]).kind != operandKind)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "operand type mismatch"))};

    types.recordResult(instr, resultType);
    return {};
}

/// @brief Validate pointer arithmetic instructions (GEP).
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr GEP instruction under validation.
/// @param types Type inference engine used for operand queries.
/// @return Empty on success; otherwise an error diagnostic describing missing
///         operands or pointer/index type mismatches.
Expected<void> checkGEP_E(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          TypeInference &types)
{
    if (instr.operands.size() < 2)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    if (types.valueType(instr.operands[0]).kind != Type::Kind::Ptr ||
        types.valueType(instr.operands[1]).kind != Type::Kind::I64)
    {
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "operand type mismatch"))};
    }

    types.recordResult(instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Validate load instructions for pointer and result type correctness.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Load instruction being verified.
/// @param types Type inference engine for operand queries.
/// @return Empty on success; otherwise an error diagnostic describing arity,
///         pointer, or result type violations.
Expected<void> checkLoad_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           TypeInference &types)
{
    if (instr.operands.empty())
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    if (instr.type.kind == Type::Kind::Void)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "void load type"))};

    if (types.valueType(instr.operands[0]).kind != Type::Kind::Ptr)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "pointer type mismatch"))};

    [[maybe_unused]] size_t sz = TypeInference::typeSize(instr.type.kind);
    types.recordResult(instr, instr.type);
    return {};
}

/// @brief Validate store instructions for pointer operand and value typing.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Store instruction being verified.
/// @param types Type inference engine for operand queries.
/// @return Empty on success; otherwise an error diagnostic describing arity,
///         pointer, or stored value violations.
Expected<void> checkStore_E(const Function &fn,
                            const BasicBlock &bb,
                            const Instr &instr,
                            TypeInference &types)
{
    if (instr.operands.size() < 2)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    if (instr.type.kind == Type::Kind::Void)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "void store type"))};

    if (types.valueType(instr.operands[0]).kind != Type::Kind::Ptr)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "pointer type mismatch"))};

    Type valueTy = types.valueType(instr.operands[1]);
    bool isBoolConst = instr.type.kind == Type::Kind::I1 && instr.operands[1].kind == Value::Kind::ConstInt;
    if (isBoolConst)
    {
        long long v = instr.operands[1].i64;
        if (v != 0 && v != 1)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "boolean store expects 0 or 1"))};
    }
    else if (valueTy.kind != instr.type.kind)
    {
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "value type mismatch"))};
    }

    [[maybe_unused]] size_t sz = TypeInference::typeSize(instr.type.kind);
    return {};
}

/// @brief Validate AddrOf instructions address globals and produce pointers.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr AddrOf instruction being verified.
/// @param types Type inference engine for result recording.
/// @return Empty on success; otherwise an error diagnostic when the operand is
///         not a global address.
Expected<void> checkAddrOf_E(const Function &fn,
                             const BasicBlock &bb,
                             const Instr &instr,
                             TypeInference &types)
{
    if (instr.operands.size() != 1 || instr.operands[0].kind != Value::Kind::GlobalAddr)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "operand must be global"))};

    types.recordResult(instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Validate ConstStr instructions reference known string globals.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr ConstStr instruction being verified.
/// @param types Type inference engine for result recording.
/// @return Empty on success; otherwise an error diagnostic when the operand is
///         not a string global.
Expected<void> checkConstStr_E(const Function &fn,
                               const BasicBlock &bb,
                               const Instr &instr,
                               TypeInference &types)
{
    if (instr.operands.size() != 1 || instr.operands[0].kind != Value::Kind::GlobalAddr)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "unknown string global"))};

    types.recordResult(instr, Type(Type::Kind::Str));
    return {};
}

/// @brief Record the result type for ConstNull instructions.
/// @param instr ConstNull instruction being validated.
/// @param types Type inference engine for result recording.
/// @return Always empty because ConstNull has no failure modes.
Expected<void> checkConstNull_E(const Instr &instr, TypeInference &types)
{
    types.recordResult(instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Validate direct calls against extern or function signatures.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Call instruction being verified.
/// @param externs Table of known extern declarations.
/// @param funcs Table of known function definitions.
/// @param types Type inference engine for operand queries and result recording.
/// @return Empty on success; otherwise an error diagnostic describing missing
///         callees, arity mismatches, or argument type violations.
Expected<void> checkCall_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           const std::unordered_map<std::string, const Extern *> &externs,
                           const std::unordered_map<std::string, const Function *> &funcs,
                           TypeInference &types)
{
    const Extern *sig = nullptr;
    const Function *fnSig = nullptr;
    if (auto it = externs.find(instr.callee); it != externs.end())
        sig = it->second;
    else if (auto itF = funcs.find(instr.callee); itF != funcs.end())
        fnSig = itF->second;

    if (!sig && !fnSig)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "unknown callee @" + instr.callee))};

    size_t paramCount = sig ? sig->params.size() : fnSig->params.size();
    if (instr.operands.size() != paramCount)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "call arg count mismatch"))};

    for (size_t i = 0; i < paramCount; ++i)
    {
        Type expected = sig ? sig->params[i] : fnSig->params[i].type;
        if (types.valueType(instr.operands[i]).kind != expected.kind)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "call arg type mismatch"))};
    }

    if (instr.result)
    {
        Type ret = sig ? sig->retType : fnSig->retType;
        types.recordResult(instr, ret);
    }

    return {};
}

/// @brief Default validator that records the declared result type.
/// @param instr Instruction being validated.
/// @param types Type inference engine for result recording.
/// @return Always empty because structural checks handle failures.
Expected<void> checkDefault_E(const Instr &instr, TypeInference &types)
{
    types.recordResult(instr, instr.type);
    return {};
}

/// @brief Dispatch opcode-specific validation for non-control instructions.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction being verified.
/// @param externs Table of known extern declarations.
/// @param funcs Table of known function definitions.
/// @param types Type inference engine for operand queries and result recording.
/// @param warnings Accumulates warning diagnostics emitted during validation.
/// @return Empty on success; otherwise an error diagnostic describing the
///         violated rule.
Expected<void> verifyInstruction_E(const Function &fn,
                                    const BasicBlock &bb,
                                    const Instr &instr,
                                    const std::unordered_map<std::string, const Extern *> &externs,
                                    const std::unordered_map<std::string, const Function *> &funcs,
                                    TypeInference &types,
                                    std::vector<Diag> &warnings)
{
    switch (instr.op)
    {
        case Opcode::Alloca:
            return checkAlloca_E(fn, bb, instr, types, warnings);
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::SDiv:
        case Opcode::UDiv:
        case Opcode::SRem:
        case Opcode::URem:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
            return checkBinary_E(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::I64));
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
            return checkBinary_E(fn, bb, instr, types, Type::Kind::F64, Type(Type::Kind::F64));
        case Opcode::ICmpEq:
        case Opcode::ICmpNe:
        case Opcode::SCmpLT:
        case Opcode::SCmpLE:
        case Opcode::SCmpGT:
        case Opcode::SCmpGE:
        case Opcode::UCmpLT:
        case Opcode::UCmpLE:
        case Opcode::UCmpGT:
        case Opcode::UCmpGE:
            return checkBinary_E(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::I1));
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
            return checkBinary_E(fn, bb, instr, types, Type::Kind::F64, Type(Type::Kind::I1));
        case Opcode::Sitofp:
            return checkUnary_E(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::F64));
        case Opcode::Fptosi:
            return checkUnary_E(fn, bb, instr, types, Type::Kind::F64, Type(Type::Kind::I64));
        case Opcode::Zext1:
            return checkUnary_E(fn, bb, instr, types, Type::Kind::I1, Type(Type::Kind::I64));
        case Opcode::Trunc1:
            return checkUnary_E(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::I1));
        case Opcode::GEP:
            return checkGEP_E(fn, bb, instr, types);
        case Opcode::Load:
            return checkLoad_E(fn, bb, instr, types);
        case Opcode::Store:
            return checkStore_E(fn, bb, instr, types);
        case Opcode::AddrOf:
            return checkAddrOf_E(fn, bb, instr, types);
        case Opcode::ConstStr:
            return checkConstStr_E(fn, bb, instr, types);
        case Opcode::ConstNull:
            return checkConstNull_E(instr, types);
        case Opcode::Call:
            return checkCall_E(fn, bb, instr, externs, funcs, types);
        default:
            return checkDefault_E(instr, types);
    }
}

} // namespace

bool verifyOpcodeSignature(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           std::ostream &err)
{
    if (auto result = verifyOpcodeSignature_E(fn, bb, instr); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

bool verifyInstruction(const Function &fn,
                       const BasicBlock &bb,
                       const Instr &instr,
                       const std::unordered_map<std::string, const Extern *> &externs,
                       const std::unordered_map<std::string, const Function *> &funcs,
                       TypeInference &types,
                       std::ostream &err)
{
    std::vector<Diag> warnings;
    if (auto result = verifyInstruction_E(fn, bb, instr, externs, funcs, types, warnings); !result)
    {
        for (const auto &warning : warnings)
            il::support::printDiag(warning, err);
        il::support::printDiag(result.error(), err);
        return false;
    }

    for (const auto &warning : warnings)
        il::support::printDiag(warning, err);
    return true;
}

Expected<void> verifyOpcodeSignature_expected(const Function &fn,
                                               const BasicBlock &bb,
                                               const Instr &instr)
{
    return verifyOpcodeSignature_E(fn, bb, instr);
}

Expected<void> verifyInstruction_expected(const Function &fn,
                                           const BasicBlock &bb,
                                           const Instr &instr,
                                           const std::unordered_map<std::string, const Extern *> &externs,
                                           const std::unordered_map<std::string, const Function *> &funcs,
                                           TypeInference &types,
                                           std::vector<Diag> &warnings)
{
    return verifyInstruction_E(fn, bb, instr, externs, funcs, types, warnings);
}

} // namespace il::verify
