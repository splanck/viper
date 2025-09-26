// File: src/frontends/basic/LowerEmit.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements IR emission helpers and program emission for BASIC lowering.
// Key invariants: Block labels are deterministic via BlockNamer or mangler.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

// Requires the consolidated Lowerer interface for emission helpers.
#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <unordered_set>
#include <utility>

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
    ProcedureContext &ctx = context();

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

    ctx.lineBlocks().clear();

    Function &f = b.startFunction("main", Type(Type::Kind::I64), {});
    ctx.setFunction(&f);
    ctx.setNextTemp(f.valueNames.size());

    b.addBlock(f, "entry");

    std::vector<int> lines;
    lines.reserve(mainStmts.size());
    for (const auto *stmt : mainStmts)
    {
        b.addBlock(f, mangler.block("L" + std::to_string(stmt->line)));
        lines.push_back(stmt->line);
    }
    ctx.setExitIndex(f.blocks.size());
    b.addBlock(f, mangler.block("exit"));

    for (size_t i = 0; i < lines.size(); ++i)
        ctx.lineBlocks()[lines[i]] = i + 1;

    resetSymbolState();
    collectVars(mainStmts);

    // allocate slots in entry
    BasicBlock *entry = &f.blocks.front();
    ctx.setCurrent(entry);
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

    ctx.setCurrent(&f.blocks[ctx.exitIndex()]);
    curLoc = {};
    releaseArrayLocals(std::unordered_set<std::string>{});
    releaseArrayParams(std::unordered_set<std::string>{});
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
    ProcedureContext &ctx = context();
    Value slot = emitAlloca(1);

    auto labelFor = [&](std::string_view base) {
        std::string hint(base);
        if (BlockNamer *blockNamer = ctx.blockNamer())
            return blockNamer->generic(hint);
        return mangler.block(hint);
    };

    std::string thenLbl = labelFor(thenLabelBase);
    std::string elseLbl = labelFor(elseLabelBase);
    std::string joinLbl = labelFor(joinLabelBase);

    Function *func = ctx.function();
    assert(func && "emitBoolFromBranches requires an active function");
    BasicBlock *thenBlk = &builder->addBlock(*func, thenLbl);
    BasicBlock *elseBlk = &builder->addBlock(*func, elseLbl);
    BasicBlock *joinBlk = &builder->addBlock(*func, joinLbl);

    ctx.setCurrent(thenBlk);
    emitThen(slot);
    if (ctx.current() && !ctx.current()->terminated)
        emitBr(joinBlk);

    ctx.setCurrent(elseBlk);
    emitElse(slot);
    if (ctx.current() && !ctx.current()->terminated)
        emitBr(joinBlk);

    ctx.setCurrent(joinBlk);
    return emitLoad(ilBoolTy(), slot);
}

/// @brief Lower the address of a BASIC array element, inserting bounds checks if enabled.
/// @param expr Array access expression referencing a declared BASIC array.
/// @return Address pointing at the computed element.
/// @details The base pointer is recovered from the symbol table and arithmetic is emitted in the
/// current block identified by @c cur. When bounds checking is active, additional ok/fail blocks
/// are created through @c builder and named with @c blockNamer (falling back to @c mangler) so the
/// failing path can trap via the runtime helper before control resumes at the success block.
Lowerer::ArrayAccess Lowerer::lowerArrayAccess(const ArrayExpr &expr)
{
    ProcedureContext &ctx = context();
    const auto *info = findSymbol(expr.name);
    assert(info && info->slotId);
    Value slot = Value::temp(*info->slotId);
    Value base = emitLoad(Type(Type::Kind::Ptr), slot);
    RVal idx = lowerExpr(*expr.index);
    idx = coerceToI64(std::move(idx), expr.loc);
    Value index = idx.value;
    curLoc = expr.loc;

    Value len = emitCallRet(Type(Type::Kind::I64), "rt_arr_i32_len", {base});
    Value neg = emitBinary(Opcode::SCmpLT, ilBoolTy(), index, Value::constInt(0));
    Value ge = emitBinary(Opcode::SCmpGE, ilBoolTy(), index, len);
    Value neg64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), neg);
    Value ge64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), ge);
    Value failSum = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), neg64, ge64);
    Value cond = emitBinary(Opcode::SCmpGT, ilBoolTy(), failSum, Value::constInt(0));

    Function *func = ctx.function();
    assert(func && ctx.current());
    size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
    unsigned bcId = ctx.consumeBoundsCheckId();
    BlockNamer *blockNamer = ctx.blockNamer();
    size_t okIdx = func->blocks.size();
    std::string okLbl = blockNamer ? blockNamer->tag("bc_ok" + std::to_string(bcId))
                                   : mangler.block("bc_ok" + std::to_string(bcId));
    builder->addBlock(*func, okLbl);
    size_t oobIdx = func->blocks.size();
    std::string oobLbl = blockNamer ? blockNamer->tag("bc_oob" + std::to_string(bcId))
                                    : mangler.block("bc_oob" + std::to_string(bcId));
    builder->addBlock(*func, oobLbl);
    BasicBlock *ok = &func->blocks[okIdx];
    BasicBlock *oob = &func->blocks[oobIdx];
    ctx.setCurrent(&func->blocks[curIdx]);
    emitCBr(cond, oob, ok);

    ctx.setCurrent(oob);
    emitCall("rt_arr_oob_panic", {index, len});
    emitTrap();

    ctx.setCurrent(ok);
    return ArrayAccess{base, index};
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
    BasicBlock *block = context().current();
    assert(block && "emitAlloca requires an active block");
    block->instructions.push_back(in);
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
    BasicBlock *block = context().current();
    assert(block && "emitLoad requires an active block");
    block->instructions.push_back(in);
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
    BasicBlock *block = context().current();
    assert(block && "emitStore requires an active block");
    block->instructions.push_back(in);
}

/// @brief Advance a FOR-loop induction variable by a step amount.
/// @param slot Stack slot containing the induction variable.
/// @param step Value representing the step.
/// @details Uses @ref emitLoad, @ref emitBinary, and @ref emitStore while @c cur references the
/// loop body block, keeping the mutation localized to the current control-flow context.
void Lowerer::emitForStep(Value slot, Value step)
{
    Value load = emitLoad(Type(Type::Kind::I64), slot);
    Value add = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), load, step);
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
    BasicBlock *block = context().current();
    assert(block && "emitBinary requires an active block");
    block->instructions.push_back(in);
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
    BasicBlock *block = context().current();
    assert(block && "emitUnary requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

/// @brief Emit a checked negation for a signed integer operand.
/// @param ty Result type describing the integer width.
/// @param val Operand being negated.
/// @return Temporary containing the negated value.
Value Lowerer::emitCheckedNeg(Type ty, Value val)
{
    return emitBinary(Opcode::ISubOvf, ty, Value::constInt(0), val);
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
    if (target->label.empty())
        target->label = nextFallbackBlockLabel();
    in.labels.push_back(target->label);
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitBr requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
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
    BasicBlock *block = context().current();
    assert(block && "emitCBr requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
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
    BasicBlock *block = context().current();
    assert(block && "emitCall requires an active block");
    block->instructions.push_back(in);
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
    BasicBlock *block = context().current();
    assert(block && "emitCallRet requires an active block");
    block->instructions.push_back(in);
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
    BasicBlock *block = context().current();
    assert(block && "emitConstStr requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

void Lowerer::storeArray(Value slot, Value value)
{
    requireArrayI32Retain();
    emitCall("rt_arr_i32_retain", {value});
    Value oldValue = emitLoad(Type(Type::Kind::Ptr), slot);
    requireArrayI32Release();
    emitCall("rt_arr_i32_release", {oldValue});
    emitStore(Type(Type::Kind::Ptr), slot, value);
}

void Lowerer::releaseArrayLocals(const std::unordered_set<std::string> &paramNames)
{
    bool requested = false;
    for (auto &[name, info] : symbols)
    {
        if (!info.referenced || !info.slotId || !info.isArray)
            continue;
        if (paramNames.contains(name))
            continue;
        Value slot = Value::temp(*info.slotId);
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);
        if (!requested)
        {
            requireArrayI32Release();
            requested = true;
        }
        emitCall("rt_arr_i32_release", {handle});
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    }
}

void Lowerer::releaseArrayParams(const std::unordered_set<std::string> &paramNames)
{
    if (paramNames.empty())
        return;
    bool requested = false;
    for (auto &[name, info] : symbols)
    {
        if (!info.referenced || !info.slotId || !info.isArray)
            continue;
        if (!paramNames.contains(name))
            continue;
        Value slot = Value::temp(*info.slotId);
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);
        if (!requested)
        {
            requireArrayI32Release();
            requested = true;
        }
        emitCall("rt_arr_i32_release", {handle});
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    }
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
    BasicBlock *block = context().current();
    assert(block && "emitRet requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

/// @brief Emit a void return terminator in the current block.
/// @details Mirrors @ref emitRet but without an operand; @c cur becomes terminated afterwards.
void Lowerer::emitRetVoid()
{
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitRetVoid requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
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
    BasicBlock *block = context().current();
    assert(block && "emitTrap requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

/// @brief Retrieve or create the global label for a string literal.
/// @param s Literal contents.
/// @return Stable label used to refer to the literal.
/// @details Caches previously generated labels in the symbol map and requests
/// @c builder to emit the global if the literal is first seen.
std::string Lowerer::getStringLabel(const std::string &s)
{
    auto &info = ensureSymbol(s);
    if (!info.stringLabel.empty())
        return info.stringLabel;
    std::string name = ".L" + std::to_string(nextStringId++);
    builder->addGlobalStr(name, s);
    info.stringLabel = name;
    return info.stringLabel;
}

/// @brief Acquire the next temporary identifier compatible with the builder's numbering.
/// @return Unsigned identifier matching the shared builder counter.
/// @details Requests the next id from @c builder so manual instruction emission stays in sync
/// with the builder-managed temporary sequence. The owning function's @c valueNames vector is
/// extended to keep VM register sizing correct and receives a default "%tN" placeholder when no
/// explicit debug name exists for the id.
unsigned Lowerer::nextTempId()
{
    ProcedureContext &ctx = context();
    unsigned id = 0;
    if (builder)
    {
        id = builder->reserveTempId();
    }
    else
    {
        id = ctx.nextTemp();
        ctx.setNextTemp(id + 1);
    }
    if (Function *func = ctx.function())
    {
        if (func->valueNames.size() <= id)
            func->valueNames.resize(id + 1);
        if (func->valueNames[id].empty())
            func->valueNames[id] = "%t" + std::to_string(id);
    }
    if (ctx.nextTemp() <= id)
        ctx.setNextTemp(id + 1);
    return id;
}

std::string Lowerer::nextFallbackBlockLabel()
{
    return mangler.block("bb_" + std::to_string(nextFallbackBlockId++));
}

} // namespace il::frontends::basic
