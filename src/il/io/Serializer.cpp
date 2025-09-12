// File: src/il/io/Serializer.cpp
// Purpose: Implements serializer for IL modules to text.
// Key invariants: Output is deterministic in canonical mode.
// Ownership/Lifetime: Serializer does not own modules.
// Links: docs/il-spec.md

#include "il/io/Serializer.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include <algorithm>
#include <sstream>

namespace il::io
{

using namespace il::core;

namespace
{

/// @brief Emit a single extern declaration.
/// @param e Describes the imported function and its signature; not owned.
/// @param os Stream that receives the textual representation; not owned.
/// @format Prints `extern @<name>(<params>) -> <ret>\n` with parameters
///         separated by commas. Types are rendered using `Type::toString()`
///         in their canonical form.
/// @assumptions Parameter and return types are valid per the IL spec.
///              The function does not take ownership of `e` or `os`.
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
    if (in.result)
        os << "%t" << *in.result << " = ";
    os << il::core::toString(in.op);
    if (in.op == Opcode::Call)
    {
        os << " @" << in.callee << "(";
        for (size_t i = 0; i < in.operands.size(); ++i)
        {
            if (i)
                os << ", ";
            os << il::core::toString(in.operands[i]);
        }
        os << ")";
    }
    else if (in.op == Opcode::Ret)
    {
        if (!in.operands.empty())
            os << " " << il::core::toString(in.operands[0]);
    }
    else if (in.op == Opcode::Br)
    {
        if (!in.labels.empty())
        {
            os << " " << in.labels[0];
            if (!in.brArgs.empty() && !in.brArgs[0].empty())
            {
                os << "(";
                for (size_t i = 0; i < in.brArgs[0].size(); ++i)
                {
                    if (i)
                        os << ", ";
                    os << il::core::toString(in.brArgs[0][i]);
                }
                os << ")";
            }
        }
    }
    else if (in.op == Opcode::CBr)
    {
        os << " " << il::core::toString(in.operands[0]);
        if (in.labels.empty())
        {
            os << " ; missing label";
        }
        else
        {
            os << ", " << in.labels[0];
            if (!in.brArgs.empty() && !in.brArgs[0].empty())
            {
                os << "(";
                for (size_t i = 0; i < in.brArgs[0].size(); ++i)
                {
                    if (i)
                        os << ", ";
                    os << il::core::toString(in.brArgs[0][i]);
                }
                os << ")";
            }
            if (in.labels.size() >= 2)
            {
                os << ", " << in.labels[1];
                if (in.brArgs.size() > 1 && !in.brArgs[1].empty())
                {
                    os << "(";
                    for (size_t i = 0; i < in.brArgs[1].size(); ++i)
                    {
                        if (i)
                            os << ", ";
                        os << il::core::toString(in.brArgs[1][i]);
                    }
                    os << ")";
                }
            }
            else
            {
                os << " ; missing label";
            }
        }
    }
    else if (in.op == Opcode::Load)
    {
        os << " " << in.type.toString() << ", " << il::core::toString(in.operands[0]);
    }
    else if (in.op == Opcode::Store)
    {
        os << " " << in.type.toString() << ", " << il::core::toString(in.operands[0]) << ", "
           << il::core::toString(in.operands[1]);
    }
    else
    {
        if (!in.operands.empty())
            os << " ";
        for (size_t i = 0; i < in.operands.size(); ++i)
        {
            if (i)
                os << ", ";
            os << il::core::toString(in.operands[i]);
        }
    }
    os << "\n";
}

} // namespace

void Serializer::write(const Module &m, std::ostream &os, Mode mode)
{
    os << "il " << m.version << "\n";
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
        os << "global const " << g.type.toString() << " @" << g.name << " = \"" << g.init << "\"\n";
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
            os << bb.label;
            if (!bb.params.empty())
            {
                os << '(';
                for (size_t i = 0; i < bb.params.size(); ++i)
                {
                    if (i)
                        os << ", ";
                    os << '%' << bb.params[i].name << ':' << bb.params[i].type.toString();
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

std::string Serializer::toString(const Module &m, Mode mode)
{
    std::ostringstream oss;
    write(m, oss, mode);
    return oss.str();
}

} // namespace il::io
