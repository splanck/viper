//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Conformance tests for Zia generic interface constraints.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

CompilerResult compileSource(const std::string &source,
                             const std::string &path = "constrained_generics.zia") {
    SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    return compile(input, {}, sm);
}

bool hasErrorContaining(const CompilerResult &result, const std::string &needle) {
    for (const auto &diag : result.diagnostics.diagnostics()) {
        if (diag.severity == Severity::Error && diag.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool hasFunction(const il::core::Module &module, const std::string &name) {
    for (const auto &fn : module.functions) {
        if (fn.name == name)
            return true;
    }
    return false;
}

bool hasCall(const il::core::Module &module,
             const std::string &functionName,
             const std::string &callee) {
    for (const auto &fn : module.functions) {
        if (fn.name != functionName)
            continue;
        for (const auto &block : fn.blocks) {
            for (const auto &instr : block.instructions) {
                if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                    return true;
            }
        }
    }
    return false;
}

bool hasOpcode(const il::core::Module &module,
               const std::string &functionName,
               il::core::Opcode opcode) {
    for (const auto &fn : module.functions) {
        if (fn.name != functionName)
            continue;
        for (const auto &block : fn.blocks) {
            for (const auto &instr : block.instructions) {
                if (instr.op == opcode)
                    return true;
            }
        }
    }
    return false;
}

} // namespace

TEST(ZiaConstrainedGenerics, AcceptsFunctionMethodClassStructAndInterfaceConstraints) {
    auto result = compileSource(R"(
module Test;

interface Named {
    func name() -> String;
}

class User implements Named {
    expose func name() -> String {
        return "user";
    }
}

struct Label implements Named {
    expose String value;
    expose func name() -> String {
        return self.value;
    }
}

class Box[T: Named] {
    expose T item;
}

struct Holder[T: Named] {
    expose T item;
}

interface Provider[T: Named] {
    func get() -> T;
}

class Helper {
    expose func accept[T: Named](value: T) -> Integer {
        return 1;
    }
}

func accept[T: Named](value: T) -> Integer {
    return 2;
}

func takeProvider(value: Provider[User]) {
}

func start() {
    var user = new User();
    var label: Label = Label { value = "label" };
    var box = new Box[User](user);
    var holder: Holder[Label];
    var helper = new Helper();
    var ok1 = accept[User](user);
    var ok2 = accept[Label](label);
    var ok3 = helper.accept[User](user);
    var ok4 = helper.accept[Label](label);
    Viper.Terminal.SayInt(ok1 + ok2 + ok3 + ok4);
}
)",
                                "constrained_matrix.zia");

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "accept$User"));
    EXPECT_TRUE(hasFunction(result.module, "accept$Label"));
}

TEST(ZiaConstrainedGenerics, AcceptsQualifiedConstraints) {
    auto result = compileSource(R"(
module Test;

namespace Contracts {
    interface Named {
        func name() -> String;
    }
}

class User implements Contracts.Named {
    expose func name() -> String {
        return "user";
    }
}

func accept[T: Contracts.Named](value: T) -> Integer {
    return 1;
}

func start() {
    var user = new User();
    var ok = accept[User](user);
    Viper.Terminal.SayInt(ok);
}
)",
                                "qualified_constraint.zia");

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "accept$User"));
}

TEST(ZiaConstrainedGenerics, AcceptsInterfacesInheritedThroughBaseClasses) {
    auto result = compileSource(R"(
module Test;

interface Named {
    func name() -> String;
}

class BaseUser implements Named {
    expose func name() -> String {
        return "base";
    }
}

class AdminUser extends BaseUser {
}

func accept[T: Named](value: T) -> Integer {
    return 1;
}

func start() {
    var admin = new AdminUser();
    var ok = accept[AdminUser](admin);
    Viper.Terminal.SayInt(ok);
}
)",
                                "inherited_constraint.zia");

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "accept$AdminUser"));
}

TEST(ZiaConstrainedGenerics, ReportsConstraintViolationWithSubjectDetails) {
    auto result = compileSource(R"(
module Test;

interface Named {
    func name() -> String;
}

class Plain {
}

class Box[T: Named] {
    expose T item;
}

func start() {
    var plain = new Plain();
    var box = new Box[Plain](plain);
}
)",
                                "constraint_violation.zia");

    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "Type 'Plain'"));
    EXPECT_TRUE(hasErrorContaining(result, "interface 'Named'"));
    EXPECT_TRUE(hasErrorContaining(result, "type parameter 'T'"));
    EXPECT_TRUE(hasErrorContaining(result, "of 'Box'"));
}

TEST(ZiaConstrainedGenerics, RejectsMultipleBoundsSyntaxUntilDesigned) {
    auto result = compileSource(R"(
module Test;

interface Named {
    func name() -> String;
}

interface Tagged {
    func tag() -> String;
}

func accept[T: Named + Tagged](value: T) -> Integer {
    return 1;
}

func start() {
}
)",
                                "multiple_bounds_future.zia");

    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.diagnostics.errorCount() > 0);
}

TEST(ZiaConstrainedGenerics, LowersConstrainedInterfaceCallInsideGenericFunction) {
    auto result = compileSource(R"(
module Test;

interface Named {
    func name() -> String;
}

class User implements Named {
    expose func name() -> String {
        return "user";
    }
}

func genericName[T: Named](value: T) -> String {
    return value.name();
}

func start() {
    var user = new User();
    var name = genericName[User](user);
    Viper.Terminal.Say(name);
}
)",
                                "constrained_generic_body_call.zia");

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "genericName$User"));
    EXPECT_TRUE(hasCall(result.module, "genericName$User", "rt_obj_class_id"));
    EXPECT_TRUE(hasCall(result.module, "genericName$User", "User.name"));
    EXPECT_FALSE(hasOpcode(result.module, "genericName$User", il::core::Opcode::CallIndirect));
}

int main() {
    return viper_test::run_all_tests();
}
