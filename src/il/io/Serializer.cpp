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
            os << " label " << in.labels[0];
    }
    else if (in.op == Opcode::CBr)
    {
        os << " " << il::core::toString(in.operands[0]) << ", label " << in.labels[0] << ", label "
           << in.labels[1];
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
    os << "il 0.1\n";
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
            os << bb.label << ":\n";
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
