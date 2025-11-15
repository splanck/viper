// File: tests/unit/test_vm_gep_nullptr.cpp
// Purpose: Ensure VM getelementptr on null base with zero offset yields null result.
// Key invariants: GEP computation must avoid UB and preserve null when offset is zero.
// Ownership: Standalone unit test executable for VM pointer arithmetic semantics.
// Links: docs/il-guide.md#reference

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"

#include "VMTestHook.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

namespace
{

il::core::Module makeModule()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::Ptr);

    BasicBlock entry;
    entry.label = "entry";

    Instr gep;
    gep.result = 0U;
    gep.op = Opcode::GEP;
    gep.type = Type(Type::Kind::Ptr);
    gep.operands.push_back(Value::null());
    gep.operands.push_back(Value::constInt(0));
    gep.loc = {1, 1, 1};
    entry.instructions.push_back(gep);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Ptr);
    ret.operands.push_back(Value::temp(0));
    ret.loc = {1, 2, 1};
    entry.instructions.push_back(ret);

    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(1);
    m.functions.push_back(std::move(fn));
    return m;
}

il::core::Module makeMinOffsetModule()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::Ptr);

    BasicBlock entry;
    entry.label = "entry";

    Instr gep;
    gep.result = 0U;
    gep.op = Opcode::GEP;
    gep.type = Type(Type::Kind::Ptr);
    gep.operands.push_back(Value::null());
    gep.operands.push_back(Value::constInt(std::numeric_limits<long long>::min()));
    gep.loc = {1, 1, 1};
    entry.instructions.push_back(gep);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Ptr);
    ret.operands.push_back(Value::temp(0));
    ret.loc = {1, 2, 1};
    entry.instructions.push_back(ret);

    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(1);
    m.functions.push_back(std::move(fn));
    return m;
}

} // namespace

int main()
{
    {
        il::core::Module module = makeModule();
        il::vm::VM vm(module);

        const auto &fn = module.functions.front();
        il::vm::Slot result = il::vm::VMTestHook::run(vm, fn, {});
        assert(result.ptr == nullptr);
    }

    {
        il::core::Module module = makeMinOffsetModule();
        il::vm::VM vm(module);

        const auto &fn = module.functions.front();
        il::vm::Slot result = il::vm::VMTestHook::run(vm, fn, {});

        const std::uintptr_t expected = std::uintptr_t{1} << 63;
        assert(reinterpret_cast<std::uintptr_t>(result.ptr) == expected);
    }

    return 0;
}
