//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_irbuilder_call_unknown.cpp
// Purpose: Ensure IRBuilder emits an error when call targets are missing.
// Key invariants: emitCall must throw for unknown callees.
// Ownership/Lifetime: Test owns all constructed objects.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include <cassert>
#include <optional>
#include <stdexcept>
#include <string>

int main()
{
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", Type(Type::Kind::Void), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    bool caught = false;
    try
    {
        b.emitCall("unknown", {}, std::nullopt, {});
    }
    catch (const std::logic_error &err)
    {
        caught = true;
        assert(std::string(err.what()).find("unknown") != std::string::npos);
    }
    assert(caught && "emitCall should throw when callee is missing");
    return 0;
}
