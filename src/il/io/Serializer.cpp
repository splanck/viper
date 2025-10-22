//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/Serializer.cpp
// Purpose: Render IL modules, functions, and instructions into deterministic
//          textual form for tooling and round-tripping.
// Key invariants: Canonical mode sorts extern declarations, handler blocks are
//                 annotated consistently, and every operand list mirrors the
//                 operand ordering prescribed by the IL specification.
// Ownership/Lifetime: The serializer is stateless; helper routines borrow the
//                     supplied module and streams without retaining references
//                     beyond the call site.
// Links: docs/il-guide.md#reference, docs/codemap.md#il-io
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

/// @brief Convert an opcode enumerator to an index usable for lookup tables.
///
/// @details The opcode enum is defined densely starting at zero. Casting to
///          @c size_t yields a stable index that aligns with static arrays of
///          per-opcode behaviour (for example @ref formatterFor's dispatcher
///          table).
///
/// @param op Opcode enumerator provided by callers.
/// @return Zero-based index suitable for addressing formatter arrays.
constexpr size_t toIndex(Opcode op)
{
    return static_cast<size_t>(op);
}

/// @brief Convert a value into its textual representation for IL emission.
///
/// @details String constants require escape processing to keep the emitted
///          program parseable. Non-string values defer to @ref il::core::toString
///          which already understands temporaries, integers, floats, and other
///          literal encodings.
///
/// @param value IL value to format.
/// @return Textual representation, including surrounding quotes for strings.
std::string formatValue(const Value &value)
{
    if (value.kind == Value::Kind::ConstStr)
        return std::string("\"") + encodeEscapedString(value.str) + "\"";
    return il::core::toString(value);
}

/// @brief Print a comma-separated list of IL values.
///
/// @details Emits each value using @ref formatValue. Separators are inserted
///          between entries only when necessary so the function gracefully
///          handles single-element or empty ranges.
///
/// @param os Output stream that receives the formatted list.
/// @param values Sequence of operands to print in order.
void printValueList(std::ostream &os, const std::vector<Value> &values)
{
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i)
            os << ", ";
        os << formatValue(values[i]);
    }
}

/// @brief Emit default operand lists separated by commas when present.
///
/// @details Many opcodes simply append their operands after the mnemonic.
///          This helper implements the common behaviour and is reused by the
///          opcode-specific formatter table.
///
/// @param instr Instruction whose operands are to be printed.
/// @param os Destination stream.
void printDefaultOperands(const Instr &instr, std::ostream &os)
{
    if (instr.operands.empty())
        return;
    os << ' ';
    printValueList(os, instr.operands);
}

/// @brief Special-case trap.kind operands to print human-readable tokens.
///
/// @details The operand may encode a trap token enumerator. When recognised the
///          textual token is emitted instead of the numeric literal to improve
///          readability. Unknown or non-integer operands fall back to the
///          generic value printer.
///
/// @param instr Trap instruction providing the operand.
/// @param os Destination stream.
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

/// @brief Print the operands required for @c trap.from_err instructions.
///
/// @details These opcodes encode the desired result type, and optionally an
///          error operand. Both components must be emitted in the textual IL to
///          round-trip correctly.
///
/// @param instr Instruction currently being serialised.
/// @param os Output stream receiving the text form.
void printTrapFromErrOperands(const Instr &instr, std::ostream &os)
{
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty())
        os << ' ' << formatValue(instr.operands.front());
}

/// @brief Serialise call instruction operands and the callee identifier.
///
/// @details Calls print their callee name prefixed with @c '@' followed by a
///          parenthesised, comma-separated argument list. Arguments are always
///          emitted even when empty so the IL syntax remains explicit.
///
/// @param instr Call instruction under serialisation.
/// @param os Destination stream.
void printCallOperands(const Instr &instr, std::ostream &os)
{
    os << " @" << instr.callee << "(";
    printValueList(os, instr.operands);
    os << ')';
}

/// @brief Emit the operand for return instructions when present.
///
/// @details Returns may carry zero or one operands. This helper appends the
///          operand prefixed with a space only when it exists, keeping void
///          returns concise.
///
/// @param instr Return instruction being printed.
/// @param os Destination stream.
void printRetOperand(const Instr &instr, std::ostream &os)
{
    if (instr.operands.empty())
        return;
    os << ' ' << formatValue(instr.operands[0]);
}

/// @brief Serialise load instruction operands and the dereferenced type.
///
/// @details The textual IL spells out the type of the loaded memory as the
///          first operand, followed optionally by the address expression.
///
/// @param instr Load instruction describing the memory read.
/// @param os Destination stream.
void printLoadOperands(const Instr &instr, std::ostream &os)
{
    os << ' ' << instr.type.toString();
    if (!instr.operands.empty())
        os << ", " << formatValue(instr.operands[0]);
}

/// @brief Serialise store instruction operands and the target type.
///
/// @details Stores list the pointee type, then the destination and value
///          operands when provided. Missing operands are tolerated to keep the
///          printer resilient to malformed IR encountered during debugging.
///
/// @param instr Store instruction to print.
/// @param os Destination stream.
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

/// @brief Helper that prints a branch target and its optional argument list.
///
/// @details Branch instructions store successor labels alongside optional lists
///          of forwarded values. When arguments exist they are emitted within
///          parentheses directly after the label.
///
/// @param instr Instruction providing labels and branch arguments.
/// @param index Index of the label/argument pair to emit.
/// @param os Destination stream.
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

/// @brief Variant of @ref printBranchTarget that prefixes labels with '^'.
///
/// @details Certain exception-handling opcodes use caret-prefixed labels in the
///          textual syntax. This helper mirrors @ref printBranchTarget but
///          applies the caret prefix when emitting the label.
///
/// @param instr Instruction providing labels and arguments.
/// @param index Index of the target to print.
/// @param os Destination stream.
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

/// @brief Print operands for unconditional branch instructions.
///
/// @details Branches list only their target label (and optional arguments).
///          The helper leaves malformed instructions untouched so diagnostic
///          dumps still reflect the underlying data.
///
/// @param instr Branch instruction to print.
/// @param os Destination stream.
void printBrOperands(const Instr &instr, std::ostream &os)
{
    if (instr.labels.empty())
        return;
    os << ' ';
    printBranchTarget(instr, 0, os);
}

/// @brief Print operands for conditional branch instructions.
///
/// @details Emits the condition operand followed by two successor labels. When
///          metadata is missing the helper appends inline comments to highlight
///          inconsistencies, which aids debugging corrupted IR.
///
/// @param instr Conditional branch instruction to print.
/// @param os Destination stream.
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

/// @brief Serialise @c switch.i32 operands including case arms.
///
/// @details Prints the scrutinee, default destination, then each case value
///          paired with its caret-prefixed label. Case argument lists piggyback
///          on @ref printCaretBranchTarget so forwarded values appear naturally.
///
/// @param instr Switch instruction to print.
/// @param os Destination stream.
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

/// @brief Retrieve the formatter callback associated with an opcode.
///
/// @details The first invocation constructs a table that defaults to printing
///          operand lists verbatim, then overrides entries for opcodes requiring
///          bespoke formatting. Subsequent calls reuse the cached table.
///
/// @param op Opcode whose formatter is requested.
/// @return Callable that knows how to emit operands for @p op.
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
///
/// @details Prints the canonical IL syntax `extern @name(params) -> ret`.
///          Parameters are separated by commas and use the canonical type
///          spelling. The helper appends a trailing newline so callers can
///          stream multiple externs without manual separators.
///
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

/// @brief Determine the default result kind for an opcode from metadata.
///
/// @details The opcode table enumerates canonical result categories. This
///          helper converts the category into a concrete @ref Type::Kind when
///          possible. Some opcodes (for example void returns) have no default,
///          in which case the result is empty.
///
/// @param info Opcode metadata describing the result category.
/// @return Populated kind when the opcode yields a value, otherwise empty.
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

/// @brief Detect whether a block represents an exception handler entry.
///
/// @details Handler blocks begin with `eh.entry`, expect at least two parameters
///          (error and resume token), and use those canonical types. The
///          serializer reuses this helper to decide whether to prefix block
///          labels with `handler` in the textual IL form.
///
/// @param bb Basic block under inspection.
/// @return @c true when the block satisfies handler conventions.
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

/// @brief Emit a single instruction in canonical textual IL form.
///
/// @details Emits optional location metadata, formats the SSA result (including
///          an explicit type when it differs from the opcode default), prints
///          the mnemonic, and finally dispatches to the opcode formatter to
///          serialise operands and branch metadata. Each instruction ends with a
///          newline so callers can stream multiple instructions directly.
///
/// @param in Instruction to serialise; operands and labels must satisfy
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

/// @brief Serialise an IL module into the textual surface syntax.
///
/// @details Writes the module header, target triple, extern declarations,
///          globals, and functions in order. Externs are optionally sorted for
///          deterministic diffs when @p mode equals @ref Mode::Canonical. Each
///          function prints its parameter list, handler annotations, and
///          instructions by delegating to @ref printInstr.
///
/// @param m Module to serialise; not owned.
/// @param os Stream that receives output; not owned.
/// @param mode Controls whether externs are emitted canonically or in source order.
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

/// @brief Materialise a module's textual IL into an owned string buffer.
///
/// @details Convenience wrapper around @ref write that funnels the output into
///          a @c std::ostringstream so callers can capture the serialised form
///          without managing streams manually.
///
/// @param m Module to serialise; not owned.
/// @param mode Printing strategy forwarded to @ref write.
/// @return Canonical or declared-order IL text depending on @p mode.
std::string Serializer::toString(const Module &m, Mode mode)
{
    std::ostringstream oss;
    write(m, oss, mode);
    return oss.str();
}

} // namespace il::io
