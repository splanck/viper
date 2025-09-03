#include "analysis/CFG.hpp"
#include "analysis/Dominators.hpp"
#include "il/utils/Utils.hpp"
#include <cassert>

using namespace il;

static core::Instr makeBr(const std::string &dst)
{
    core::Instr i;
    i.op = core::Opcode::Br;
    i.labels = {dst};
    return i;
}

static core::Instr makeCBr(const std::string &t, const std::string &f)
{
    core::Instr i;
    i.op = core::Opcode::CBr;
    i.operands = {core::Value::temp(0)};
    i.labels = {t, f};
    return i;
}

static core::Instr makeRet()
{
    core::Instr i;
    i.op = core::Opcode::Ret;
    return i;
}

int main()
{
    // Diamond CFG
    core::Function f;
    core::BasicBlock a;
    a.label = "A";
    a.instructions = {makeCBr("B", "C")};
    a.terminated = true;
    core::BasicBlock b;
    b.label = "B";
    b.instructions = {makeBr("D")};
    b.terminated = true;
    core::BasicBlock c;
    c.label = "C";
    c.instructions = {makeBr("D")};
    c.terminated = true;
    core::BasicBlock d;
    d.label = "D";
    d.instructions = {makeRet()};
    d.terminated = true;
    f.blocks = {a, b, c, d};

    analysis::CFG cfg(f);
    assert(cfg.successors(&f.blocks[0]).size() == 2);
    assert(cfg.predecessors(&f.blocks[3]).size() == 2);
    assert(cfg.postorder().size() == 4);

    analysis::DominatorTree dom(cfg);
    const auto *A = &f.blocks[0];
    const auto *B = &f.blocks[1];
    const auto *C = &f.blocks[2];
    const auto *D = &f.blocks[3];
    assert(dom.dominates(A, B));
    assert(dom.dominates(A, C));
    assert(dom.dominates(A, D));
    assert(dom.idom(B) == A);
    assert(dom.idom(C) == A);
    assert(dom.idom(D) == A);
    const auto &instrB = f.blocks[1].instructions[0];
    assert(utils::isInstrInBlock(instrB, f.blocks[1]));
    assert(!utils::isInstrInBlock(instrB, f.blocks[2]));

    // Loop CFG
    core::Function lf;
    core::BasicBlock e;
    e.label = "E";
    e.instructions = {makeBr("H")};
    e.terminated = true;
    core::BasicBlock h;
    h.label = "H";
    h.instructions = {makeCBr("B", "X")};
    h.terminated = true;
    core::BasicBlock body;
    body.label = "B";
    body.instructions = {makeBr("H")};
    body.terminated = true;
    core::BasicBlock exit;
    exit.label = "X";
    exit.instructions = {makeRet()};
    exit.terminated = true;
    lf.blocks = {e, h, body, exit};

    analysis::CFG cfg2(lf);
    analysis::DominatorTree dom2(cfg2);
    const auto *E = &lf.blocks[0];
    const auto *H = &lf.blocks[1];
    const auto *B2 = &lf.blocks[2];
    const auto *X = &lf.blocks[3];
    assert(dom2.dominates(E, H));
    assert(dom2.dominates(E, B2));
    assert(dom2.dominates(E, X));
    assert(dom2.dominates(H, B2));
    assert(dom2.dominates(H, X));
    assert(dom2.idom(H) == E);
    assert(dom2.idom(B2) == H);
    assert(dom2.idom(X) == H);

    return 0;
}
