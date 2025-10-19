// File: tests/unit/test_il_parse_global_qualifier.cpp
// Purpose: Validate parsing of global qualifiers for IL modules.
// Key invariants: Only `global const str` qualifiers are accepted.
// Ownership/Lifetime: Test owns all parser state and modules.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>

int main()
{
    const char *valid = R"(il 0.1.2
global const str @message = "hello"
)";

    il::core::Module okModule;
    std::istringstream validStream(valid);
    auto validParse = il::api::v2::parse_text_expected(validStream, okModule);
    assert(validParse);
    assert(okModule.globals.size() == 1);
    assert(okModule.globals.front().type.kind == il::core::Type::Kind::Str);
    assert(okModule.globals.front().init == "hello");

    const char *invalid = R"(il 0.1.2
global mutable str @message = "nope"
)";

    il::core::Module badModule;
    std::istringstream invalidStream(invalid);
    std::ostringstream diag;
    auto invalidParse = il::api::v2::parse_text_expected(invalidStream, badModule);
    if (!invalidParse)
    {
        il::support::printDiag(invalidParse.error(), diag);
    }
    assert(!invalidParse);
    const std::string diagText = diag.str();
    assert(diagText.find("expected 'global const str'") != std::string::npos);

    return 0;
}
