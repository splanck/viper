// File: tests/unit/test_vm_debug_src_breakpoint.cpp
// Purpose: Ensure source breakpoints coalesce repeated hits at the same location.
// Key invariants: Re-executing the same file/line pair without reset should not
// trigger a new breakpoint hit.
// Ownership: Standalone unit test executable.
// Links: docs/codemap.md
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include "viper/vm/debug/Debug.hpp"
#include <cassert>

int main()
{
    il::vm::DebugCtrl debug;
    il::support::SourceManager sm;
    const auto fileId = sm.addFile("/tmp/examples/foo.bas");

    debug.setSourceManager(&sm);
    debug.addBreakSrcLine("/tmp/examples/foo.bas", 7);

    il::core::Instr instr;
    instr.loc.file_id = fileId;
    instr.loc.line = 7;
    instr.loc.column = 1;

    assert(debug.shouldBreakOn(instr));
    assert(!debug.shouldBreakOn(instr));

    debug.resetLastHit();
    assert(debug.shouldBreakOn(instr));

    return 0;
}
