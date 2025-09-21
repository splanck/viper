// File: tests/unit/test_basic_lowerer_collect.cpp
// Purpose: Ensure BASIC lowerer collects variables from all statement visitors.
// Key invariants: RANDOMIZE/RETURN statements must allocate referenced variables.
// Ownership: Test owns constructed AST and module.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

bool entryHasAlloca(const il::core::Function &fn)
{
    if (fn.blocks.empty())
        return false;
    for (const auto &instr : fn.blocks.front().instructions)
    {
        if (instr.op == il::core::Opcode::Alloca)
            return true;
    }
    return false;
}

} // namespace

int main()
{
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    std::string src =
        "10 FUNCTION F()\n"
        "20 RANDOMIZE SEED\n"
        "30 RETURN SEED\n"
        "40 END FUNCTION\n"
        "100 RANDOMIZE MAINSEED\n"
        "110 PRINT MAINSEED\n";

    Parser parser(src, fid);
    auto prog = parser.parseProgram();
    assert(prog);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*prog);

    const il::core::Function *mainFn = nullptr;
    const il::core::Function *funcF = nullptr;
    for (const auto &fn : module.functions)
    {
        if (fn.name == "main")
            mainFn = &fn;
        else if (fn.name == "F")
            funcF = &fn;
    }
    assert(mainFn && funcF);
    assert(entryHasAlloca(*mainFn));
    assert(entryHasAlloca(*funcF));
    return 0;
}
