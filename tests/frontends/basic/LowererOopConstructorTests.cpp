// File: tests/frontends/basic/LowererOopConstructorTests.cpp
// Purpose: Ensure BASIC lowering synthesises default constructors for classes without SUB NEW.
// Key invariants: Lowered module defines the mangled constructor symbol and NEW expressions
//                 emit calls to it even when the source lacks an explicit constructor.
// Ownership/Lifetime: Test owns parser, lowerer, and produced module per scenario.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/Parser.hpp"
#include "il/core/Instr.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

const il::core::Function *findFunction(const il::core::Module &module, const std::string &name)
{
    for (const auto &fn : module.functions)
    {
        if (fn.name == name)
            return &fn;
    }
    return nullptr;
}

bool functionCallsCtor(const il::core::Function &fn, const std::string &ctorName)
{
    for (const auto &block : fn.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::Call && instr.callee == ctorName)
                return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    const std::string src = "10 CLASS C\n"
                             "20   v AS INTEGER\n"
                             "30   SUB SET()\n"
                             "40     LET v = 7\n"
                             "50   END SUB\n"
                             "60   SUB SHOW()\n"
                             "70     PRINT v\n"
                             "80   END SUB\n"
                             "90 END CLASS\n"
                             "100 DIM c\n"
                             "110 LET c = NEW C()\n"
                             "120 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("default_ctor.bas");

    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();
    assert(program);

    Lowerer lowerer;
    lowerer.setDiagnosticEmitter(&emitter);
    il::core::Module module = lowerer.lowerProgram(*program);

    const std::string ctorName = mangleClassCtor("C");

    const il::core::Function *ctorFn = findFunction(module, ctorName);
    assert(ctorFn && "synthetic constructor missing");
    assert(ctorFn->params.size() == 1 && "constructor should only take self parameter");

    const il::core::Function *mainFn = findFunction(module, "main");
    assert(mainFn && "main function not generated");
    assert(functionCallsCtor(*mainFn, ctorName) && "NEW expression must call constructor");

    return 0;
}

