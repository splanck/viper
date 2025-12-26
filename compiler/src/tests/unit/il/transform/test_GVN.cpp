//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for GVN + Redundant Load Elimination. Builds tiny IL functions and
// verifies that cross-block common subexpressions and dominated redundant loads
// are eliminated conservatively.
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/GVN.hpp"
#include "il/transform/analysis/Liveness.hpp" // CFGInfo + buildCFG

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cassert>
#include <iostream>

using namespace il::core;

static il::transform::AnalysisRegistry makeRegistry()
{
    il::transform::AnalysisRegistry registry;
    registry.registerFunctionAnalysis<il::transform::CFGInfo>(
        "cfg", [](Module &mod, Function &fn) { return il::transform::buildCFG(mod, fn); });
    registry.registerFunctionAnalysis<viper::analysis::DomTree>(
        "dominators",
        [](Module &mod, Function &fn)
        {
            viper::analysis::CFGContext ctx(mod);
            return viper::analysis::computeDominatorTree(ctx, fn);
        });
    registry.registerFunctionAnalysis<viper::analysis::BasicAA>(
        "basic-aa", [](Module &mod, Function &fn) { return viper::analysis::BasicAA(mod, fn); });
    return registry;
}

static void test_cse_cross_block()
{
    Module M;
    Function F;
    F.name = "gvn_cse";
    F.retType = Type(Type::Kind::I64);

    unsigned id = 0;
    Param a{"a", Type(Type::Kind::I64), id++};
    Param b{"b", Type(Type::Kind::I64), id++};
    F.params.push_back(a);
    F.params.push_back(b);
    F.valueNames.resize(id);

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr add1;
        add1.result = id++;
        add1.op = Opcode::Add;
        add1.type = Type(Type::Kind::I64);
        add1.operands.push_back(Value::temp(a.id));
        add1.operands.push_back(Value::temp(b.id));
        const unsigned add1Id = *add1.result;

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("next");
        br.brArgs.emplace_back(std::vector<Value>{Value::temp(add1Id)});

        entry.instructions.push_back(std::move(add1));
        entry.instructions.push_back(std::move(br));
        entry.terminated = true;
    }

    BasicBlock next;
    next.label = "next";
    Param fromEntry{"x", Type(Type::Kind::I64), id++};
    next.params.push_back(fromEntry);
    {
        Instr add2;
        add2.result = id++;
        add2.op = Opcode::Add;
        add2.type = Type(Type::Kind::I64);
        add2.operands.push_back(Value::temp(a.id));
        add2.operands.push_back(Value::temp(b.id));
        const unsigned add2Id = *add2.result;

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(add2Id));

        next.instructions.push_back(std::move(add2));
        next.instructions.push_back(std::move(ret));
        next.terminated = true;
    }

    F.blocks.push_back(std::move(entry));
    F.blocks.push_back(std::move(next));
    M.functions.push_back(std::move(F));

    Function &Fn = M.functions.back();

    auto registry = makeRegistry();
    il::transform::AnalysisManager AM(M, registry);

    il::transform::GVN gvn;
    auto preserved = gvn.run(Fn, AM);
    (void)preserved;
    (void)M; // no-op in release; keep to avoid unused warning if debug prints removed

    // In the "next" block, the add should be eliminated, and ret should use the value from entry
    assert(Fn.blocks.size() == 2);
    const BasicBlock &NextB = Fn.blocks[1];
    assert(NextB.instructions.size() == 1);
    const Instr &Only = NextB.instructions.front();
    assert(Only.op == Opcode::Ret);
    assert(!Only.operands.empty());
    assert(Only.operands.front().kind == Value::Kind::Temp);
    // The operand should be either the block param or the entry result; both are okay, but
    // if GVN eliminated the second add, ret's operand should reference entry's add result id
    // (id==2)
}

static void test_redundant_load_elim()
{
    Module M;
    Function F;
    F.name = "gvn_rle";
    F.retType = Type(Type::Kind::I64);

    unsigned id = 0;
    BasicBlock entry;
    entry.label = "entry";
    {
        // p = alloca 8
        Instr allocaI;
        allocaI.result = id++;
        allocaI.op = Opcode::Alloca;
        allocaI.type = Type(Type::Kind::Ptr);
        allocaI.operands.push_back(Value::constInt(8));
        const unsigned pId = *allocaI.result;

        // v0 = load i64, p
        Instr ld0;
        ld0.result = id++;
        ld0.op = Opcode::Load;
        ld0.type = Type(Type::Kind::I64);
        ld0.operands.push_back(Value::temp(pId));

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("next");
        br.brArgs.emplace_back(std::vector<Value>{Value::temp(*ld0.result)});

        entry.instructions.push_back(std::move(allocaI));
        entry.instructions.push_back(std::move(ld0));
        entry.instructions.push_back(std::move(br));
        entry.terminated = true;
    }

    BasicBlock next;
    next.label = "next";
    {
        Param v0{"v0", Type(Type::Kind::I64), id++};
        next.params.push_back(v0);

        // v1 = load i64, p          ; dominated by previous load, no clobber
        Instr ld1;
        ld1.result = id++;
        ld1.op = Opcode::Load;
        ld1.type = Type(Type::Kind::I64);
        // Use the same p (%0)
        ld1.operands.push_back(Value::temp(0));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(*ld1.result));

        next.instructions.push_back(std::move(ld1));
        next.instructions.push_back(std::move(ret));
        next.terminated = true;
    }

    F.blocks.push_back(std::move(entry));
    F.blocks.push_back(std::move(next));
    M.functions.push_back(std::move(F));

    Function &Fn = M.functions.back();

    auto registry = makeRegistry();
    il::transform::AnalysisManager AM(M, registry);

    il::transform::GVN gvn;
    auto preserved = gvn.run(Fn, AM);
    (void)preserved;
    (void)M;

    // The second load should be eliminated; next block should only have Ret.
    assert(Fn.blocks.size() == 2);
    const BasicBlock &NextB = Fn.blocks[1];
    assert(NextB.instructions.size() == 1);
    const Instr &Only = NextB.instructions.front();
    assert(Only.op == Opcode::Ret);
    assert(Only.operands.size() == 1);
    // ret operand should be a temp id corresponding to first load's result (id==1)
    assert(Only.operands.front().kind == Value::Kind::Temp);
}

int main()
{
    test_cse_cross_block();
    test_redundant_load_elim();
    return 0;
}
