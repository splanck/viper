//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_codegen_x86_64_lowering_rules.cpp
// Purpose: Validate table-driven selection of x86-64 lowering rules.
// Key invariants: Rule lookup honours opcode prefixes and operand shapes.
// Ownership/Lifetime: Constructs IL instructions on the stack without touching MIR.
// Links: src/codegen/x86_64/LoweringRules.cpp, src/codegen/x86_64/LoweringRuleTable.hpp
//
//===----------------------------------------------------------------------===//
#include "codegen/x86_64/LowerILToMIR.hpp"
#include "codegen/x86_64/LoweringRules.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using viper::codegen::x64::ILInstr;
using viper::codegen::x64::ILValue;

namespace
{

ILValue makeValue(ILValue::Kind kind, int id)
{
    ILValue value{};
    value.kind = kind;
    value.id = id;
    return value;
}

ILValue makeImmediate(ILValue::Kind kind, int64_t imm)
{
    ILValue value{};
    value.kind = kind;
    value.id = -1;
    value.i64 = imm;
    return value;
}

ILValue makeLabel(std::string name)
{
    ILValue value{};
    value.kind = ILValue::Kind::LABEL;
    value.label = std::move(name);
    value.id = -1;
    return value;
}

} // namespace

TEST(LoweringRuleLookup, SelectsArithmeticRule)
{
    ILInstr instr{};
    instr.opcode = "add";
    instr.resultKind = ILValue::Kind::I64;
    instr.resultId = 0;
    instr.ops = {makeValue(ILValue::Kind::I64, 1), makeValue(ILValue::Kind::I64, 2)};

    const auto *rule = viper::codegen::x64::viper_select_rule(instr);
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(std::string(rule->name), std::string{"add"});
    EXPECT_TRUE(rule->match(instr));
}

TEST(LoweringRuleLookup, SelectsComparePrefixRule)
{
    ILInstr instr{};
    instr.opcode = "icmp_eq";
    instr.resultKind = ILValue::Kind::I1;
    instr.resultId = 5;
    instr.ops = {makeValue(ILValue::Kind::I64, 10), makeImmediate(ILValue::Kind::I64, 0)};

    const auto *rule = viper::codegen::x64::viper_select_rule(instr);
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(std::string(rule->name), std::string{"icmp"});
    EXPECT_TRUE(rule->match(instr));
}

TEST(LoweringRuleLookup, SelectsShiftRule)
{
    ILInstr instr{};
    instr.opcode = "shl";
    instr.resultKind = ILValue::Kind::I64;
    instr.resultId = 7;
    instr.ops = {makeValue(ILValue::Kind::I64, 8), makeImmediate(ILValue::Kind::I64, 1)};

    const auto *rule = viper::codegen::x64::viper_select_rule(instr);
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(std::string(rule->name), std::string{"shl"});
    EXPECT_TRUE(rule->match(instr));
}

TEST(LoweringRuleLookup, SelectsLoadAndStoreRules)
{
    ILInstr load{};
    load.opcode = "load";
    load.resultKind = ILValue::Kind::I64;
    load.resultId = 3;
    load.ops = {makeValue(ILValue::Kind::PTR, 9), makeImmediate(ILValue::Kind::I64, 16)};

    const auto *loadRule = viper::codegen::x64::viper_select_rule(load);
    ASSERT_NE(loadRule, nullptr);
    EXPECT_EQ(std::string(loadRule->name), std::string{"load"});
    EXPECT_TRUE(loadRule->match(load));

    ILInstr store{};
    store.opcode = "store";
    store.ops = {makeValue(ILValue::Kind::I64, 11),
                 makeValue(ILValue::Kind::PTR, 12),
                 makeImmediate(ILValue::Kind::I64, 8)};

    const auto *storeRule = viper::codegen::x64::viper_select_rule(store);
    ASSERT_NE(storeRule, nullptr);
    EXPECT_EQ(std::string(storeRule->name), std::string{"store"});
    EXPECT_TRUE(storeRule->match(store));
}

TEST(LoweringRuleLookup, SelectsCallRule)
{
    ILInstr instr{};
    instr.opcode = "call";
    instr.ops = {makeLabel("callee"), makeValue(ILValue::Kind::I64, 13)};

    const auto *rule = viper::codegen::x64::viper_select_rule(instr);
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(std::string(rule->name), std::string{"call"});
    EXPECT_TRUE(rule->match(instr));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
