//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Emitter.cpp
// Purpose: Implement the helper responsible for emitting IL for BASIC lowering.
// Key invariants: Builders only append to the active block and honour Lowerer
//                 location tracking when synthesising instructions.
// Ownership/Lifetime: Emitter borrows the Lowerer context and never owns IR
//                     functions, blocks, or runtime handles.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/Emitter.hpp"

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include <cassert>
#include <optional>
#include <unordered_set>
#include <utility>

using namespace il::core;

namespace il::frontends::basic::lower
{


/// @brief Construct an emitter bound to the enclosing lowering context.
///
/// @details Stores a reference to the owning @ref Lowerer so helper routines
///          can query shared state such as the current function, block naming
///          utilities, and the monotonic temporary identifier generator.
///          Construction performs no additional work, keeping emitter creation
///          cheap for transient helpers.
Emitter::Emitter(Lowerer &lowerer) noexcept : lowerer_(lowerer), common_(lowerer) {}

/// @brief Produce the canonical IL boolean type used by BASIC lowering.
///
/// @details The BASIC front end frequently needs to coerce scalar results into
///          @c i1 slots.  This accessor centralises the construction of the
///          @ref il::core::Type instance so all call sites agree on the
///          representation and avoid repeating the `Type::Kind::I1` literal.
Emitter::Type Emitter::ilBoolTy() const
{
    return common_.ilBoolTy();
}

/// @brief Emit a boolean constant in the current function.
///
/// @details Wraps @ref emitUnary with the `trunc.1` opcode so boolean literals
///          funnel through a single path.  The helper converts @c true to 1 and
///          @c false to 0, matching the IL expectation for integer truncation.
///
/// @param v Whether the emitted literal should be @c true.
/// @return SSA value representing the boolean constant.
Emitter::Value Emitter::emitBoolConst(bool v)
{
    return common_.emitBoolConst(v);
}

/// @brief Materialise a control-flow diamond that collapses to a boolean value.
///
/// @details Allocates a temporary stack slot, generates distinct then/else
///          blocks using the @ref Lowerer naming utilities, and evaluates the
///          provided closures to store a boolean result into that slot.  Both
///          branches fall through to a join block, after which the stored value
///          is reloaded to yield an SSA result.
///
/// @param emitThen Callback invoked when the true branch executes; receives the
///                 slot used to store the branch result.
/// @param emitElse Callback invoked for the false branch.
/// @param thenLabelBase Prefix used when synthesising the true block label.
/// @param elseLabelBase Prefix used when synthesising the false block label.
/// @param joinLabelBase Prefix used for the join block label.
/// @return SSA value containing the boolean result written by the callbacks.
Emitter::Value Emitter::emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                             const std::function<void(Value)> &emitElse,
                                             std::string_view thenLabelBase,
                                             std::string_view elseLabelBase,
                                             std::string_view joinLabelBase)
{
    return common_.emitBoolFromBranches(
        emitThen, emitElse, thenLabelBase, elseLabelBase, joinLabelBase);
}

/// @brief Allocate stack storage in the active block.
///
/// @details Creates an `alloca` instruction typed as a pointer whose operand is
///          the requested byte size.  The instruction is appended to the active
///          block owned by the @ref Lowerer context and the resulting temporary
///          identifier is returned as an SSA value.
///
/// @param bytes Number of bytes to reserve in the current frame.
/// @return SSA pointer referencing the allocated storage.
Emitter::Value Emitter::emitAlloca(int bytes)
{
    return common_.emitAlloca(bytes);
}

/// @brief Load a value of the given type from the supplied address.
///
/// @details Emits a `load` instruction with the provided type and address
///          operand.  The helper asserts that a current block exists, ensuring
///          the lowering context is properly initialised before appending the
///          instruction.
///
/// @param ty IL type describing the loaded value.
/// @param addr Address operand from which to load.
/// @return SSA value representing the loaded result.
Emitter::Value Emitter::emitLoad(Type ty, Value addr)
{
    return common_.emitLoad(ty, addr);
}

/// @brief Store a value to memory within the active block.
///
/// @details Appends a `store` instruction with the supplied operands.  The
///          helper records the lowering location so diagnostics that refer to
///          emitted instructions remain accurate.
///
/// @param ty IL type describing the stored value.
/// @param addr Destination pointer.
/// @param val Value to write to memory.
void Emitter::emitStore(Type ty, Value addr, Value val)
{
    common_.emitStore(ty, addr, val);
}

/// @brief Emit a binary SSA instruction.
///
/// @details Creates an instruction with two operands and the specified opcode,
///          returning the SSA result identifier allocated by the @ref Lowerer.
///          The helper centralises boilerplate for arithmetic and comparison
///          emissions, guaranteeing consistent metadata initialisation.
///
/// @param op Opcode representing the binary operation.
/// @param ty Result type to associate with the instruction.
/// @param lhs Left operand value.
/// @param rhs Right operand value.
/// @return SSA value produced by the emitted instruction.
Emitter::Value Emitter::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    return common_.emitBinary(op, ty, lhs, rhs);
}

/// @brief Emit a unary SSA instruction.
///
/// @details Mirrors @ref emitBinary for one-operand instructions.  The helper
///          is used heavily when narrowing or extending integer values so the
///          IL emitted for casts stays uniform.
///
/// @param op Opcode for the unary operation.
/// @param ty Result type to apply to the instruction.
/// @param val Operand consumed by the opcode.
/// @return SSA value representing the instruction result.
Emitter::Value Emitter::emitUnary(Opcode op, Type ty, Value val)
{
    return common_.emitUnary(op, ty, val);
}

/// @brief Create an IL constant representing a signed 64-bit integer.
///
/// @details Delegates to @ref Value::constInt so arithmetic helpers consistently
///          model integer literals, keeping lowering code concise.
///
/// @param v Literal value to encode.
/// @return SSA value referring to the constant literal.
Emitter::Value Emitter::emitConstI64(std::int64_t v)
{
    return common_.emitConstI64(v);
}

/// @brief Zero-extend a boolean into a 64-bit integer slot.
///
/// @details Emits a unary `zext.1` instruction so boolean predicates can be
///          promoted to the scalar width expected by runtime helpers.
///
/// @param val Boolean SSA value to extend.
/// @return 64-bit integer SSA value.
Emitter::Value Emitter::emitZext1ToI64(Value val)
{
    return common_.emitZext1ToI64(val);
}

/// @brief Emit a checked integer subtraction.
///
/// @details Uses the overflow-detecting `isub.ovf` opcode to guarantee runtime
///          errors when BASIC operations exceed the representable range.
///
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return SSA value with the subtraction result.
Emitter::Value Emitter::emitISub(Value lhs, Value rhs)
{
    return common_.emitISub(lhs, rhs);
}

/// @brief Normalise a BASIC logical value to an @c i64 mask.
///
/// @details BASIC expects logical @c true to materialise as @c -1.  When a
///          current block is available, the helper emits instructions that
///          zero-extend the boolean and subtract from zero to form the mask.
///          If lowering occurs outside a block (e.g., constant folding), the
///          routine falls back to immediate constants.
///
/// @param b1 Boolean SSA value or constant.
/// @return 64-bit integer representing the logical mask.
Emitter::Value Emitter::emitBasicLogicalI64(Value b1)
{
    return common_.emitBasicLogicalI64(b1);
}

/// @brief Emit a checked unary negation for the provided type.
///
/// @details Synthesises `0 - value` using the overflow-checking subtraction
///          opcode.  This keeps negation semantics consistent with BASIC's
///          overflow rules regardless of the target integer width.
///
/// @param ty Result type describing the negated value.
/// @param val Operand to negate.
/// @return SSA value representing the negation result.
Emitter::Value Emitter::emitCheckedNeg(Type ty, Value val)
{
    return common_.emitCheckedNeg(ty, val);
}

/// @brief Emit an unconditional branch to the specified block.
///
/// @details Appends a `br` instruction to the active block, synthesising a
///          fallback label when the destination is unnamed.  The helper marks
///          the current block as terminated to prevent subsequent instructions
///          from being appended inadvertently.
///
/// @param target Destination block that becomes the active successor.
void Emitter::emitBr(BasicBlock *target)
{
    common_.emitBr(target);
}

/// @brief Emit a conditional branch based on the supplied predicate.
///
/// @details Generates a `cbr` instruction referencing both successor labels and
///          records the condition operand.  The helper assumes both target
///          blocks have already been named by the caller.
///
/// @param cond Boolean SSA value that selects the true or false branch.
/// @param t Block executed when @p cond evaluates to true.
/// @param f Block executed when @p cond evaluates to false.
void Emitter::emitCBr(Value cond, BasicBlock *t, BasicBlock *f)
{
    common_.emitCBr(cond, t, f);
}

/// @brief Emit a call that produces a return value.
///
/// @details Constructs a `call` instruction pointing at the named callee and
///          stores the SSA identifier assigned to its return slot.  The helper
///          is used when invoking runtime helpers or functions lowered earlier
///          in the compilation pipeline.
///
/// @param ty Return type of the call.
/// @param callee Mangled name of the function to invoke.
/// @param args Argument list forwarded to the callee.
/// @return SSA value bound to the call's result.
Emitter::Value Emitter::emitCallRet(Type ty,
                                    const std::string &callee,
                                    const std::vector<Value> &args)
{
    return common_.emitCallRet(ty, callee, args);
}

/// @brief Emit a call whose result is discarded.
///
/// @details Produces a `call` instruction with `void` result type.  This is the
///          primary entry point for invoking runtime helpers that perform side
///          effects only, such as printing or retaining resources.
///
/// @param callee Name of the function to invoke.
/// @param args Argument list forwarded to the callee.
void Emitter::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    common_.emitCall(callee, args);
}

Emitter::Value Emitter::emitCallIndirectRet(Type ty, Value callee, const std::vector<Value> &args)
{
    return common_.emitCallIndirectRet(ty, callee, args);
}

void Emitter::emitCallIndirect(Value callee, const std::vector<Value> &args)
{
    common_.emitCallIndirect(callee, args);
}

/// @brief Materialise a constant string handle from a global symbol.
///
/// @details Emits a `const.str` instruction referencing the global string's
///          name.  The helper returns the SSA handle so later operations can
///          pass it to runtime helpers.
///
/// @param globalName Mangled name of the global string literal.
/// @return SSA value representing the runtime handle.
Emitter::Value Emitter::emitConstStr(const std::string &globalName)
{
    return common_.emitConstStr(globalName);
}

/// @brief Store an array handle while maintaining runtime reference counts.
///
/// @details Retains the new handle, releases the previous value stored in the
///          slot, and then writes the updated handle.  The helper requests the
///          necessary runtime thunks lazily so linking pulls them in only when
///          required.
///
/// @param slot Pointer to the stack slot storing the array reference.
/// @param value New array handle to record.
void Emitter::storeArray(Value slot, Value value, AstType elementType, bool isObjectArray)
{
    if (elementType == AstType::Str)
    {
        // String array: no retain needed (rt_arr_str_alloc returns unretained)
        Value oldValue = emitLoad(Type(Type::Kind::Ptr), slot);
        lowerer_.requireArrayStrRelease();
        emitCall("rt_arr_str_release", {oldValue, Value::constInt(0)});
        emitStore(Type(Type::Kind::Ptr), slot, value);
    }
    else if (isObjectArray)
    {
        // Object array: allocator returns owned handle; just release previous
        Value oldValue = emitLoad(Type(Type::Kind::Ptr), slot);
        lowerer_.requireArrayObjRelease();
        emitCall("rt_arr_obj_release", {oldValue});
        emitStore(Type(Type::Kind::Ptr), slot, value);
    }
    else
    {
        // Integer/numeric array
        lowerer_.requireArrayI32Retain();
        emitCall("rt_arr_i32_retain", {value});
        Value oldValue = emitLoad(Type(Type::Kind::Ptr), slot);
        lowerer_.requireArrayI32Release();
        emitCall("rt_arr_i32_release", {oldValue});
        emitStore(Type(Type::Kind::Ptr), slot, value);
    }
}

/// @brief Release array locals that fall out of scope.
///
/// @details Iterates over tracked symbols, skipping parameters and unreferenced
///          variables, and calls the runtime release helper for each active
///          array handle.  Slots are cleared to @c null after release so repeated
///          epilogues remain idempotent.
///
/// @param paramNames Names of parameters that should remain alive.
void Emitter::releaseArrayLocals(const std::unordered_set<std::string> &paramNames)
{
    bool requestedI32 = false;
    bool requestedStr = false;
    bool requestedObj = false;
    for (auto &[name, info] : lowerer_.symbols)
    {
        if (!info.referenced || !info.slotId || !info.isArray)
            continue;
        if (paramNames.contains(name))
            continue;
        Value slot = Value::temp(*info.slotId);
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);
        if (info.type == AstType::Str)
        {
            // String array
            if (!requestedStr)
            {
                lowerer_.requireArrayStrRelease();
                requestedStr = true;
            }
            emitCall("rt_arr_str_release", {handle, Value::constInt(0)});
        }
        else if (info.isObject)
        {
            if (!requestedObj)
            {
                lowerer_.requireArrayObjRelease();
                requestedObj = true;
            }
            emitCall("rt_arr_obj_release", {handle});
        }
        else
        {
            // Integer/numeric array
            if (!requestedI32)
            {
                lowerer_.requireArrayI32Release();
                requestedI32 = true;
            }
            emitCall("rt_arr_i32_release", {handle});
        }
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    }
}

/// @brief Release array parameters at the end of a routine.
///
/// @details Mirrors @ref releaseArrayLocals but only touches symbols whose
///          names appear in @p paramNames.  The helper ensures runtime release
///          helpers are requested exactly once even when multiple parameters are
///          processed.
///
/// @param paramNames Parameter names that should be released.
void Emitter::releaseArrayParams(const std::unordered_set<std::string> &paramNames)
{
    if (paramNames.empty())
        return;
    bool requestedI32 = false;
    bool requestedStr = false;
    bool requestedObj = false;
    for (auto &[name, info] : lowerer_.symbols)
    {
        if (!info.referenced || !info.slotId || !info.isArray)
            continue;
        if (!paramNames.contains(name))
            continue;
        Value slot = Value::temp(*info.slotId);
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);
        if (info.type == AstType::Str)
        {
            // String array
            if (!requestedStr)
            {
                lowerer_.requireArrayStrRelease();
                requestedStr = true;
            }
            emitCall("rt_arr_str_release", {handle, Value::constInt(0)});
        }
        else if (info.isObject)
        {
            if (!requestedObj)
            {
                lowerer_.requireArrayObjRelease();
                requestedObj = true;
            }
            emitCall("rt_arr_obj_release", {handle});
        }
        else
        {
            // Integer/numeric array
            if (!requestedI32)
            {
                lowerer_.requireArrayI32Release();
                requestedI32 = true;
            }
            emitCall("rt_arr_i32_release", {handle});
        }
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    }
}

void Emitter::deferReleaseStr(Value v)
{
    if (v.kind != Value::Kind::Temp)
        return;
    deferredTemps_.push_back(TempRelease{v, /*isString=*/true, {}});
}

void Emitter::deferReleaseObj(Value v, const std::string &className)
{
    if (v.kind != Value::Kind::Temp)
        return;
    deferredTemps_.push_back(TempRelease{v, /*isString=*/false, className});
}

void Emitter::releaseDeferredTemps()
{
    if (deferredTemps_.empty())
        return;

    // Deduplicate by temporary id so repeated uses do not double release.
    std::unordered_set<unsigned> seen;
    for (const auto &t : deferredTemps_)
    {
        if (t.v.kind != Value::Kind::Temp)
            continue;
        if (!seen.insert(t.v.id).second)
            continue;

        if (t.isString)
        {
            lowerer_.requireStrReleaseMaybe();
            lowerer_.emitCall("rt_str_release_maybe", {t.v});
            continue;
        }

        // Object: emit conditional destructor + free when last ref.
        auto &ctx = lowerer_.context();
        auto *func = ctx.function();
        if (!func || !ctx.current())
            continue;
        std::size_t originIdx = static_cast<std::size_t>(ctx.current() - &func->blocks[0]);
        auto *origin = &func->blocks[originIdx];

        // Create destroy and continue blocks.
        std::string destroyLabel;
        std::string contLabel;
        if (auto *bn = ctx.blockNames().namer())
        {
            destroyLabel = bn->generic("obj_epilogue_dtor");
            contLabel = bn->generic("obj_epilogue_cont");
        }
        else
        {
            destroyLabel = lowerer_.mangler.block("obj_epilogue_dtor");
            contLabel = lowerer_.mangler.block("obj_epilogue_cont");
        }

        lowerer_.builder->addBlock(*func, destroyLabel);
        lowerer_.builder->addBlock(*func, contLabel);
        auto *destroyBlk = &func->blocks[func->blocks.size() - 2];
        auto *contBlk = &func->blocks.back();

        // Reset current block pointer after adding blocks, since vector may have reallocated.
        ctx.setCurrent(&func->blocks[originIdx]);

        Value needDtor = lowerer_.emitCallRet(Type(Type::Kind::I1), "rt_obj_release_check0", {t.v});
        lowerer_.emitCBr(needDtor, destroyBlk, contBlk);

        ctx.setCurrent(destroyBlk);
        if (!t.className.empty())
        {
            // Call destructor if present in module
            std::string dtor = mangleClassDtor(t.className);
            bool haveDtor = false;
            if (auto *mod = lowerer_.mod)
            {
                for (const auto &f : mod->functions)
                {
                    if (f.name == dtor)
                    {
                        haveDtor = true;
                        break;
                    }
                }
            }
            if (haveDtor)
                lowerer_.emitCall(dtor, {t.v});
        }
        lowerer_.emitCall("rt_obj_free", {t.v});
        lowerer_.emitBr(contBlk);
        ctx.setCurrent(contBlk);
    }

    deferredTemps_.clear();
}

/// @brief Clear accumulated deferred temps without emitting releases.
/// @details Used at procedure entry to prevent leaking cleanup code from
///          module-level initialization or prior procedures (BUG-063 fix).
void Emitter::clearDeferredTemps()
{
    deferredTemps_.clear();
}

/// @brief Emit destructor epilogues for object locals.
///
/// @details For each tracked object local not excluded by @p paramNames, the
///          helper synthesises a conditional branch that queries the runtime to
///          determine whether destruction is required.  If so, it invokes the
///          mangled class destructor and releases the handle before storing
///          @c null back into the slot.
///
/// @param paramNames Names of parameters that remain owned by the caller.
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
        {
            std::string dtor = mangleClassDtor(info.objectClass);
            bool haveDtor = false;
            if (lowerer_.mod)
            {
                for (const auto &fn : lowerer_.mod->functions)
                {
                    if (fn.name == dtor)
                    {
                        haveDtor = true;
                        break;
                    }
                }
            }
            if (haveDtor)
                emitCall(dtor, {handle});
        }
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

/// @brief Release object parameters that the routine owns by convention.
///
/// @details Uses the same logic as @ref releaseObjectLocals but restricts
///          processing to parameters listed in @p paramNames.  This is used by
///          routines that transfer ownership of certain arguments during
///          execution.
///
/// @param paramNames Parameter names eligible for release.
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
        {
            std::string dtor = mangleClassDtor(info.objectClass);
            bool haveDtor = false;
            if (lowerer_.mod)
            {
                for (const auto &fn : lowerer_.mod->functions)
                {
                    if (fn.name == dtor)
                    {
                        haveDtor = true;
                        break;
                    }
                }
            }
            if (haveDtor)
                emitCall(dtor, {handle});
        }
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

/// @brief Emit an unconditional trap instruction.
///
/// @details Appends a `trap` opcode and marks the current block as terminated
///          so no further instructions are emitted.  Used when lowering runtime
///          error paths.
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

/// @brief Emit a trap that forwards a runtime error code.
///
/// @details Generates a `trap.from_err` instruction consuming the provided error
///          operand.  The block is marked terminated to match the trap's
///          semantics.
///
/// @param errCode SSA value describing the runtime error token.
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

/// @brief Register an exception handler block on the runtime stack.
///
/// @details Emits an `eh.push` instruction referencing the handler label.  The
///          helper assumes the lowering context has already named the handler
///          block.
///
/// @param handler Exception handler block to push.
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

/// @brief Pop the active exception handler.
///
/// @details Appends an `eh.pop` instruction, leaving block termination unchanged
///          because control returns to the caller.
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

/// @brief Pop any active handler before emitting a return.
///
/// @details Checks the lowering context to determine whether a handler is
///          active.  When present the routine emits @ref emitEhPop so returns do
///          not leak handler state.
void Emitter::emitEhPopForReturn()
{
    if (!lowerer_.context().errorHandlers().active())
        return;
    emitEhPop();
}

/// @brief Clear the lowering bookkeeping for the active error handler.
///
/// @details Emits a pop instruction when necessary and resets the handler state
///          tracked by @ref Lowerer::ErrorHandlers so subsequent statements do
///          not assume a handler remains in effect.
void Emitter::clearActiveErrorHandler()
{
    auto &ctx = lowerer_.context();
    if (ctx.errorHandlers().active())
        emitEhPop();
    ctx.errorHandlers().setActive(false);
    ctx.errorHandlers().setActiveIndex(std::nullopt);
    ctx.errorHandlers().setActiveLine(std::nullopt);
}

/// @brief Retrieve or create the error handler block for a BASIC line.
///
/// @details Looks up an existing block in the lowering context's handler map.
///          When absent, it synthesises a new block with `err` and `tok`
///          parameters, inserts the canonical `eh.entry` instruction, and
///          records the mapping so future lookups reuse the block.
///
/// @param targetLine BASIC source line number associated with the handler.
/// @return Pointer to the handler block ready for use.
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

/// @brief Emit a non-void return that releases handlers first.
///
/// @details Invokes @ref emitEhPopForReturn to balance handler stacks, then
///          generates a `ret` instruction carrying the supplied operand and
///          terminates the block.
///
/// @param v SSA value returned to the caller.
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

/// @brief Emit a void return following handler teardown.
///
/// @details Mirrors @ref emitRet but without an operand, ensuring the active
///          block terminates cleanly after popping any outstanding error
///          handlers.
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
