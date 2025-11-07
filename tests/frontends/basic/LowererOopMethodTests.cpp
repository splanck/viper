// File: tests/frontends/basic/LowererOopMethodTests.cpp
// Purpose: Verify class method lowering handles return values and call sites.
// Key invariants: Method functions expose non-void signatures and call sites
//                 capture results when expressions use them.
// Ownership/Lifetime: Test owns parser, lowerer, and resulting module.
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
} // namespace

int main()
{
    const std::string src = "10 CLASS M\n"
                             "20   FUNCTION Twice(n AS INTEGER) AS INTEGER\n"
                             "30     RETURN n + n\n"
                             "40   END FUNCTION\n"
                             "50 END CLASS\n"
                             "60 DIM m AS M\n"
                             "70 LET m = NEW M()\n"
                             "80 PRINT m.Twice(21)\n"
                             "90 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("method_return.bas");

    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();
    assert(program);

    Lowerer lowerer;
    lowerer.setDiagnosticEmitter(&emitter);
    il::core::Module module = lowerer.lowerProgram(*program);

    const std::string methodName = mangleMethod("M", "TWICE");
    const il::core::Function *methodFn = findFunction(module, methodName);
    assert(methodFn && "Lowerer must emit method function");
    assert(methodFn->retType.kind == il::core::Type::Kind::I64 &&
           "Method should retain integer return type");

    bool hasRetValue = false;
    for (const auto &block : methodFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::Ret && !instr.operands.empty())
            {
                hasRetValue = true;
                break;
            }
        }
        if (hasRetValue)
            break;
    }
    assert(hasRetValue && "Method must return the computed value");

    const il::core::Function *mainFn = findFunction(module, "main");
    assert(mainFn && "Program lowering should define main");

    bool callCapturesResult = false;
    for (const auto &block : mainFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::Call && instr.callee == methodName)
            {
                callCapturesResult = instr.result.has_value();
                break;
            }
        }
        if (callCapturesResult)
            break;
    }
    assert(callCapturesResult && "Method call should produce a value");

    return 0;
}
