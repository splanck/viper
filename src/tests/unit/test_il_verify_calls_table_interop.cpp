//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_verify_calls_table_interop.cpp
// Purpose: Validate call verification against extern/function tables including success and failure 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <sstream>
#include <string>

int main()
{
    using namespace il::core;

    auto makeFooExtern = []()
    {
        Extern foo;
        foo.name = "foo";
        foo.retType = Type(Type::Kind::I32);
        foo.params = {Type(Type::Kind::I32), Type(Type::Kind::I32)};
        return foo;
    };

    {
        Module module;
        module.externs.push_back(makeFooExtern());

        Function bar;
        bar.name = "bar";
        bar.retType = Type(Type::Kind::I32);

        BasicBlock entry;
        entry.label = "entry";

        Instr firstArg;
        firstArg.result = 0u;
        firstArg.op = Opcode::CastSiNarrowChk;
        firstArg.type = Type(Type::Kind::I32);
        firstArg.operands.push_back(Value::constInt(1));

        Instr secondArg;
        secondArg.result = 1u;
        secondArg.op = Opcode::CastSiNarrowChk;
        secondArg.type = Type(Type::Kind::I32);
        secondArg.operands.push_back(Value::constInt(2));

        Instr call;
        call.result = 2u;
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::I32);
        call.callee = "foo";
        call.operands.push_back(Value::temp(0));
        call.operands.push_back(Value::temp(1));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::I32);
        ret.operands.push_back(Value::temp(2));

        entry.instructions.push_back(firstArg);
        entry.instructions.push_back(secondArg);
        entry.instructions.push_back(call);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        bar.blocks.push_back(entry);
        module.functions.push_back(bar);

        auto result = il::verify::Verifier::verify(module);
        std::ostringstream diag;
        if (!result)
        {
            il::support::printDiag(result.error(), diag);
        }

        assert(result);
        assert(diag.str().empty());
    }

    {
        Module module;
        module.externs.push_back(makeFooExtern());

        Function bar;
        bar.name = "bar";
        bar.retType = Type(Type::Kind::I32);

        BasicBlock entry;
        entry.label = "entry";

        Instr arg;
        arg.result = 0u;
        arg.op = Opcode::CastSiNarrowChk;
        arg.type = Type(Type::Kind::I32);
        arg.operands.push_back(Value::constInt(1));

        Instr call;
        call.result = 1u;
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::I32);
        call.callee = "foo";
        call.operands.push_back(Value::temp(0));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::I32);
        ret.operands.push_back(Value::temp(1));

        entry.instructions.push_back(arg);
        entry.instructions.push_back(call);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        bar.blocks.push_back(entry);
        module.functions.push_back(bar);

        auto result = il::verify::Verifier::verify(module);
        std::ostringstream diag;
        if (!result)
        {
            il::support::printDiag(result.error(), diag);
        }

        assert(!result);
        const std::string diagStr = diag.str();
        const bool mentionsCall = diagStr.find("call arg") != std::string::npos;
        const bool mentionsCount = diagStr.find("count mismatch") != std::string::npos;
        assert(mentionsCall && mentionsCount);
    }

    {
        Module module;
        module.externs.push_back(makeFooExtern());

        Function bar;
        bar.name = "bar";
        bar.retType = Type(Type::Kind::I32);

        BasicBlock entry;
        entry.label = "entry";

        Instr firstArg;
        firstArg.result = 0u;
        firstArg.op = Opcode::CastSiNarrowChk;
        firstArg.type = Type(Type::Kind::I32);
        firstArg.operands.push_back(Value::constInt(1));

        Instr ptrArg;
        ptrArg.result = 1u;
        ptrArg.op = Opcode::ConstNull;
        ptrArg.type = Type(Type::Kind::Ptr);

        Instr call;
        call.result = 2u;
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::I32);
        call.callee = "foo";
        call.operands.push_back(Value::temp(0));
        call.operands.push_back(Value::temp(1));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::I32);
        ret.operands.push_back(Value::temp(2));

        entry.instructions.push_back(firstArg);
        entry.instructions.push_back(ptrArg);
        entry.instructions.push_back(call);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        bar.blocks.push_back(entry);
        module.functions.push_back(bar);

        auto result = il::verify::Verifier::verify(module);
        std::ostringstream diag;
        if (!result)
        {
            il::support::printDiag(result.error(), diag);
        }

        assert(!result);
        const std::string diagStr = diag.str();
        const bool mentionsCall = diagStr.find("call arg") != std::string::npos;
        const bool mentionsType = diagStr.find("type mismatch") != std::string::npos;
        assert(mentionsCall && mentionsType);
    }

    return 0;
}
