//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/viperlang/test_viperlang_optional_field.cpp
// Purpose: Verify Bug #023 fix - optional type field access in helper functions
// Key invariants: Field access on unwrapped optional types should work correctly
// Ownership/Lifetime: Test file.
// Links: docs/bugs/sqldb_bugs.md
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Compiler.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using namespace il::frontends::viperlang;
using namespace il::support;

namespace
{

/// @brief Test simple entity field access (sanity check).
TEST(ViperLangOptionalField, SimpleFieldAccess)
{
    const std::string src = R"(
module Test;

entity MyNode {
    expose String myLabel;

    func init(l: String) {
        myLabel = l;
    }
}

func start() {
    MyNode n = MyNode("test");
    String s = n.myLabel;
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.viper"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for AccessAfterNullCheck:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test accessing optional field and assigning to variable.
TEST(ViperLangOptionalField, OptionalFieldAssignment)
{
    const std::string src = R"(
module Test;

entity Container {
    expose String val;
    expose Container? other;

    func init(v: String) {
        val = v;
    }
}

func start() {
    Container c = Container("hello");
    Container? maybeOther = c.other;
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.viper"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for DirectAccessAfterCheck:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test field access on optional variable after null check.
/// This is the core of Bug #023.
TEST(ViperLangOptionalField, FieldAccessAfterNullCheck)
{
    const std::string src = R"(
module Test;

entity Data {
    expose String text;
    expose Data? link;

    func init(t: String) {
        text = t;
    }
}

func getLinkedText(d: Data) -> String {
    if d.link == null {
        return "";
    }
    // After null check, d.link should be usable as Data (not Data?)
    // Bug #023: This returns (Data) -> String instead of String
    var linked = d.link;
    return linked.text;
}

func start() {
    Data x = Data("test");
    String r = getLinkedText(x);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.viper"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ChainedOptionalAccess:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
