// File: lib/VM/Trace.cpp
// Purpose: Implement deterministic tracing for IL VM steps.
// Key invariants: Each executed instruction produces at most one flushed line.
// Ownership/Lifetime: Uses external streams; no resource ownership.
// Links: docs/dev/vm.md
#include "VM/Trace.h"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace il::vm
{

TraceSink::TraceSink(TraceConfig cfg) : cfg(cfg) {}

const std::string &TraceSink::getLine(const std::string &path, uint32_t line)
{
    auto &lines = cache[path];
    if (lines.empty())
    {
        std::ifstream in(path);
        std::string l;
        while (std::getline(in, l))
            lines.push_back(l);
    }
    static const std::string empty;
    if (line == 0 || line > lines.size())
        return empty;
    return lines[line - 1];
}

void TraceSink::onStep(const il::core::Instr &in, const Frame &fr)
{
    if (!cfg.enabled())
        return;
    const auto *fn = fr.func;
    const il::core::BasicBlock *blk = nullptr;
    size_t ip = 0;
    for (const auto &b : fn->blocks)
    {
        for (size_t i = 0; i < b.instructions.size(); ++i)
        {
            if (&b.instructions[i] == &in)
            {
                blk = &b;
                ip = i;
                break;
            }
        }
        if (blk)
            break;
    }
    if (!blk)
        return;
    if (cfg.mode == TraceConfig::IL)
    {
        std::cerr << "[IL] fn=@" << fn->name << " blk=" << blk->label << " ip=#" << ip
                  << " op=" << il::core::toString(in.op);
        if (!in.operands.empty())
        {
            std::cerr << ' ';
            for (size_t i = 0; i < in.operands.size(); ++i)
            {
                if (i)
                    std::cerr << ", ";
                std::cerr << il::core::toString(in.operands[i]);
            }
        }
        if (in.result)
            std::cerr << " -> %t" << *in.result;
        std::cerr << '\n' << std::flush;
        return;
    }
    if (cfg.mode == TraceConfig::SRC)
    {
        std::cerr << "[SRC] ";
        std::string stmt;
        if (in.loc.isValid() && in.loc.file_id > 0 && in.loc.file_id <= cfg.files.size())
        {
            const std::string &path = cfg.files[in.loc.file_id - 1];
            auto disp = std::filesystem::path(path).filename().string();
            std::cerr << disp << ':' << in.loc.line << ':' << in.loc.column;
            const std::string &line = getLine(path, in.loc.line);
            stmt = line;
            size_t start = stmt.find_first_not_of(" \t");
            if (start != std::string::npos)
                stmt = stmt.substr(start);
            size_t i = 0;
            while (i < stmt.size() && std::isdigit(static_cast<unsigned char>(stmt[i])))
                ++i;
            while (i < stmt.size() && std::isspace(static_cast<unsigned char>(stmt[i])))
                ++i;
            stmt = stmt.substr(i);
        }
        else
        {
            std::cerr << "<unknown>";
        }
        std::cerr << "  (fn=@" << fn->name << " blk=" << blk->label << " ip=#" << ip << ')';
        if (!stmt.empty())
            std::cerr << "  " << stmt;
        std::cerr << '\n' << std::flush;
    }
}

} // namespace il::vm
