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
#include <iostream>

namespace il::vm
{

TraceSink::TraceSink(TraceConfig cfg) : cfg(cfg) {}

void TraceSink::onStep(const il::core::Instr &in, const Frame &fr)
{
    if (!cfg.enabled() || cfg.mode != TraceConfig::IL)
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
}

} // namespace il::vm
