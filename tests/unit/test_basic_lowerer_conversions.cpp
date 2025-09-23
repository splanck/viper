// File: tests/unit/test_basic_lowerer_conversions.cpp
// Purpose: Verify BASIC lowerer emits conversions for mixed-type statements.
// Key invariants: Assignments, prints, and inputs coerce values to target types.
// Ownership/Lifetime: Test owns parser, lowerer, and resulting module.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

bool hasLine(const std::vector<int> &lines, int target)
{
    return std::find(lines.begin(), lines.end(), target) != lines.end();
}

} // namespace

int main()
{
    const std::string src =
        "10 DIM FLAG AS BOOLEAN\n"
        "20 LET I = 3.14\n"
        "30 LET D# = 1\n"
        "40 LET I = TRUE\n"
        "50 PRINT TRUE\n"
        "70 INPUT \"?\", FLAG\n"
        "80 INPUT \"?\", D#\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("conversions.bas");
    Parser parser(src, fid);
    auto prog = parser.parseProgram();
    assert(prog);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*prog);

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

    std::vector<int> fptosiLines;
    std::vector<int> sitofpLines;
    std::vector<int> zextLines;
    std::vector<int> truncLines;

    for (const auto &block : mainFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            switch (instr.op)
            {
                case il::core::Opcode::Fptosi:
                    fptosiLines.push_back(instr.loc.line);
                    break;
                case il::core::Opcode::Sitofp:
                    sitofpLines.push_back(instr.loc.line);
                    break;
                case il::core::Opcode::Zext1:
                    zextLines.push_back(instr.loc.line);
                    break;
                case il::core::Opcode::Trunc1:
                    truncLines.push_back(instr.loc.line);
                    break;
                default:
                    break;
            }
        }
    }

    assert(hasLine(fptosiLines, 2));  // LET I = 3.14
    assert(hasLine(sitofpLines, 3));  // LET D# = 1
    assert(hasLine(sitofpLines, 7));  // INPUT "?", D#
    assert(hasLine(zextLines, 4));    // LET I = TRUE
    assert(hasLine(zextLines, 5));    // PRINT TRUE
    assert(hasLine(truncLines, 6));   // INPUT "?", FLAG

    return 0;
}
