//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_verify_forward_call.cpp
// Purpose: Ensure the verifier resolves forward callee lookups and rejects duplicates.
// Key invariants: Forward calls verify successfully; duplicate function names still fail.
// Ownership/Lifetime: Modules and functions are local to the test.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>
#include <string>

int main()
{
    using namespace il::core;

    {
        Module module;

        Function caller;
        caller.name = "caller";
        caller.retType = Type(Type::Kind::Void);

        BasicBlock callerEntry;
        callerEntry.label = "entry";

        Instr callInstr;
        callInstr.op = Opcode::Call;
        callInstr.type = Type(Type::Kind::Void);
        callInstr.callee = "callee";
        callerEntry.instructions.push_back(callInstr);

        Instr callerRet;
        callerRet.op = Opcode::Ret;
        callerRet.type = Type(Type::Kind::Void);
        callerEntry.instructions.push_back(callerRet);
        callerEntry.terminated = true;

        caller.blocks.push_back(callerEntry);

        Function callee;
        callee.name = "callee";
        callee.retType = Type(Type::Kind::Void);

        BasicBlock calleeEntry;
        calleeEntry.label = "entry";

        Instr calleeRet;
        calleeRet.op = Opcode::Ret;
        calleeRet.type = Type(Type::Kind::Void);
        calleeEntry.instructions.push_back(calleeRet);
        calleeEntry.terminated = true;

        callee.blocks.push_back(calleeEntry);

        module.functions.push_back(caller);
        module.functions.push_back(callee);

        auto forwardResult = il::verify::Verifier::verify(module);
        assert(forwardResult && "verifier should allow calls to later functions");
    }

    {
        Module module;

        auto makeVoidFunction = [](const std::string &name)
        {
            Function fn;
            fn.name = name;
            fn.retType = Type(Type::Kind::Void);

            BasicBlock entry;
            entry.label = "entry";

            Instr ret;
            ret.op = Opcode::Ret;
            ret.type = Type(Type::Kind::Void);
            entry.instructions.push_back(ret);
            entry.terminated = true;

            fn.blocks.push_back(entry);
            return fn;
        };

        module.functions.push_back(makeVoidFunction("dup"));
        module.functions.push_back(makeVoidFunction("dup"));

        auto duplicateResult = il::verify::Verifier::verify(module);
        assert(!duplicateResult && "duplicate function names must still be rejected");
        assert(duplicateResult.error().message.find("duplicate function @dup") !=
               std::string::npos);
    }

    return 0;
}
