// File: src/il/build/IRBuilder.cpp
// Purpose: Implements helpers to construct IL modules.
// Key invariants: None.
// Ownership/Lifetime: Builder references module owned externally.
// Links: docs/il-spec.md

#include "il/build/IRBuilder.hpp"
#include <cassert>

namespace il::build
{

IRBuilder::IRBuilder(Module &m) : mod(m) {}

Extern &IRBuilder::addExtern(const std::string &name, Type ret, const std::vector<Type> &params)
{
    mod.externs.push_back({name, ret, params});
    return mod.externs.back();
}

Global &IRBuilder::addGlobalStr(const std::string &name, const std::string &value)
{
    mod.globals.push_back({name, Type(Type::Kind::Str), value});
    return mod.globals.back();
}

Function &IRBuilder::startFunction(const std::string &name,
                                   Type ret,
                                   const std::vector<Param> &params)
{
    mod.functions.push_back({name, ret, {}, {}, {}});
    curFunc = &mod.functions.back();
    curBlock = nullptr;
    nextTemp = 0;
    for (auto p : params)
    {
        Param np = p;
        np.id = nextTemp++;
        curFunc->params.push_back(np);
    }
    curFunc->valueNames.resize(nextTemp);
    for (const auto &p : curFunc->params)
        curFunc->valueNames[p.id] = p.name;
    return *curFunc;
}

BasicBlock &IRBuilder::createBlock(Function &fn,
                                   const std::string &label,
                                   const std::vector<Param> &params)
{
    fn.blocks.push_back({label, {}, {}, false});
    BasicBlock &bb = fn.blocks.back();
    for (auto p : params)
    {
        Param np = p;
        np.id = nextTemp++;
        bb.params.push_back(np);
        if (fn.valueNames.size() <= np.id)
            fn.valueNames.resize(np.id + 1);
        fn.valueNames[np.id] = np.name;
    }
    return bb;
}

BasicBlock &IRBuilder::addBlock(Function &fn, const std::string &label)
{
    return createBlock(fn, label, {});
}

Value IRBuilder::blockParam(BasicBlock &bb, unsigned idx)
{
    assert(idx < bb.params.size());
    return Value::temp(bb.params[idx].id);
}

void IRBuilder::br(BasicBlock &dst, const std::vector<Value> &args)
{
    assert(args.size() == dst.params.size());
    Instr instr;
    instr.op = Opcode::Br;
    instr.type = Type(Type::Kind::Void);
    instr.labels.push_back(dst.label);
    instr.brArgs.push_back(args);
    append(std::move(instr));
}

void IRBuilder::cbr(Value cond,
                    BasicBlock &t,
                    const std::vector<Value> &targs,
                    BasicBlock &f,
                    const std::vector<Value> &fargs)
{
    assert(targs.size() == t.params.size());
    assert(fargs.size() == f.params.size());
    Instr instr;
    instr.op = Opcode::CBr;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(cond);
    instr.labels.push_back(t.label);
    instr.labels.push_back(f.label);
    instr.brArgs.push_back(targs);
    instr.brArgs.push_back(fargs);
    append(std::move(instr));
}

void IRBuilder::setInsertPoint(BasicBlock &bb)
{
    curBlock = &bb;
}

Instr &IRBuilder::append(Instr instr)
{
    assert(curBlock && "insert point not set");
    if (isTerminator(instr.op))
    {
        assert(!curBlock->terminated && "block already terminated");
        curBlock->terminated = true;
    }
    curBlock->instructions.push_back(std::move(instr));
    return curBlock->instructions.back();
}

bool IRBuilder::isTerminator(Opcode op) const
{
    return op == Opcode::Br || op == Opcode::CBr || op == Opcode::Ret || op == Opcode::Trap;
}

Value IRBuilder::emitConstStr(const std::string &globalName, il::support::SourceLoc loc)
{
    unsigned id = nextTemp++;
    Instr instr;
    instr.result = id;
    instr.op = Opcode::ConstStr;
    instr.type = Type(Type::Kind::Str);
    instr.operands.push_back(Value::global(globalName));
    instr.loc = loc;
    append(std::move(instr));
    return Value::temp(id);
}

void IRBuilder::emitCall(const std::string &callee,
                         const std::vector<Value> &args,
                         const std::optional<Value> &dst,
                         il::support::SourceLoc loc)
{
    Instr instr;
    instr.op = Opcode::Call;

    Type ret(Type::Kind::Void);
    for (const auto &fn : mod.functions)
        if (fn.name == callee)
        {
            ret = fn.retType;
            break;
        }
    if (ret.kind == Type::Kind::Void)
        for (const auto &ex : mod.externs)
            if (ex.name == callee)
            {
                ret = ex.retType;
                break;
            }
    instr.type = ret;
    instr.callee = callee;
    instr.operands = args;
    if (dst)
    {
        instr.result = dst->id;
        if (dst->id >= nextTemp)
        {
            nextTemp = dst->id + 1;
            if (curFunc->valueNames.size() <= dst->id)
                curFunc->valueNames.resize(dst->id + 1);
        }
    }
    instr.loc = loc;
    append(std::move(instr));
}

void IRBuilder::emitRet(const std::optional<Value> &v, il::support::SourceLoc loc)
{
    Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    if (v)
        instr.operands.push_back(*v);
    instr.loc = loc;
    append(std::move(instr));
}

} // namespace il::build
