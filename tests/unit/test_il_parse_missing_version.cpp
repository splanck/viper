// File: tests/unit/test_il_parse_missing_version.cpp
// Purpose: Validate parser errors when the module omits the leading version directive.
// Key invariants: Parser should reject modules without an `il` directive before other content or EOF.
// Ownership/Lifetime: Test owns the module and input streams.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <sstream>

int main()
{
    {
        const char *src = R"(target "x86_64-unknown-unknown")";
        std::istringstream in(src);
        il::core::Module m;
        auto parse = il::api::v2::parse_text_expected(in, m);
        assert(!parse);
        std::ostringstream diag;
        il::support::printDiag(parse.error(), diag);
        std::string msg = diag.str();
        assert(msg.find("missing 'il' version directive") != std::string::npos);
    }

    {
        const char *src = "\n\n";
        std::istringstream in(src);
        il::core::Module m;
        auto parse = il::api::v2::parse_text_expected(in, m);
        assert(!parse);
        std::ostringstream diag;
        il::support::printDiag(parse.error(), diag);
        std::string msg = diag.str();
        assert(msg.find("missing 'il' version directive") != std::string::npos);
    }

    return 0;
}
