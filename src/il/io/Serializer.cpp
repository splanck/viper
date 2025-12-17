//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the textual serializer for IL modules.  The serializer prints
// deterministic output that mirrors the parser grammar so modules can round
// trip through the textual form for diagnostics and tooling.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Renders IL modules, functions, and instructions to textual form.
/// @details Helper routines convert operands, types, and instruction-specific
///          payloads into the canonical syntax accepted by the parser.  The
///          resulting string is used by human-facing tools and golden tests to
///          verify lowering behaviour.

#include "il/io/Serializer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "il/internal/io/ParserUtil.hpp"
#include "il/io/StringEscape.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <sstream>
#include <string>

namespace il::io
{

using namespace il::core;

namespace
{

/// @brief Context for serialization, carrying function-scoped metadata.
/// @details Allows value printing to resolve temp IDs to their declared names,
///          ensuring IL round-trips correctly through serialize/parse cycles.
struct SerializeContext
{
    /// @brief Value name table from the current function (may be null for global context).
    const std::vector<std::string> *valueNames = nullptr;

    /// @brief Look up a name for the given temp ID.
    /// @param id Temp ID to resolve.
    /// @return The declared name if available and non-empty, empty string otherwise.
    [[nodiscard]] std::string_view nameForTemp(unsigned id) const
    {
        if (!valueNames || id >= valueNames->size())
            return {};
        return (*valueNames)[id];
    }
};

using Formatter = void (*)(const Instr &, std::ostream &, const SerializeContext &);

/// @brief Convert an opcode enumerator into an array index.
/// @param op Opcode value to convert.
/// @return Zero-based index suitable for array lookups.
constexpr size_t toIndex(Opcode op)
{
    return static_cast<size_t>(op);
}

/// @brief Format a value operand into the textual representation used by IL.
/// @details For temp values, attempts to resolve the ID to its declared name
///          using the serialize context. Falls back to %tN format when no
///          name is available.
/// @param os    Stream receiving the textual value.
/// @param value Operand to serialise.
/// @param ctx   Serialization context with value name mappings.
void printValue(std::ostream &os, const Value &value, const SerializeContext &ctx)
{
    if (value.kind == Value::Kind::ConstStr)
    {
        os << '"' << encodeEscapedString(value.str) << '"';
        return;
    }
    // For temp values, try to resolve to declared name for correct round-tripping
    if (value.kind == Value::Kind::Temp)
    {
        auto name = ctx.nameForTemp(value.id);
        if (!name.empty() && name[0] != '%')
        {
            // Use the declared name (e.g., "X" for parameter)
            os << '%' << name;
            return;
        }
    }
    os << il::core::toString(value);
}

/// @brief Emit a comma-separated list of operands to a stream.
/// @param os Stream receiving the textual operands.
/// @param values Sequence of operands to print.
/// @param ctx Serialization context with value name mappings.
void printValueList(std::ostream &os, const std::vector<Value> &values, const SerializeContext &ctx)
{
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i)
            os << ", ";
        printValue(os, values[i], ctx);
    }
}

/// @brief Serialize standard operand lists for instructions.
/// @param instr Instruction providing operands.
/// @param os Stream receiving the serialized operands.
/// @param ctx Serialization context with value name mappings.
void printDefaultOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx)
{
    if (instr.operands.empty())
        return;
    os << ' ';
    printValueList(os, instr.operands, ctx);
}

/// @brief Render the trap kind operand, mapping integers to tokens when possible.
/// @param instr Instruction containing the operand.
/// @param os Stream receiving the textual operand.
/// @param ctx Serialization context with value name mappings.
void printTrapKindOperand(const Instr &instr, std::ostream &os, const SerializeContext &ctx)
{
    if (instr.operands.empty())
        return;
    const auto &operand = instr.operands.front();
    if (operand.kind == Value::Kind::ConstInt)
    {
        if (auto token = trapKindTokenFromValue(operand.i64))
        {
            os << ' ' << *token;
            return;
        }
    }
    os << ' ';
    printValue(os, operand, ctx);
}

/// @brief Emit operands for trap.from.err instructions.
/// @param instr Instruction containing the type and optional operand.
/// @param os Stream receiving serialized operands.
/// @param ctx Serialization context with value name mappings.
void printTrapFromErrOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx)
{
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty())
    {
        os << ' ';
        printValue(os, instr.operands.front(), ctx);
    }
}

/// @brief Emit operand list for call instructions.
/// @param instr Instruction referencing the callee and operands.
/// @param os Stream receiving serialized operands.
/// @param ctx Serialization context with value name mappings.
void printCallOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx)
{
    os << " @" << instr.callee << "(";
    printValueList(os, instr.operands, ctx);
    os << ')';
}

/// @brief Emit optional return operand for ret instructions.
/// @param instr Return instruction to serialise.
/// @param os Stream receiving serialized operand.
/// @param ctx Serialization context with value name mappings.
void printRetOperand(const Instr &instr, std::ostream &os, const SerializeContext &ctx)
{
    if (instr.operands.empty())
        return;
    os << ' ';
    printValue(os, instr.operands[0], ctx);
}

/// @brief Emit operands for load instructions including type annotation.
/// @param instr Instruction providing the operands.
/// @param os Stream receiving serialized operands.
/// @param ctx Serialization context with value name mappings.
void printLoadOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx)
{
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty())
    {
        os << ", ";
        printValue(os, instr.operands[0], ctx);
    }
}

/// @brief Emit operands for store instructions including type annotation.
/// @param instr Instruction providing the destination and value.
/// @param os Stream receiving serialized operands.
/// @param ctx Serialization context with value name mappings.
void printStoreOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx)
{
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty())
    {
        os << ", ";
        printValue(os, instr.operands[0], ctx);
        if (instr.operands.size() > 1)
        {
            os << ", ";
            printValue(os, instr.operands[1], ctx);
        }
    }
}

/// @brief Emit the branch target label and arguments at a given index.
/// @param instr Instruction containing the branch metadata.
/// @param index Successor index to print.
/// @param os Stream receiving serialized output.
/// @param ctx Serialization context with value name mappings.
void printBranchTarget(const Instr &instr,
                       size_t index,
                       std::ostream &os,
                       const SerializeContext &ctx)
{
    if (index >= instr.labels.size())
        return;
    os << instr.labels[index];
    if (index < instr.brArgs.size() && !instr.brArgs[index].empty())
    {
        os << '(';
        printValueList(os, instr.brArgs[index], ctx);
        os << ')';
    }
}

/// @brief Emit a caret-prefixed branch target for resume instructions.
/// @param instr Instruction containing the branch metadata.
/// @param index Successor index to print.
/// @param os Stream receiving serialized output.
/// @param ctx Serialization context with value name mappings.
void printCaretBranchTarget(const Instr &instr,
                            size_t index,
                            std::ostream &os,
                            const SerializeContext &ctx)
{
    if (index >= instr.labels.size())
        return;
    os << '^' << instr.labels[index];
    if (index < instr.brArgs.size() && !instr.brArgs[index].empty())
    {
        os << '(';
        printValueList(os, instr.brArgs[index], ctx);
        os << ')';
    }
}

/// @brief Emit operands for unconditional branch instructions.
/// @param instr Branch instruction to serialise.
/// @param os Stream receiving serialized output.
/// @param ctx Serialization context with value name mappings.
void printBrOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx)
{
    if (instr.labels.empty())
        return;
    os << ' ';
    printBranchTarget(instr, 0, os, ctx);
}

/// @brief Emit operands for conditional branch instructions.
/// @param instr Branch instruction to serialise.
/// @param os Stream receiving serialized output.
/// @param ctx Serialization context with value name mappings.
void printCBrOperands(const Instr &instr, std::ostream &os, const SerializeContext &ctx)
{
    if (instr.operands.empty())
    {
        os << " ; missing label";
        return;
    }

    os << ' ';
    printValue(os, instr.operands[0], ctx);

    if (instr.labels.empty())
    {
        os << " ; missing label";
        return;
    }

    os << ", ";
    printBranchTarget(instr, 0, os, ctx);

    if (instr.labels.size() >= 2)
    {
        os << ", ";
        printBranchTarget(instr, 1, os, ctx);
    }
    else
    {
        os << " ; missing label";
    }
}

/// @brief Emit operands for switch.i32 instructions including case table.
/// @param instr Switch instruction providing the scrutinee and cases.
/// @param os Stream receiving serialized output.
/// @param ctx Serialization context with value name mappings.
void printSwitchI32Operands(const Instr &instr, std::ostream &os, const SerializeContext &ctx)
{
    if (instr.operands.empty() || instr.labels.empty())
        return;

    os << ' ';
    printValue(os, switchScrutinee(instr), ctx);
    os << ", ";
    printCaretBranchTarget(instr, 0, os, ctx);

    const size_t caseCount = switchCaseCount(instr);
    for (size_t idx = 0; idx < caseCount; ++idx)
    {
        os << ", ";
        printValue(os, switchCaseValue(instr, idx), ctx);
        os << " -> ";
        printCaretBranchTarget(instr, idx + 1, os, ctx);
    }
}

/// @brief Retrieve the formatter function for a given opcode.
/// @param op Opcode to format.
/// @return Function object responsible for serialising operands of @p op.
const Formatter &formatterFor(Opcode op)
{
    static const auto formatters = []
    {
        std::array<Formatter, kNumOpcodes> table;
        table.fill(&printDefaultOperands);
        table[toIndex(Opcode::Call)] = &printCallOperands;
        table[toIndex(Opcode::Ret)] = &printRetOperand;
        table[toIndex(Opcode::Br)] = &printBrOperands;
        table[toIndex(Opcode::CBr)] = &printCBrOperands;
        table[toIndex(Opcode::SwitchI32)] = &printSwitchI32Operands;
        table[toIndex(Opcode::Load)] = &printLoadOperands;
        table[toIndex(Opcode::Store)] = &printStoreOperands;
        table[toIndex(Opcode::TrapKind)] = &printTrapKindOperand;
        table[toIndex(Opcode::TrapFromErr)] = &printTrapFromErrOperands;
        table[toIndex(Opcode::EhPush)] =
            [](const Instr &instr, std::ostream &os, const SerializeContext &ctx)
        {
            if (!instr.labels.empty())
            {
                os << ' ';
                printCaretBranchTarget(instr, 0, os, ctx);
            }
        };
        table[toIndex(Opcode::ResumeLabel)] =
            [](const Instr &instr, std::ostream &os, const SerializeContext &ctx)
        {
            if (!instr.operands.empty())
            {
                os << ' ';
                printValue(os, instr.operands[0], ctx);
            }
            if (!instr.labels.empty())
            {
                os << ", ";
                printCaretBranchTarget(instr, 0, os, ctx);
            }
        };
        return table;
    }();
    return formatters[toIndex(op)];
}

/// @brief Emit a single extern declaration following canonical IL syntax.
/// @param e Imported function descriptor to serialise; not owned.
/// @param os Stream that receives the textual representation; not owned.
void printExtern(const Extern &e, std::ostream &os)
{
    os << "extern @" << e.name << "(";
    for (size_t i = 0; i < e.params.size(); ++i)
    {
        if (i)
            os << ", ";
        os << e.params[i].toString();
    }
    os << ") -> " << e.retType.toString() << "\n";
}

/// @brief Determine the default result type kind for an opcode.
/// @param info Opcode metadata describing result categories.
/// @return Matching kind when the opcode produces a fixed result type; empty optional otherwise.
std::optional<Type::Kind> defaultResultKind(const OpcodeInfo &info)
{
    using Kind = Type::Kind;
    switch (info.resultType)
    {
        case TypeCategory::I1:
            return Kind::I1;
        case TypeCategory::I32:
            return Kind::I32;
        case TypeCategory::I64:
            return Kind::I64;
        case TypeCategory::F64:
            return Kind::F64;
        case TypeCategory::Ptr:
            return Kind::Ptr;
        case TypeCategory::Str:
            return Kind::Str;
        case TypeCategory::Error:
            return Kind::Error;
        case TypeCategory::ResumeTok:
            return Kind::ResumeTok;
        default:
            break;
    }
    return std::nullopt;
}

/// @brief Identify whether a basic block models an exception handler entry.
/// @param bb Block to inspect.
/// @return @c true when @p bb begins with `eh.entry` and carries error/resume parameters.
bool isHandlerBlock(const BasicBlock &bb)
{
    if (bb.instructions.empty())
        return false;
    if (bb.instructions.front().op != Opcode::EhEntry)
        return false;
    if (bb.params.size() < 2)
        return false;
    if (bb.params[0].type.kind != Type::Kind::Error)
        return false;
    if (bb.params[1].type.kind != Type::Kind::ResumeTok)
        return false;
    return true;
}

/// @brief Emit a single instruction.
/// @param in Instruction to serialize; operands and labels must satisfy
///           opcode-specific invariants.
/// @param os Stream that receives text output; not owned.
/// @param ctx Serialization context with value name mappings.
/// @format Begins with optional `.loc` metadata, then prints result, opcode,
///         and operands according to `Opcode`. Branches emit labels and
///         associated arguments. Values and types use `toString()` helpers.
/// @assumptions `in`'s operand and label vectors are sized appropriately for
///              its opcode (e.g., `Call` has `callee` and operands, `Br`
///              provides at most one label, `CBr` provides two). The function
///              assumes `os` remains valid for the duration of the call.
void printInstr(const Instr &in, std::ostream &os, const SerializeContext &ctx)
{
    if (in.loc.isValid())
        os << "  .loc " << in.loc.file_id << ' ' << in.loc.line << ' ' << in.loc.column << "\n";
    os << "  ";
    const auto &info = getOpcodeInfo(in.op);
    if (in.result)
    {
        // Use named result if available for readability
        auto name = ctx.nameForTemp(*in.result);
        if (!name.empty() && name[0] != '%')
            os << '%' << name;
        else
            os << "%t" << *in.result;
        if (auto def = defaultResultKind(info))
        {
            if (in.type.kind != *def)
                os << ':' << in.type.toString();
        }
        os << " = ";
    }
    os << il::core::toString(in.op);
    const auto &formatter = formatterFor(in.op);
    formatter(in, os, ctx);
    os << "\n";
}

} // namespace

/// @brief Serialize an IL module into a textual stream.
/// @param m Module to serialize; not owned.
/// @param os Stream that receives output; not owned.
/// @param mode Controls whether externs are emitted canonically or in definition order.
/// Workflow: print the IL version header, emit externs (sorting them when canonical),
/// then globals and functions by walking their basic blocks and delegating instruction
/// formatting to @c printInstr.
/// @returns Nothing; the serialized form is written directly to @p os.
void Serializer::write(const Module &m, std::ostream &os, Mode mode)
{
    os << "il " << m.version << "\n";
    if (m.target)
        os << "target \"" << *m.target << "\"\n";
    if (mode == Mode::Canonical)
    {
        std::vector<Extern> ex(m.externs.begin(), m.externs.end());
        std::sort(
            ex.begin(), ex.end(), [](const Extern &a, const Extern &b) { return a.name < b.name; });
        for (const auto &e : ex)
            printExtern(e, os);
    }
    else
    {
        for (const auto &e : m.externs)
            printExtern(e, os);
    }

    for (const auto &g : m.globals)
    {
        os << "global const " << g.type.toString() << " @" << g.name << " = \""
           << encodeEscapedString(g.init) << "\"\n";
    }

    for (const auto &f : m.functions)
    {
        // Create serialization context with this function's value names
        SerializeContext ctx;
        ctx.valueNames = &f.valueNames;

        os << "func @" << f.name << "(";
        for (size_t i = 0; i < f.params.size(); ++i)
        {
            if (i)
                os << ", ";
            os << f.params[i].type.toString() << " %" << f.params[i].name;
        }
        os << ") -> " << f.retType.toString() << " {\n";
        for (const auto &bb : f.blocks)
        {
            const bool handler = isHandlerBlock(bb);
            if (handler)
                os << "handler ^" << bb.label;
            else
                os << bb.label;
            if (!bb.params.empty())
            {
                os << '(';
                for (size_t i = 0; i < bb.params.size(); ++i)
                {
                    if (i)
                        os << ", ";
                    os << '%' << bb.params[i].name << ':';
                    if (handler)
                    {
                        if (bb.params[i].type.kind == Type::Kind::Error)
                            os << "Error";
                        else if (bb.params[i].type.kind == Type::Kind::ResumeTok)
                            os << "ResumeTok";
                        else
                            os << bb.params[i].type.toString();
                    }
                    else
                    {
                        os << bb.params[i].type.toString();
                    }
                }
                os << ')';
            }
            os << ":\n";
            for (const auto &in : bb.instructions)
                printInstr(in, os, ctx);
        }
        os << "}\n";
    }
}

/// @brief Materialize a module's textual IL into an owned string.
/// @param m Module to serialize; not owned.
/// @param mode Printing strategy forwarded to @c write.
/// Workflow: accumulate output in an @c ostringstream by delegating to @c write
/// with the requested @p mode and return the resulting buffer.
/// @returns Canonical or declared-order IL text depending on @p mode.
std::string Serializer::toString(const Module &m, Mode mode)
{
    std::ostringstream oss;
    write(m, oss, mode);
    return oss.str();
}

} // namespace il::io
