//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/zia/test_zia_entity_string_field.cpp
// Purpose: Regression test for BUG-ADV-001 — entity string field loads must
//          emit rt_str_retain_maybe to prevent use-after-free.
// Key invariants:
//   - Every Load of a Str-typed field must be followed by rt_str_retain_maybe
//   - Applies to both value types and entity types
// Ownership/Lifetime:
//   - Test-scoped objects only
// Links: demos/zia/sqldb/PLATFORM_BUGS_20260228.md (BUG-ADV-001)
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;
using namespace il::core;

namespace
{

/// @brief Helper: compile Zia source and return the result.
CompilerResult compileSource(SourceManager &sm, const std::string &source)
{
    CompilerInput input{.source = source, .path = "<test>"};
    CompilerOptions opts{};
    return compile(input, opts, sm);
}

/// @brief Count how many times a callee appears as a Call instruction
///        in the named function within the compiled module.
int countCallsTo(const Module &mod, const std::string &funcName,
                 const std::string &calleeName)
{
    int count = 0;
    for (const auto &fn : mod.functions)
    {
        if (fn.name != funcName)
            continue;
        for (const auto &block : fn.blocks)
        {
            for (const auto &instr : block.instructions)
            {
                if (instr.op == Opcode::Call && instr.callee == calleeName)
                    ++count;
            }
        }
    }
    return count;
}

/// @brief Check that a Load of Str type is followed by rt_str_retain_maybe
///        in the named function.
bool hasRetainAfterStrLoad(const Module &mod, const std::string &funcName)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name != funcName)
            continue;
        for (const auto &block : fn.blocks)
        {
            const auto &instrs = block.instructions;
            for (size_t i = 0; i + 1 < instrs.size(); ++i)
            {
                if (instrs[i].op == Opcode::Load &&
                    instrs[i].type.kind == Type::Kind::Str)
                {
                    // The next instruction should be a Call to
                    // rt_str_retain_maybe
                    if (instrs[i + 1].op == Opcode::Call &&
                        instrs[i + 1].callee == "rt_str_retain_maybe")
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

//===----------------------------------------------------------------------===//
// BUG-ADV-001: Entity string field read must emit rt_str_retain_maybe
//===----------------------------------------------------------------------===//

/// @brief Entity with String field — reading field should emit retain.
TEST(ZiaEntityStringField, EntityFieldReadEmitsRetain)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Wrapper {
    expose String name;
}

func start() {
    var w = new Wrapper();
    w.name = "hello";
    var s = w.name;
    Viper.Terminal.Say(s);
}
)";
    auto result = compileSource(sm, source);
    EXPECT_TRUE(result.succeeded());

    // The main function should contain rt_str_retain_maybe calls
    int retainCount = countCallsTo(result.module, "main", "rt_str_retain_maybe");
    EXPECT_TRUE(retainCount >= 1);
}

/// @brief Verify retain is emitted when entity string field is used directly
///        in concatenation (the actual crash scenario from BUG-ADV-001).
TEST(ZiaEntityStringField, FieldConcatEmitsRetain)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Result {
    expose Boolean success;
    expose String message;
}

func makeResult() -> Result {
    var r = new Result();
    r.success = false;
    r.message = "Something went wrong";
    return r;
}

func start() {
    var r = makeResult();
    if r.success == false {
        var msg = "Error: " + r.message;
        Viper.Terminal.Say(msg);
    }
}
)";
    auto result = compileSource(sm, source);
    EXPECT_TRUE(result.succeeded());

    // main should have retain after loading r.message
    EXPECT_TRUE(hasRetainAfterStrLoad(result.module, "main"));
}

/// @brief Value type with String field — also needs retain.
TEST(ZiaEntityStringField, ValueTypeFieldReadEmitsRetain)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Pair {
    expose String key;
    expose String val;
}

func start() {
    var p = new Pair();
    p.key = "name";
    p.val = "Alice";
    Viper.Terminal.Say(p.key);
    Viper.Terminal.Say(p.val);
}
)";
    auto result = compileSource(sm, source);
    EXPECT_TRUE(result.succeeded());

    // Should have at least 2 retains (one per field read)
    int retainCount = countCallsTo(result.module, "main", "rt_str_retain_maybe");
    EXPECT_TRUE(retainCount >= 2);
}

/// @brief Nested entity string field access should also emit retain.
TEST(ZiaEntityStringField, NestedEntityFieldRetain)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Inner {
    expose String text;
}

entity Outer {
    expose Inner inner;
}

func start() {
    var o = new Outer();
    o.inner = new Inner();
    o.inner.text = "nested";
    var s = o.inner.text;
    Viper.Terminal.Say(s);
}
)";
    auto result = compileSource(sm, source);
    EXPECT_TRUE(result.succeeded());

    // Should retain the loaded string from nested access
    int retainCount = countCallsTo(result.module, "main", "rt_str_retain_maybe");
    EXPECT_TRUE(retainCount >= 1);
}

/// @brief Non-string fields should NOT emit rt_str_retain_maybe.
TEST(ZiaEntityStringField, NonStringFieldNoRetain)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Counter {
    expose Integer count;
    expose Boolean active;
}

func start() {
    var c = new Counter();
    c.count = 42;
    c.active = true;
    Viper.Terminal.SayInt(c.count);
}
)";
    auto result = compileSource(sm, source);
    EXPECT_TRUE(result.succeeded());

    // No string fields => no rt_str_retain_maybe calls in main
    int retainCount = countCallsTo(result.module, "main", "rt_str_retain_maybe");
    EXPECT_EQ(retainCount, 0);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
