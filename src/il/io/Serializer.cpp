//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the IL text serializer.  Helper routines format values, types,
// and instruction operands so modules can be rendered deterministically for
// diagnostics and golden tests.
//
//===----------------------------------------------------------------------===//

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

/// @brief Convert an opcode enum into an index for formatter tables.
/// @details Uses the underlying integer value of the opcode so helper arrays
///          keyed by opcode can be indexed without casting at each call site.
/// @param op Opcode to translate.
/// @return Zero-based index corresponding to @p op.
constexpr size_t toIndex(Opcode op)
{
    return static_cast<size_t>(op);
}

/// @brief Render a value into its textual IL representation.
/// @details Special-cases string literals to ensure embedded characters are
///          escaped consistently; all other values delegate to Value::toString().
/// @param value Operand to format.
/// @return String suitable for direct emission in IL text.
std::string formatValue(const Value &value)
{
    if (value.kind == Value::Kind::ConstStr)
        return std::string("\"") + encodeEscapedString(value.str) + "\"";
    return il::core::toString(value);
}

/// @brief Emit a comma-separated list of values.
/// @details Iterates over the provided vector and prints each operand using
///          @ref formatValue while inserting separators between entries.
/// @param os Stream that receives formatted text.
/// @param values Operands to print in order.
void printValueList(std::ostream &os, const std::vector<Value> &values)
{
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i)
            os << ", ";
        os << formatValue(values[i]);
    }
}

/// @brief Print a space-separated operand list for non-special opcodes.
/// @details Emits a leading space and then formats every operand using
///          @ref printValueList, skipping output entirely when no operands are
///          present.
/// @param instr Instruction providing operand data.
/// @param os Stream that receives textual output.
void printDefaultOperands(const Instr &instr, std::ostream &os)
{
    if (instr.operands.empty())
        return;
    os << ' ';
    printValueList(os, instr.operands);
}

/// @brief Pretty-print the trap kind operand.
/// @details Attempts to translate integer trap tokens into their mnemonic form;
///          falls back to generic value formatting when the operand is missing or
///          unrecognised.
/// @param instr Instruction supplying the operand.
/// @param os Stream that receives textual output.
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

/// @brief Render the operands for @c trap.from_err.
/// @details Prints the expected error type followed by the optional payload
///          operand when present.
/// @param instr Instruction supplying the operands.
/// @param os Stream that receives textual output.
void printTrapFromErrOperands(const Instr &instr, std::ostream &os)
{
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty())
        os << ' ' << formatValue(instr.operands.front());
}

/// @brief Emit the callee and operand list for call instructions.
/// @details Writes the callee symbol prefixed with @ and delegates operand
///          rendering to @ref printValueList.
/// @param instr Instruction describing the call.
/// @param os Stream that receives textual output.
void printCallOperands(const Instr &instr, std::ostream &os)
{
    os << " @" << instr.callee << "(";
    printValueList(os, instr.operands);
    os << ')';
}

/// @brief Emit the return value operand when present.
/// @details Prints a leading space followed by the single operand formatted via
///          @ref formatValue.  No output is produced for void returns.
/// @param instr Return instruction to format.
/// @param os Stream that receives textual output.
void printRetOperand(const Instr &instr, std::ostream &os)
{
    if (instr.operands.empty())
        return;
    os << ' ' << formatValue(instr.operands[0]);
}

/// @brief Emit the type and optional pointer operand for loads.
/// @details Always prints the result type; if an address operand exists it is
///          appended after a comma.
/// @param instr Load instruction to format.
/// @param os Stream that receives textual output.
void printLoadOperands(const Instr &instr, std::ostream &os)
{
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty())
        os << ", " << formatValue(instr.operands[0]);
}

/// @brief Emit the type, destination, and stored value for stores.
/// @details Prints the pointee type followed by one or two operands depending on
///          whether the instruction encodes an address and value pair.
/// @param instr Store instruction to format.
/// @param os Stream that receives textual output.
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

/// @brief Print a branch label and its argument list.
/// @details Emits the requested label and, when present, renders argument values
///          in parentheses using @ref printValueList.
/// @param instr Instruction containing branch metadata.
/// @param index Position of the label to print.
/// @param os Stream that receives textual output.
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

/// @brief Print a branch label prefixed with a caret for handler targets.
/// @details Mirrors @ref printBranchTarget but prefixes the label with ^ to match
///          handler syntax.
/// @param instr Instruction containing branch metadata.
/// @param index Position of the label to print.
/// @param os Stream that receives textual output.
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

/// @brief Format the operands for an unconditional branch.
/// @details Emits the sole branch target when present; missing labels are left
///          unprinted so diagnostics can append comments elsewhere.
/// @param instr Branch instruction to format.
/// @param os Stream that receives textual output.
void printBrOperands(const Instr &instr, std::ostream &os)
{
    if (instr.labels.empty())
        return;
    os << ' ';
    printBranchTarget(instr, 0, os);
}

/// @brief Format the operands for a conditional branch.
/// @details Prints the condition operand followed by up to two branch targets,
///          adding explanatory comments when labels are absent.
/// @param instr Branch instruction to format.
/// @param os Stream that receives textual output.
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

/// @brief Emit the scrutinee and case table for @c switch.i32.
/// @details Prints the discriminant value, the default target, and each case
///          mapping as `<value> -> ^label` pairs.
/// @param instr Switch instruction supplying operands and labels.
/// @param os Stream that receives textual output.
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

/// @brief Retrieve the operand formatter associated with an opcode.
/// @details Lazily initialises a table of lambdas, defaulting to
///          @ref printDefaultOperands and overriding entries that require special
///          formatting logic.
/// @param op Opcode whose formatter is requested.
/// @return Reference to the formatter functor.
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

/// @brief Emit a single extern declaration.
/// @details Prints the extern signature as `extern @name(params) -> type` using
///          the canonical `Type::toString()` representation for each parameter
///          and the return type.  No ownership is transferred to the stream.
/// @param e Describes the imported function and its signature; not owned.
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

/// @brief Determine the canonical result type for an opcode.
/// @details Maps opcode metadata to a @ref Type::Kind so printers can elide
///          explicit type annotations when the instruction uses the expected
///          result type.
/// @param info Opcode metadata descriptor.
/// @return Matching type kind or std::nullopt when no default exists.
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

/// @brief Test whether a basic block begins an exception handler.
/// @details Checks for the @c eh.entry opcode and verifies the implicit error and
///          resume-token parameters that handlers must expose.
/// @param bb Block to inspect.
/// @return True when @p bb conforms to the handler signature.
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
/// @details Optionally writes `.loc` metadata, prints the result with a type
///          annotation when it deviates from the opcode's default, and then
///          dispatches to the opcode-specific formatter to render operands and
///          labels.  The routine assumes the instruction's operand and label
///          vectors follow the invariants enforced by the verifier.
/// @param in Instruction to serialize; operands and labels must satisfy
///           opcode-specific invariants.
/// @param os Stream that receives text output; not owned.
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
        os << " = ";
    }
    os << il::core::toString(in.op);
    const auto &formatter = formatterFor(in.op);
    formatter(in, os);
    os << "\n";
}

} // namespace

/// @brief Serialize an IL module into a textual stream.
/// @details Writes the module header, target triple, extern declarations (sorted
///          when canonical output is requested), followed by globals and
///          functions.  Each basic block is rendered with handler annotations when
///          necessary and instructions delegate to @ref printInstr for operand
///          formatting.
/// @param m Module to serialize; not owned.
/// @param os Stream that receives output; not owned.
/// @param mode Controls whether externs are emitted canonically or in definition order.
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
/// @details Captures the streaming output produced by @ref write inside an
///          @c ostringstream and returns the buffer as a standard string.  The
///          supplied @p mode is forwarded unchanged to control extern ordering.
/// @param m Module to serialize; not owned.
/// @param mode Printing strategy forwarded to @ref write.
/// @return Canonical or declared-order IL text depending on @p mode.
std::string Serializer::toString(const Module &m, Mode mode)
{
    std::ostringstream oss;
    write(m, oss, mode);
    return oss.str();
}

} // namespace il::io
