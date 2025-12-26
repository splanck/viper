//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_verify_release_lifetime.cpp
// Purpose: Validate that the verifier rejects runtime array handle uses after release.
// Key invariants: Intra-block release-after-use and double-release must fail verification.
// Ownership/Lifetime: Constructs modules locally for verification.
// Links: docs/il-guide.md#reference
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
#include "support/source_location.hpp"

#include <cassert>
#include <string>

namespace
{

void appendRuntimeArrayExterns(il::core::Module &module)
{
    using il::core::Extern;
    using il::core::Type;

    Extern release;
    release.name = "rt_arr_i32_release";
    release.retType = Type(Type::Kind::Void);
    release.params.push_back(Type(Type::Kind::Ptr));
    module.externs.push_back(release);

    Extern len;
    len.name = "rt_arr_i32_len";
    len.retType = Type(Type::Kind::I64);
    len.params.push_back(Type(Type::Kind::Ptr));
    module.externs.push_back(len);
}

} // namespace

int main()
{
    using namespace il::core;

    {
        Module module;
        appendRuntimeArrayExterns(module);

        Function fn;
        fn.name = "use_after";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";

        Instr makeNull;
        makeNull.result = 0;
        makeNull.op = Opcode::ConstNull;
        makeNull.type = Type(Type::Kind::Ptr);
        makeNull.loc = {1, 1};

        Instr releaseHandle;
        releaseHandle.op = Opcode::Call;
        releaseHandle.type = Type(Type::Kind::Void);
        releaseHandle.callee = "rt_arr_i32_release";
        releaseHandle.operands.push_back(Value::temp(0));
        releaseHandle.loc = {2, 1};

        Instr lenCall;
        lenCall.result = 1;
        lenCall.op = Opcode::Call;
        lenCall.type = Type(Type::Kind::I64);
        lenCall.callee = "rt_arr_i32_len";
        lenCall.operands.push_back(Value::temp(0));
        lenCall.loc = {3, 1};

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.loc = {4, 1};

        entry.instructions.push_back(makeNull);
        entry.instructions.push_back(releaseHandle);
        entry.instructions.push_back(lenCall);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        auto result = il::verify::Verifier::verify(module);
        assert(!result && "use after release must fail verification");
        assert(result.error().message.find("use after release") != std::string::npos);
    }

    {
        Module module;
        appendRuntimeArrayExterns(module);

        Function fn;
        fn.name = "double_release";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";

        Instr makeNull;
        makeNull.result = 0;
        makeNull.op = Opcode::ConstNull;
        makeNull.type = Type(Type::Kind::Ptr);
        makeNull.loc = {1, 1};

        Instr firstRelease;
        firstRelease.op = Opcode::Call;
        firstRelease.type = Type(Type::Kind::Void);
        firstRelease.callee = "rt_arr_i32_release";
        firstRelease.operands.push_back(Value::temp(0));
        firstRelease.loc = {2, 1};

        Instr secondRelease;
        secondRelease.op = Opcode::Call;
        secondRelease.type = Type(Type::Kind::Void);
        secondRelease.callee = "rt_arr_i32_release";
        secondRelease.operands.push_back(Value::temp(0));
        secondRelease.loc = {3, 1};

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.loc = {4, 1};

        entry.instructions.push_back(makeNull);
        entry.instructions.push_back(firstRelease);
        entry.instructions.push_back(secondRelease);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        auto result = il::verify::Verifier::verify(module);
        assert(!result && "double release must fail verification");
        assert(result.error().message.find("double release") != std::string::npos);
    }

    return 0;
}
