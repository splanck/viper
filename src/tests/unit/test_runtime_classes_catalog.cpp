//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_runtime_classes_catalog.cpp
// Purpose: Smoke test for runtime class catalog exposing Viper.String.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/runtime/classes/RuntimeClasses.hpp"

#include <cassert>
#include <cstddef>
#include <string_view>

int main()
{
    const auto &cat = il::runtime::runtimeClassCatalog();
    assert(cat.size() >= 1);

    const auto &cls = cat.front();
    assert(std::string_view(cls.qname) == std::string_view("Viper.String"));
    assert(cls.properties.size() >= 2);
    assert(std::string_view(cls.properties[0].name) == std::string_view("Length"));
    assert(std::string_view(cls.properties[1].name) == std::string_view("IsEmpty"));

    return 0;
}
