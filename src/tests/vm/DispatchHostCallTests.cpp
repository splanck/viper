//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/DispatchHostCallTests.cpp
// Purpose: Verify VM dispatch loops exit correctly after host runtime calls.
// Key invariants: Host calls complete with switch/threaded dispatch without stalling.
// Ownership/Lifetime: Builds ephemeral modules per dispatch strategy.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "support/source_location.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdlib>
#include <optional>
#ifdef _WIN32
#include "tests/common/PosixCompat.h"
#endif

using namespace il::core;

namespace
{

void addPowExtern(Module &module)
{
    il::build::IRBuilder builder(module);
    builder.addExtern(
        "rt_pow_f64_chkdom", Type(Type::Kind::F64), {Type(Type::Kind::F64), Type(Type::Kind::F64)});
}

Module buildHostCallModule()
{
    Module module;
    addPowExtern(module);

    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    const auto powTemp = builder.reserveTempId();
    const il::support::SourceLoc loc{1, 1, 1};
    builder.emitCall("rt_pow_f64_chkdom",
                     {Value::constFloat(2.0), Value::constFloat(5.0)},
                     Value::temp(powTemp),
                     loc);

    Instr convert;
    convert.result = builder.reserveTempId();
    convert.op = Opcode::Fptosi;
    convert.type = Type(Type::Kind::I64);
    convert.operands.push_back(Value::temp(powTemp));
    convert.loc = loc;
    bb.instructions.push_back(convert);

    builder.emitRet(std::optional<Value>{Value::temp(*convert.result)}, loc);

    return module;
}

int64_t runWithDispatch(const char *dispatch)
{
    assert(dispatch != nullptr);
    assert(setenv("VIPER_DISPATCH", dispatch, 1) == 0);

    Module module = buildHostCallModule();
    il::vm::VM vm(module);
    const int64_t rv = vm.run();

    assert(unsetenv("VIPER_DISPATCH") == 0);
    return rv;
}

} // namespace

int main()
{
    const int64_t expected = 32; // 2^5 converted via host pow helper.

    assert(runWithDispatch("switch") == expected);

#if VIPER_THREADING_SUPPORTED
    assert(runWithDispatch("threaded") == expected);
#endif

    return 0;
}
