//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia visibility enforcement.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Linkage.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

/// @brief Test that visibility enforcement works (private members are rejected).
TEST(ZiaVisibility, VisibilityEnforcement) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Person {
    Integer secretAge;
    expose Integer publicAge;
}

func start() {    var p: Person = new Person(30, 25);
    var age: Integer = p.secretAge;
}
)";
    CompilerInput input{.source = source, .path = "visibility.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // This should FAIL because secretAge is private
    EXPECT_FALSE(result.succeeded());

    bool foundVisibilityError = false;
    for (const auto &d : result.diagnostics.diagnostics()) {
        if (d.message.find("private") != std::string::npos) {
            foundVisibilityError = true;
            EXPECT_EQ(d.code, "V-ZIA-SEMA");
        }
    }
    EXPECT_TRUE(foundVisibilityError);
}

/// @brief Test that visibility works correctly with exposed members.
TEST(ZiaVisibility, VisibilityExposed) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Person {
    expose Integer age;

    expose func init(a: Integer) {        age = a;
    }
}

func start() {    var p: Person = new Person(30);
    var age: Integer = p.age;
    Zanna.Terminal.SayInt(age);
}
)";
    CompilerInput input{.source = source, .path = "visibility_exposed.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for VisibilityExposed:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaVisibility, PrivateClassFieldsAreNotExternalConstructorParameters) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class SecretBox {
    Integer secret;
    expose Integer visible;
}

func start() {
    var b = new SecretBox(secret: 42, visible: 7);
    Zanna.Terminal.SayInt(b.visible);
}
)";
    CompilerInput input{.source = source, .path = "private_constructor_field.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_FALSE(result.succeeded());

    bool foundSecretError = false;
    for (const auto &d : result.diagnostics.diagnostics()) {
        if (d.severity == Severity::Error && d.message.find("secret") != std::string::npos)
            foundSecretError = true;
    }
    EXPECT_TRUE(foundSecretError);
}

TEST(ZiaVisibility, PrivateStructFieldsAreNotExternalLiteralFields) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct SecretPoint {
    hide Integer x;
    expose Integer y;
}

func start() {
    var p: SecretPoint = SecretPoint { x = 1, y = 2 };
    Zanna.Terminal.SayInt(p.y);
}
)";
    CompilerInput input{.source = source, .path = "private_struct_literal_field.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_FALSE(result.succeeded());

    bool foundPrivateError = false;
    for (const auto &d : result.diagnostics.diagnostics()) {
        if (d.severity == Severity::Error && d.message.find("private") != std::string::npos)
            foundPrivateError = true;
    }
    EXPECT_TRUE(foundPrivateError);
}

TEST(ZiaVisibility, ExposedAsyncAndForeignDeclarations) {
    SourceManager sm;
    const std::string source = R"(
module Test;

expose async func fetch() -> Integer {
    return 7;
}

expose foreign func hostValue() -> Integer;

func start() {
    var future = fetch();
}
)";
    CompilerInput input{.source = source, .path = "expose_async_foreign.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    bool sawFetchExport = false;
    bool sawHostImport = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "fetch" && fn.linkage == il::core::Linkage::Export)
            sawFetchExport = true;
        if (fn.name == "hostValue" && fn.linkage == il::core::Linkage::Import)
            sawHostImport = true;
    }
    EXPECT_TRUE(sawFetchExport);
    EXPECT_TRUE(sawHostImport);
}

} // namespace

int main() {
    return zanna_test::run_all_tests();
}
