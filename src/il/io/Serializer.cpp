//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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
#include "il/io/ParserUtil.hpp"
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

using Formatter = std::function<void(const Instr &, std::ostream &)>;

/// @brief Convert an opcode enumerator into an array index.
/// @param op Opcode value to convert.
/// @return Zero-based index suitable for array lookups.
constexpr size_t toIndex(Opcode op)
{
    return static_cast<size_t>(op);
}

/// @brief Format a value operand into the textual representation used by IL.
/// @param value Operand to serialise.
/// @return Printable string capturing the operand, including string escapes.
std::string formatValue(const Value &value)
{
    if (value.kind == Value::Kind::ConstStr)
        return std::string("\"") + encodeEscapedString(value.str) + "\"";
    return il::core::toString(value);
}

/// @brief Emit a comma-separated list of operands to a stream.
/// @param os Stream receiving the textual operands.
/// @param values Sequence of operands to print.
void printValueList(std::ostream &os, const std::vector<Value> &values)
{
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i)
            os << ", ";
        os << formatValue(values[i]);
    }
}

/// @brief Serialize standard operand lists for instructions.
/// @param instr Instruction providing operands.
/// @param os Stream receiving the serialized operands.
void printDefaultOperands(const Instr &instr, std::ostream &os)
{
    if (instr.operands.empty())
        return;
    os << ' ';
    printValueList(os, instr.operands);
}

/// @brief Render the trap kind operand, mapping integers to tokens when possible.
/// @param instr Instruction containing the operand.
/// @param os Stream receiving the textual operand.
void printTrapKindOperand(const Instr &instr, std::ostream &os)
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
    os << ' ' << formatValue(operand);
}

/// @brief Emit operands for trap.from.err instructions.
/// @param instr Instruction containing the type and optional operand.
/// @param os Stream receiving serialized operands.
void printTrapFromErrOperands(const Instr &instr, std::ostream &os)
{
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty())
        os << ' ' << formatValue(instr.operands.front());
}

/// @brief Emit operand list for call instructions.
/// @param instr Instruction referencing the callee and operands.
/// @param os Stream receiving serialized operands.
void printCallOperands(const Instr &instr, std::ostream &os)
{
    os << " @" << instr.callee << "(";
    printValueList(os, instr.operands);
    os << ')';
}

/// @brief Emit optional return operand for ret instructions.
/// @param instr Return instruction to serialise.
/// @param os Stream receiving serialized operand.
void printRetOperand(const Instr &instr, std::ostream &os)
{
    if (instr.operands.empty())
        return;
    os << ' ' << formatValue(instr.operands[0]);
}

/// @brief Emit operands for load instructions including type annotation.
/// @param instr Instruction providing the operands.
/// @param os Stream receiving serialized operands.
void printLoadOperands(const Instr &instr, std::ostream &os)
{
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty())
        os << ", " << formatValue(instr.operands[0]);
}

/// @brief Emit operands for store instructions including type annotation.
/// @param instr Instruction providing the destination and value.
/// @param os Stream receiving serialized operands.
void printStoreOperands(const Instr &instr, std::ostream &os)
{
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty())
    {
        os << ", " << formatValue(instr.operands[0]);
        if (instr.operands.size() > 1)
            os << ", " << formatValue(instr.operands[1]);
    }
}

/// @brief Emit the branch target label and arguments at a given index.
/// @param instr Instruction containing the branch metadata.
/// @param index Successor index to print.
/// @param os Stream receiving serialized output.
void printBranchTarget(const Instr &instr, size_t index, std::ostream &os)
{
    if (index >= instr.labels.size())
        return;
    os << instr.labels[index];
    if (index < instr.brArgs.size() && !instr.brArgs[index].empty())
    {
        os << '(';
        printValueList(os, instr.brArgs[index]);
        os << ')';
    }
}

/// @brief Emit a caret-prefixed branch target for resume instructions.
/// @param instr Instruction containing the branch metadata.
/// @param index Successor index to print.
/// @param os Stream receiving serialized output.
void printCaretBranchTarget(const Instr &instr, size_t index, std::ostream &os)
{
    if (index >= instr.labels.size())
        return;
    os << '^' << instr.labels[index];
    if (index < instr.brArgs.size() && !instr.brArgs[index].empty())
    {
        os << '(';
        printValueList(os, instr.brArgs[index]);
        os << ')';
    }
}

/// @brief Emit operands for unconditional branch instructions.
/// @param instr Branch instruction to serialise.
/// @param os Stream receiving serialized output.
void printBrOperands(const Instr &instr, std::ostream &os)
{
    if (instr.labels.empty())
        return;
    os << ' ';
    printBranchTarget(instr, 0, os);
}

/// @brief Emit operands for conditional branch instructions.
/// @param instr Branch instruction to serialise.
/// @param os Stream receiving serialized output.
void printCBrOperands(const Instr &instr, std::ostream &os)
{
    if (instr.operands.empty())
    {
        os << " ; missing label";
        return;
    }

    os << ' ' << formatValue(instr.operands[0]);

    if (instr.labels.empty())
    {
        os << " ; missing label";
        return;
    }

    os << ", ";
    printBranchTarget(instr, 0, os);

    if (instr.labels.size() >= 2)
    {
        os << ", ";
        printBranchTarget(instr, 1, os);
    }
    else
    {
        os << " ; missing label";
    }
}

/// @brief Emit operands for switch.i32 instructions including case table.
/// @param instr Switch instruction providing the scrutinee and cases.
/// @param os Stream receiving serialized output.
void printSwitchI32Operands(const Instr &instr, std::ostream &os)
{
    if (instr.operands.empty() || instr.labels.empty())
        return;

    os << ' ' << formatValue(switchScrutinee(instr));
    os << ", ";
    printCaretBranchTarget(instr, 0, os);

    const size_t caseCount = switchCaseCount(instr);
    for (size_t idx = 0; idx < caseCount; ++idx)
    {
        os << ", " << formatValue(switchCaseValue(instr, idx)) << " -> ";
        printCaretBranchTarget(instr, idx + 1, os);
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
        for (auto &fmt : table)
        {
            fmt = [](const Instr &instr, std::ostream &os) { printDefaultOperands(instr, os); };
        }
        table[toIndex(Opcode::Call)] = [](const Instr &instr, std::ostream &os)
        { printCallOperands(instr, os); };
        table[toIndex(Opcode::Ret)] = [](const Instr &instr, std::ostream &os)
        { printRetOperand(instr, os); };
        table[toIndex(Opcode::Br)] = [](const Instr &instr, std::ostream &os)
        { printBrOperands(instr, os); };
        table[toIndex(Opcode::CBr)] = [](const Instr &instr, std::ostream &os)
        { printCBrOperands(instr, os); };
        table[toIndex(Opcode::SwitchI32)] = [](const Instr &instr, std::ostream &os)
        { printSwitchI32Operands(instr, os); };
        table[toIndex(Opcode::Load)] = [](const Instr &instr, std::ostream &os)
        { printLoadOperands(instr, os); };
        table[toIndex(Opcode::Store)] = [](const Instr &instr, std::ostream &os)
        { printStoreOperands(instr, os); };
        table[toIndex(Opcode::TrapKind)] = [](const Instr &instr, std::ostream &os)
        { printTrapKindOperand(instr, os); };
        table[toIndex(Opcode::TrapFromErr)] = [](const Instr &instr, std::ostream &os)
        { printTrapFromErrOperands(instr, os); };
        table[toIndex(Opcode::EhPush)] = [](const Instr &instr, std::ostream &os)
        {
            if (!instr.labels.empty())
            {
                os << ' ';
                printCaretBranchTarget(instr, 0, os);
            }
        };
        table[toIndex(Opcode::ResumeLabel)] = [](const Instr &instr, std::ostream &os)
        {
            if (!instr.operands.empty())
                os << ' ' << formatValue(instr.operands[0]);
            if (!instr.labels.empty())
            {
                os << ", ";
                printCaretBranchTarget(instr, 0, os);
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
/// @format Begins with optional `.loc` metadata, then prints result, opcode,
///         and operands according to `Opcode`. Branches emit labels and
///         associated arguments. Values and types use `toString()` helpers.
/// @assumptions `in`'s operand and label vectors are sized appropriately for
///              its opcode (e.g., `Call` has `callee` and operands, `Br`
///              provides at most one label, `CBr` provides two). The function
///              assumes `os` remains valid for the duration of the call.
void printInstr(const Instr &in, std::ostream &os)
{
    if (in.loc.isValid())
        os << "  .loc " << in.loc.file_id << ' ' << in.loc.line << ' ' << in.loc.column << "\n";
    os << "  ";
    const auto &info = getOpcodeInfo(in.op);
    if (in.result)
    {
        os << "%t" << *in.result;
        if (auto def = defaultResultKind(info))
        {
            if (in.type.kind != *def)
                os << ':' << in.type.toString();
        }
        else if (info.resultType == TypeCategory::InstrType && in.type.kind == Type::Kind::F32)
        {
            os << ':' << in.type.toString();
        }
        os << " = ";
    }
    os << il::core::toString(in.op);
    const auto &formatter = formatterFor(in.op);
    formatter(in, os);
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
                printInstr(in, os);
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
