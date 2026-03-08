//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_null_mem_ops.cpp
// Purpose: Verify VM traps when load/store operate on null or misaligned pointers.
// Key invariants: Null or misaligned pointer operands surface InvalidOperation traps with detail
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <string>

namespace
{

il::core::Module makeLoadModule()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr load;
    load.result = 0U;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::null());
    load.loc = {1, 1, 1};
    bb.instructions.push_back(load);

    fn.blocks.push_back(std::move(bb));
    m.functions.push_back(std::move(fn));
    return m;
}

il::core::Module makeStoreModule()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::I64);
    store.operands.push_back(Value::null());
    store.operands.push_back(Value::constInt(42));
    store.loc = {1, 2, 1};
    bb.instructions.push_back(store);

    fn.blocks.push_back(std::move(bb));
    m.functions.push_back(std::move(fn));
    return m;
}

il::core::Module makeMisalignedLoadModule(il::core::Type::Kind kind)
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr alloca;
    alloca.result = 0U;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(16));
    alloca.loc = {1, 1, 1};
    bb.instructions.push_back(alloca);

    Instr gep;
    gep.result = 1U;
    gep.op = Opcode::GEP;
    gep.type = Type(Type::Kind::Ptr);
    gep.operands.push_back(Value::temp(0));
    gep.operands.push_back(Value::constInt(1));
    gep.loc = {1, 2, 1};
    bb.instructions.push_back(gep);

    Instr load;
    load.result = 2U;
    load.op = Opcode::Load;
    load.type = Type(kind);
    load.operands.push_back(Value::temp(1));
    load.loc = {1, 3, 1};
    bb.instructions.push_back(load);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(0));
    ret.loc = {1, 4, 1};
    bb.instructions.push_back(ret);

    bb.terminated = true;
    fn.blocks.push_back(std::move(bb));
    fn.valueNames.resize(3);
    m.functions.push_back(std::move(fn));
    return m;
}

il::core::Module makeMisalignedStoreModule(il::core::Type::Kind kind)
{
    using namespace il::core;

    auto storeValueFor = [](Type::Kind k)
    {
        switch (k)
        {
            case Type::Kind::I16:
            case Type::Kind::I32:
            case Type::Kind::I64:
                return Value::constInt(42);
            case Type::Kind::F64:
                return Value::constFloat(1.0);
            case Type::Kind::Ptr:
            case Type::Kind::Str:
            case Type::Kind::Error:
            case Type::Kind::ResumeTok:
                return Value::null();
            case Type::Kind::I1:
            case Type::Kind::Void:
                break;
        }
        return Value::constInt(0);
    };

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr alloca;
    alloca.result = 0U;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(16));
    alloca.loc = {1, 1, 1};
    bb.instructions.push_back(alloca);

    Instr gep;
    gep.result = 1U;
    gep.op = Opcode::GEP;
    gep.type = Type(Type::Kind::Ptr);
    gep.operands.push_back(Value::temp(0));
    gep.operands.push_back(Value::constInt(1));
    gep.loc = {1, 2, 1};
    bb.instructions.push_back(gep);

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(kind);
    store.operands.push_back(Value::temp(1));
    store.operands.push_back(storeValueFor(kind));
    store.loc = {1, 3, 1};
    bb.instructions.push_back(store);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(0));
    ret.loc = {1, 4, 1};
    bb.instructions.push_back(ret);

    bb.terminated = true;
    fn.blocks.push_back(std::move(bb));
    fn.valueNames.resize(2);
    m.functions.push_back(std::move(fn));
    return m;
}

} // namespace

int main(int argc, char *argv[])
{
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    {
        auto m = makeLoadModule();
        auto result = viper::tests::runModuleIsolated(m);
        assert(result.trapped());
        bool loadOk = result.stderrText.find(
                          "Trap @main:entry#0 line 1: InvalidOperation (code=0): null load") !=
                      std::string::npos;
        assert(loadOk);
    }

    {
        auto m = makeStoreModule();
        auto result = viper::tests::runModuleIsolated(m);
        assert(result.trapped());
        bool storeOk = result.stderrText.find(
                           "Trap @main:entry#0 line 2: InvalidOperation (code=0): null store") !=
                       std::string::npos;
        assert(storeOk);
    }

    const il::core::Type::Kind misalignedKinds[] = {il::core::Type::Kind::I16,
                                                    il::core::Type::Kind::I32,
                                                    il::core::Type::Kind::I64,
                                                    il::core::Type::Kind::F64,
                                                    il::core::Type::Kind::Ptr,
                                                    il::core::Type::Kind::Str,
                                                    il::core::Type::Kind::Error,
                                                    il::core::Type::Kind::ResumeTok};

    for (il::core::Type::Kind kind : misalignedKinds)
    {
        auto m = makeMisalignedLoadModule(kind);
        auto result = viper::tests::runModuleIsolated(m);
        assert(result.trapped());
        bool trapped = result.stderrText.find("InvalidOperation (code=0): misaligned load") !=
                       std::string::npos;
        assert(trapped);
    }

    for (il::core::Type::Kind kind : misalignedKinds)
    {
        auto m = makeMisalignedStoreModule(kind);
        auto result = viper::tests::runModuleIsolated(m);
        assert(result.trapped());
        bool trapped = result.stderrText.find("InvalidOperation (code=0): misaligned store") !=
                       std::string::npos;
        assert(trapped);
    }

    return 0;
}
