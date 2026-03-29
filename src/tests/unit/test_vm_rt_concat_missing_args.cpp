//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_rt_concat_missing_args.cpp
// Purpose: Ensure runtime bridge traps when rt_concat is called with too few arguments.
// Key invariants: Calls with insufficient args should emit descriptive trap rather than crash.
// Ownership/Lifetime: Test constructs IL module and executes VM.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <string>

static void buildAndRun() {
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);
    b.addExtern(
        "rt_str_concat", Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::Str)});
    auto &fn = b.startFunction("main", Type(Type::Kind::Void), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    // Deliberately omit both required arguments.
    b.emitCall("rt_str_concat", {}, std::optional<Value>{}, {1, 1, 1});
    b.emitRet(std::optional<Value>{}, {1, 1, 1});

    il::vm::VM vm(m);
    vm.run();
}

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(buildAndRun);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(buildAndRun);
    assert(result.trapped());
    bool ok = result.stderrText.find("Trap @main:entry#0 line 1: DomainError (code=0)") !=
              std::string::npos;
    assert(ok);
    return 0;
}
