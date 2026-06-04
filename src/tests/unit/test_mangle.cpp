//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_mangle.cpp
// Purpose: Exercise common linker-symbol mangling collision and readability rules.
// Key invariants: Plain entry-point symbols stay readable, while qualified or
//                 escaped names use a reversible reserved-prefix encoding.
// Ownership/Lifetime: Standalone unit test binary.
// Links: src/common/Mangle.hpp
//
//===----------------------------------------------------------------------===//

#include "common/Mangle.hpp"

#include <cassert>

/// @brief Validate the common linker-symbol mangling contract.
/// @details The test intentionally checks both compatibility and collision
///          avoidance: simple symbols such as @c main remain plain for native
///          toolchain entry points, reserved-prefix user symbols are escaped,
///          and dotted/underscore/special names no longer collapse to the same
///          linker symbol.
/// @return Zero when all invariants hold.
int main() {
    using viper::common::DemangleLink;
    using viper::common::MangleLink;

    assert(MangleLink("main") == "main");
    assert(DemangleLink("main") == "main");

    const std::string dotted = MangleLink("A.B");
    const std::string underscored = MangleLink("A_B");
    const std::string dashed = MangleLink("A-B");
    assert(dotted != underscored);
    assert(dotted != dashed);
    assert(underscored != dashed);

    assert(DemangleLink(dotted) == "a.b");
    assert(DemangleLink(underscored) == "a_b");
    assert(DemangleLink(dashed) == "a-b");

    const std::string reserved = MangleLink("vpr_user_symbol");
    assert(reserved != "vpr_user_symbol");
    assert(DemangleLink(reserved) == "vpr_user_symbol");

    assert(DemangleLink("@legacy_symbol") == "legacy.symbol");
    return 0;
}
