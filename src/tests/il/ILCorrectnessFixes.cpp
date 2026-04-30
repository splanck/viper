//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/io/Parser.hpp"
#include "il/io/Serializer.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/verify/Verifier.hpp"
#include "il/verify/VerifierTable.hpp"
#include "tests/TestHarness.hpp"
#include "viper/vm/VM.hpp"

#include <sstream>
#include <string>

using namespace il::core;

namespace {

Module parseModule(const char *text) {
    Module module;
    std::istringstream input{text};
    auto parsed = il::io::Parser::parse(input, module);
    EXPECT_TRUE(parsed.hasValue());
    return module;
}

bool verifyFailsWith(const Module &module, const std::string &needle) {
    auto result = il::verify::Verifier::verify(module);
    if (result)
        return false;
    return result.error().message.find(needle) != std::string::npos;
}

Function makeMain(Type ret = Type(Type::Kind::I64)) {
    Function fn;
    fn.name = "main";
    fn.retType = ret;
    BasicBlock entry;
    entry.label = "entry";
    fn.blocks.push_back(std::move(entry));
    return fn;
}

} // namespace

TEST(ILCorrectness, ParserAcceptsInlineCommentsAndScalarGlobals) {
    Module module = parseModule(R"(il 0.2.0 # version comment
global const str @.msg = "http://example/#frag" # string comment
global const i64 @.answer = 42 # scalar comment
func @main() -> i64 {
entry:
  %p = gaddr @.answer # address scalar global
  %v = load i64, %p
  ret %v # done
}
)");

    ASSERT_EQ(module.globals.size(), 2u);
    EXPECT_EQ(module.globals[0].init, "http://example/#frag");
    EXPECT_EQ(module.globals[1].type.kind, Type::Kind::I64);
    EXPECT_EQ(module.globals[1].init, "42");
    EXPECT_TRUE(module.globals[1].isConst);

    auto verified = il::verify::Verifier::verify(module);
    ASSERT_TRUE(verified.hasValue());

    const std::string text = il::io::Serializer::toString(module);
    EXPECT_CONTAINS(text, "global const i64 @.answer = 42");
}

TEST(ILCorrectness, VmInitializesScalarGlobals) {
    Module module = parseModule(R"(il 0.2.0
global i64 @counter = 41
func @main() -> i64 {
entry:
  %p = gaddr @counter
  %v = load i64, %p
  ret %v
}
)");
    auto verified = il::verify::Verifier::verify(module);
    ASSERT_TRUE(verified.hasValue());

    il::vm::Runner runner(module, {});
    EXPECT_EQ(runner.run(), 41);
}

TEST(ILCorrectness, VerifierRejectsBadGlobalReferences) {
    Module missing = parseModule(R"(il 0.2.0
func @main() -> str {
entry:
  %s = const_str @missing
  ret %s
}
)");
    EXPECT_TRUE(verifyFailsWith(missing, "unknown string global @missing"));

    Module wrongKind = parseModule(R"(il 0.2.0
global i64 @n = 1
func @main() -> str {
entry:
  %s = const_str @n
  ret %s
}
)");
    EXPECT_TRUE(verifyFailsWith(wrongKind, "const.str operand must name a string global"));

    Module stringGAddr = parseModule(R"(il 0.2.0
global const str @s = "x"
func @main() -> ptr {
entry:
  %p = gaddr @s
  ret %p
}
)");
    EXPECT_TRUE(verifyFailsWith(stringGAddr, "gaddr requires a scalar storage global"));
}

TEST(ILCorrectness, DominanceRejectsCrossComponentUses) {
    Module module;
    Function fn = makeMain();
    BasicBlock &entry = fn.blocks.front();
    Instr add;
    add.result = 0;
    add.op = Opcode::IAddOvf;
    add.type = Type(Type::Kind::I64);
    add.operands = {Value::constInt(1), Value::constInt(2)};
    entry.instructions.push_back(std::move(add));
    Instr retEntry;
    retEntry.op = Opcode::Ret;
    retEntry.type = Type(Type::Kind::Void);
    retEntry.operands = {Value::constInt(0)};
    entry.instructions.push_back(std::move(retEntry));
    entry.terminated = true;

    BasicBlock dead;
    dead.label = "dead";
    Instr retDead;
    retDead.op = Opcode::Ret;
    retDead.type = Type(Type::Kind::Void);
    retDead.operands = {Value::temp(0)};
    dead.instructions.push_back(std::move(retDead));
    dead.terminated = true;
    fn.blocks.push_back(std::move(dead));
    fn.valueNames.resize(1);
    module.functions.push_back(std::move(fn));

    EXPECT_TRUE(verifyFailsWith(module, "not dominated by definition"));
}

TEST(ILCorrectness, PureFunctionMayUsePrivateStack) {
    Module module;
    Function fn = makeMain();
    fn.attrs().pure = true;
    BasicBlock &entry = fn.blocks.front();

    Instr alloca;
    alloca.result = 0;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands = {Value::constInt(8)};
    entry.instructions.push_back(std::move(alloca));

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::I64);
    store.operands = {Value::temp(0), Value::constInt(7)};
    entry.instructions.push_back(std::move(store));

    Instr load;
    load.result = 1;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands = {Value::temp(0)};
    entry.instructions.push_back(std::move(load));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(1)};
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;
    fn.valueNames.resize(2);
    module.functions.push_back(std::move(fn));

    auto verified = il::verify::Verifier::verify(module);
    EXPECT_TRUE(verified.hasValue());
}

TEST(ILCorrectness, TrappingDivisionViolatesNothrow) {
    Module module;
    Function fn = makeMain();
    fn.attrs().nothrow = true;
    BasicBlock &entry = fn.blocks.front();

    Instr div;
    div.result = 0;
    div.op = Opcode::SDivChk0;
    div.type = Type(Type::Kind::I64);
    div.operands = {Value::constInt(4), Value::constInt(2)};
    entry.instructions.push_back(std::move(div));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(0)};
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;
    fn.valueNames.resize(1);
    module.functions.push_back(std::move(fn));

    EXPECT_TRUE(verifyFailsWith(module, "nothrow function contains trapping instruction"));
}

TEST(ILCorrectness, VerifierTableMarksPlainDivRemAsTrapping) {
    for (Opcode op : {Opcode::SDiv, Opcode::UDiv, Opcode::SRem, Opcode::URem}) {
        auto props = il::verify::lookup(op);
        ASSERT_TRUE(props.has_value());
        EXPECT_TRUE(props->canTrap);
    }
}

TEST(ILCorrectness, ExternCallAttributesRequireMetadata) {
    Module module = parseModule(R"(il 0.2.0
extern @host_value() -> i64
func @main() -> i64 {
entry:
  %v = call @host_value() [pure, nothrow]
  ret %v
}
)");
    EXPECT_TRUE(verifyFailsWith(module, "call attributes require callee effect metadata"));
}

TEST(ILCorrectness, ConstNullAllowsPointerLikeTypesOnly) {
    Module module;
    Function fn = makeMain(Type(Type::Kind::Error));
    BasicBlock &entry = fn.blocks.front();
    Instr nullErr;
    nullErr.result = 0;
    nullErr.op = Opcode::ConstNull;
    nullErr.type = Type(Type::Kind::Error);
    entry.instructions.push_back(std::move(nullErr));
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(0)};
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;
    fn.valueNames.resize(1);
    module.functions.push_back(std::move(fn));
    EXPECT_TRUE(il::verify::Verifier::verify(module).hasValue());

    module.functions.front().blocks.front().instructions.front().type = Type(Type::Kind::I64);
    EXPECT_TRUE(verifyFailsWith(module, "const.null result type must be"));
}

TEST(ILCorrectness, ConstFoldMasksShiftCounts) {
    Module module;
    Function fn = makeMain();
    BasicBlock &entry = fn.blocks.front();
    Instr shl;
    shl.result = 0;
    shl.op = Opcode::Shl;
    shl.type = Type(Type::Kind::I64);
    shl.operands = {Value::constInt(1), Value::constInt(65)};
    entry.instructions.push_back(std::move(shl));
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(0)};
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;
    fn.valueNames.resize(1);
    module.functions.push_back(std::move(fn));

    il::transform::constFold(module);
    const auto &retInstr = module.functions.front().blocks.front().instructions.back();
    ASSERT_EQ(retInstr.operands.size(), 1u);
    ASSERT_EQ(retInstr.operands.front().kind, Value::Kind::ConstInt);
    EXPECT_EQ(retInstr.operands.front().i64, 2);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
