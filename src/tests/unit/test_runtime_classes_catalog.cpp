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

    // Find Viper.String in the catalog (order-independent)
    const il::runtime::RuntimeClass *stringCls = nullptr;
    for (const auto &cls : cat)
    {
        if (std::string_view(cls.qname) == std::string_view("Viper.String"))
        {
            stringCls = &cls;
            break;
        }
    }

    assert(stringCls != nullptr && "Viper.String not found in catalog");
    assert(stringCls->properties.size() >= 2);

    // Find Length and IsEmpty properties (order-independent)
    bool hasLength = false, hasIsEmpty = false;
    for (const auto &prop : stringCls->properties)
    {
        if (std::string_view(prop.name) == std::string_view("Length"))
            hasLength = true;
        if (std::string_view(prop.name) == std::string_view("IsEmpty"))
            hasIsEmpty = true;
    }
    assert(hasLength && "Viper.String should have Length property");
    assert(hasIsEmpty && "Viper.String should have IsEmpty property");

    return 0;
}
