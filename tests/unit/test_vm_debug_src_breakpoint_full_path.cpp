// File: tests/unit/test_vm_debug_src_breakpoint_full_path.cpp
// Purpose: Verify that full-path source breakpoints do not trigger on files
//          that only share the same basename.
// Key invariants: Breakpoints entered with directory information must require a
//                 normalized path match, while basename-only breakpoints remain
//                 permissive.
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

    const auto targetId = sm.addFile("/tmp/examples/foo.bas");
    const auto otherId = sm.addFile("/tmp/other/foo.bas");

    debug.setSourceManager(&sm);
    debug.addBreakSrcLine("/tmp/examples/foo.bas", 5);

    il::core::Instr targetInstr;
    targetInstr.loc.file_id = targetId;
    targetInstr.loc.line = 5;
    targetInstr.loc.column = 1;

    il::core::Instr otherInstr;
    otherInstr.loc.file_id = otherId;
    otherInstr.loc.line = 5;
    otherInstr.loc.column = 1;

    assert(debug.shouldBreakOn(targetInstr));
    assert(!debug.shouldBreakOn(otherInstr));

    debug.resetLastHit();
    debug.addBreakSrcLine("foo.bas", 7);

    il::core::Instr sharedBaseInstr;
    sharedBaseInstr.loc.file_id = otherId;
    sharedBaseInstr.loc.line = 7;
    sharedBaseInstr.loc.column = 1;

    assert(debug.shouldBreakOn(sharedBaseInstr));
    assert(!debug.shouldBreakOn(sharedBaseInstr));

    return 0;
}
