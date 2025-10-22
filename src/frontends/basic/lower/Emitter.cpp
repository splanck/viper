//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Emitter.cpp
// Purpose: Implement the IL emission helper composed by the BASIC lowerer.
// Key invariants: Helpers append instructions to the current block when set and
//                 register required runtime helpers.
// Ownership/Lifetime: Borrows Lowerer-managed contexts and IR builder state.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the `Emitter` façade used by the BASIC lowerer to
///        materialise IL instructions.
/// @details The emitter consolidates instruction-building helpers (allocas,
///          loads, branching, runtime calls) so lowering code can focus on
///          semantics while the emitter keeps track of block termination,
///          runtime feature requests, and error-handler bookkeeping.

#include "frontends/basic/lower/Emitter.hpp"

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include <cassert>
#include <optional>
#include <utility>

using namespace il::core;

namespace il::frontends::basic::lower
{


/// @brief Construct an emitter bound to the owning lowering driver.
/// @details Keeps a reference to @ref Lowerer so helper routines can allocate
///          temporaries, access the current IR builder, and request runtime
///          helpers while emitting instructions.
/// @param lowerer Lowering engine that orchestrates IL emission.
Emitter::Emitter(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

/// @brief Retrieve the canonical boolean type used during lowering.
/// @details Returns a one-bit integer wrapper matching the IL representation of
///          boolean values.
/// @return Boolean type descriptor.
Emitter::Type Emitter::ilBoolTy() const
{
    return Type(Type::Kind::I1);
}

/// @brief Emit a boolean literal as a truncation from an integer constant.
/// @details Produces a `trunc1` instruction that converts @p v into the IL
///          boolean representation and returns the resulting temporary.
/// @param v Boolean value to encode.
/// @return Temporary referencing the emitted constant.
Emitter::Value Emitter::emitBoolConst(bool v)
{
    return emitUnary(Opcode::Trunc1, ilBoolTy(), Value::constInt(v ? 1 : 0));
}

/// @brief Emit control flow that materialises a boolean from branch callbacks.
/// @details Allocates a temporary slot, spawns then/else/join blocks using the
///          provided label hints, and invokes @p emitThen/@p emitElse to write to
///          the slot before joining and loading the result.
/// @param emitThen Callback that stores a boolean in the then branch.
/// @param emitElse Callback that stores a boolean in the else branch.
/// @param thenLabelBase Base label hint for the then block.
/// @param elseLabelBase Base label hint for the else block.
/// @param joinLabelBase Base label hint for the join block.
/// @return Temporary holding the boolean read from the slot in the join block.
Emitter::Value Emitter::emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                             const std::function<void(Value)> &emitElse,
                                             std::string_view thenLabelBase,
                                             std::string_view elseLabelBase,
                                             std::string_view joinLabelBase)
{
    auto &ctx = lowerer_.context();
    Value slot = emitAlloca(1);

    auto labelFor = [&](std::string_view base)
    {
        std::string hint(base);
        if (auto *blockNamer = ctx.blockNames().namer())
            return blockNamer->generic(hint);
        return lowerer_.mangler.block(hint);
    };

    std::string thenLbl = labelFor(thenLabelBase);
    std::string elseLbl = labelFor(elseLabelBase);
    std::string joinLbl = labelFor(joinLabelBase);

    Function *func = ctx.function();
    assert(func && "emitBoolFromBranches requires an active function");
    BasicBlock *thenBlk = &lowerer_.builder->addBlock(*func, thenLbl);
    BasicBlock *elseBlk = &lowerer_.builder->addBlock(*func, elseLbl);
    BasicBlock *joinBlk = &lowerer_.builder->addBlock(*func, joinLbl);

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

/// @brief Emit an `alloca` instruction reserving stack memory.
/// @details Appends an ALLOCA to the current block, allocating @p bytes of
///          storage, and returns a temporary pointing at the reserved slot.
/// @param bytes Number of bytes to allocate on the stack.
/// @return Temporary referencing the allocated pointer.
Emitter::Value Emitter::emitAlloca(int bytes)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Alloca;
    in.type = Type(Type::Kind::Ptr);
    in.operands.push_back(Value::constInt(bytes));
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitAlloca requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

/// @brief Emit a load from the given address.
/// @details Constructs a LOAD instruction using the supplied type and address
///          operands and returns the resulting temporary.
/// @param ty Type of the value being loaded.
/// @param addr Pointer operand supplying the address.
/// @return Temporary representing the loaded value.
Emitter::Value Emitter::emitLoad(Type ty, Value addr)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Load;
    in.type = ty;
    in.operands.push_back(addr);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitLoad requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

/// @brief Emit a store to the given address.
/// @details Appends a STORE instruction that writes @p val into @p addr using the
///          provided element type.
/// @param ty Element type stored at the address.
/// @param addr Pointer receiving the value.
/// @param val Value to store.
void Emitter::emitStore(Type ty, Value addr, Value val)
{
    Instr in;
    in.op = Opcode::Store;
    in.type = ty;
    in.operands = {addr, val};
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitStore requires an active block");
    block->instructions.push_back(in);
}

/// @brief Emit a binary instruction with two operands.
/// @details Allocates a new temporary, appends the instruction to the current
///          block, and returns the temporary identifier.
/// @param op Opcode describing the operation.
/// @param ty Result type of the instruction.
/// @param lhs Left-hand operand.
/// @param rhs Right-hand operand.
/// @return Temporary referencing the emitted instruction.
Emitter::Value Emitter::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {lhs, rhs};
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitBinary requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

/// @brief Emit a unary instruction with a single operand.
/// @details Mirrors @ref emitBinary for one-operand instructions such as casts.
/// @param op Opcode describing the operation.
/// @param ty Result type of the instruction.
/// @param val Operand value.
/// @return Temporary referencing the emitted instruction.
Emitter::Value Emitter::emitUnary(Opcode op, Type ty, Value val)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {val};
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitUnary requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

/// @brief Emit a 64-bit integer constant value.
/// @details Wraps @ref Value::constInt to keep emitter call sites uniform when
///          building arithmetic sequences.
/// @param v Signed integer literal to encode.
/// @return Constant value descriptor.
Emitter::Value Emitter::emitConstI64(std::int64_t v)
{
    return Value::constInt(v);
}

/// @brief Zero-extend a boolean to a 64-bit integer.
/// @details Emits the `zext1` opcode to widen BASIC booleans to I64 for use with
///          runtime helpers that expect integer semantics.
/// @param val Boolean value to widen.
/// @return Temporary containing the widened integer.
Emitter::Value Emitter::emitZext1ToI64(Value val)
{
    return emitUnary(Opcode::Zext1, Type(Type::Kind::I64), val);
}

/// @brief Emit a saturating integer subtraction.
/// @details Wraps the `isub.ovf` opcode with the canonical 64-bit integer type.
/// @param lhs Left-hand operand.
/// @param rhs Right-hand operand.
/// @return Temporary produced by the subtraction instruction.
Emitter::Value Emitter::emitISub(Value lhs, Value rhs)
{
    return emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), lhs, rhs);
}

/// @brief Convert a boolean into BASIC's logical integer representation.
/// @details BASIC treats true as -1. This helper emits the sequence needed to
///          honour that contract, falling back to constants when no block is
///          active (e.g., folding).
/// @param b1 Boolean value to convert.
/// @return I64 value matching BASIC logical semantics.
Emitter::Value Emitter::emitBasicLogicalI64(Value b1)
{
    if (lowerer_.context().current() == nullptr)
    {
        if (b1.kind == Value::Kind::ConstInt)
            return Value::constInt(b1.i64 != 0 ? -1 : 0);
        return Value::constInt(0);
    }
    Value i64zero = emitConstI64(0);
    Value zext = emitZext1ToI64(b1);
    return emitISub(i64zero, zext);
}

/// @brief Emit a negation guarded by overflow checking.
/// @details Performs `0 - val` with the overflow flag enabled so arithmetic
///          traps propagate correctly.
/// @param ty Type of the result.
/// @param val Operand being negated.
/// @return Temporary holding the negated value.
Emitter::Value Emitter::emitCheckedNeg(Type ty, Value val)
{
    return emitBinary(Opcode::ISubOvf, ty, Value::constInt(0), val);
}

/// @brief Emit an unconditional branch to the specified block.
/// @details Appends a BR instruction when the current block differs from the
///          target and marks the block as terminated.
/// @param target Destination block to branch to.
void Emitter::emitBr(BasicBlock *target)
{
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitBr requires an active block");

    if (block == target)
        return;

    Instr in;
    in.op = Opcode::Br;
    in.type = Type(Type::Kind::Void);
    if (target->label.empty())
        target->label = lowerer_.nextFallbackBlockLabel();
    in.labels.push_back(target->label);
    in.loc = lowerer_.curLoc;
    block->instructions.push_back(in);
    block->terminated = true;
}

/// @brief Emit a conditional branch referencing two successor blocks.
/// @details Appends a CBR instruction with @p cond and terminates the current
///          block.
/// @param cond Condition controlling the branch.
/// @param t Destination when @p cond evaluates to true.
/// @param f Destination when @p cond evaluates to false.
void Emitter::emitCBr(Value cond, BasicBlock *t, BasicBlock *f)
{
    Instr in;
    in.op = Opcode::CBr;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(cond);
    in.labels.push_back(t->label);
    in.labels.push_back(f->label);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitCBr requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

/// @brief Emit a call that returns a value.
/// @details Adds a CALL instruction producing a temporary result and returns the
///          new temporary identifier.
/// @param ty Result type of the call.
/// @param callee Symbol name to invoke.
/// @param args Argument values forwarded to the callee.
/// @return Temporary capturing the call's result.
Emitter::Value Emitter::emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Call;
    in.type = ty;
    in.callee = callee;
    in.operands = args;
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitCallRet requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

/// @brief Emit a call that discards the return value.
/// @details Appends a void-typed CALL instruction with the provided callee and
///          argument list.
/// @param callee Symbol name to invoke.
/// @param args Argument values.
void Emitter::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    Instr in;
    in.op = Opcode::Call;
    in.type = Type(Type::Kind::Void);
    in.callee = callee;
    in.operands = args;
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitCall requires an active block");
    block->instructions.push_back(in);
}

/// @brief Emit a CONSTSTR instruction referencing a global literal.
/// @details Allocates a new temporary, loads the named global string, and
///          returns the temporary identifier.
/// @param globalName Name of the global string constant.
/// @return Temporary holding the string handle.
Emitter::Value Emitter::emitConstStr(const std::string &globalName)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::ConstStr;
    in.type = Type(Type::Kind::Str);
    in.operands.push_back(Value::global(globalName));
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitConstStr requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

/// @brief Store an array handle into a slot with retain/release semantics.
/// @details Requests the retain/release helpers, retains the new handle,
///          releases the existing one, and stores the updated pointer.
/// @param slot Pointer to the slot storing the handle.
/// @param value New array handle to record.
void Emitter::storeArray(Value slot, Value value)
{
    lowerer_.requireArrayI32Retain();
    emitCall("rt_arr_i32_retain", {value});
    Value oldValue = emitLoad(Type(Type::Kind::Ptr), slot);
    lowerer_.requireArrayI32Release();
    emitCall("rt_arr_i32_release", {oldValue});
    emitStore(Type(Type::Kind::Ptr), slot, value);
}

/// @brief Release array-valued locals tracked in the lowering symbol table.
/// @details Iterates over symbols, skipping parameters, and emits release calls
///          for any retained array handles before clearing their slots.
/// @param paramNames Names of procedure parameters to exclude from release.
void Emitter::releaseArrayLocals(const std::unordered_set<std::string> &paramNames)
{
    bool requested = false;
    for (auto &[name, info] : lowerer_.symbols)
    {
        if (!info.referenced || !info.slotId || !info.isArray)
            continue;
        if (paramNames.contains(name))
            continue;
        Value slot = Value::temp(*info.slotId);
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);
        if (!requested)
        {
            lowerer_.requireArrayI32Release();
            requested = true;
        }
        emitCall("rt_arr_i32_release", {handle});
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    }
}

/// @brief Release array parameters at the end of a procedure.
/// @details Processes the supplied parameter names, invoking the release helper
///          for each referenced array and clearing the slot afterwards.
/// @param paramNames Names of parameters to release.
void Emitter::releaseArrayParams(const std::unordered_set<std::string> &paramNames)
{
    if (paramNames.empty())
        return;
    bool requested = false;
    for (auto &[name, info] : lowerer_.symbols)
    {
        if (!info.referenced || !info.slotId || !info.isArray)
            continue;
        if (!paramNames.contains(name))
            continue;
        Value slot = Value::temp(*info.slotId);
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);
        if (!requested)
        {
            lowerer_.requireArrayI32Release();
            requested = true;
        }
        emitCall("rt_arr_i32_release", {handle});
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    }
}

/// @brief Release object-valued locals, invoking destructors when required.
/// @details Synthesises control flow that calls runtime helpers and, when the
///          object class is known, the user-defined destructor prior to freeing
///          the handle. Slots are cleared after release.
/// @param paramNames Names of parameters to skip while releasing locals.
void Emitter::releaseObjectLocals(const std::unordered_set<std::string> &paramNames)
{
    auto releaseSlot = [this](Lowerer::SymbolInfo &info)
    {
        if (!lowerer_.builder || !info.slotId)
            return;
        auto &ctx = lowerer_.context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (!func || !origin)
            return;

        std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
        Value slot = Value::temp(*info.slotId);

        lowerer_.curLoc = {};
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);

        lowerer_.requestHelper(Lowerer::RuntimeFeature::ObjReleaseChk0);
        lowerer_.requestHelper(Lowerer::RuntimeFeature::ObjFree);

        Value shouldDestroy = emitCallRet(ilBoolTy(), "rt_obj_release_check0", {handle});

        std::string destroyLabel;
        if (auto *blockNamer = ctx.blockNames().namer())
            destroyLabel = blockNamer->generic("obj_epilogue_dtor");
        else
            destroyLabel = lowerer_.mangler.block("obj_epilogue_dtor");
        std::size_t destroyIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, destroyLabel);

        std::string contLabel;
        if (auto *blockNamer = ctx.blockNames().namer())
            contLabel = blockNamer->generic("obj_epilogue_cont");
        else
            contLabel = lowerer_.mangler.block("obj_epilogue_cont");
        std::size_t contIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, contLabel);

        BasicBlock *destroyBlk = &func->blocks[destroyIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[originIdx]);
        lowerer_.curLoc = {};
        emitCBr(shouldDestroy, destroyBlk, contBlk);

        ctx.setCurrent(destroyBlk);
        lowerer_.curLoc = {};
        if (!info.objectClass.empty())
            emitCall(mangleClassDtor(info.objectClass), {handle});
        emitCall("rt_obj_free", {handle});
        emitBr(contBlk);

        ctx.setCurrent(contBlk);
        lowerer_.curLoc = {};
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    };

    for (auto &[name, info] : lowerer_.symbols)
    {
        if (!info.referenced || !info.isObject)
            continue;
        if (name == "ME")
            continue;
        if (paramNames.contains(name))
            continue;
        if (!info.slotId)
            continue;
        releaseSlot(info);
    }
}

/// @brief Release object-valued parameters that were retained during lowering.
/// @details Mirrors @ref releaseObjectLocals but only processes the supplied
///          parameter set, ensuring borrowed handles are freed at function exit.
/// @param paramNames Parameter names targeted for release.
void Emitter::releaseObjectParams(const std::unordered_set<std::string> &paramNames)
{
    if (paramNames.empty())
        return;

    auto releaseSlot = [this](Lowerer::SymbolInfo &info)
    {
        if (!lowerer_.builder || !info.slotId)
            return;
        auto &ctx = lowerer_.context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (!func || !origin)
            return;

        std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
        Value slot = Value::temp(*info.slotId);

        lowerer_.curLoc = {};
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);

        lowerer_.requestHelper(Lowerer::RuntimeFeature::ObjReleaseChk0);
        lowerer_.requestHelper(Lowerer::RuntimeFeature::ObjFree);

        Value shouldDestroy = emitCallRet(ilBoolTy(), "rt_obj_release_check0", {handle});

        std::string destroyLabel;
        if (auto *blockNamer = ctx.blockNames().namer())
            destroyLabel = blockNamer->generic("obj_epilogue_dtor");
        else
            destroyLabel = lowerer_.mangler.block("obj_epilogue_dtor");
        std::size_t destroyIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, destroyLabel);

        std::string contLabel;
        if (auto *blockNamer = ctx.blockNames().namer())
            contLabel = blockNamer->generic("obj_epilogue_cont");
        else
            contLabel = lowerer_.mangler.block("obj_epilogue_cont");
        std::size_t contIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, contLabel);

        BasicBlock *destroyBlk = &func->blocks[destroyIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[originIdx]);
        lowerer_.curLoc = {};
        emitCBr(shouldDestroy, destroyBlk, contBlk);

        ctx.setCurrent(destroyBlk);
        lowerer_.curLoc = {};
        if (!info.objectClass.empty())
            emitCall(mangleClassDtor(info.objectClass), {handle});
        emitCall("rt_obj_free", {handle});
        emitBr(contBlk);

        ctx.setCurrent(contBlk);
        lowerer_.curLoc = {};
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    };

    for (auto &[name, info] : lowerer_.symbols)
    {
        if (!info.referenced || !info.isObject)
            continue;
        if (name == "ME")
            continue;
        if (!paramNames.contains(name))
            continue;
        if (!info.slotId)
            continue;
        releaseSlot(info);
    }
}

/// @brief Emit a TRAP instruction that terminates the current block.
/// @details Produces a trap without operands, relying on the runtime to consult
///          the VM's trap metadata. The current block is marked terminated so
///          execution cannot continue.
void Emitter::emitTrap()
{
    Instr in;
    in.op = Opcode::Trap;
    in.type = Type(Type::Kind::Void);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitTrap requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

/// @brief Emit a TrapFromErr instruction to translate runtime errors into traps.
/// @details Appends the instruction with @p errCode as its operand and marks the
///          block terminated to reflect the exceptional transfer of control.
/// @param errCode Runtime error code value.
void Emitter::emitTrapFromErr(Value errCode)
{
    Instr in;
    in.op = Opcode::TrapFromErr;
    in.type = Type(Type::Kind::I32);
    in.operands.push_back(errCode);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitTrapFromErr requires an active block");
    block->instructions.push_back(std::move(in));
    block->terminated = true;
}

/// @brief Push an error-handler block onto the handler stack.
/// @details Records the handler label so future trap instructions can resume at
///          @p handler.
/// @param handler Basic block serving as the handler entry point.
void Emitter::emitEhPush(BasicBlock *handler)
{
    assert(handler && "emitEhPush requires a handler block");
    Instr in;
    in.op = Opcode::EhPush;
    in.type = Type(Type::Kind::Void);
    in.labels.push_back(handler->label);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitEhPush requires an active block");
    block->instructions.push_back(in);
}

/// @brief Pop the active error handler from the handler stack.
void Emitter::emitEhPop()
{
    Instr in;
    in.op = Opcode::EhPop;
    in.type = Type(Type::Kind::Void);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitEhPop requires an active block");
    block->instructions.push_back(in);
}

/// @brief Pop an active error handler prior to returning.
/// @details Ensures the handler stack remains balanced when control leaves the
///          current scope.
void Emitter::emitEhPopForReturn()
{
    if (!lowerer_.context().errorHandlers().active())
        return;
    emitEhPop();
}

/// @brief Clear the active error-handler bookkeeping in the lowering context.
/// @details Pops the handler when active and resets the associated metadata.
void Emitter::clearActiveErrorHandler()
{
    auto &ctx = lowerer_.context();
    if (ctx.errorHandlers().active())
        emitEhPop();
    ctx.errorHandlers().setActive(false);
    ctx.errorHandlers().setActiveIndex(std::nullopt);
    ctx.errorHandlers().setActiveLine(std::nullopt);
}

/// @brief Ensure an error-handler block exists for the specified source line.
/// @details Reuses any cached handler; otherwise creates a new block with an EH
///          entry instruction and records it in the handler map.
/// @param targetLine Line number associated with the handler.
/// @return Pointer to the handler block.
Emitter::BasicBlock *Emitter::ensureErrorHandlerBlock(int targetLine)
{
    auto &ctx = lowerer_.context();
    Function *func = ctx.function();
    assert(func && "ensureErrorHandlerBlock requires an active function");

    auto &handlers = ctx.errorHandlers().blocks();
    auto it = handlers.find(targetLine);
    if (it != handlers.end())
        return &func->blocks[it->second];

    std::string base = "handler_L" + std::to_string(targetLine);
    std::string label;
    if (auto *blockNamer = ctx.blockNames().namer())
        label = blockNamer->tag(base);
    else
        label = lowerer_.mangler.block(base);

    std::vector<il::core::Param> params = {{"err", Type(Type::Kind::Error)},
                                           {"tok", Type(Type::Kind::ResumeTok)}};
    BasicBlock &bb = lowerer_.builder->createBlock(*func, label, params);

    Instr entry;
    entry.op = Opcode::EhEntry;
    entry.type = Type(Type::Kind::Void);
    entry.loc = {};
    bb.instructions.push_back(entry);

    size_t idx = static_cast<size_t>(&bb - &func->blocks[0]);
    handlers[targetLine] = idx;
    ctx.errorHandlers().handlerTargets()[idx] = targetLine;
    return &bb;
}

/// @brief Emit a return instruction that yields a value.
/// @details Pops the active error handler when present, appends a RET carrying
///          @p v, and marks the current block as terminated.
/// @param v Value returned to the caller.
void Emitter::emitRet(Value v)
{
    emitEhPopForReturn();
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(v);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitRet requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

/// @brief Emit a void return instruction.
/// @details Pops active error handlers and appends a RET with no operands before
///          terminating the block.
void Emitter::emitRetVoid()
{
    emitEhPopForReturn();
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitRetVoid requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

} // namespace il::frontends::basic::lower
