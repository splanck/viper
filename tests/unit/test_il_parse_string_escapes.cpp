// File: tests/unit/test_il_parse_string_escapes.cpp
// Purpose: Verify IL parser decodes escaped globals and serializer re-escapes them.
// Key invariants: Parsed globals store literal characters; serialization round-trips escapes.
// Ownership/Lifetime: Test owns module and buffers.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/io/Serializer.hpp"

#include <cassert>
#include <sstream>
#include <unordered_map>

int main()
{
    const char *source = R"(il 0.1.2
global const str @nl = "\n"
global const str @tab = "tab:\t"
global const str @quote = "quote:\""
global const str @mix = "slashes\\ and hex\x21"
func @main() -> void {
entry:
  ret
}
)";

    std::istringstream in(source);
    il::core::Module module;
    auto parsed = il::api::v2::parse_text_expected(in, module);
    assert(parsed);

    assert(module.globals.size() == 4);
    std::unordered_map<std::string, std::string> values;
    for (const auto &g : module.globals)
    {
        assert(g.init.kind == il::core::Value::Kind::ConstStr);
        values[g.name] = g.init.str;
    }
    assert(values.at("nl") == std::string("\n"));
    assert(values.at("tab") == std::string("tab:\t"));
    assert(values.at("quote") == std::string("quote:\""));
    assert(values.at("mix") == std::string("slashes\\ and hex!"));

    std::string serialized = il::io::Serializer::toString(module);
    assert(serialized.find(R"(@nl = "\n")") != std::string::npos);
    assert(serialized.find(R"(@tab = "tab:\t")") != std::string::npos);
    assert(serialized.find(R"(@quote = "quote:\"")") != std::string::npos);
    assert(serialized.find(R"(@mix = "slashes\\ and hex!")") != std::string::npos);

    return 0;
}
