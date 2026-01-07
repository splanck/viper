//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Alias precision and its consumers (DSE/LICM/GVN).
//
// Exercises new BasicAA distinctions (stack vs global, constant-offset GEPs)
// and ensures the optimisation passes exploit that extra precision safely.
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/DSE.hpp"
#include "il/transform/GVN.hpp"
#include "il/transform/LICM.hpp"
#include "il/transform/LoopSimplify.hpp"
#include "il/transform/PassRegistry.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/io/Serializer.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>

using namespace il::core;

namespace
{

struct AnalysisSetup
{
    il::transform::AnalysisRegistry registry;

    AnalysisSetup()
    {
        registry.registerFunctionAnalysis<il::transform::CFGInfo>(
            "cfg", [](Module &mod, Function &fn) { return il::transform::buildCFG(mod, fn); });
        registry.registerFunctionAnalysis<viper::analysis::DomTree>(
            "dominators",
            [](Module &mod, Function &fn)
            {
                viper::analysis::CFGContext ctx(mod);
                return viper::analysis::computeDominatorTree(ctx, fn);
            });
        registry.registerFunctionAnalysis<il::transform::LoopInfo>(
            "loop-info",
            [](Module &mod, Function &fn) { return il::transform::computeLoopInfo(mod, fn); });
        registry.registerFunctionAnalysis<viper::analysis::BasicAA>(
            "basic-aa",
            [](Module &mod, Function &fn) { return viper::analysis::BasicAA(mod, fn); });
    }
};

static void verifyOrDie(const Module &module)
{
    auto verifyResult = il::verify::Verifier::verify(module);
    if (!verifyResult)
    {
        il::support::printDiag(verifyResult.error(), std::cerr);
        assert(false && "Module verification failed");
    }
}

void testDSENoElimOnDisjointFields()
{
    Module m;
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("dse_disjoint", Type(Type::Kind::Void), {});
    b.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    b.setInsertPoint(entry);

    const unsigned base = b.reserveTempId();
    Instr alloca;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.result = base;
    alloca.operands.push_back(Value::constInt(16));
    entry.instructions.push_back(std::move(alloca));

    const unsigned f0 = b.reserveTempId();
    Instr gep0;
    gep0.op = Opcode::GEP;
    gep0.type = Type(Type::Kind::Ptr);
    gep0.result = f0;
    gep0.operands = {Value::temp(base), Value::constInt(0)};
    entry.instructions.push_back(std::move(gep0));

    const unsigned f1 = b.reserveTempId();
    Instr gep1;
    gep1.op = Opcode::GEP;
    gep1.type = Type(Type::Kind::Ptr);
    gep1.result = f1;
    gep1.operands = {Value::temp(base), Value::constInt(8)};
    entry.instructions.push_back(std::move(gep1));

    Instr store0;
    store0.op = Opcode::Store;
    store0.type = Type(Type::Kind::I64);
    store0.operands = {Value::temp(f0), Value::constInt(1)};
    entry.instructions.push_back(std::move(store0));

    Instr store1;
    store1.op = Opcode::Store;
    store1.type = Type(Type::Kind::I64);
    store1.operands = {Value::temp(f1), Value::constInt(2)};
    entry.instructions.push_back(std::move(store1));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    verifyOrDie(m);

    AnalysisSetup setup;
    il::transform::AnalysisManager am(m, setup.registry);
    const bool changed = il::transform::runDSE(fn, am);
    // Stores touch disjoint struct fields; DSE should keep both.
    std::cerr << "dse-changed=" << changed << " instrs=" << entry.instructions.size() << std::endl;
    assert(!changed);
    assert(entry.instructions.size() == 6);
}

void testLICMLoadHoistWithDisjointStore()
{
    Module m;
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("licm_alias", Type(Type::Kind::Void), {});

    // Preheader
    b.createBlock(fn, "entry");
    BasicBlock &pre = fn.blocks[0];
    b.setInsertPoint(pre);
    const unsigned base = b.reserveTempId();
    Instr alloca;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.result = base;
    alloca.operands.push_back(Value::constInt(8));
    pre.instructions.push_back(std::move(alloca));
    Instr toHeader;
    toHeader.op = Opcode::Br;
    toHeader.type = Type(Type::Kind::Void);
    toHeader.labels = {"header"};
    toHeader.brArgs.emplace_back();
    pre.instructions.push_back(std::move(toHeader));
    pre.terminated = true;

    // Header with invariant load and store to global
    b.createBlock(fn, "header");
    BasicBlock &header = fn.blocks[1];
    b.setInsertPoint(header);
    const unsigned loadId = b.reserveTempId();
    Instr load;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.result = loadId;
    load.operands = {Value::temp(base)};
    header.instructions.push_back(std::move(load));

    const unsigned gPtrId = b.reserveTempId();
    Instr gaddr;
    gaddr.op = Opcode::GAddr;
    gaddr.type = Type(Type::Kind::Ptr);
    gaddr.result = gPtrId;
    gaddr.operands = {Value::global("g")};
    header.instructions.push_back(std::move(gaddr));

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::I64);
    store.operands = {Value::temp(gPtrId), Value::temp(loadId)};
    header.instructions.push_back(std::move(store));

    Instr toLatch;
    toLatch.op = Opcode::Br;
    toLatch.type = Type(Type::Kind::Void);
    toLatch.labels = {"latch"};
    toLatch.brArgs.emplace_back();
    header.instructions.push_back(std::move(toLatch));
    header.terminated = true;

    // Latch backedge
    b.createBlock(fn, "latch");
    BasicBlock &latch = fn.blocks[2];
    b.setInsertPoint(latch);
    Instr back;
    back.op = Opcode::Br;
    back.type = Type(Type::Kind::Void);
    back.labels = {"header"};
    back.brArgs.emplace_back();
    latch.instructions.push_back(std::move(back));
    latch.terminated = true;

    // Exit (unreachable but keeps verifier happy)
    b.createBlock(fn, "exit");
    BasicBlock &exit = fn.blocks[3];
    b.setInsertPoint(exit);
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    verifyOrDie(m);

    AnalysisSetup setup;
    il::transform::AnalysisManager am(m, setup.registry);
    il::transform::LoopSimplify simplify;
    simplify.run(fn, am);

    auto &loopInfo = am.getFunctionResult<il::transform::LoopInfo>("loop-info", fn);
    std::cerr << "loops=" << loopInfo.loops().size() << std::endl;

    auto &aa = am.getFunctionResult<viper::analysis::BasicAA>("basic-aa", fn);
    auto loadSize = viper::analysis::BasicAA::typeSizeBytes(header.instructions[0].type);
    auto storeSize = viper::analysis::BasicAA::typeSizeBytes(header.instructions[2].type);
    std::cerr << "alias(load,store)="
              << static_cast<int>(aa.alias(header.instructions[0].operands[0],
                                           header.instructions[2].operands[0],
                                           loadSize,
                                           storeSize))
              << std::endl;

    bool loopHasMod = false;
    if (!loopInfo.loops().empty())
    {
        for (const auto &label : loopInfo.loops().front().blockLabels)
        {
            auto it = std::find_if(fn.blocks.begin(),
                                   fn.blocks.end(),
                                   [&](const BasicBlock &blk) { return blk.label == label; });
            if (it == fn.blocks.end())
                continue;
            for (const auto &ins : it->instructions)
            {
                auto me = memoryEffects(ins.op);
                if (me == MemoryEffects::Write || me == MemoryEffects::ReadWrite ||
                    me == MemoryEffects::Unknown)
                    loopHasMod = true;
            }
        }
    }
    std::cerr << "loopHasModFlag=" << loopHasMod << " stores=" << header.instructions.size()
              << std::endl;

    il::transform::LICM licm;
    licm.run(fn, am);

    bool loadInHeader = false;
    for (const auto &ins : header.instructions)
        loadInHeader |= ins.op == Opcode::Load;

    bool loadInPre = false;
    for (const auto &ins : fn.blocks[0].instructions)
        loadInPre |= ins.op == Opcode::Load;

    if (!(loadInPre && !loadInHeader))
        std::cerr << il::io::Serializer::toString(m, il::io::Serializer::Mode::Pretty) << std::endl;
    std::cerr << "licm-pre=" << loadInPre << " hdr=" << loadInHeader << std::endl;
    assert(loadInPre && !loadInHeader && "load should be hoisted to preheader");
}

void testGVNRedundantLoadSameField()
{
    Module m;
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("gvn_alias", Type(Type::Kind::I64), {});
    b.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    b.setInsertPoint(entry);

    const unsigned base = b.reserveTempId();
    Instr alloca;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.result = base;
    alloca.operands.push_back(Value::constInt(8));
    entry.instructions.push_back(std::move(alloca));

    const unsigned gepA = b.reserveTempId();
    Instr gep0;
    gep0.op = Opcode::GEP;
    gep0.type = Type(Type::Kind::Ptr);
    gep0.result = gepA;
    gep0.operands = {Value::temp(base), Value::constInt(0)};
    entry.instructions.push_back(std::move(gep0));

    const unsigned gepB = b.reserveTempId();
    Instr gep1;
    gep1.op = Opcode::GEP;
    gep1.type = Type(Type::Kind::Ptr);
    gep1.result = gepB;
    gep1.operands = {Value::temp(base), Value::constInt(0)};
    entry.instructions.push_back(std::move(gep1));

    const unsigned load0 = b.reserveTempId();
    Instr l0;
    l0.op = Opcode::Load;
    l0.type = Type(Type::Kind::I64);
    l0.result = load0;
    l0.operands = {Value::temp(gepA)};
    entry.instructions.push_back(std::move(l0));

    const unsigned load1 = b.reserveTempId();
    Instr l1;
    l1.op = Opcode::Load;
    l1.type = Type(Type::Kind::I64);
    l1.result = load1;
    l1.operands = {Value::temp(gepB)};
    entry.instructions.push_back(std::move(l1));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::I64);
    ret.operands = {Value::temp(load1)};
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    verifyOrDie(m);

    AnalysisSetup setup;
    il::transform::AnalysisManager am(m, setup.registry);
    il::transform::GVN gvn;
    gvn.run(fn, am);

    // Second load should be eliminated and the return should use load0.
    size_t loadCount = 0;
    for (const auto &ins : entry.instructions)
        if (ins.op == Opcode::Load)
            ++loadCount;
    std::cerr << "gvn-loads=" << loadCount << " ret-id="
              << (entry.instructions.back().operands.empty()
                      ? -1
                      : static_cast<int>(entry.instructions.back().operands[0].id))
              << std::endl;
    assert(loadCount == 1);
    const Instr &retI = entry.instructions.back();
    assert(retI.operands[0].kind == Value::Kind::Temp && retI.operands[0].id == load0);
}

} // namespace

int main()
{
    std::cerr << "dse" << std::endl;
    testDSENoElimOnDisjointFields();
    std::cerr << "licm" << std::endl;
    testLICMLoadHoistWithDisjointStore();
    std::cerr << "gvn" << std::endl;
    testGVNRedundantLoadSameField();
    std::cerr << "done" << std::endl;
    return 0;
}
