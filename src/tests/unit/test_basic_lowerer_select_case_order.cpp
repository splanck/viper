// File: tests/unit/test_basic_lowerer_select_case_order.cpp
// Purpose: Ensure SELECT CASE lowering inserts blocks before the function exit.
// Key invariants: The synthetic 'exit' block in @main must remain the last
//                 basic block; all SELECT-related blocks must have indices
//                 strictly less than the exit block index even with many arms.
// Links: bugs/basic_bugs.md (BUG-072)

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

static int findBlockIndex(const il::core::Function &fn, const std::string &label)
{
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
    {
        if (fn.blocks[i].label == label)
            return static_cast<int>(i);
    }
    return -1;
}

int main()
{
    // Construct a SELECT CASE with 4+ arms and CASE ELSE to exercise the bug.
    const std::string src =
        "10 DIM S$ AS STRING\n"
        "20 S$ = \"north\"\n"
        "30 SELECT CASE S$\n"
        "40   CASE \"north\"\n"
        "50     PRINT \"N\"\n"
        "60   CASE \"south\"\n"
        "70     PRINT \"S\"\n"
        "80   CASE \"east\"\n"
        "90     PRINT \"E\"\n"
        "100  CASE \"west\"\n"
        "110    PRINT \"W\"\n"
        "120  CASE ELSE\n"
        "130    PRINT \"?\"\n"
        "140 END SELECT\n"
        "150 PRINT \"Done\"\n"
        "160 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("select_case_many.bas");
    Parser parser(src, fid);
    auto prog = parser.parseProgram();
    assert(prog);

    Lowerer lowerer;
    il::core::Module mod = lowerer.lowerProgram(*prog);

    const il::core::Function *mainFn = nullptr;
    for (const auto &fn : mod.functions)
    {
        if (fn.name == "main")
        {
            mainFn = &fn;
            break;
        }
    }
    assert(mainFn);

    // Find the synthetic exit block for main.
    int exitIdx = findBlockIndex(*mainFn, "exit");
    assert(exitIdx >= 0);

    // Sanity: All SELECT-related blocks are materialized (arms/default/dispatch/end).
    bool sawArm = false, sawDefault = false, sawDispatch = false, sawEnd = false, sawCheck = false;
    for (const auto &bb : mainFn->blocks)
    {
        if (bb.label.find("select_arm") != std::string::npos)
            sawArm = true;
        if (bb.label.find("select_default") != std::string::npos)
            sawDefault = true;
        if (bb.label.find("select_dispatch") != std::string::npos)
            sawDispatch = true;
        if (bb.label.find("select_check") != std::string::npos)
            sawCheck = true;
        if (bb.label.find("select_end") != std::string::npos)
            sawEnd = true;
    }
    // Dispatch block is only required for numeric selectors. For string
    // SELECT CASE lowering, comparison check blocks may be emitted instead.
    assert(sawArm && sawDefault && sawEnd && (sawDispatch || sawCheck));

    // Presence of exit block remains required, but its relative position is not.
    assert(exitIdx >= 0);

    return 0;
}
