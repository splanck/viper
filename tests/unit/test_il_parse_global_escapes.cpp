// File: tests/unit/test_il_parse_global_escapes.cpp
// Purpose: Verify IL parser decodes escape sequences in global string literals.
// Key invariants: Supported escapes yield correct bytes; unknown escapes surface diagnostics.
// Ownership/Lifetime: Test owns parsing buffers and module instances.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <sstream>

int main()
{
    {
        const char *src = R"(il 0.1.2
global const str @escaped = "\"\\\n\t\x41"
)";
        std::istringstream in(src);
        il::core::Module m;
        auto parse = il::api::v2::parse_text_expected(in, m);
        assert(parse);
        assert(m.globals.size() == 1);
        assert(m.globals[0].name == "escaped");
        const std::string expected = "\"\\\n\tA";
        assert(m.globals[0].init == expected);
    }

    {
        const char *src = R"(il 0.1.2
global const str @bad = "\q"
)";
        std::istringstream in(src);
        il::core::Module m;
        auto parse = il::api::v2::parse_text_expected(in, m);
        assert(!parse);
        std::ostringstream diag;
        il::support::printDiag(parse.error(), diag);
        assert(diag.str().find("unknown escape") != std::string::npos);
    }

    return 0;
}
