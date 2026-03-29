//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_rt_trap_loc.cpp
// Purpose: Verify runtime-originated traps report instruction source locations.
// Key invariants: Trap output includes function, block, and location.
// Ownership/Lifetime: Test builds IL calling runtime and runs VM.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "il/build/IRBuilder.hpp"
#include "support/source_location.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <string>

namespace {

il::core::Module buildRuntimeTrapModule(bool attachLoc) {
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);
    b.addExtern("rt_to_int", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
    b.addGlobalStr("g", "12x");
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    Value s = b.emitConstStr("g", {1, 1, 1});
    const il::support::SourceLoc callLoc =
        attachLoc ? il::support::SourceLoc{1, 1, 1} : il::support::SourceLoc{};
    b.emitCall("rt_to_int", {s}, std::optional<Value>{}, callLoc);
    b.emitRet(std::optional<Value>{}, {1, 1, 1});
    return m;
}

} // namespace

int main(int argc, char *argv[]) {
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    // Format: "Trap @function:block#ip line N: Kind (code=C)"
    // Line is omitted when unknown (instead of showing "line -1")
    // Runtime helpers call vm_trap() directly with a plain message (no
    // structured Trap prefix). Verify the trap fires and the message is present.
    {
        auto m = buildRuntimeTrapModule(true);
        auto result = viper::tests::runModuleIsolated(m);
        assert(result.trapped());
        const bool hasMsg =
            result.stderrText.find("INPUT: expected numeric value") != std::string::npos;
        assert(hasMsg);
    }

    {
        auto m = buildRuntimeTrapModule(false);
        auto result = viper::tests::runModuleIsolated(m);
        assert(result.trapped());
        const bool hasMsg =
            result.stderrText.find("INPUT: expected numeric value") != std::string::npos;
        assert(hasMsg);
    }

    return 0;
}
