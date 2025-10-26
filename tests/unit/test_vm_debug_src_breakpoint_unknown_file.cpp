// File: tests/unit/test_vm_debug_src_breakpoint_unknown_file.cpp
// Purpose: Ensure unresolved file ids do not trigger source breakpoints.
// Key invariants: Unknown file ids with matching breakpoints should skip stops.
// Ownership: Standalone unit test executable.
// Links: docs/codemap.md
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include "vm/Debug.hpp"
#include <cassert>

int main()
{
    il::vm::DebugCtrl debug;
    il::support::SourceManager sm;

    debug.setSourceManager(&sm);
    debug.addBreakSrcLine(".", 5);

    il::core::Instr instr;
    instr.loc.file_id = 42; // file id not present in source manager
    instr.loc.line = 5;

    assert(!debug.shouldBreakOn(instr));
    assert(!debug.shouldBreakOn(instr));

    return 0;
}
