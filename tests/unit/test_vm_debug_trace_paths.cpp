// File: tests/unit/test_vm_debug_trace_paths.cpp
// Purpose: Ensure debugger breakpoints and trace sink agree on normalized paths.
// Key invariants: Normalized filenames use forward slashes and match between components.
// Ownership: Standalone executable.
// Links: docs/codemap.md

#include "support/source_manager.hpp"
#include "vm/Debug.hpp"
#include "vm/Trace.hpp"
#include "vm/VM.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

int main()
{
    il::support::SourceManager sm;
    uint32_t fileId = sm.addFile(R"(C:\project\src\trace_src.bas)");

    il::vm::DebugCtrl debug;
    debug.setSourceManager(&sm);
    debug.addBreakSrcLine(R"(C:\project\src\trace_src.bas)", 5);

    il::core::Instr inst{};
    inst.op = il::core::Opcode::Add;
    inst.loc.file_id = fileId;
    inst.loc.line = 5;
    inst.loc.column = 3;

    il::core::BasicBlock block;
    block.label = "entry";
    block.instructions.push_back(inst);

    il::core::Function fn;
    fn.name = "main";
    fn.blocks.push_back(block);

    il::vm::Frame frame{};
    frame.func = &fn;

    assert(debug.shouldBreakOn(fn.blocks[0].instructions[0]));

    il::vm::TraceConfig cfg;
    cfg.mode = il::vm::TraceConfig::SRC;
    cfg.sm = &sm;
    il::vm::TraceSink sink(cfg);
    sink.onFramePrepared(frame);

    std::ostringstream capture;
    auto *oldBuf = std::cerr.rdbuf(capture.rdbuf());
    sink.onStep(fn.blocks[0].instructions[0], frame);
    std::cerr.rdbuf(oldBuf);

    std::string output = capture.str();
    assert(output.find("trace_src.bas:5:3") != std::string::npos);
    assert(output.find('\\') == std::string::npos);
    return 0;
}

