// File: tests/il/InvalidEhTests.cpp
// Purpose: Ensure EH verifier reports clear diagnostics for invalid handler patterns.
// Key invariants: Parsing succeeds but verification fails with targeted error substrings.
// Ownership/Lifetime: Test owns loaded modules and input streams.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diag_expected.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

namespace
{
struct InvalidCase
{
    const char *fileName;
    std::initializer_list<const char *> expectedSubstrings;
};
} // namespace

int main()
{
    using il::api::v2::parse_text_expected;
    using il::api::v2::verify_module_expected;

    const std::array<InvalidCase, 3> cases = {{
        {"unbalanced_push_pop.il", {"verify.eh.unreleased", "unmatched eh.push depth"}},
        {"resume_without_token.il",
         {"verify.eh.resume_token_missing", "resume.* requires active resume token"}},
        {"resume_label_not_postdom.il",
         {"verify.eh.resume_label_target", "must postdominate block"}},
    }};

    for (const auto &testCase : cases)
    {
        const std::string path = std::string(INVALID_EH_DIR) + "/" + testCase.fileName;
        std::ifstream in(path);
        if (!in)
        {
            std::fprintf(stderr, "failed to open %s\n", path.c_str());
            return 1;
        }

        il::core::Module module;
        auto parseResult = parse_text_expected(in, module);
        if (!parseResult)
        {
            std::ostringstream diag;
            il::support::printDiag(parseResult.error(), diag);
            std::fprintf(
                stderr, "unexpected parse failure for %s: %s\n", path.c_str(), diag.str().c_str());
            return 1;
        }

        auto verifyResult = verify_module_expected(module);
        if (verifyResult)
        {
            std::fprintf(stderr, "expected verifier to fail for %s\n", path.c_str());
            return 1;
        }

        std::ostringstream diag;
        il::support::printDiag(verifyResult.error(), diag);
        const std::string diagText = diag.str();
        if (diagText.empty())
        {
            std::fprintf(stderr, "expected diagnostic text for %s\n", path.c_str());
            return 1;
        }

        for (const char *expected : testCase.expectedSubstrings)
        {
            if (diagText.find(expected) == std::string::npos)
            {
                std::fprintf(stderr,
                             "diagnostic for %s missing substring '%s': %s\n",
                             path.c_str(),
                             expected,
                             diagText.c_str());
                return 1;
            }
        }
    }

    return 0;
}
