//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_rt_unknown_helper.cpp
// Purpose: Ensure runtime bridge traps when unknown runtime helpers are invoked.
// Key invariants: Calls to helpers absent from the runtime registry must produce traps in all build
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <optional>
#include <string>

static void buildAndRun()
{
    using namespace il::core;
    Module module;
    il::build::IRBuilder builder(module);
    builder.addExtern("rt_missing", Type(Type::Kind::Void), {});

    auto &fn = builder.startFunction("main", Type(Type::Kind::Void), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);
    builder.emitCall("rt_missing", {}, std::optional<Value>{}, {1, 1, 1});
    builder.emitRet(std::optional<Value>{}, {1, 1, 1});

    il::vm::VM vm(module);
    vm.run();
}

int main(int argc, char *argv[])
{
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(buildAndRun);
    assert(result.trapped());
    bool messageOk = result.stderrText.find("Trap @main:entry#0 line 1: DomainError (code=0)") !=
                     std::string::npos;
    assert(messageOk && "expected runtime trap diagnostic for unknown runtime helper");
    return 0;
}
