//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_verify_all.cpp
// Purpose: Verify that top-level IL verification reports more than the first issue.
//
//===----------------------------------------------------------------------===//

#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/verify/Verifier.hpp"
#include "tests/TestHarness.hpp"

#include <string>
#include <vector>

namespace {

bool anyDiagnosticContains(const std::vector<il::support::Diag> &diagnostics,
                           const std::string &needle) {
    for (const auto &diag : diagnostics) {
        if (diag.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

TEST(ILVerifier, VerifyAllCollectsIndependentFailures) {
    using namespace il::core;

    Module module;
    module.externs.push_back(Extern{"dup_ext", Type(Type::Kind::Void), {}});
    module.externs.push_back(Extern{"dup_ext", Type(Type::Kind::I64), {}});
    module.globals.push_back(Global{"dup_global", Type(Type::Kind::I64), ""});
    module.globals.push_back(Global{"dup_global", Type(Type::Kind::I64), ""});

    Function fn;
    fn.name = "broken";
    fn.retType = Type(Type::Kind::Void);
    module.functions.push_back(fn);

    auto diagnostics = il::verify::Verifier::verifyAll(module);
    EXPECT_GE(diagnostics.size(), 3u);
    EXPECT_TRUE(anyDiagnosticContains(diagnostics, "duplicate extern @dup_ext"));
    EXPECT_TRUE(anyDiagnosticContains(diagnostics, "duplicate global @dup_global"));
    EXPECT_TRUE(anyDiagnosticContains(diagnostics, "broken: function has no blocks"));

    auto result = il::verify::Verifier::verify(module);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "V-IL-VERIFY");
    EXPECT_CONTAINS(result.error().message, "additional verifier diagnostics");
    EXPECT_GE(result.error().notes.size(), 2u);
}

TEST(ILVerifier, VerifyAllCollectsMultipleFunctionBodyFailures) {
    using namespace il::core;

    Module module;

    Function first;
    first.name = "missing_blocks";
    first.retType = Type(Type::Kind::Void);
    module.functions.push_back(first);

    Function second;
    second.name = "also_missing_blocks";
    second.retType = Type(Type::Kind::Void);
    module.functions.push_back(second);

    auto diagnostics = il::verify::Verifier::verifyAll(module);
    EXPECT_GE(diagnostics.size(), 2u);
    EXPECT_TRUE(anyDiagnosticContains(diagnostics, "missing_blocks: function has no blocks"));
    EXPECT_TRUE(anyDiagnosticContains(diagnostics, "also_missing_blocks: function has no blocks"));
    for (const auto &diag : diagnostics)
        EXPECT_EQ(diag.code, "V-IL-VERIFY");
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
