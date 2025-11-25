//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_lowerer_string_assignment.cpp
// Purpose: Verify BASIC lowerer retains and releases strings on assignment.
// Key invariants: String variables release old values before retaining new ones.
// Ownership/Lifetime: Test owns parser, lowerer, and resulting module.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

std::unordered_set<std::string> collectExternNames(const il::core::Module &module)
{
    std::unordered_set<std::string> names;
    for (const auto &ext : module.externs)
        names.insert(ext.name);
    return names;
}

} // namespace

int main()
{
    const std::string src = "10 LET S$ = \"HELLO\"\n"
                            "20 LET S$ = \"WORLD\"\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("string_assign.bas");
    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*program);

    auto externs = collectExternNames(module);
    assert(externs.count("rt_str_release_maybe") == 1);
    assert(externs.count("rt_str_retain_maybe") == 1);

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

    std::unordered_map<int, int> releaseCounts;
    std::unordered_map<int, int> retainCounts;
    std::unordered_set<int> assignmentLines;

    for (const auto &block : mainFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op != il::core::Opcode::Call)
                continue;
            const int line = instr.loc.line;
            if (instr.callee == "rt_str_release_maybe")
            {
                ++releaseCounts[line];
                assignmentLines.insert(line);
            }
            else if (instr.callee == "rt_str_retain_maybe")
            {
                assert(releaseCounts[line] > 0);
                ++retainCounts[line];
                assignmentLines.insert(line);
            }
        }
    }

    assert(assignmentLines.size() == 2);
    for (int line : assignmentLines)
    {
        assert(releaseCounts[line] == 1);
        assert(retainCounts[line] == 1);
    }

    return 0;
}
