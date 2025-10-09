// File: tests/unit/test_basic_lowerer_entry_params.cpp
// Purpose: Verify BASIC lowerer mirrors function parameters onto entry blocks.
// Key invariants: Entry block parameter ids/names/types match the function signature.
// Ownership/Lifetime: Test owns parser, lowerer, and resulting module.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <cstddef>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

void checkEntryParams(const il::core::Function &fn, std::size_t expected)
{
    assert(fn.params.size() == expected);
    assert(!fn.blocks.empty());
    const auto &entry = fn.blocks.front();
    assert(entry.params.size() == expected);
    for (std::size_t i = 0; i < expected; ++i)
    {
        assert(entry.params[i].id == fn.params[i].id);
        assert(entry.params[i].name == fn.params[i].name);
        assert(entry.params[i].type.kind == fn.params[i].type.kind);
    }
}

} // namespace

int main()
{
    const std::string src =
        "100 FUNCTION SQRINT%(N%)\n"
        "110 RETURN N% * N%\n"
        "120 END FUNCTION\n"
        "200 FUNCTION ADD%(A%, B%)\n"
        "210 RETURN A% + B%\n"
        "220 END FUNCTION\n"
        "10 PRINT SQRINT%(5)\n"
        "20 PRINT ADD%(2, 3)\n"
        "30 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("functions.bas");
    Parser parser(src, fid);
    auto prog = parser.parseProgram();
    assert(prog);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*prog);

    const il::core::Function *sqr = nullptr;
    const il::core::Function *add = nullptr;
    for (const auto &fn : module.functions)
    {
        if (fn.name == "SQRINT%")
            sqr = &fn;
        else if (fn.name == "ADD%")
            add = &fn;
    }

    assert(sqr);
    assert(add);

    checkEntryParams(*sqr, 1);
    checkEntryParams(*add, 2);

    return 0;
}

