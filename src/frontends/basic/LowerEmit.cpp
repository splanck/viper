// File: src/frontends/basic/LowerEmit.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements IR emission helpers and program emission for BASIC lowering.
// Key invariants: Block labels are deterministic via BlockNamer or mangler.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <unordered_set>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Emit the IR entry point for a BASIC program.
/// @param prog Parsed program containing the main statements and nested declarations.
/// @details The shared IR @c builder creates the @c main function, adds explicit entry and
/// exit blocks, and establishes deterministic line blocks so @c lineBlocks maps statement line
/// numbers to block indices. The entry block becomes @c cur to ensure stack allocations for
/// scalars, arrays, and bookkeeping temporaries are emitted before control flow jumps to the
/// first numbered statement. Control flow either branches from the entry block to the first
/// numbered block or returns immediately when the program body is empty, and each lowered line
/// emits an explicit branch to the subsequent block or the synthetic exit recorded in @c fnExit.
void Lowerer::emitProgram(const Program &prog)
{
    build::IRBuilder &b = *builder;

    std::vector<const Stmt *> mainStmts;
    collectProcedureSignatures(prog);
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
    nextTemp = func->valueNames.size();

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
    allocateLocalSlots(std::unordered_set<std::string>(), /*includeParams=*/true);

    if (mainStmts.empty())
    {
        curLoc = {};
        emitRet(Value::constInt(0));
    }
    else
    {
        lowerStatementSequence(
            mainStmts,
            /*stopOnTerminated=*/false,
            [&](const Stmt &stmt) { curLoc = stmt.loc; });
    }

    cur = &f.blocks[fnExit];
    curLoc = {};
    emitRet(Value::constInt(0));
}

/// @brief Return the canonical IL boolean type used by the BASIC front end.
/// @return A 1-bit integral type produced once per call.
Lowerer::IlType Lowerer::ilBoolTy()
{
    return Type(Type::Kind::I1);
}

/// @brief Materialise an IL boolean constant in the current block.
/// @param v Host boolean that should appear in the IL stream.
/// @return Temporary representing the truncated constant.
/// @details Values are produced by truncating a 64-bit literal through @ref emitUnary while
/// respecting the current block referenced by @c cur.
Lowerer::IlValue Lowerer::emitBoolConst(bool v)
{
    return emitUnary(Opcode::Trunc1, ilBoolTy(), Value::constInt(v ? 1 : 0));
}

/// @brief Build a boolean by merging results from synthetic then/else blocks.
/// @param emitThen Callback that stores the truthy value to the provided slot within the
/// then block.
/// @param emitElse Callback that stores the falsy value to the provided slot within the
/// else block.
/// @param thenLabelBase Hint for naming the then block.
/// @param elseLabelBase Hint for naming the else block.
/// @param joinLabelBase Hint for naming the join block.
/// @return A temporary containing the loaded boolean once control reaches the join block.
/// @details A 1-byte stack slot is reserved via @ref emitAlloca while @c cur references the
/// predecessor block. New blocks are requested from @c builder and are named using
/// @c blockNamer when available (otherwise falling back to @c mangler). Each branch callback is
/// executed after @c cur is rebound to the corresponding block, and non-terminating callbacks
/// fall through by emitting a branch to the join block. Finally, @c cur is positioned on the
/// join block and the stored predicate is reloaded.
Lowerer::IlValue Lowerer::emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                               const std::function<void(Value)> &emitElse,
                                               std::string_view thenLabelBase,
                                               std::string_view elseLabelBase,
                                               std::string_view joinLabelBase)
{
    Value slot = emitAlloca(1);

    auto labelFor = [&](std::string_view base) {
        std::string hint(base);
        return blockNamer ? blockNamer->generic(hint) : mangler.block(hint);
    };

    std::string thenLbl = labelFor(thenLabelBase);
    std::string elseLbl = labelFor(elseLabelBase);
    std::string joinLbl = labelFor(joinLabelBase);

    BasicBlock *thenBlk = &builder->addBlock(*func, thenLbl);
    BasicBlock *elseBlk = &builder->addBlock(*func, elseLbl);
    BasicBlock *joinBlk = &builder->addBlock(*func, joinLbl);

    cur = thenBlk;
    emitThen(slot);
    if (!cur->terminated)
        emitBr(joinBlk);

    cur = elseBlk;
    emitElse(slot);
    if (!cur->terminated)
        emitBr(joinBlk);

    cur = joinBlk;
    return emitLoad(ilBoolTy(), slot);
}

/// @brief Lower the address of a BASIC array element, inserting bounds checks if enabled.
/// @param expr Array access expression referencing a declared BASIC array.
/// @return Address pointing at the computed element.
/// @details The base pointer is recovered from @c varSlots and arithmetic is emitted in the
/// current block identified by @c cur. When bounds checking is active, additional ok/fail blocks
/// are created through @c builder and named with @c blockNamer (falling back to @c mangler) so the
/// failing path can trap via the runtime helper before control resumes at the success block.
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

/// @brief Allocate stack storage within the current block.
/// @param bytes Size of the stack slot in bytes.
/// @return Temporary pointing to the allocated slot.
/// @details Appends an @c alloca instruction to @c cur using @ref nextTempId to keep temporary
/// identifiers in sync with the @c builder's notion of value numbering.
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

/// @brief Load a value from memory in the current block.
/// @param ty Result type of the load.
/// @param addr Address to load from.
/// @return Temporary holding the loaded value.
/// @details Inserts a @c load instruction into @c cur. The caller is responsible for ensuring the
/// pointer was created via this lowerer and thus agrees with the @c builder's layout.
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

/// @brief Store a value to memory in the current block.
/// @param ty Element type recorded on the store.
/// @param addr Address receiving the value.
/// @param val Value to be written.
/// @details Appends a @c store instruction to @c cur without affecting termination state so the
/// caller may continue emitting instructions in the same block.
void Lowerer::emitStore(Type ty, Value addr, Value val)
{
    Instr in;
    in.op = Opcode::Store;
    in.type = ty;
    in.operands = {addr, val};
    in.loc = curLoc;
    cur->instructions.push_back(in);
}

/// @brief Advance a FOR-loop induction variable by a step amount.
/// @param slot Stack slot containing the induction variable.
/// @param step Value representing the step.
/// @details Uses @ref emitLoad, @ref emitBinary, and @ref emitStore while @c cur references the
/// loop body block, keeping the mutation localized to the current control-flow context.
void Lowerer::emitForStep(Value slot, Value step)
{
    Value load = emitLoad(Type(Type::Kind::I64), slot);
    Value add = emitBinary(Opcode::Add, Type(Type::Kind::I64), load, step);
    emitStore(Type(Type::Kind::I64), slot, add);
}

/// @brief Emit a binary instruction in the current block.
/// @param op Opcode to insert.
/// @param ty Result type.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Temporary containing the result of the instruction.
/// @details The instruction is appended to @c cur and consumes the next available temporary id via
/// @ref nextTempId so the surrounding builder state remains coherent.
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

/// @brief Emit a unary instruction in the current block.
/// @param op Opcode to insert.
/// @param ty Result type.
/// @param val Operand value.
/// @return Temporary containing the result of the instruction.
/// @details Behaviour mirrors @ref emitBinary but records a single operand, again appending the
/// instruction to the block referenced by @c cur.
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

/// @brief Emit an unconditional branch to the target block.
/// @param target Destination block that must already exist in the enclosing function.
/// @details Appends a terminator to the block referenced by @c cur and marks it as such. The
/// branch records the label previously minted by @c builder (or via @c blockNamer) ensuring
/// deterministic control-flow stitching.
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

/// @brief Emit a conditional branch in the current block.
/// @param cond Predicate controlling which successor is taken.
/// @param t Block to execute when @p cond is true.
/// @param f Block to execute when @p cond is false.
/// @details Encodes successor labels supplied by @c blockNamer/@c builder, appends the
/// instruction to @c cur, and marks the block as terminated.
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

/// @brief Emit a call with no returned value.
/// @param callee Symbolic callee name.
/// @param args Operand list passed to the call.
/// @details Appends a void call to @c cur while preserving the builder's notion of call
/// side-effects.
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

/// @brief Emit a call returning a value.
/// @param ty Result type recorded for the call.
/// @param callee Symbolic callee name.
/// @param args Operand list passed to the call.
/// @return Temporary receiving the call result.
/// @details Reserves a new temporary via @ref nextTempId and appends a call instruction to @c cur.
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

/// @brief Load the address of a string literal global.
/// @param globalName Label assigned to the string literal.
/// @return Temporary referring to the string address.
/// @details Adds a @c conststr instruction to @c cur. The literal must have been registered with
/// @ref getStringLabel so the @c builder has emitted the backing global.
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

/// @brief Emit a return carrying a value.
/// @param v Value to return to the caller.
/// @details Appends a @c ret instruction to @c cur, records the operand, and marks the block as
/// terminated so no further instructions are added accidentally.
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

/// @brief Emit a void return terminator in the current block.
/// @details Mirrors @ref emitRet but without an operand; @c cur becomes terminated afterwards.
void Lowerer::emitRetVoid()
{
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

/// @brief Emit a trap terminator in the current block.
/// @details Used by bounds checks and runtime helpers. After insertion @c cur is marked
/// terminated, preventing further instructions from being appended.
void Lowerer::emitTrap()
{
    Instr in;
    in.op = Opcode::Trap;
    in.type = Type(Type::Kind::Void);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

/// @brief Retrieve or create the global label for a string literal.
/// @param s Literal contents.
/// @return Stable label used to refer to the literal.
/// @details Caches previously generated labels in @c strings and requests @c builder to emit the
/// global if the literal is first seen.
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

/// @brief Acquire the next temporary identifier compatible with the builder's numbering.
/// @return Unsigned identifier matching the shared builder counter.
/// @details Requests the next id from @c builder so manual instruction emission stays in sync
/// with the builder-managed temporary sequence. The owning function's @c valueNames vector is
/// extended to keep VM register sizing correct and receives a default "%tN" placeholder when no
/// explicit debug name exists for the id.
unsigned Lowerer::nextTempId()
{
    unsigned id = builder ? builder->reserveTempId() : nextTemp++;
    if (func)
    {
        if (func->valueNames.size() <= id)
            func->valueNames.resize(id + 1);
        if (func->valueNames[id].empty())
            func->valueNames[id] = "%t" + std::to_string(id);
    }
    if (nextTemp <= id)
        nextTemp = id + 1;
    return id;
}

} // namespace il::frontends::basic
