//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_serialize_negative_zero.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "viper/il/IO.hpp"

#include <cassert>
#include <optional>
#include <string>

int main()
{
    il::core::Module module;
    il::build::IRBuilder builder(module);

    auto &function =
        builder.startFunction("neg_zero", il::core::Type(il::core::Type::Kind::F64), {});
    auto &entry = builder.addBlock(function, "entry");
    builder.setInsertPoint(entry);

    builder.emitRet(std::optional<il::core::Value>{il::core::Value::constFloat(-0.0)}, {});

    const std::string serialized = il::io::Serializer::toString(module);
    assert(serialized.find("-0.0") != std::string::npos);
    return 0;
}
