// tui/tests/test_syntax.cpp
// @brief Tests for regex-based syntax highlighting rules.
// @invariant Loaded rules produce expected spans for JSON snippet.
// @ownership Test owns rule set and verifies span attributes.

#include "tui/syntax/rules.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using viper::tui::syntax::SyntaxRuleSet;

int main()
{
    SyntaxRuleSet rules;
    bool ok = rules.loadFromFile(SYNTAX_JSON);
    assert(ok);
    std::vector<std::string> lines = {"{", "  \"key\": true", "}"};
    std::string dump;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const auto &sp = rules.spans(i, lines[i]);
        for (const auto &s : sp)
        {
            char buf[64];
            std::snprintf(buf,
                          sizeof(buf),
                          "%zu:%zu+%zu:%02x%02x%02x:%u\n",
                          i,
                          s.start,
                          s.length,
                          s.style.fg.r,
                          s.style.fg.g,
                          s.style.fg.b,
                          s.style.attrs);
            dump += buf;
        }
    }
    assert(dump == "1:2+5:00ff00:0\n1:9+4:0000ff:1\n");

    namespace fs = std::filesystem;
    const fs::path tmpDir = fs::temp_directory_path();

    const fs::path truncatedArrayPath = tmpDir / "viper_syntax_truncated_array.json";
    {
        std::ofstream out(truncatedArrayPath);
        out << "[{\"regex\":\"foo\",\"style\":{\"fg\":\"#ffffff\"}}";
    }
    SyntaxRuleSet truncatedArrayRules;
    bool truncatedArrayOk = truncatedArrayRules.loadFromFile(truncatedArrayPath.string());
    assert(!truncatedArrayOk);
    fs::remove(truncatedArrayPath);

    const fs::path truncatedMapPath = tmpDir / "viper_syntax_truncated_map.json";
    {
        std::ofstream out(truncatedMapPath);
        out << "[{\"regex\":\"foo\",\"style\":{\"fg\":\"#ffffff\"}";
    }
    SyntaxRuleSet truncatedMapRules;
    bool truncatedMapOk = truncatedMapRules.loadFromFile(truncatedMapPath.string());
    assert(!truncatedMapOk);
    fs::remove(truncatedMapPath);

    return 0;
}
