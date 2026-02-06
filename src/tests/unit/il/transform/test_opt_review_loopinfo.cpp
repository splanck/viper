//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for LoopInfo fixes from the IL optimization review:
// - No duplicate blockLabels when latch == header (self-loop)
// - blockLabels and latchLabels remain consistent
//
//===----------------------------------------------------------------------===//

#include "il/transform/analysis/LoopInfo.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <unordered_set>

using namespace il::core;

namespace
{

// Build a function with a self-loop: entry -> header -> header (back edge)
//                                                  \-> exit
Module buildSelfLoopModule()
{
    Module module;
    Function fn;
    fn.name = "self_loop";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";
    Instr brEntry;
    brEntry.op = Opcode::Br;
    brEntry.type = Type(Type::Kind::Void);
    brEntry.labels.push_back("header");
    brEntry.brArgs.emplace_back();
    entry.instructions.push_back(std::move(brEntry));
    entry.terminated = true;

    BasicBlock header;
    header.label = "header";
    // CBr back to header or exit
    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(Value::constBool(true));
    cbr.labels = {"header", "exit"};
    cbr.brArgs = {{}, {}};
    header.instructions.push_back(std::move(cbr));
    header.terminated = true;

    BasicBlock exit;
    exit.label = "exit";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    fn.blocks = {std::move(entry), std::move(header), std::move(exit)};
    module.functions.push_back(std::move(fn));
    return module;
}

// Build a function with a normal loop: entry -> header -> body -> header
//                                                  \-> exit
Module buildNormalLoopModule()
{
    Module module;
    Function fn;
    fn.name = "normal_loop";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";
    Instr brEntry;
    brEntry.op = Opcode::Br;
    brEntry.type = Type(Type::Kind::Void);
    brEntry.labels.push_back("header");
    brEntry.brArgs.emplace_back();
    entry.instructions.push_back(std::move(brEntry));
    entry.terminated = true;

    BasicBlock header;
    header.label = "header";
    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(Value::constBool(true));
    cbr.labels = {"body", "exit"};
    cbr.brArgs = {{}, {}};
    header.instructions.push_back(std::move(cbr));
    header.terminated = true;

    BasicBlock body;
    body.label = "body";
    Instr brBody;
    brBody.op = Opcode::Br;
    brBody.type = Type(Type::Kind::Void);
    brBody.labels.push_back("header");
    brBody.brArgs.emplace_back();
    body.instructions.push_back(std::move(brBody));
    body.terminated = true;

    BasicBlock exit;
    exit.label = "exit";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    fn.blocks = {std::move(entry), std::move(header), std::move(body), std::move(exit)};
    module.functions.push_back(std::move(fn));
    return module;
}

bool hasDuplicates(const std::vector<std::string> &vec)
{
    std::unordered_set<std::string> seen;
    for (const auto &s : vec)
    {
        if (!seen.insert(s).second)
            return true;
    }
    return false;
}

} // namespace

// The fix ensures latch blocks are not added to blockLabels twice
// when the latch is the header itself (self-loop)
TEST(LoopInfo, SelfLoopNoDuplicateBlockLabels)
{
    Module module = buildSelfLoopModule();
    Function &fn = module.functions.front();

    il::transform::LoopInfo info = il::transform::computeLoopInfo(module, fn);

    ASSERT_EQ(info.loops().size(), 1U);
    const il::transform::Loop &loop = info.loops()[0];

    EXPECT_EQ(loop.headerLabel, "header");
    EXPECT_FALSE(hasDuplicates(loop.blockLabels));
    EXPECT_TRUE(loop.contains("header"));

    // Latch should be "header" (self-loop)
    ASSERT_FALSE(loop.latchLabels.empty());
    EXPECT_EQ(loop.latchLabels[0], "header");
}

// Normal loop with separate latch block should also have no duplicates
TEST(LoopInfo, NormalLoopNoDuplicateBlockLabels)
{
    Module module = buildNormalLoopModule();
    Function &fn = module.functions.front();

    il::transform::LoopInfo info = il::transform::computeLoopInfo(module, fn);

    ASSERT_EQ(info.loops().size(), 1U);
    const il::transform::Loop &loop = info.loops()[0];

    EXPECT_EQ(loop.headerLabel, "header");
    EXPECT_FALSE(hasDuplicates(loop.blockLabels));

    // Should contain header and body
    EXPECT_TRUE(loop.contains("header"));
    EXPECT_TRUE(loop.contains("body"));

    // Should not contain entry or exit
    EXPECT_FALSE(loop.contains("entry"));
    EXPECT_FALSE(loop.contains("exit"));

    // Latch should be "body"
    ASSERT_FALSE(loop.latchLabels.empty());
    EXPECT_EQ(loop.latchLabels[0], "body");
}

// Verify blockLabels count matches expected
TEST(LoopInfo, SelfLoopBlockCount)
{
    Module module = buildSelfLoopModule();
    Function &fn = module.functions.front();

    il::transform::LoopInfo info = il::transform::computeLoopInfo(module, fn);

    ASSERT_EQ(info.loops().size(), 1U);
    const il::transform::Loop &loop = info.loops()[0];

    // Self-loop: only the header block is in the loop
    EXPECT_EQ(loop.blockLabels.size(), 1U);
}

TEST(LoopInfo, NormalLoopBlockCount)
{
    Module module = buildNormalLoopModule();
    Function &fn = module.functions.front();

    il::transform::LoopInfo info = il::transform::computeLoopInfo(module, fn);

    ASSERT_EQ(info.loops().size(), 1U);
    const il::transform::Loop &loop = info.loops()[0];

    // Normal loop: header + body = 2 blocks
    EXPECT_EQ(loop.blockLabels.size(), 2U);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
