// File: tests/unit/test_basic_lowerer_runtime_helpers.cpp
// Purpose: Verify BASIC lowering requests runtime helpers via the shared AST walker.
// Key invariants: Array assignment, PRINT #, and INPUT trigger their respective helpers.
// Ownership: Test constructs AST via parser and owns emitted module.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>
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
    SourceManager sm;
    uint32_t fid = sm.addFile("runtime_walk.bas");
    const std::string src = "10 DIM A(3)\n"
                            "20 LET A(1) = 5\n"
                            "30 OPEN \"out.dat\" FOR OUTPUT AS #1\n"
                            "40 PRINT #1, 42\n"
                            "50 INPUT X, Y$\n"
                            "60 CLOSE #1\n";

    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*program);

    auto names = collectExternNames(module);
    assert(names.count("rt_arr_i32_set") == 1);
    assert(names.count("rt_split_fields") == 1);
    assert(names.count("rt_to_int") == 1);

    const std::string stringHelpers[] = {
        "rt_str_i16_alloc",
        "rt_str_i32_alloc",
        "rt_str_f_alloc",
        "rt_str_d_alloc",
    };
    bool foundStringHelper = false;
    for (const auto &helper : stringHelpers)
        foundStringHelper = foundStringHelper || names.count(helper) == 1;
    assert(foundStringHelper);
    return 0;
}
