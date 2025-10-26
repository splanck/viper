// File: tests/unit/test_il_parse_global_const_str.cpp
// Purpose: Ensure IL parser accepts global const string directives.
// Key invariants: Parser records name, type, and initializer for string globals.
// Ownership/Lifetime: Test owns module and parsing streams.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>

int main()
{
    constexpr const char *kProgram = R"(il 0.1.2
global const str @greeting = "hello"
)";

    std::istringstream input(kProgram);
    il::core::Module module;

    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(parsed);
    assert(module.globals.size() == 1);
    const auto &global = module.globals.front();
    assert(global.name == "greeting");
    assert(global.type.kind == il::core::Type::Kind::Str);
    assert(global.init == "hello");
    return 0;
}
