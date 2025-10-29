// File: tests/unit/test_basic_emit_common.cpp
// Purpose: Validate the BASIC emit helpers produce expected IR patterns.
// Key invariants: Checked addition, boolean logic, and narrowing appear with correct opcodes.
// Ownership/Lifetime: Test owns parser, lowerer, and resulting module.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <algorithm>
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    const std::string src = "10 COLOR 1, 2\n"
                            "20 DIM ARR%(2)\n"
                            "30 LET ARR%(0) = 1\n"
                            "40 LET ARR%(1) = ARR%(0) + 1\n"
                            "50 LET L& = ARR%(0) AND ARR%(1)\n"
                            "60 LET M& = ARR%(0) OR ARR%(1)\n"
                            "70 FOR I% = 1 TO 2\n"
                            "80 NEXT I%\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("emit_common.bas");
    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*program);

    const il::core::Function *mainFn = nullptr;
    for (const auto &fn : module.functions)
    {
        if (fn.name == "main")
        {
            mainFn = &fn;
            break;
        }
    }
    assert(mainFn);

    bool sawCheckedAdd = false;
    bool sawNarrowCast = false;
    bool sawLogicalAnd = false;
    bool sawLogicalOr = false;

    for (const auto &block : mainFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::IAddOvf)
                sawCheckedAdd = true;
            if (instr.op == il::core::Opcode::CastSiNarrowChk)
                sawNarrowCast = true;
            if (instr.op == il::core::Opcode::And)
                sawLogicalAnd = true;
            if (instr.op == il::core::Opcode::Or)
                sawLogicalOr = true;
        }
    }

    assert(sawCheckedAdd);
    assert(sawNarrowCast);
    assert(sawLogicalAnd);
    assert(sawLogicalOr);

    return 0;
}
