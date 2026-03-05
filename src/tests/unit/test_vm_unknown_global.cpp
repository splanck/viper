//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_unknown_global.cpp
// Purpose: Ensure VM traps when referencing undefined globals.
// Key invariants: Missing global names must emit "unknown global" trap.
// Ownership/Lifetime: Test constructs IL module and executes VM.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <string>

static void buildAndRun()
{
    il::core::Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    b.emitConstStr("missing", {1, 1, 1});
    b.emitRet(std::optional<il::core::Value>{}, {1, 1, 1});

    il::vm::VM vm(m);
    vm.run();
}

int main(int argc, char *argv[])
{
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(buildAndRun);
    assert(result.trapped());
    bool ok = result.stderrText.find("Trap @main:entry#0 line 1: DomainError (code=0)") !=
              std::string::npos;
    assert(ok);
    return 0;
}
