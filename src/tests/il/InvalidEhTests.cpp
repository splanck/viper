//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/InvalidEhTests.cpp
// Purpose: Ensure EH verifier reports clear diagnostics for invalid handler patterns.
// Key invariants: Parsing succeeds but verification fails with targeted error substrings.
// Ownership/Lifetime: Test owns loaded modules and input streams.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diag_expected.hpp"

#include "tests/TestHarness.hpp"

#include <array>
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

TEST(IL, InvalidEhTests)
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
        ASSERT_TRUE(in);

        il::core::Module module;
        auto parseResult = parse_text_expected(in, module);
        ASSERT_TRUE(parseResult);

        auto verifyResult = verify_module_expected(module);
        ASSERT_FALSE(verifyResult);

        std::ostringstream diag;
        il::support::printDiag(verifyResult.error(), diag);
        const std::string diagText = diag.str();
        ASSERT_FALSE(diagText.empty());

        for (const char *expected : testCase.expectedSubstrings)
        {
            ASSERT_TRUE(diagText.find(expected) != std::string::npos);
        }
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
