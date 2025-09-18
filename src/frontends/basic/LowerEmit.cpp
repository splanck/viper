// File: src/frontends/basic/LowerEmit.cpp
// Purpose: Implements IR emission helpers and program emission for BASIC lowering.
// Key invariants: Block labels are deterministic via BlockNamer or mangler.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

// Purpose: emit program.
// Parameters: const Program &prog.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::emitProgram(const Program &prog)
{
    build::IRBuilder &b = *builder;

    std::vector<const Stmt *> mainStmts;
    for (const auto &s : prog.procs)
    {
        if (auto *fn = dynamic_cast<const FunctionDecl *>(s.get()))
            lowerFunctionDecl(*fn);
        else if (auto *sub = dynamic_cast<const SubDecl *>(s.get()))
            lowerSubDecl(*sub);
    }
    for (const auto &s : prog.main)
        mainStmts.push_back(s.get());

    lineBlocks.clear();

    Function &f = b.startFunction("main", Type(Type::Kind::I64), {});
    func = &f;

    b.addBlock(f, "entry");

    std::vector<int> lines;
    lines.reserve(mainStmts.size());
    for (const auto *stmt : mainStmts)
    {
        b.addBlock(f, mangler.block("L" + std::to_string(stmt->line)));
        lines.push_back(stmt->line);
    }
    fnExit = f.blocks.size();
    b.addBlock(f, mangler.block("exit"));

    for (size_t i = 0; i < lines.size(); ++i)
        lineBlocks[lines[i]] = i + 1;

    vars.clear();
    arrays.clear();
    varTypes.clear();
    collectVars(mainStmts);

    // allocate slots in entry
    BasicBlock *entry = &f.blocks.front();
    cur = entry;
    for (const auto &v : vars)
    {
        curLoc = {};
        if (arrays.count(v))
        {
            Value slot = emitAlloca(8);
            varSlots[v] = slot.id; // Value::temp id
            continue;
        }
        bool isBoolVar = false;
        auto itType = varTypes.find(v);
        if (itType != varTypes.end() && itType->second == AstType::Bool)
            isBoolVar = true;
        Value slot = emitAlloca(isBoolVar ? 1 : 8);
        varSlots[v] = slot.id; // Value::temp id
        if (isBoolVar)
            emitStore(ilBoolTy(), slot, emitBoolConst(false));
    }
    if (boundsChecks)
    {
        for (const auto &a : arrays)
        {
            curLoc = {};
            Value slot = emitAlloca(8);
            arrayLenSlots[a] = slot.id;
        }
    }
    if (!mainStmts.empty())
    {
        curLoc = {};
        emitBr(&f.blocks[lineBlocks[mainStmts.front()->line]]);
    }
    else
    {
        curLoc = {};
        emitRet(Value::constInt(0));
    }

    // lower statements sequentially
    for (size_t i = 0; i < mainStmts.size(); ++i)
    {
        cur = &f.blocks[lineBlocks[mainStmts[i]->line]];
        lowerStmt(*mainStmts[i]);
        if (!cur->terminated)
        {
            BasicBlock *next = (i + 1 < mainStmts.size())
                                   ? &f.blocks[lineBlocks[mainStmts[i + 1]->line]]
                                   : &f.blocks[fnExit];
            curLoc = mainStmts[i]->loc;
            emitBr(next);
        }
    }

    cur = &f.blocks[fnExit];
    curLoc = {};
    emitRet(Value::constInt(0));
}

// Purpose: retrieve IL boolean type.
// Parameters: none.
// Returns: IlType representing i1.
// Side effects: none.
Lowerer::IlType Lowerer::ilBoolTy()
{
    return Type(Type::Kind::I1);
}

// Purpose: emit a boolean constant.
// Parameters: bool v.
// Returns: IlValue for IL i1 constant (0 or 1).
// Side effects: none.
Lowerer::IlValue Lowerer::emitBoolConst(bool v)
{
    return Value::constInt(v ? 1 : 0);
}

Lowerer::IlValue Lowerer::emitBoolFromBranches(std::function<void()> emitThen,
                                               std::function<void()> emitElse)
{
    Value slot = emitAlloca(1);
    if (boolBranchSlotPtr)
        *boolBranchSlotPtr = slot;

    std::string thenLbl = blockNamer ? blockNamer->generic("bool_then")
                                     : mangler.block("bool_then");
    std::string elseLbl = blockNamer ? blockNamer->generic("bool_else")
                                     : mangler.block("bool_else");
    std::string joinLbl = blockNamer ? blockNamer->generic("bool_join")
                                     : mangler.block("bool_join");

    BasicBlock *thenBlk = &builder->addBlock(*func, thenLbl);
    BasicBlock *elseBlk = &builder->addBlock(*func, elseLbl);
    BasicBlock *joinBlk = &builder->addBlock(*func, joinLbl);

    cur = thenBlk;
    emitThen();
    if (!cur->terminated)
        emitBr(joinBlk);

    cur = elseBlk;
    emitElse();
    if (!cur->terminated)
        emitBr(joinBlk);

    cur = joinBlk;
    return emitLoad(ilBoolTy(), slot);
}

// Purpose: lower array addr.
// Parameters: const ArrayExpr &expr.
// Returns: Value.
// Side effects: may modify lowering state or emit IL.
Value Lowerer::lowerArrayAddr(const ArrayExpr &expr)
{
    auto it = varSlots.find(expr.name);
    assert(it != varSlots.end());
    Value slot = Value::temp(it->second);
    Value base = emitLoad(Type(Type::Kind::Ptr), slot);
    RVal idx = lowerExpr(*expr.index);
    curLoc = expr.loc;
    if (boundsChecks)
    {
        auto lenIt = arrayLenSlots.find(expr.name);
        assert(lenIt != arrayLenSlots.end());
        Value len = emitLoad(Type(Type::Kind::I64), Value::temp(lenIt->second));
        Value neg = emitBinary(Opcode::SCmpLT, ilBoolTy(), idx.value, Value::constInt(0));
        Value ge = emitBinary(Opcode::SCmpGE, ilBoolTy(), idx.value, len);
        Value neg64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), neg);
        Value ge64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), ge);
        Value or64 = emitBinary(Opcode::Or, Type(Type::Kind::I64), neg64, ge64);
        Value cond = emitUnary(Opcode::Trunc1, ilBoolTy(), or64);
        size_t curIdx = static_cast<size_t>(cur - &func->blocks[0]);
        size_t okIdx = func->blocks.size();
        std::string okLbl = blockNamer ? blockNamer->tag("bc_ok" + std::to_string(boundsCheckId))
                                       : mangler.block("bc_ok" + std::to_string(boundsCheckId));
        builder->addBlock(*func, okLbl);
        size_t failIdx = func->blocks.size();
        std::string failLbl = blockNamer
                                  ? blockNamer->tag("bc_fail" + std::to_string(boundsCheckId))
                                  : mangler.block("bc_fail" + std::to_string(boundsCheckId));
        builder->addBlock(*func, failLbl);
        BasicBlock *ok = &func->blocks[okIdx];
        BasicBlock *fail = &func->blocks[failIdx];
        cur = &func->blocks[curIdx];
        ++boundsCheckId;
        emitCBr(cond, fail, ok);
        cur = fail;
        std::string msg = "bounds check failed: " + expr.name + "[i]";
        Value s = emitConstStr(getStringLabel(msg));
        emitCall("rt_trap", {s});
        emitTrap();
        cur = ok;
    }
    Value off = emitBinary(Opcode::Shl, Type(Type::Kind::I64), idx.value, Value::constInt(3));
    Value ptr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), base, off);
    return ptr;
}

// Purpose: emit alloca.
// Parameters: int bytes.
// Returns: Value.
// Side effects: may modify lowering state or emit IL.
Value Lowerer::emitAlloca(int bytes)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Alloca;
    in.type = Type(Type::Kind::Ptr);
    in.operands.push_back(Value::constInt(bytes));
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

// Purpose: emit load.
// Parameters: Type ty, Value addr.
// Returns: Value.
// Side effects: may modify lowering state or emit IL.
Value Lowerer::emitLoad(Type ty, Value addr)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Load;
    in.type = ty;
    in.operands.push_back(addr);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

// Purpose: emit store.
// Parameters: Type ty, Value addr, Value val.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::emitStore(Type ty, Value addr, Value val)
{
    Instr in;
    in.op = Opcode::Store;
    in.type = ty;
    in.operands = {addr, val};
    in.loc = curLoc;
    cur->instructions.push_back(in);
}

// Purpose: emit for step.
// Parameters: Value slot, Value step.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::emitForStep(Value slot, Value step)
{
    Value load = emitLoad(Type(Type::Kind::I64), slot);
    Value add = emitBinary(Opcode::Add, Type(Type::Kind::I64), load, step);
    emitStore(Type(Type::Kind::I64), slot, add);
}

// Purpose: emit binary.
// Parameters: Opcode op, Type ty, Value lhs, Value rhs.
// Returns: Value.
// Side effects: may modify lowering state or emit IL.
Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {lhs, rhs};
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

// Purpose: emit unary.
// Parameters: Opcode op, Type ty, Value val.
// Returns: Value.
// Side effects: may modify lowering state or emit IL.
Value Lowerer::emitUnary(Opcode op, Type ty, Value val)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {val};
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

// Purpose: emit br.
// Parameters: BasicBlock *target.
// Returns: void.
// Side effects: may modify lowering state or emit IL. Relies on deterministic block naming via
// BlockNamer.
void Lowerer::emitBr(BasicBlock *target)
{
    Instr in;
    in.op = Opcode::Br;
    in.type = Type(Type::Kind::Void);
    in.labels.push_back(target->label);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

// Purpose: emit cbr.
// Parameters: Value cond, BasicBlock *t, BasicBlock *f.
// Returns: void.
// Side effects: may modify lowering state or emit IL. Relies on deterministic block naming via
// BlockNamer.
void Lowerer::emitCBr(Value cond, BasicBlock *t, BasicBlock *f)
{
    Instr in;
    in.op = Opcode::CBr;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(cond);
    in.labels.push_back(t->label);
    in.labels.push_back(f->label);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

// Purpose: emit call.
// Parameters: const std::string &callee, const std::vector<Value> &args.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    Instr in;
    in.op = Opcode::Call;
    in.type = Type(Type::Kind::Void);
    in.callee = callee;
    in.operands = args;
    in.loc = curLoc;
    cur->instructions.push_back(in);
}

// Purpose: emit call ret.
// Parameters: Type ty, const std::string &callee, const std::vector<Value> &args.
// Returns: Value.
// Side effects: may modify lowering state or emit IL.
Value Lowerer::emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Call;
    in.type = ty;
    in.callee = callee;
    in.operands = args;
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

// Purpose: emit const str.
// Parameters: const std::string &globalName.
// Returns: Value.
// Side effects: may modify lowering state or emit IL.
Value Lowerer::emitConstStr(const std::string &globalName)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::ConstStr;
    in.type = Type(Type::Kind::Str);
    in.operands.push_back(Value::global(globalName));
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

// Purpose: emit ret.
// Parameters: Value v.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::emitRet(Value v)
{
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(v);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

// Purpose: emit ret void.
// Parameters: none.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::emitRetVoid()
{
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

// Purpose: emit trap.
// Parameters: none.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::emitTrap()
{
    Instr in;
    in.op = Opcode::Trap;
    in.type = Type(Type::Kind::Void);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

// Purpose: get string label.
// Parameters: const std::string &s.
// Returns: std::string.
// Side effects: may modify lowering state or emit IL.
std::string Lowerer::getStringLabel(const std::string &s)
{
    auto it = strings.find(s);
    if (it != strings.end())
        return it->second;
    std::string name = ".L" + std::to_string(strings.size());
    builder->addGlobalStr(name, s);
    strings[s] = name;
    return name;
}

// Purpose: next temp id.
// Parameters: none.
// Returns: unsigned.
// Side effects: may modify lowering state or emit IL.
unsigned Lowerer::nextTempId()
{
    std::string name = mangler.nextTemp();
    return static_cast<unsigned>(std::stoul(name.substr(2)));
}

} // namespace il::frontends::basic
