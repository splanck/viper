// File: tests/unit/VM_TailCallTests.cpp
// Purpose: Verify tail-call optimisation maintains constant frame depth.
// Notes: Runs with VIPER_VM_TAILCALL=1 (set on target) and steps VM to measure depth.

#include "VMTestHook.hpp"
#include "il/build/IRBuilder.hpp"

#include <algorithm>
#include <cassert>

using namespace il::core;

static il::core::Module build_tail_fact_module()
{
    Module m;
    il::build::IRBuilder b(m);

    // fact(n: i64, acc: i64) -> i64
    Function &fact = b.startFunction(
        "fact",
        Type(Type::Kind::I64),
        {Param{"n", Type(Type::Kind::I64), 0}, Param{"acc", Type(Type::Kind::I64), 1}});
    // Create blocks first, then reacquire stable references
    b.createBlock(fact,
                  "entry",
                  {Param{"n", Type(Type::Kind::I64), 0}, Param{"acc", Type(Type::Kind::I64), 1}});
    b.createBlock(fact, "retb", {Param{"acc", Type(Type::Kind::I64), 0}});
    b.createBlock(fact,
                  "recb",
                  {Param{"n", Type(Type::Kind::I64), 0}, Param{"acc", Type(Type::Kind::I64), 1}});
    BasicBlock *entry = nullptr;
    BasicBlock *retb = nullptr;
    BasicBlock *recb = nullptr;
    for (auto &bb : fact.blocks)
    {
        if (bb.label == "entry")
            entry = &bb;
        else if (bb.label == "retb")
            retb = &bb;
        else if (bb.label == "recb")
            recb = &bb;
    }
    assert(entry && retb && recb);
    b.setInsertPoint(*entry);
    // if n == 0 then ret acc else goto recb
    Instr cmp;
    cmp.result = b.reserveTempId();
    cmp.op = Opcode::ICmpEq;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands.push_back(b.blockParam(*entry, 0));
    cmp.operands.push_back(Value::constInt(0));
    entry->instructions.push_back(cmp);
    b.cbr(Value::temp(*cmp.result),
          *retb,
          {b.blockParam(*entry, 1)},
          *recb,
          {b.blockParam(*entry, 0), b.blockParam(*entry, 1)});

    b.setInsertPoint(*retb);
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(b.blockParam(*retb, 0));
    retb->instructions.push_back(ret);
    retb->terminated = true;

    b.setInsertPoint(*recb);
    // acc2 = acc * n (use recb params)
    Instr mul;
    mul.result = b.reserveTempId();
    mul.op = Opcode::Mul;
    mul.type = Type(Type::Kind::I64);
    mul.operands.push_back(b.blockParam(*recb, 1));
    mul.operands.push_back(b.blockParam(*recb, 0));
    recb->instructions.push_back(mul);
    // n2 = n - 1
    Instr sub;
    sub.result = b.reserveTempId();
    sub.op = Opcode::Sub;
    sub.type = Type(Type::Kind::I64);
    sub.operands.push_back(b.blockParam(*recb, 0));
    sub.operands.push_back(Value::constInt(1));
    recb->instructions.push_back(sub);
    // call fact(n2, acc2)
    unsigned dst = b.reserveTempId();
    b.emitCall(
        "fact", {Value::temp(*sub.result), Value::temp(*mul.result)}, Value::temp(dst), {0, 1, 1});
    // ret (call result) — tail position
    Instr ret2;
    ret2.op = Opcode::Ret;
    ret2.type = Type(Type::Kind::Void);
    ret2.operands.push_back(Value::temp(dst));
    recb->instructions.push_back(ret2);
    recb->terminated = true;

    // main() -> i64: return fact(5, 1)
    Function &mainFn = b.startFunction("main", Type(Type::Kind::I64), {});
    BasicBlock &mb = b.addBlock(mainFn, "entry");
    b.setInsertPoint(mb);
    unsigned callDst = b.reserveTempId();
    b.emitCall("fact", {Value::constInt(5), Value::constInt(1)}, Value::temp(callDst), {0, 1, 1});
    b.emitRet(Value::temp(callDst), {0, 1, 1});
    return m;
}

static il::core::Module build_mutual_module()
{
    Module m;
    il::build::IRBuilder b(m);
    // Register both functions to allow mutual recursion
    b.startFunction("f",
                    Type(Type::Kind::I64),
                    {Param{"n", Type(Type::Kind::I64), 0}, Param{"acc", Type(Type::Kind::I64), 1}});
    b.startFunction("g",
                    Type(Type::Kind::I64),
                    {Param{"n", Type(Type::Kind::I64), 0}, Param{"acc", Type(Type::Kind::I64), 1}});
    // Reacquire references after potential vector reallocation
    Function *fRef = nullptr;
    Function *gRef = nullptr;
    for (auto &fnc : m.functions)
    {
        if (fnc.name == "f")
            fRef = &fnc;
        else if (fnc.name == "g")
            gRef = &fnc;
    }
    assert(fRef && gRef);
    Function &f = *fRef;
    Function &g = *gRef;
    b.createBlock(
        f, "entry", {Param{"n", Type(Type::Kind::I64), 0}, Param{"acc", Type(Type::Kind::I64), 1}});
    b.createBlock(f, "retb", {Param{"acc", Type(Type::Kind::I64), 0}});
    b.createBlock(
        f, "recb", {Param{"n", Type(Type::Kind::I64), 0}, Param{"acc", Type(Type::Kind::I64), 1}});
    BasicBlock *fe = nullptr;
    BasicBlock *fr = nullptr;
    BasicBlock *fn = nullptr;
    for (auto &bb : f.blocks)
    {
        if (bb.label == "entry")
            fe = &bb;
        else if (bb.label == "retb")
            fr = &bb;
        else if (bb.label == "recb")
            fn = &bb;
    }
    assert(fe && fr && fn);
    b.setInsertPoint(*fe);
    Instr fcmp;
    fcmp.result = b.reserveTempId();
    fcmp.op = Opcode::ICmpEq;
    fcmp.type = Type(Type::Kind::I1);
    fcmp.operands.push_back(b.blockParam(*fe, 0));
    fcmp.operands.push_back(Value::constInt(0));
    fe->instructions.push_back(fcmp);
    b.cbr(Value::temp(*fcmp.result),
          *fr,
          {b.blockParam(*fe, 1)},
          *fn,
          {b.blockParam(*fe, 0), b.blockParam(*fe, 1)});
    b.setInsertPoint(*fr);
    Instr fret;
    fret.op = Opcode::Ret;
    fret.type = Type(Type::Kind::Void);
    fret.operands.push_back(b.blockParam(*fr, 0));
    fr->instructions.push_back(fret);
    fr->terminated = true;
    b.setInsertPoint(*fn);
    Instr finc;
    finc.result = b.reserveTempId();
    finc.op = Opcode::Add;
    finc.type = Type(Type::Kind::I64);
    finc.operands.push_back(b.blockParam(*fn, 1));
    finc.operands.push_back(Value::constInt(1));
    fn->instructions.push_back(finc);
    Instr fdec;
    fdec.result = b.reserveTempId();
    fdec.op = Opcode::Sub;
    fdec.type = Type(Type::Kind::I64);
    fdec.operands.push_back(b.blockParam(*fn, 0));
    fdec.operands.push_back(Value::constInt(1));
    fn->instructions.push_back(fdec);
    unsigned fdst = b.reserveTempId();
    b.emitCall(
        "g", {Value::temp(*fdec.result), Value::temp(*finc.result)}, Value::temp(fdst), {0, 1, 1});
    Instr fret2;
    fret2.op = Opcode::Ret;
    fret2.type = Type(Type::Kind::Void);
    fret2.operands.push_back(Value::temp(fdst));
    fn->instructions.push_back(fret2);
    fn->terminated = true;

    // g mirrors f (build body now that both are registered)
    b.createBlock(
        g, "entry", {Param{"n", Type(Type::Kind::I64), 0}, Param{"acc", Type(Type::Kind::I64), 1}});
    b.createBlock(g, "retb", {Param{"acc", Type(Type::Kind::I64), 0}});
    b.createBlock(
        g, "recb", {Param{"n", Type(Type::Kind::I64), 0}, Param{"acc", Type(Type::Kind::I64), 1}});
    BasicBlock *ge = nullptr;
    BasicBlock *gr = nullptr;
    BasicBlock *gn = nullptr;
    for (auto &bb : g.blocks)
    {
        if (bb.label == "entry")
            ge = &bb;
        else if (bb.label == "retb")
            gr = &bb;
        else if (bb.label == "recb")
            gn = &bb;
    }
    assert(ge && gr && gn);
    b.setInsertPoint(*ge);
    Instr gcmp;
    gcmp.result = b.reserveTempId();
    gcmp.op = Opcode::ICmpEq;
    gcmp.type = Type(Type::Kind::I1);
    gcmp.operands.push_back(b.blockParam(*ge, 0));
    gcmp.operands.push_back(Value::constInt(0));
    ge->instructions.push_back(gcmp);
    b.cbr(Value::temp(*gcmp.result),
          *gr,
          {b.blockParam(*ge, 1)},
          *gn,
          {b.blockParam(*ge, 0), b.blockParam(*ge, 1)});
    b.setInsertPoint(*gr);
    Instr gret;
    gret.op = Opcode::Ret;
    gret.type = Type(Type::Kind::Void);
    gret.operands.push_back(b.blockParam(*gr, 0));
    gr->instructions.push_back(gret);
    gr->terminated = true;
    b.setInsertPoint(*gn);
    Instr ginc;
    ginc.result = b.reserveTempId();
    ginc.op = Opcode::Add;
    ginc.type = Type(Type::Kind::I64);
    ginc.operands.push_back(b.blockParam(*gn, 1));
    ginc.operands.push_back(Value::constInt(1));
    gn->instructions.push_back(ginc);
    Instr gdec;
    gdec.result = b.reserveTempId();
    gdec.op = Opcode::Sub;
    gdec.type = Type(Type::Kind::I64);
    gdec.operands.push_back(b.blockParam(*gn, 0));
    gdec.operands.push_back(Value::constInt(1));
    gn->instructions.push_back(gdec);
    unsigned gdst = b.reserveTempId();
    b.emitCall(
        "f", {Value::temp(*gdec.result), Value::temp(*ginc.result)}, Value::temp(gdst), {0, 1, 1});
    Instr gret2;
    gret2.op = Opcode::Ret;
    gret2.type = Type(Type::Kind::Void);
    gret2.operands.push_back(Value::temp(gdst));
    gn->instructions.push_back(gret2);
    gn->terminated = true;

    // main: return f(1000, 0)
    Function &mainFn = b.startFunction("main", Type(Type::Kind::I64), {});
    BasicBlock &mb = b.addBlock(mainFn, "entry");
    b.setInsertPoint(mb);
    unsigned cd = b.reserveTempId();
    b.emitCall("f", {Value::constInt(1000), Value::constInt(0)}, Value::temp(cd), {0, 1, 1});
    b.emitRet(Value::temp(cd), {0, 1, 1});
    return m;
}

int main()
{
    std::fprintf(stderr, "[TEST] VM_TailCallTests start\n");
    std::fflush(stderr);
    // Tail-recursive factorial should keep depth constant at 1
    {
        std::fprintf(stderr, "[TEST] build tail_fact module\n");
        std::fflush(stderr);
        Module m = build_tail_fact_module();
        std::fprintf(stderr, "[TEST] module built\n");
        std::fflush(stderr);
        il::vm::VM vm(m);
        std::fprintf(stderr, "[TEST] VM constructed\n");
        std::fflush(stderr);
        auto it = std::find_if(m.functions.begin(),
                               m.functions.end(),
                               [](const Function &f) { return f.name == "main"; });
        assert(it != m.functions.end());
        std::fprintf(stderr, "[TEST] found main\n");
        std::fflush(stderr);
        auto state = il::vm::VMTestHook::prepare(vm, *it);
        std::fprintf(stderr, "[TEST] prepared state\n");
        std::fflush(stderr);
        std::size_t maxDepth = 0;
        while (true)
        {
            maxDepth = std::max(maxDepth, il::vm::VMTestHook::execDepth(vm));
            auto res = il::vm::VMTestHook::step(vm, state);
            if (res)
            {
                assert(res->i64 == 120);
                break;
            }
        }
        assert(maxDepth <= 1);
    }

    // Mutual recursion f<->g should also keep depth constant at 1
    {
        std::fprintf(stderr, "[TEST] build mutual module\n");
        std::fflush(stderr);
        Module m = build_mutual_module();
        std::fprintf(stderr, "[TEST] mutual module built\n");
        std::fflush(stderr);
        il::vm::VM vm(m);
        auto it = std::find_if(m.functions.begin(),
                               m.functions.end(),
                               [](const Function &f) { return f.name == "main"; });
        assert(it != m.functions.end());
        auto state = il::vm::VMTestHook::prepare(vm, *it);
        std::size_t maxDepth = 0;
        while (true)
        {
            maxDepth = std::max(maxDepth, il::vm::VMTestHook::execDepth(vm));
            auto res = il::vm::VMTestHook::step(vm, state);
            if (res)
            {
                assert(res->i64 == 1000);
                break;
            }
        }
        assert(maxDepth <= 1);
    }

    return 0;
}
