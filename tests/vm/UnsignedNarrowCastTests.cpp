// File: tests/vm/UnsignedNarrowCastTests.cpp
// Purpose: Verify unsigned narrowing casts accept full unsigned ranges without trapping.
// Key invariants: cast.ui_narrow.chk must succeed for values representable in the target width.
// Ownership/Lifetime: Builds throwaway modules executed via VmFixture.

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cstdint>

using namespace il::core;

namespace
{

int64_t runCastUiNarrow(Type::Kind targetKind, uint64_t input)
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(targetKind), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr cast;
    cast.result = builder.reserveTempId();
    cast.op = Opcode::CastUiNarrowChk;
    cast.type = Type(targetKind);
    cast.operands.push_back(Value::constInt(static_cast<int64_t>(input)));
    cast.loc = {1, 1, 1};
    bb.instructions.push_back(cast);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*cast.result));
    bb.instructions.push_back(ret);

    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

} // namespace

int main()
{
    const int64_t u16Value = runCastUiNarrow(Type::Kind::I16, UINT64_C(65535));
    assert(u16Value == static_cast<int64_t>(UINT16_C(65535)));

    const int64_t u32Value = runCastUiNarrow(Type::Kind::I32, UINT64_C(4294967295));
    assert(u32Value == static_cast<int64_t>(UINT32_C(4294967295)));

    return 0;
}
