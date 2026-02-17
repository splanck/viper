//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for the SCC computation added to buildCallGraph.
//
// Test cases:
//   1. Linear chain (A → B → C): each function is its own SCC; order is
//      reverse-topological (C, B, A).
//   2. Mutual recursion (F ↔ G + H → F): F and G form one SCC; H is another.
//   3. Self-recursive function: single-node SCC flagged as recursive.
//   4. isRecursive: correctly identifies recursive vs. non-recursive functions.
//
//===----------------------------------------------------------------------===//

#include "il/analysis/CallGraph.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace
{

/// Build a minimal void→void function with a single Ret instruction.
Function makeRetFn(const std::string &name)
{
    Function f;
    f.name = name;
    f.retType = Type(Type::Kind::Void);
    BasicBlock entry;
    entry.label = "entry";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;
    f.blocks.push_back(std::move(entry));
    return f;
}

/// Add a void direct call from caller to callee (no result, no args).
void addCall(Function &caller, const std::string &callee)
{
    // Insert before the existing Ret.
    Instr call;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::Void);
    call.callee = callee;
    auto &entry = caller.blocks.front();
    entry.instructions.insert(entry.instructions.begin(), std::move(call));
}

/// Return the names in an SCC as a sorted vector for deterministic comparison.
std::vector<std::string> sorted(std::vector<std::string> v)
{
    std::sort(v.begin(), v.end());
    return v;
}

} // namespace

// A → B → C linear chain. Each function is its own SCC.
// SCCs must appear in reverse-topological order: C before B before A.
TEST(CallGraphSCC, LinearChainHasOneSCCPerFunction)
{
    Module M;
    M.functions.push_back(makeRetFn("A"));
    M.functions.push_back(makeRetFn("B"));
    M.functions.push_back(makeRetFn("C"));
    addCall(M.functions[0], "B"); // A → B
    addCall(M.functions[1], "C"); // B → C

    viper::analysis::CallGraph cg = viper::analysis::buildCallGraph(M);

    ASSERT_EQ(cg.sccs.size(), 3u); // one SCC per function

    // Every SCC must contain exactly one function.
    for (const auto &scc : cg.sccs)
        EXPECT_EQ(scc.size(), 1u);

    // sccIndex must cover all three functions.
    EXPECT_TRUE(cg.sccIndex.count("A"));
    EXPECT_TRUE(cg.sccIndex.count("B"));
    EXPECT_TRUE(cg.sccIndex.count("C"));

    // C must appear before B, and B before A (reverse-topo order).
    EXPECT_TRUE(cg.sccIndex.at("C") < cg.sccIndex.at("B"));
    EXPECT_TRUE(cg.sccIndex.at("B") < cg.sccIndex.at("A"));
}

// F ↔ G (mutual recursion), H → F (external caller).
// F and G must be in one SCC; H is its own SCC.
// H's SCC appears after {F,G}'s SCC in reverse-topo order.
TEST(CallGraphSCC, MutualRecursionFormsOneSCC)
{
    Module M;
    M.functions.push_back(makeRetFn("F"));
    M.functions.push_back(makeRetFn("G"));
    M.functions.push_back(makeRetFn("H"));
    addCall(M.functions[0], "G"); // F → G
    addCall(M.functions[1], "F"); // G → F  (mutual recursion)
    addCall(M.functions[2], "F"); // H → F

    viper::analysis::CallGraph cg = viper::analysis::buildCallGraph(M);

    ASSERT_EQ(cg.sccs.size(), 2u); // {F,G} and {H}

    // Locate the SCC that contains both F and G.
    ASSERT_TRUE(cg.sccIndex.count("F"));
    ASSERT_TRUE(cg.sccIndex.count("G"));
    ASSERT_TRUE(cg.sccIndex.count("H"));

    EXPECT_EQ(cg.sccIndex.at("F"), cg.sccIndex.at("G")); // same SCC
    EXPECT_NE(cg.sccIndex.at("F"), cg.sccIndex.at("H")); // different SCC

    // The {F,G} SCC must contain exactly 2 members.
    std::size_t fgSccIdx = cg.sccIndex.at("F");
    EXPECT_EQ(cg.sccs[fgSccIdx].size(), 2u);

    // {F,G} SCC must precede H's SCC (reverse-topo: callee before caller).
    EXPECT_TRUE(fgSccIdx < cg.sccIndex.at("H"));
}

// A self-recursive function forms a single-node SCC with isRecursive()=true.
TEST(CallGraphSCC, SelfRecursiveFunction)
{
    Module M;
    M.functions.push_back(makeRetFn("recur"));
    addCall(M.functions[0], "recur"); // recur → recur

    viper::analysis::CallGraph cg = viper::analysis::buildCallGraph(M);

    ASSERT_EQ(cg.sccs.size(), 1u);
    EXPECT_EQ(cg.sccs[0].size(), 1u);
    EXPECT_EQ(cg.sccs[0][0], "recur");

    EXPECT_TRUE(cg.isRecursive("recur"));
}

// Non-recursive function in a linear chain must report isRecursive()=false.
TEST(CallGraphSCC, NonRecursiveFunctionIsNotRecursive)
{
    Module M;
    M.functions.push_back(makeRetFn("leaf"));
    M.functions.push_back(makeRetFn("root"));
    addCall(M.functions[1], "leaf"); // root → leaf

    viper::analysis::CallGraph cg = viper::analysis::buildCallGraph(M);

    EXPECT_FALSE(cg.isRecursive("leaf"));
    EXPECT_FALSE(cg.isRecursive("root"));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
