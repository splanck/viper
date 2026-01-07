//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "il/core/Global.hpp"

#include <optional>
#include <unordered_map>
#include <utility>

namespace viper::codegen::x64::passes
{
namespace
{

/// @brief Emit a backend-unsupported diagnostic and terminate lowering.
[[noreturn]] void reportUnsupported(std::string detail)
{
    viper::codegen::x64::phaseAUnsupported(detail.c_str());
}

/// @brief Adapter module builder that converts IL to backend IR.
/// @details Encapsulates the conversion logic, maintaining state for value kinds
///          and providing helper methods for different instruction categories.
class ModuleAdapter
{
  public:
    explicit ModuleAdapter() = default;

    /// @brief Convert an IL module to the backend adapter representation.
    ILModule adapt(const il::core::Module &module)
    {
        currentModule_ = &module;
        ILModule result{};
        result.funcs.reserve(module.functions.size());

        for (const auto &func : module.functions)
        {
            result.funcs.push_back(adaptFunction(func));
        }

        currentModule_ = nullptr;
        return result;
    }

  private:
    /// @brief Map from SSA ids to their value kinds.
    std::unordered_map<unsigned, ILValue::Kind> valueKinds_{};

    /// @brief Current function being adapted (for return type access).
    const il::core::Function *currentFunc_{nullptr};

    /// @brief Current module being adapted (for global lookup).
    const il::core::Module *currentModule_{nullptr};

    /// @brief Look up a global by name.
    const il::core::Global *findGlobal(const std::string &name) const
    {
        if (!currentModule_)
            return nullptr;
        for (const auto &g : currentModule_->globals)
        {
            if (g.name == name)
                return &g;
        }
        return nullptr;
    }

    //-------------------------------------------------------------------------
    // Type Conversion
    //-------------------------------------------------------------------------

    /// @brief Map an IL type to the backend adapter value classification.
    static ILValue::Kind typeToKind(const il::core::Type &type)
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
    }

    //-------------------------------------------------------------------------
    // Value Construction Helpers
    //-------------------------------------------------------------------------

    /// @brief Construct an adapter value representing a block label.
    static ILValue makeLabelValue(std::string name)
    {
        ILValue label{};
        label.kind = ILValue::Kind::LABEL;
        label.label = std::move(name);
        label.id = -1;
        return label;
    }

    /// @brief Construct an adapter value representing a local block label.
    /// @details Prefixes the label with ".L" to make it assembly-local, avoiding
    ///          symbol collisions between functions.
    static ILValue makeBlockLabelValue(const std::string &funcName, std::string name)
    {
        return makeLabelValue(".L_" + funcName + "_" + name);
    }

    /// @brief Create an immediate adapter value storing a condition code.
    static ILValue makeCondImmediate(int code)
    {
        ILValue imm{};
        imm.kind = ILValue::Kind::I64;
        imm.i64 = il::common::integer::narrow_to(
            static_cast<long long>(code), 64, il::common::integer::OverflowPolicy::Wrap);
        imm.id = -1;
        return imm;
    }

    /// @brief Translate IL comparison opcodes into backend condition codes.
    static int condCodeFor(il::core::Opcode op)
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
    }

    //-------------------------------------------------------------------------
    // Value Conversion
    //-------------------------------------------------------------------------

    /// @brief Convert an IL operand into the backend adapter value.
    ILValue convertValue(const il::core::Value &value, std::optional<ILValue::Kind> hint)
    {
        ILValue converted{};
        converted.id = -1;

        switch (value.kind)
        {
            case il::core::Value::Kind::Temp:
            {
                const auto it = valueKinds_.find(value.id);
                if (it == valueKinds_.end())
                {
                    reportUnsupported("ssa temp without registered kind in Phase A lowering");
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
    }

    /// @brief Append converted operands to an adapter instruction.
    void convertOperands(const il::core::Instr &instr,
                         std::initializer_list<std::optional<ILValue::Kind>> hints,
                         ILInstr &out)
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
    }

    //-------------------------------------------------------------------------
    // Result Registration
    //-------------------------------------------------------------------------

    /// @brief Record the kind associated with the instruction result.
    ILValue::Kind setResultKind(ILInstr &out,
                                const il::core::Instr &instr,
                                const il::core::Type &type)
    {
        const ILValue::Kind kind = typeToKind(type);
        if (instr.result)
        {
            out.resultId = static_cast<int>(*instr.result);
            out.resultKind = kind;
            valueKinds_[*instr.result] = kind;
        }
        else
        {
            out.resultKind = kind;
        }
        return kind;
    }

    /// @brief Set result kind to a fixed type (for bitwise ops that always produce I64).
    void setFixedResultKind(ILInstr &out, const il::core::Instr &instr, ILValue::Kind kind)
    {
        if (instr.result)
        {
            out.resultId = static_cast<int>(*instr.result);
            out.resultKind = kind;
            valueKinds_[*instr.result] = kind;
        }
        else
        {
            out.resultKind = kind;
        }
    }

    //-------------------------------------------------------------------------
    // Function/Block Adaptation
    //-------------------------------------------------------------------------

    /// @brief Adapt an entire IL function.
    ILFunction adaptFunction(const il::core::Function &func)
    {
        currentFunc_ = &func;
        valueKinds_.clear();
        valueKinds_.reserve(func.valueNames.size() + func.params.size());

        ILFunction adapted{};
        adapted.name = func.name;

        // Register parameter kinds
        for (const auto &param : func.params)
        {
            valueKinds_.emplace(param.id, typeToKind(param.type));
        }

        // Adapt each block
        adapted.blocks.reserve(func.blocks.size());
        for (const auto &block : func.blocks)
        {
            adapted.blocks.push_back(adaptBlock(block));
        }

        return adapted;
    }

    /// @brief Adapt an IL block.
    ILBlock adaptBlock(const il::core::BasicBlock &block)
    {
        ILBlock adapted{};
        adapted.name = block.label;

        // Register block parameter kinds
        adapted.paramIds.reserve(block.params.size());
        adapted.paramKinds.reserve(block.params.size());
        for (const auto &param : block.params)
        {
            const ILValue::Kind kind = typeToKind(param.type);
            adapted.paramIds.push_back(static_cast<int>(param.id));
            adapted.paramKinds.push_back(kind);
            valueKinds_[param.id] = kind;
        }

        // Adapt each instruction
        for (const auto &instr : block.instructions)
        {
            adaptInstruction(instr, adapted);
        }

        return adapted;
    }

    //-------------------------------------------------------------------------
    // Instruction Adaptation (by category)
    //-------------------------------------------------------------------------

    /// @brief Adapt a single instruction and append to block.
    void adaptInstruction(const il::core::Instr &instr, ILBlock &block)
    {
        ILInstr out{};
        out.resultId = -1;

        switch (instr.op)
        {
            // Arithmetic operations
            case il::core::Opcode::Add:
            case il::core::Opcode::FAdd:
            case il::core::Opcode::IAddOvf:
                adaptBinaryArithmetic(instr, out, "add");
                break;
            case il::core::Opcode::Sub:
            case il::core::Opcode::FSub:
            case il::core::Opcode::ISubOvf:
                adaptBinaryArithmetic(instr, out, "sub");
                break;
            case il::core::Opcode::Mul:
            case il::core::Opcode::FMul:
            case il::core::Opcode::IMulOvf:
                adaptBinaryArithmetic(instr, out, "mul");
                break;
            case il::core::Opcode::FDiv:
                adaptFDiv(instr, out);
                break;

            // Division and remainder
            case il::core::Opcode::SDiv:
            case il::core::Opcode::SDivChk0:
                adaptIntDiv(instr, out, "sdiv");
                break;
            case il::core::Opcode::SRem:
            case il::core::Opcode::SRemChk0:
                adaptIntDiv(instr, out, "srem");
                break;
            case il::core::Opcode::UDiv:
            case il::core::Opcode::UDivChk0:
                adaptIntDiv(instr, out, "udiv");
                break;
            case il::core::Opcode::URem:
            case il::core::Opcode::URemChk0:
                adaptIntDiv(instr, out, "urem");
                break;

            // Shift operations
            case il::core::Opcode::Shl:
                adaptShift(instr, out, "shl");
                break;
            case il::core::Opcode::LShr:
                adaptShift(instr, out, "lshr");
                break;
            case il::core::Opcode::AShr:
                adaptShift(instr, out, "ashr");
                break;

            // Bitwise operations
            case il::core::Opcode::And:
                adaptBitwise(instr, out, "and");
                break;
            case il::core::Opcode::Or:
                adaptBitwise(instr, out, "or");
                break;
            case il::core::Opcode::Xor:
                adaptBitwise(instr, out, "xor");
                break;

            // Integer comparisons
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
                adaptIntCompare(instr, out);
                break;

            // Float comparisons
            case il::core::Opcode::FCmpEQ:
            case il::core::Opcode::FCmpNE:
            case il::core::Opcode::FCmpLT:
            case il::core::Opcode::FCmpLE:
            case il::core::Opcode::FCmpGT:
            case il::core::Opcode::FCmpGE:
                adaptFloatCompare(instr, out);
                break;

            // Call
            case il::core::Opcode::Call:
                adaptCall(instr, out);
                break;

            // Exception handling
            case il::core::Opcode::EhPush:
                adaptEhPush(instr, out);
                break;
            case il::core::Opcode::EhPop:
                adaptEhPop(out);
                break;
            case il::core::Opcode::EhEntry:
                adaptEhEntry(out);
                break;

            // Memory operations
            case il::core::Opcode::Load:
                adaptLoad(instr, out);
                break;
            case il::core::Opcode::Store:
                adaptStore(instr, out);
                break;

            // Cast operations
            case il::core::Opcode::Zext1:
                adaptZext(instr, out);
                break;
            case il::core::Opcode::Trunc1:
                adaptTrunc(instr, out);
                break;
            case il::core::Opcode::CastSiToFp:
            case il::core::Opcode::Sitofp:
                adaptSiToFp(instr, out);
                break;
            case il::core::Opcode::CastFpToSiRteChk:
            case il::core::Opcode::Fptosi:
                adaptFpToSi(instr, out);
                break;
            case il::core::Opcode::CastSiNarrowChk:
            case il::core::Opcode::CastUiNarrowChk:
                adaptNarrowCast(instr, out);
                break;

            // Control flow
            case il::core::Opcode::Ret:
                adaptRet(instr, out);
                break;
            case il::core::Opcode::Br:
                adaptBr(instr, out, block);
                break;
            case il::core::Opcode::CBr:
                adaptCBr(instr, out, block);
                break;
            case il::core::Opcode::Trap:
                out.opcode = "trap";
                break;
            case il::core::Opcode::TrapFromErr:
                out.opcode = "trap";
                // TrapFromErr takes an error code, but for now we just trap
                convertOperands(instr, {ILValue::Kind::I64}, out);
                break;

            // String operations
            case il::core::Opcode::ConstStr:
                adaptConstStr(instr, out);
                break;

            // Memory allocation
            case il::core::Opcode::Alloca:
                adaptAlloca(instr, out);
                break;
            case il::core::Opcode::GEP:
                adaptGEP(instr, out);
                break;

            default:
                reportUnsupported(std::string{"IL opcode '"} + il::core::toString(instr.op) +
                                  "' not supported by x86-64 Phase A");
        }

        block.instrs.push_back(std::move(out));
    }

    //-------------------------------------------------------------------------
    // Arithmetic Instruction Adapters
    //-------------------------------------------------------------------------

    void adaptBinaryArithmetic(const il::core::Instr &instr, ILInstr &out, const char *opcode)
    {
        const ILValue::Kind kind = setResultKind(out, instr, instr.type);
        out.opcode = opcode;
        convertOperands(instr, {kind, kind}, out);
    }

    void adaptFDiv(const il::core::Instr &instr, ILInstr &out)
    {
        out.opcode = "fdiv";
        out.resultKind = ILValue::Kind::F64;
        if (instr.result)
        {
            out.resultId = static_cast<int>(*instr.result);
            valueKinds_[*instr.result] = ILValue::Kind::F64;
        }
        convertOperands(instr, {ILValue::Kind::F64, ILValue::Kind::F64}, out);
    }

    void adaptIntDiv(const il::core::Instr &instr, ILInstr &out, const char *opcode)
    {
        setResultKind(out, instr, instr.type);
        out.opcode = opcode;
        convertOperands(instr, {ILValue::Kind::I64, ILValue::Kind::I64}, out);
    }

    void adaptShift(const il::core::Instr &instr, ILInstr &out, const char *opcode)
    {
        const ILValue::Kind kind = setResultKind(out, instr, instr.type);
        out.opcode = opcode;
        convertOperands(instr, {kind, ILValue::Kind::I64}, out);
    }

    void adaptBitwise(const il::core::Instr &instr, ILInstr &out, const char *opcode)
    {
        setFixedResultKind(out, instr, ILValue::Kind::I64);
        out.opcode = opcode;
        convertOperands(instr, {ILValue::Kind::I64, ILValue::Kind::I64}, out);
    }

    //-------------------------------------------------------------------------
    // Comparison Instruction Adapters
    //-------------------------------------------------------------------------

    void adaptIntCompare(const il::core::Instr &instr, ILInstr &out)
    {
        out.opcode = "cmp";
        setFixedResultKind(out, instr, ILValue::Kind::I1);
        convertOperands(instr, {ILValue::Kind::I64, ILValue::Kind::I64}, out);
        out.ops.push_back(makeCondImmediate(condCodeFor(instr.op)));
    }

    void adaptFloatCompare(const il::core::Instr &instr, ILInstr &out)
    {
        out.opcode = "fcmp";
        setFixedResultKind(out, instr, ILValue::Kind::I1);
        convertOperands(instr, {ILValue::Kind::F64, ILValue::Kind::F64}, out);
        out.ops.push_back(makeCondImmediate(condCodeFor(instr.op)));
    }

    //-------------------------------------------------------------------------
    // Call Instruction Adapter
    //-------------------------------------------------------------------------

    void adaptCall(const il::core::Instr &instr, ILInstr &out)
    {
        if (instr.type.kind != il::core::Type::Kind::Void)
        {
            setResultKind(out, instr, instr.type);
        }
        else if (instr.result)
        {
            // Some IL files have call instructions with result SSA ids but type set to void.
            // This can happen when the IL parser doesn't correctly infer the return type
            // from extern declarations. In this case, assume PTR as a safe default since
            // most such calls return pointers (e.g., rt_get_class_vtable).
            // The VM handles this correctly, so we should too.
            out.resultId = static_cast<int>(*instr.result);
            out.resultKind = ILValue::Kind::PTR;
            valueKinds_[*instr.result] = ILValue::Kind::PTR;
        }
        out.opcode = "call";
        out.ops.push_back(makeLabelValue(instr.callee));
        for (const auto &operand : instr.operands)
        {
            out.ops.push_back(convertValue(operand, std::nullopt));
        }
    }

    //-------------------------------------------------------------------------
    // Exception Handling Adapters
    //-------------------------------------------------------------------------

    void adaptEhPush(const il::core::Instr &instr, ILInstr &out)
    {
        out.opcode = "eh.push";
        convertOperands(instr, {std::nullopt}, out);
        if (out.ops.size() > 1)
        {
            out.ops.resize(1);
        }
    }

    void adaptEhPop(ILInstr &out)
    {
        out.opcode = "eh.pop";
        out.resultKind = ILValue::Kind::I64; // unused
    }

    void adaptEhEntry(ILInstr &out)
    {
        out.opcode = "eh.entry";
        out.resultKind = ILValue::Kind::I64; // unused
    }

    //-------------------------------------------------------------------------
    // Memory Operation Adapters
    //-------------------------------------------------------------------------

    void adaptLoad(const il::core::Instr &instr, ILInstr &out)
    {
        setResultKind(out, instr, instr.type);
        out.opcode = "load";
        convertOperands(instr, {ILValue::Kind::PTR, ILValue::Kind::I64}, out);
        if (out.ops.size() > 2)
        {
            out.ops.resize(2);
        }
    }

    void adaptStore(const il::core::Instr &instr, ILInstr &out)
    {
        out.opcode = "store";
        convertOperands(instr, {std::nullopt, ILValue::Kind::PTR, ILValue::Kind::I64}, out);
        if (out.ops.size() > 3)
        {
            out.ops.resize(3);
        }
    }

    /// @brief Adapt alloca instruction for stack allocation.
    void adaptAlloca(const il::core::Instr &instr, ILInstr &out)
    {
        setFixedResultKind(out, instr, ILValue::Kind::PTR);
        out.opcode = "alloca";
        // Operand 0 is the size in bytes
        convertOperands(instr, {ILValue::Kind::I64}, out);
    }

    /// @brief Adapt GEP (get element pointer) instruction.
    void adaptGEP(const il::core::Instr &instr, ILInstr &out)
    {
        setFixedResultKind(out, instr, ILValue::Kind::PTR);
        out.opcode = "gep";
        // Operand 0 is the base pointer, operand 1 is the byte offset
        convertOperands(instr, {ILValue::Kind::PTR, ILValue::Kind::I64}, out);
    }

    //-------------------------------------------------------------------------
    // Cast Operation Adapters
    //-------------------------------------------------------------------------

    void adaptZext(const il::core::Instr &instr, ILInstr &out)
    {
        setResultKind(out, instr, instr.type);
        out.opcode = "zext";
        convertOperands(instr, {ILValue::Kind::I1}, out);
    }

    void adaptTrunc(const il::core::Instr &instr, ILInstr &out)
    {
        setResultKind(out, instr, instr.type);
        out.opcode = "trunc";
        convertOperands(instr, {ILValue::Kind::I64}, out);
    }

    void adaptSiToFp(const il::core::Instr &instr, ILInstr &out)
    {
        setResultKind(out, instr, instr.type);
        out.opcode = "sitofp";
        convertOperands(instr, {ILValue::Kind::I64}, out);
    }

    void adaptFpToSi(const il::core::Instr &instr, ILInstr &out)
    {
        setResultKind(out, instr, instr.type);
        out.opcode = "fptosi";
        convertOperands(instr, {ILValue::Kind::F64}, out);
    }

    /// @brief Adapt narrowing cast (i64 -> i32 etc).
    void adaptNarrowCast(const il::core::Instr &instr, ILInstr &out)
    {
        // For now, treat narrow cast as just passing through the lower bits
        // (same as trunc behavior). The checked variant would need to verify
        // the value fits in the smaller type.
        setResultKind(out, instr, instr.type);
        out.opcode = "trunc";
        convertOperands(instr, {ILValue::Kind::I64}, out);
    }

    //-------------------------------------------------------------------------
    // Control Flow Adapters
    //-------------------------------------------------------------------------

    void adaptRet(const il::core::Instr &instr, ILInstr &out)
    {
        out.opcode = "ret";
        if (!instr.operands.empty())
        {
            const auto returnKind =
                currentFunc_->retType.kind == il::core::Type::Kind::Void
                    ? std::optional<ILValue::Kind>{}
                    : std::optional<ILValue::Kind>{typeToKind(currentFunc_->retType)};
            out.ops.push_back(convertValue(instr.operands.front(), returnKind));
        }
    }

    void adaptBr(const il::core::Instr &instr, ILInstr &out, ILBlock &block)
    {
        out.opcode = "br";
        if (!instr.labels.empty())
        {
            out.ops.push_back(makeBlockLabelValue(currentFunc_->name, instr.labels.front()));
        }
        addTerminatorEdges(instr, block);
    }

    void adaptCBr(const il::core::Instr &instr, ILInstr &out, ILBlock &block)
    {
        out.opcode = "cbr";
        if (instr.operands.empty())
        {
            reportUnsupported("conditional branch missing condition operand");
        }
        out.ops.push_back(convertValue(instr.operands.front(), ILValue::Kind::I1));
        for (const auto &label : instr.labels)
        {
            out.ops.push_back(makeBlockLabelValue(currentFunc_->name, label));
        }
        addTerminatorEdges(instr, block);
    }

    //-------------------------------------------------------------------------
    // String Operation Adapters
    //-------------------------------------------------------------------------

    /// @brief Adapt const_str instruction to produce a string value.
    void adaptConstStr(const il::core::Instr &instr, ILInstr &out)
    {
        setFixedResultKind(out, instr, ILValue::Kind::STR);
        out.opcode = "const_str";

        // The operand is a GlobalAddr pointing to the global string constant
        if (instr.operands.empty())
        {
            reportUnsupported("const_str missing operand");
        }

        const auto &op = instr.operands.front();
        if (op.kind != il::core::Value::Kind::GlobalAddr)
        {
            reportUnsupported("const_str with non-global operand");
        }

        // Look up the global to get the actual string content
        const il::core::Global *global = findGlobal(op.str);
        if (!global)
        {
            reportUnsupported("const_str references unknown global: " + op.str);
        }

        // Create an ILValue with the string content
        ILValue strValue{};
        strValue.kind = ILValue::Kind::STR;
        strValue.str = global->init;
        strValue.strLen = static_cast<std::uint64_t>(global->init.size());
        strValue.id = -1;
        out.ops.push_back(strValue);
    }

    /// @brief Add terminator edges for branch instructions.
    void addTerminatorEdges(const il::core::Instr &instr, ILBlock &block)
    {
        const std::size_t succCount = instr.labels.size();
        block.terminatorEdges.reserve(block.terminatorEdges.size() + succCount);

        for (std::size_t idx = 0; idx < succCount; ++idx)
        {
            ILBlock::EdgeArg edge{};
            edge.to = instr.labels[idx];
            if (idx < instr.brArgs.size())
            {
                for (const auto &arg : instr.brArgs[idx])
                {
                    if (arg.kind != il::core::Value::Kind::Temp)
                    {
                        reportUnsupported("non-SSA block argument in Phase A lowering");
                    }
                    edge.argIds.push_back(static_cast<int>(arg.id));
                }
            }
            block.terminatorEdges.push_back(std::move(edge));
        }
    }
};

} // namespace

/// @brief Execute Phase A lowering for the provided pipeline module.
bool LoweringPass::run(Module &module, Diagnostics &)
{
    ModuleAdapter adapter{};
    module.lowered = adapter.adapt(module.il);
    return true;
}

} // namespace viper::codegen::x64::passes
