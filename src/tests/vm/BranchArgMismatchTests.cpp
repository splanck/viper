//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/BranchArgMismatchTests.cpp
// Purpose: Ensure the VM traps when a branch supplies the wrong number of arguments.
// Key invariants: Branch argument count mismatches produce InvalidOperation traps mentioning the
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cerrno>
#include <optional>
#include <string>

using namespace il::core;

/// Build the malformed module and run it in the VM (used as child function).
/// The module has a branch with cleared args → branch argument count mismatch.
static void buildAndRunMalformedBranch() {
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    builder.addBlock(fn, "entry");
    builder.createBlock(fn, "target", {Param{"x", Type(Type::Kind::I64), 0}});
    auto &entry = fn.blocks.front();
    auto &target = fn.blocks.back();

    builder.setInsertPoint(entry);
    builder.br(target, {Value::constInt(42)});
    auto &brInstr = entry.instructions.back();
    brInstr.loc = {1, 1, 1};
    brInstr.brArgs[0].clear();

    builder.setInsertPoint(target);
    builder.emitRet(std::optional<Value>(Value::constInt(0)), {1, 2, 1});

    il::vm::VM vm(module);
    vm.run();
}

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(buildAndRunMalformedBranch);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(buildAndRunMalformedBranch);
    assert(result.trapped());
    const std::string &diag = result.stderrText;

    const bool hasMessage = diag.find("branch argument count mismatch") != std::string::npos;
    assert(hasMessage && "expected branch argument mismatch diagnostic");

    const bool mentionsTarget = diag.find("'target'") != std::string::npos;
    assert(mentionsTarget && "expected diagnostic to mention callee block label");

    const bool mentionsCounts = diag.find("expected 1, got 0") != std::string::npos;
    assert(mentionsCounts && "expected diagnostic to report argument counts");

    return 0;
}
