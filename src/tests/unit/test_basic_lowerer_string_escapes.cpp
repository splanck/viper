//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_lowerer_string_escapes.cpp
// Purpose: Ensure BASIC lowering treats backslash as a regular character
//          (standard BASIC behavior - no escape sequence processing).
// Key invariants: Lowered globals store literal characters including backslash.
// Ownership/Lifetime: Test owns parser, program, and module instances.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>
#include <unordered_map>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    // In standard BASIC, backslash is a regular character, not an escape.
    // So "HELLO\nWORLD" should be 12 characters: H E L L O \ n W O R L D
    const std::string src = "10 PRINT \"PATH\\TO\\FILE\"\n"
                            "20 PRINT \"BACKSLASH:\\\\\"\n"
                            "30 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("string_escapes.bas");
    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*program);

    std::unordered_map<std::string, std::string> values;
    for (const auto &global : module.globals)
    {
        values[global.name] = global.init;
    }

    // Verify backslash is treated as literal character
    bool foundPath = false;
    bool foundBackslash = false;
    for (const auto &entry : values)
    {
        // "PATH\\TO\\FILE" in C++ is "PATH\TO\FILE" as literal string
        if (entry.second == "PATH\\TO\\FILE")
            foundPath = true;
        // "BACKSLASH:\\\\" in C++ is "BACKSLASH:\\" (two backslashes in output)
        if (entry.second == "BACKSLASH:\\\\")
            foundBackslash = true;
    }

    assert(foundPath && "Expected PATH\\TO\\FILE with literal backslashes");
    assert(foundBackslash && "Expected BACKSLASH:\\\\ with literal backslashes");

    return 0;
}
