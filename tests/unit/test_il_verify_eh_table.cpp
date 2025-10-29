// File: tests/unit/test_il_verify_eh_table.cpp
// Purpose: Validate verifier diagnostics for exception handler table instructions.
// Key invariants: EH stack operations enforce successor arity and resume token typing.
// Ownership/Lifetime: Constructs IL modules locally for verification and discards after use.
// Links: docs/il-guide.md#reference

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>

namespace
{

using namespace il::core;

Module buildEhFixture()
{
    Module module;

    Function fn;
    fn.name = "eh_demo";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";

    Instr push;
    push.op = Opcode::EhPush;
    push.labels.push_back("handler");

    Instr pop;
    pop.op = Opcode::EhPop;

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);

    entry.instructions.push_back(push);
    entry.instructions.push_back(pop);
    entry.instructions.push_back(ret);
    entry.terminated = true;

    BasicBlock handler;
    handler.label = "handler";

    Param errParam;
    errParam.name = "err";
    errParam.id = 10;
    errParam.type = Type(Type::Kind::Error);

    Param tokParam;
    tokParam.name = "tok";
    tokParam.id = 11;
    tokParam.type = Type(Type::Kind::ResumeTok);

    handler.params.push_back(errParam);
    handler.params.push_back(tokParam);

    Instr entryInstr;
    entryInstr.op = Opcode::EhEntry;

    Instr resumeSame;
    resumeSame.op = Opcode::ResumeSame;
    resumeSame.operands.push_back(Value::temp(tokParam.id));

    handler.instructions.push_back(entryInstr);
    handler.instructions.push_back(resumeSame);
    handler.terminated = true;

    fn.blocks.push_back(entry);
    fn.blocks.push_back(handler);

    module.functions.push_back(fn);

    return module;
}

std::string verifyAndCaptureMessage(Module &module)
{
    auto result = il::verify::Verifier::verify(module);
    assert(!result && "verification should fail for negative cases");
    return result.error().message;
}

} // namespace

int main()
{
    {
        Module module = buildEhFixture();

        std::ostringstream diag;
        auto result = il::verify::Verifier::verify(module);
        if (!result)
        {
            const std::string diagMessage = result.error().message;
            std::fprintf(stderr, "%s\n", diagMessage.c_str());
            il::support::printDiag(result.error(), diag);
        }

        assert(result && "balanced push/pop with resume token should verify");
        assert(diag.str().empty());
    }

    {
        Module module = buildEhFixture();
        auto &handler = module.functions.front().blocks[1];
        Instr redefine;
        redefine.result = handler.params[1].id;
        redefine.op = Opcode::IAddOvf;
        redefine.type = Type(Type::Kind::I64);
        redefine.operands.push_back(Value::constInt(0));
        redefine.operands.push_back(Value::constInt(0));
        handler.instructions.insert(handler.instructions.begin() + 1, redefine);

        const std::string message = verifyAndCaptureMessage(module);
        if (message.find("operand type mismatch") == std::string::npos &&
            message.find("operand 0 must be resume_tok") == std::string::npos)
        {
            std::fprintf(stderr, "%s\n", message.c_str());
        }
        const bool typeMismatch = message.find("operand type mismatch") != std::string::npos;
        const bool resumeTokMessage =
            message.find("operand 0 must be resume_tok") != std::string::npos;
        assert(typeMismatch || resumeTokMessage);
    }

    {
        Module module = buildEhFixture();
        auto &entry = module.functions.front().blocks.front();
        auto &pushInstr = entry.instructions.front();
        pushInstr.labels.push_back("duplicate");

        const std::string message = verifyAndCaptureMessage(module);
        if (message.find("expected 1 successor") == std::string::npos)
        {
            std::fprintf(stderr, "%s\n", message.c_str());
        }
        assert(message.find("expected 1 successor") != std::string::npos);
    }

    return 0;
}
