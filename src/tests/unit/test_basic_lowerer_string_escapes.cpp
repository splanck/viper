//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_lowerer_string_escapes.cpp
// Purpose: Ensure BASIC lowering decodes escapes before emitting string globals.
// Key invariants: Lowered globals store literal characters rather than escape sequences.
// Ownership/Lifetime: Test owns parser, program, and module instances.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>
#include <unordered_map>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    const std::string src = "10 PRINT \"LINE\\n\"\n"
                            "20 PRINT \"quote:\\\"\"\n"
                            "30 PRINT \"tab:\\t\"\n"
                            "40 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("string_escapes.bas");
    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*program);

    std::unordered_map<std::string, std::string> values;
    for (const auto &global : module.globals)
    {
        values[global.name] = global.init;
    }

    bool foundLineBreak = false;
    bool foundQuote = false;
    bool foundTab = false;
    for (const auto &entry : values)
    {
        if (entry.second == "LINE\n")
            foundLineBreak = true;
        if (entry.second == "quote:\"")
            foundQuote = true;
        if (entry.second == "tab:\t")
            foundTab = true;
    }

    assert(foundLineBreak);
    assert(foundQuote);
    assert(foundTab);

    return 0;
}
