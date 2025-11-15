// File: tests/vm/BranchArgMismatchTests.cpp
// Purpose: Ensure the VM traps when a branch supplies the wrong number of arguments.
// Key invariants: Branch argument count mismatches produce InvalidOperation traps mentioning the
// callee block. Ownership/Lifetime: Constructs an in-memory module executed in a subprocess to
// capture diagnostics. Links: docs/il-guide.md#reference

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cerrno>
#include <optional>
#include <string>

using namespace il::core;

int main()
{
    using viper::tests::VmFixture;

    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    builder.addBlock(fn, "entry");
    builder.createBlock(fn, "target", {Param{"x", Type(Type::Kind::I64), 0}});
    auto &entry = fn.blocks.front();
    auto &target = fn.blocks.back();
    assert(target.params.size() == 1);

    builder.setInsertPoint(entry);
    builder.br(target, {Value::constInt(42)});
    auto &brInstr = entry.instructions.back();
    brInstr.loc = {1, 1, 1};
    brInstr.brArgs[0].clear();

    builder.setInsertPoint(target);
    builder.emitRet(std::optional<Value>(Value::constInt(0)), {1, 2, 1});

    VmFixture fixture;
    const std::string diag = fixture.captureTrap(module);
    const bool hasMessage = diag.find("branch argument count mismatch") != std::string::npos;
    assert(hasMessage && "expected branch argument mismatch diagnostic");

    const bool mentionsTarget = diag.find("'target'") != std::string::npos;
    assert(mentionsTarget && "expected diagnostic to mention callee block label");

    const bool mentionsCounts = diag.find("expected 1, got 0") != std::string::npos;
    assert(mentionsCounts && "expected diagnostic to report argument counts");

    return 0;
}
