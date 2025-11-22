//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_expected_api_build.cpp
// Purpose: Compile-only coverage for Expected-based IL API wrappers. 
// Key invariants: Ensures headers compile and link under Clang in unit test matrix.
// Ownership/Lifetime: Main owns module and stream for the duration of the test.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

int main()
{
    il::core::Module module;
    std::istringstream fake_stream("");
    auto parse_result = il::api::v2::parse_text_expected(fake_stream, module);
    (void)parse_result;
    return 0;
}
