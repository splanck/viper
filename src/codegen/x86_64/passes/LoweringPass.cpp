//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/passes/LoweringPass.cpp
// Purpose: Implement the IL lowering pass that adapts front-end IL into the backend's
//          intermediate representation.
// Key invariants: Value kinds are inferred deterministically and stored alongside SSA ids.
// Ownership/Lifetime: Pass mutates the supplied Module in place without owning external state.
// Links: docs/codemap.md, src/codegen/x86_64/CodegenPipeline.cpp
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Phase A lowering pass translating IL into backend-friendly IR.
/// @details Contains the bulk of the adapter logic that maps IL types,
///          opcodes, and SSA values into the simplified structures consumed by
///          the x86-64 code generator. The helpers emit detailed diagnostics
///          when unsupported features are encountered so future backend
///          expansions have a clear starting point.

#include "codegen/x86_64/passes/LoweringPass.hpp"

#include "codegen/x86_64/Unsupported.hpp"
#include "common/IntegerHelpers.hpp"

#include <optional>
#include <unordered_map>
#include <utility>

namespace viper::codegen::x64::passes
{
namespace
{
/// @brief Emit a backend-unsupported diagnostic and terminate lowering.
/// @details Centralises the failure path so that every unsupported feature
///          surfaces via @ref phaseAUnsupported with a consistent message.
///          The function never returns because Phase A lowering cannot recover
///          once it encounters an unsupported construct.
[[noreturn]] void reportUnsupported(std::string detail)
{
    viper::codegen::x64::phaseAUnsupported(detail.c_str());
}

/// @brief Translate an IL module into the intermediate adapter representation.
/// @details Walks each IL function, block, and instruction while mapping types
///          to backend value kinds. During conversion the helper records
///          diagnostics for unsupported patterns and ensures label/value IDs
///          remain consistent for later passes.
/// @param module IL module produced by the front-end pipeline.
/// @return Backend adapter module ready for Phase B code generation.
ILModule convertToAdapterModule(const il::core::Module &module)
{
    using viper::codegen::x64::ILBlock;
    using viper::codegen::x64::ILFunction;
    using viper::codegen::x64::ILModule;
    using viper::codegen::x64::ILValue;

    /// @brief Map an IL type to the backend adapter value classification.
    /// @details Consolidates the lowering decisions that collapse several IL
    ///          integer sizes onto a single 64-bit kind while rejecting
    ///          unsupported constructs such as resume tokens.
    const auto typeToKind = [](const il::core::Type &type) -> ILValue::Kind
    {
        using il::core::Type;
        switch (type.kind)
        {
            case Type::Kind::I1:
                return ILValue::Kind::I1;
            case Type::Kind::I16:
            case Type::Kind::I32:
            case Type::Kind::I64:
                return ILValue::Kind::I64;
            case Type::Kind::F64:
                return ILValue::Kind::F64;
            case Type::Kind::Ptr:
                return ILValue::Kind::PTR;
            case Type::Kind::Str:
                return ILValue::Kind::STR;
            case Type::Kind::Void:
                reportUnsupported("void-typed value requested by backend adapter");
            case Type::Kind::Error:
            case Type::Kind::ResumeTok:
                reportUnsupported("non-scalar IL type encountered during Phase A lowering");
        }
        reportUnsupported("unknown IL type kind encountered during Phase A lowering");
    };

    /// @brief Construct an adapter value representing a block label.
    /// @details Assigns the sentinel ID expected by the backend so labels can
    ///          participate in operand lists without colliding with SSA
    ///          temporaries.
    const auto makeLabelValue = [](std::string name) -> ILValue
    {
        ILValue label{};
        label.kind = ILValue::Kind::LABEL;
        label.label = std::move(name);
        label.id = -1;
        return label;
    };

    /// @brief Create an immediate adapter value storing a condition code.
    /// @details Wraps raw integers into @ref ILValue objects so branches and
    ///          selects can share the same operand representation as other
    ///          instructions.
    const auto makeCondImmediate = [](int code) -> ILValue
    {
        ILValue imm{};
        imm.kind = ILValue::Kind::I64;
        imm.i64 = il::common::integer::narrow_to(static_cast<long long>(code),
                                                 64,
                                                 il::common::integer::OverflowPolicy::Wrap);
        imm.id = -1;
        return imm;
    };

    /// @brief Translate IL comparison opcodes into backend condition codes.
    /// @details The encoding matches the expectations of the backend Phase B
    ///          lowering logic and groups signed/unsigned comparisons so they
    ///          can reuse the same adapter opcode.
    const auto condCodeFor = [](il::core::Opcode op) -> int
    {
        using il::core::Opcode;
        switch (op)
        {
            case Opcode::ICmpEq:
            case Opcode::FCmpEQ:
                return 0;
            case Opcode::ICmpNe:
            case Opcode::FCmpNE:
                return 1;
            case Opcode::SCmpLT:
            case Opcode::FCmpLT:
                return 2;
            case Opcode::SCmpLE:
            case Opcode::FCmpLE:
                return 3;
            case Opcode::SCmpGT:
            case Opcode::FCmpGT:
                return 4;
            case Opcode::SCmpGE:
            case Opcode::FCmpGE:
                return 5;
            case Opcode::UCmpGT:
                return 6;
            case Opcode::UCmpGE:
                return 7;
            case Opcode::UCmpLT:
                return 8;
            case Opcode::UCmpLE:
                return 9;
            default:
                return 0;
        }
    };

    ILModule adapted{};
    adapted.funcs.reserve(module.functions.size());

    for (const auto &func : module.functions)
    {
        ILFunction adaptedFunc{};
        adaptedFunc.name = func.name;

        std::unordered_map<unsigned, ILValue::Kind> valueKinds{};
        valueKinds.reserve(func.valueNames.size() + func.params.size());

        for (const auto &param : func.params)
        {
            valueKinds.emplace(param.id, typeToKind(param.type));
        }

        for (const auto &block : func.blocks)
        {
            ILBlock adaptedBlock{};
            adaptedBlock.name = block.label;

            for (const auto &param : block.params)
            {
                const ILValue::Kind kind = typeToKind(param.type);
                adaptedBlock.paramIds.push_back(static_cast<int>(param.id));
                adaptedBlock.paramKinds.push_back(kind);
                valueKinds[param.id] = kind;
            }

            /// @brief Convert an IL operand into the backend adapter value.
            /// @details Uses recorded kind information when available and falls
            ///          back to @p hint for immediates so consumers receive
            ///          strongly typed operands. Unrecognised SSA ids trigger a
            ///          fatal diagnostic because the adapter would otherwise
            ///          produce inconsistent code.
            const auto convertValue = [&valueKinds](const il::core::Value &value,
                                                    std::optional<ILValue::Kind> hint) -> ILValue
            {
                ILValue converted{};
                converted.id = -1;

                switch (value.kind)
                {
                    case il::core::Value::Kind::Temp:
                    {
                        const auto it = valueKinds.find(value.id);
                        if (it == valueKinds.end())
                        {
                            reportUnsupported(
                                "ssa temp without registered kind in Phase A lowering");
                        }
                        converted.kind = it->second;
                        converted.id = static_cast<int>(value.id);
                        break;
                    }
                    case il::core::Value::Kind::ConstInt:
                    {
                        converted.kind =
                            hint.value_or(value.isBool ? ILValue::Kind::I1 : ILValue::Kind::I64);
                        converted.i64 = value.i64;
                        break;
                    }
                    case il::core::Value::Kind::ConstFloat:
                        converted.kind = ILValue::Kind::F64;
                        converted.f64 = value.f64;
                        break;
                    case il::core::Value::Kind::ConstStr:
                        converted.kind = ILValue::Kind::STR;
                        converted.str = value.str;
                        converted.strLen = static_cast<std::uint64_t>(value.str.size());
                        break;
                    case il::core::Value::Kind::GlobalAddr:
                        converted.kind = ILValue::Kind::LABEL;
                        converted.label = value.str;
                        break;
                    case il::core::Value::Kind::NullPtr:
                        converted.kind = ILValue::Kind::PTR;
                        converted.i64 = 0;
                        break;
                }

                if (hint && value.kind != il::core::Value::Kind::Temp)
                {
                    converted.kind = *hint;
                }

                return converted;
            };

            /// @brief Append converted operands to an adapter instruction.
            /// @details Iterates the IL operand list, supplying parallel hints
            ///          so constants can be forced to the correct type. The
            ///          helper keeps hint lookup bounds-checked and delegates
            ///          to @p convertValue for the actual conversion work.
            const auto convertOperands =
                [&](const il::core::Instr &instr,
                    std::initializer_list<std::optional<ILValue::Kind>> hints,
                    viper::codegen::x64::ILInstr &out)
            {
                std::size_t index = 0;
                for (const auto &operand : instr.operands)
                {
                    const std::optional<ILValue::Kind> hint =
                        index < hints.size() ? *(hints.begin() + static_cast<std::ptrdiff_t>(index))
                                             : std::optional<ILValue::Kind>{};
                    out.ops.push_back(convertValue(operand, hint));
                    ++index;
                }
            };

            for (const auto &instr : block.instructions)
            {
                viper::codegen::x64::ILInstr adaptedInstr{};
                adaptedInstr.resultId = -1;

                /// @brief Record the kind associated with the instruction result.
                /// @details Updates the adapter instruction metadata and caches
                ///          the mapping in @p valueKinds when the IL instruction
                ///          yields an SSA identifier. The helper returns the
                ///          deduced kind so callers can reuse it immediately.
                const auto setResultKind = [&](const il::core::Type &type) -> ILValue::Kind
                {
                    const ILValue::Kind kind = typeToKind(type);
                    if (instr.result)
                    {
                        adaptedInstr.resultId = static_cast<int>(*instr.result);
                        adaptedInstr.resultKind = kind;
                        valueKinds[*instr.result] = kind;
                    }
                    else
                    {
                        adaptedInstr.resultKind = kind;
                    }
                    return kind;
                };

                switch (instr.op)
                {
                    case il::core::Opcode::Add:
                    case il::core::Opcode::FAdd:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "add";
                        convertOperands(instr, {kind, kind}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Sub:
                    case il::core::Opcode::FSub:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "sub";
                        convertOperands(instr, {kind, kind}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Mul:
                    case il::core::Opcode::FMul:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "mul";
                        convertOperands(instr, {kind, kind}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::FDiv:
                    {
                        adaptedInstr.opcode = "fdiv";
                        adaptedInstr.resultKind = ILValue::Kind::F64;
                        if (instr.result)
                        {
                            adaptedInstr.resultId = static_cast<int>(*instr.result);
                            valueKinds[*instr.result] = ILValue::Kind::F64;
                        }
                        convertOperands(
                            instr, {ILValue::Kind::F64, ILValue::Kind::F64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::SDiv:
                    case il::core::Opcode::SDivChk0:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "sdiv";
                        convertOperands(
                            instr, {ILValue::Kind::I64, ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::SRem:
                    case il::core::Opcode::SRemChk0:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "srem";
                        convertOperands(
                            instr, {ILValue::Kind::I64, ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::UDiv:
                    case il::core::Opcode::UDivChk0:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "udiv";
                        convertOperands(
                            instr, {ILValue::Kind::I64, ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::URem:
                    case il::core::Opcode::URemChk0:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "urem";
                        convertOperands(
                            instr, {ILValue::Kind::I64, ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Shl:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "shl";
                        convertOperands(instr, {kind, ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::LShr:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "lshr";
                        convertOperands(instr, {kind, ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::AShr:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "ashr";
                        convertOperands(instr, {kind, ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::And:
                    {
                        if (instr.result)
                        {
                            adaptedInstr.resultId = static_cast<int>(*instr.result);
                            adaptedInstr.resultKind = ILValue::Kind::I64;
                            valueKinds[*instr.result] = ILValue::Kind::I64;
                        }
                        else
                        {
                            adaptedInstr.resultKind = ILValue::Kind::I64;
                        }
                        adaptedInstr.opcode = "and";
                        convertOperands(instr, {ILValue::Kind::I64, ILValue::Kind::I64},
                                        adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Or:
                    {
                        if (instr.result)
                        {
                            adaptedInstr.resultId = static_cast<int>(*instr.result);
                            adaptedInstr.resultKind = ILValue::Kind::I64;
                            valueKinds[*instr.result] = ILValue::Kind::I64;
                        }
                        else
                        {
                            adaptedInstr.resultKind = ILValue::Kind::I64;
                        }
                        adaptedInstr.opcode = "or";
                        convertOperands(instr, {ILValue::Kind::I64, ILValue::Kind::I64},
                                        adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Xor:
                    {
                        if (instr.result)
                        {
                            adaptedInstr.resultId = static_cast<int>(*instr.result);
                            adaptedInstr.resultKind = ILValue::Kind::I64;
                            valueKinds[*instr.result] = ILValue::Kind::I64;
                        }
                        else
                        {
                            adaptedInstr.resultKind = ILValue::Kind::I64;
                        }
                        adaptedInstr.opcode = "xor";
                        convertOperands(instr, {ILValue::Kind::I64, ILValue::Kind::I64},
                                        adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::ICmpEq:
                    case il::core::Opcode::ICmpNe:
                    case il::core::Opcode::SCmpLT:
                    case il::core::Opcode::SCmpLE:
                    case il::core::Opcode::SCmpGT:
                    case il::core::Opcode::SCmpGE:
                    case il::core::Opcode::UCmpGT:
                    case il::core::Opcode::UCmpGE:
                    case il::core::Opcode::UCmpLT:
                    case il::core::Opcode::UCmpLE:
                    {
                        adaptedInstr.opcode = "cmp";
                        if (instr.result)
                        {
                            adaptedInstr.resultId = static_cast<int>(*instr.result);
                            adaptedInstr.resultKind = ILValue::Kind::I1;
                            valueKinds[*instr.result] = ILValue::Kind::I1;
                        }
                        else
                        {
                            adaptedInstr.resultKind = ILValue::Kind::I1;
                        }
                        convertOperands(instr, {ILValue::Kind::I64, ILValue::Kind::I64},
                                        adaptedInstr);
                        adaptedInstr.ops.push_back(makeCondImmediate(condCodeFor(instr.op)));
                        break;
                    }
                    case il::core::Opcode::FCmpEQ:
                    case il::core::Opcode::FCmpNE:
                    case il::core::Opcode::FCmpLT:
                    case il::core::Opcode::FCmpLE:
                    case il::core::Opcode::FCmpGT:
                    case il::core::Opcode::FCmpGE:
                    {
                        adaptedInstr.opcode = "fcmp";
                        if (instr.result)
                        {
                            adaptedInstr.resultId = static_cast<int>(*instr.result);
                            adaptedInstr.resultKind = ILValue::Kind::I1;
                            valueKinds[*instr.result] = ILValue::Kind::I1;
                        }
                        else
                        {
                            adaptedInstr.resultKind = ILValue::Kind::I1;
                        }
                        convertOperands(instr, {ILValue::Kind::F64, ILValue::Kind::F64},
                                        adaptedInstr);
                        adaptedInstr.ops.push_back(makeCondImmediate(condCodeFor(instr.op)));
                        break;
                    }
                    case il::core::Opcode::Call:
                    {
                        if (instr.type.kind != il::core::Type::Kind::Void)
                        {
                            setResultKind(instr.type);
                        }
                        else if (instr.result)
                        {
                            reportUnsupported("void call returning SSA id in Phase A lowering");
                        }
                        adaptedInstr.opcode = "call";
                        adaptedInstr.ops.push_back(makeLabelValue(instr.callee));
                        for (const auto &operand : instr.operands)
                        {
                            adaptedInstr.ops.push_back(convertValue(operand, std::nullopt));
                        }
                        break;
                    }
                    case il::core::Opcode::Load:
                    {
                        const ILValue::Kind resultKind = setResultKind(instr.type);
                        adaptedInstr.opcode = "load";
                        convertOperands(
                            instr, {ILValue::Kind::PTR, ILValue::Kind::I64}, adaptedInstr);
                        if (adaptedInstr.ops.size() > 2)
                        {
                            adaptedInstr.ops.resize(2);
                        }
                        (void)resultKind;
                        break;
                    }
                    case il::core::Opcode::Store:
                    {
                        adaptedInstr.opcode = "store";
                        convertOperands(instr,
                                        {std::nullopt, ILValue::Kind::PTR, ILValue::Kind::I64},
                                        adaptedInstr);
                        if (adaptedInstr.ops.size() > 3)
                        {
                            adaptedInstr.ops.resize(3);
                        }
                        break;
                    }
                    case il::core::Opcode::Zext1:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "zext";
                        convertOperands(instr, {ILValue::Kind::I1}, adaptedInstr);
                        (void)kind;
                        break;
                    }
                    case il::core::Opcode::Trunc1:
                    {
                        const ILValue::Kind kind = setResultKind(instr.type);
                        adaptedInstr.opcode = "trunc";
                        convertOperands(instr, {ILValue::Kind::I64}, adaptedInstr);
                        (void)kind;
                        break;
                    }
                    case il::core::Opcode::CastSiToFp:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "sitofp";
                        convertOperands(instr, {ILValue::Kind::I64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::CastFpToSiRteChk:
                    {
                        setResultKind(instr.type);
                        adaptedInstr.opcode = "fptosi";
                        convertOperands(instr, {ILValue::Kind::F64}, adaptedInstr);
                        break;
                    }
                    case il::core::Opcode::Ret:
                    {
                        adaptedInstr.opcode = "ret";
                        if (!instr.operands.empty())
                        {
                            const auto returnKind = func.retType.kind == il::core::Type::Kind::Void
                                                        ? std::optional<ILValue::Kind>{}
                                                        : std::optional<ILValue::Kind>{typeToKind(func.retType)};
                            adaptedInstr.ops.push_back(
                                convertValue(instr.operands.front(), returnKind));
                        }
                        break;
                    }
                    case il::core::Opcode::Br:
                    {
                        adaptedInstr.opcode = "br";
                        if (!instr.labels.empty())
                        {
                            adaptedInstr.ops.push_back(makeLabelValue(instr.labels.front()));
                        }
                        const std::size_t succCount = instr.labels.size();
                        adaptedBlock.terminatorEdges.reserve(adaptedBlock.terminatorEdges.size() +
                                                             succCount);
                        for (std::size_t idx = 0; idx < succCount; ++idx)
                        {
                            viper::codegen::x64::ILBlock::EdgeArg edge{};
                            edge.to = instr.labels[idx];
                            if (idx < instr.brArgs.size())
                            {
                                for (const auto &arg : instr.brArgs[idx])
                                {
                                    if (arg.kind != il::core::Value::Kind::Temp)
                                    {
                                        reportUnsupported(
                                            "non-SSA block argument in Phase A lowering");
                                    }
                                    edge.argIds.push_back(static_cast<int>(arg.id));
                                }
                            }
                            adaptedBlock.terminatorEdges.push_back(std::move(edge));
                        }
                        break;
                    }
                    case il::core::Opcode::CBr:
                    {
                        adaptedInstr.opcode = "cbr";
                        if (instr.operands.empty())
                        {
                            reportUnsupported("conditional branch missing condition operand");
                        }
                        adaptedInstr.ops.push_back(
                            convertValue(instr.operands.front(), ILValue::Kind::I1));
                        const std::size_t succCount = instr.labels.size();
                        for (std::size_t idx = 0; idx < succCount; ++idx)
                        {
                            adaptedInstr.ops.push_back(makeLabelValue(instr.labels[idx]));
                        }
                        adaptedBlock.terminatorEdges.reserve(adaptedBlock.terminatorEdges.size() +
                                                             succCount);
                        for (std::size_t idx = 0; idx < succCount; ++idx)
                        {
                            viper::codegen::x64::ILBlock::EdgeArg edge{};
                            edge.to = instr.labels[idx];
                            if (idx < instr.brArgs.size())
                            {
                                for (const auto &arg : instr.brArgs[idx])
                                {
                                    if (arg.kind != il::core::Value::Kind::Temp)
                                    {
                                        reportUnsupported(
                                            "non-SSA block argument in Phase A lowering");
                                    }
                                    edge.argIds.push_back(static_cast<int>(arg.id));
                                }
                            }
                            adaptedBlock.terminatorEdges.push_back(std::move(edge));
                        }
                        break;
                    }
                    case il::core::Opcode::Trap:
                    {
                        adaptedInstr.opcode = "trap";
                        break;
                    }
                    default:
                        reportUnsupported(std::string{"IL opcode '"} +
                                          il::core::toString(instr.op) +
                                          "' not supported by x86-64 Phase A");
                }

                adaptedBlock.instrs.push_back(std::move(adaptedInstr));
            }

            adaptedFunc.blocks.push_back(std::move(adaptedBlock));
        }

        adapted.funcs.push_back(std::move(adaptedFunc));
    }

    return adapted;
}

} // namespace

/// @brief Execute Phase A lowering for the provided pipeline module.
/// @details Replaces the module's adapter representation with a freshly
///          constructed one derived from the IL module. Diagnostics are routed
///          through @ref reportUnsupported which terminates the process when an
///          unsupported feature is encountered.
/// @param module Pipeline state containing the IL module to adapt.
/// @param diags  Unused diagnostics sink (reserved for future richer reporting).
/// @return @c true when lowering completed successfully.
bool LoweringPass::run(Module &module, Diagnostics &)
{
    module.lowered = convertToAdapterModule(module.il);
    return true;
}

} // namespace viper::codegen::x64::passes
