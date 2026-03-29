//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_alloca_missing_size.cpp
// Purpose: Ensure VM traps when alloca is missing size operand.
// Key invariants: Alloca without size must emit "missing allocation size" trap.
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
    il::core::Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    il::core::Instr in;
    in.op = il::core::Opcode::Alloca;
    in.type = il::core::Type(il::core::Type::Kind::Ptr);
    in.loc = {1, 1, 1};
    bb.instructions.push_back(in);

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
