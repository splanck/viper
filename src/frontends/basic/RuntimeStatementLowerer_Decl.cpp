//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/RuntimeStatementLowerer_Decl.cpp
// Purpose: Implementation of variable declaration runtime statement lowering.
//          Handles DIM, REDIM, CONST, STATIC, RANDOMIZE, and SWAP statements.
// Key invariants: Maintains Lowerer's runtime lowering semantics exactly.
// Ownership/Lifetime: Borrows Lowerer reference; coordinates with parent.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "RuntimeStatementLowerer.hpp"
#include "Lowerer.hpp"
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/LocationScope.hpp"

#include <cassert>

using namespace il::core;
using AstType = ::il::frontends::basic::Type;

namespace il::frontends::basic
{

/// @brief Lower a BASIC @c CONST statement.
///
/// @details Evaluates the initializer expression and stores it into the constant's
///          storage location. The lowering is similar to LET - constants are treated
///          as read-only variables at compile-time (semantic analysis prevents reassignment).
///
/// @param stmt Parsed @c CONST statement.
void RuntimeStatementLowerer::lowerConst(const ConstStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);

    // Evaluate the initializer expression
    Lowerer::RVal value = lowerer_.lowerExpr(*stmt.initializer);

    // Resolve storage for the constant (same as variable)
    auto storage = lowerer_.resolveVariableStorage(stmt.name, stmt.loc);
    assert(storage && "CONST target should have storage");

    // Store the value
    Lowerer::SlotType slotInfo = storage->slotInfo;
    if (slotInfo.isArray)
    {
        lowerer_.storeArray(storage->pointer,
                            value.value,
                            /*elementType*/ AstType::I64,
                            /*isObjectArray*/ storage->slotInfo.isObject);
    }
    else
    {
        assignScalarSlot(slotInfo, storage->pointer, std::move(value), stmt.loc);
    }
}

/// @brief Lower BASIC @c STATIC statements declaring procedure-local persistent variables.
///
/// @details STATIC variables are allocated at module scope rather than as stack locals.
///          The actual storage allocation happens during variable collection and is
///          materialized as a module-level global. This lowering method is a no-op
///          because the declaration itself doesn't generate runtime code - only
///          uses of the variable will reference the module-level storage.
///
/// @param stmt Parsed @c STATIC statement identifying the variable name and type.
void RuntimeStatementLowerer::lowerStatic(const StaticStmt &stmt)
{
    // No code emission needed - storage is allocated as module-level global
    // during variable collection phase, and variable references will resolve
    // to that global storage automatically.
    (void)stmt;
}

/// @brief Emit runtime validation logic for array length expressions.
///
/// @details Adjusts the requested bound to account for BASIC's inclusive array
///          lengths, generates overflow-aware addition, and emits a conditional
///          branch to the runtime failure path when the bound is invalid.  The
///          @p labelBase parameter keeps generated block names deterministic for
///          debugging and reproducibility.
///
/// @param bound     Value representing the user-supplied length expression.
/// @param loc       Source location used for diagnostics and helper emission.
/// @param labelBase Prefix used when naming generated failure blocks.
/// @return Validated length value produced by the runtime helper.
Value RuntimeStatementLowerer::emitArrayLengthCheck(Value bound,
                                                    il::support::SourceLoc loc,
                                                    std::string_view labelBase)
{
    LocationScope location(lowerer_, loc);
    Value length =
        lowerer_.emitCommon(loc).add_checked(bound, Value::constInt(1), OverflowPolicy::Checked);

    ProcedureContext &ctx = lowerer_.context();
    Function *func = ctx.function();
    BasicBlock *original = ctx.current();
    if (func && original)
    {
        size_t curIdx = static_cast<size_t>(original - &func->blocks[0]);
        BlockNamer *blockNamer = ctx.blockNames().namer();

        std::string base(labelBase);
        std::string failName = base.empty() ? "arr_len_fail" : base + "_fail";
        std::string contName = base.empty() ? "arr_len_cont" : base + "_cont";

        std::string failLbl =
            blockNamer ? blockNamer->generic(failName) : lowerer_.mangler.block(failName);
        std::string contLbl =
            blockNamer ? blockNamer->generic(contName) : lowerer_.mangler.block(contName);

        size_t failIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, failLbl);
        // Create contBlk with a block parameter for length to ensure proper SSA form
        // across the conditional branch. This is required for native codegen which
        // cannot reference values defined in predecessor blocks.
        std::vector<il::core::Param> contParams = {
            {"len", il::core::Type(il::core::Type::Kind::I64)}};
        size_t contIdx = func->blocks.size();
        lowerer_.builder->createBlock(*func, contLbl, contParams);

        BasicBlock *failBlk = &func->blocks[failIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[curIdx]);
        Value isNeg =
            lowerer_.emitBinary(Opcode::SCmpLT, lowerer_.ilBoolTy(), length, Value::constInt(0));

        // Emit CBr with branch arguments: pass length to contBlk via block parameter
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = il::core::Type(il::core::Type::Kind::Void);
        cbr.operands.push_back(isNeg);
        cbr.labels.push_back(failLbl);
        cbr.labels.push_back(contLbl);
        cbr.brArgs.push_back({});       // failBlk has no parameters
        cbr.brArgs.push_back({length}); // contBlk receives length
        cbr.loc = lowerer_.curLoc;
        BasicBlock *curBlock = ctx.current();
        curBlock->instructions.push_back(cbr);
        curBlock->terminated = true;

        ctx.setCurrent(failBlk);
        lowerer_.emitTrap();

        ctx.setCurrent(contBlk);
        // Return the block parameter value instead of the original length.
        // This ensures the value is properly defined in contBlk for SSA.
        return lowerer_.builder->blockParam(*contBlk, 0);
    }

    return length;
}

/// @brief Lower BASIC @c DIM declarations into runtime allocations.
///
/// @details Iterates the declared arrays, evaluates bounds with
///          @ref emitArrayLengthCheck, and emits runtime helper calls to allocate
///          the storage.  Newly allocated arrays are stored into their target
///          slots with retain bookkeeping configured so later scope exits release
///          the memory.
///
/// @param stmt Parsed @c DIM statement to lower.
void RuntimeStatementLowerer::lowerDim(const DimStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);

    // Collect dimension expressions (backward compat: check 'size' first, then 'dimensions')
    std::vector<const ExprPtr *> dimExprs;
    if (stmt.size)
    {
        dimExprs.push_back(&stmt.size);
    }
    else
    {
        for (const auto &dimExpr : stmt.dimensions)
        {
            if (dimExpr)
                dimExprs.push_back(&dimExpr);
        }
    }
    assert(!dimExprs.empty() && "DIM array must have at least one dimension");

    // For single-dimensional arrays, use the dimension directly
    Value length;
    if (dimExprs.size() == 1)
    {
        Lowerer::RVal bound = lowerer_.lowerExpr(**dimExprs[0]);
        bound = lowerer_.ensureI64(std::move(bound), stmt.loc);
        length = emitArrayLengthCheck(bound.value, stmt.loc, "dim_len");
    }
    else
    {
        // For multi-dimensional arrays, compute total size = product of all extents
        // Start with first dimension
        Lowerer::RVal bound = lowerer_.lowerExpr(**dimExprs[0]);
        bound = lowerer_.ensureI64(std::move(bound), stmt.loc);
        Value firstLen = emitArrayLengthCheck(bound.value, stmt.loc, "dim_len");

        // Multiply by remaining dimensions
        Value totalSize = firstLen;
        for (size_t i = 1; i < dimExprs.size(); ++i)
        {
            Lowerer::RVal dimBound = lowerer_.lowerExpr(**dimExprs[i]);
            dimBound = lowerer_.ensureI64(std::move(dimBound), stmt.loc);
            Value dimLen = emitArrayLengthCheck(dimBound.value, stmt.loc, "dim_len");
            totalSize = lowerer_.emitBinary(
                Opcode::IMulOvf, il::core::Type(il::core::Type::Kind::I64), totalSize, dimLen);
        }
        length = totalSize;
    }

    const auto *info = lowerer_.findSymbol(stmt.name);
    // Determine array element type and call appropriate runtime allocator
    Value handle;
    if (info->type == AstType::Str)
    {
        // String array: use rt_arr_str_alloc
        lowerer_.requireArrayStrAlloc();
        handle = lowerer_.emitCallRet(
            il::core::Type(il::core::Type::Kind::Ptr), "rt_arr_str_alloc", {length});
    }
    else if (info->isObject)
    {
        // Object array
        lowerer_.requireArrayObjNew();
        handle = lowerer_.emitCallRet(
            il::core::Type(il::core::Type::Kind::Ptr), "rt_arr_obj_new", {length});
    }
    else
    {
        // Integer/numeric array: use rt_arr_i32_new
        lowerer_.requireArrayI32New();
        handle = lowerer_.emitCallRet(
            il::core::Type(il::core::Type::Kind::Ptr), "rt_arr_i32_new", {length});
    }

    // Store into the resolved storage (supports module-level globals across procedures)
    if (auto storage = lowerer_.resolveVariableStorage(stmt.name, stmt.loc))
    {
        lowerer_.storeArray(
            storage->pointer, handle, info ? info->type : AstType::I64, info && info->isObject);
    }
    else
    {
        // Avoid hard assertions in production builds; emit a trap so the
        // failure is observable without terminating the entire test suite.
        lowerer_.emitTrap();
    }
    if (lowerer_.boundsChecks)
    {
        if (info && info->arrayLengthSlot)
            lowerer_.emitStore(il::core::Type(il::core::Type::Kind::I64),
                               Value::temp(*info->arrayLengthSlot),
                               length);
    }
}

/// @brief Lower BASIC @c REDIM statements that resize dynamic arrays.
///
/// @details Reuses @ref emitArrayLengthCheck for bounds validation, requests the
///          runtime helpers that implement preserving or non-preserving reallocation,
///          and updates the stored array handle while releasing the previous one
///          to prevent leaks.
///
/// @param stmt Parsed @c REDIM statement describing the new bounds.
void RuntimeStatementLowerer::lowerReDim(const ReDimStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);
    Lowerer::RVal bound = lowerer_.lowerExpr(*stmt.size);
    bound = lowerer_.ensureI64(std::move(bound), stmt.loc);
    Value length = emitArrayLengthCheck(bound.value, stmt.loc, "redim_len");
    const auto *info = lowerer_.findSymbol(stmt.name);
    auto storage = lowerer_.resolveVariableStorage(stmt.name, stmt.loc);
    assert(storage && "REDIM target should have resolvable storage");
    Value current = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr), storage->pointer);
    if (info && info->isObject)
        lowerer_.requireArrayObjResize();
    else
        lowerer_.requireArrayI32Resize();
    Value resized =
        lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::Ptr),
                             (info && info->isObject) ? "rt_arr_obj_resize" : "rt_arr_i32_resize",
                             {current, length});
    lowerer_.storeArray(storage->pointer,
                        resized,
                        /*elementType*/ AstType::I64,
                        /*isObjectArray*/ info && info->isObject);
    if (lowerer_.boundsChecks && info && info->arrayLengthSlot)
        lowerer_.emitStore(
            il::core::Type(il::core::Type::Kind::I64), Value::temp(*info->arrayLengthSlot), length);
}

/// @brief Lower the BASIC @c RANDOMIZE statement configuring the RNG seed.
///
/// @details Requests the runtime feature that exposes the random subsystem,
///          evaluates the optional seed expression (defaulting to zero), and
///          invokes the helper that applies the seed.
///
/// @param stmt Parsed @c RANDOMIZE statement.
void RuntimeStatementLowerer::lowerRandomize(const RandomizeStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);
    Lowerer::RVal s = lowerer_.lowerExpr(*stmt.seed);
    Value seed = lowerer_.coerceToI64(std::move(s), stmt.loc).value;
    lowerer_.emitCall("rt_randomize_i64", {seed});
}

/// @brief Lower a @c SWAP statement to exchange two lvalue contents.
///
/// @details Emits IL instructions to: (1) load both lvalues, (2) store first
///          value into second location, (3) store second value into first location.
///          Uses a temporary slot to hold the first value during the exchange.
///
/// @param stmt Parsed @c SWAP statement.
void RuntimeStatementLowerer::lowerSwap(const SwapStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);

    // Lower both lvalues to get their RVals
    Lowerer::RVal lhsVal = lowerer_.lowerExpr(*stmt.lhs);
    Lowerer::RVal rhsVal = lowerer_.lowerExpr(*stmt.rhs);

    // Store lhs value to temp
    Value tempSlot = lowerer_.emitAlloca(8);
    lowerer_.emitStore(lhsVal.type, tempSlot, lhsVal.value);

    // Assign rhs to lhs
    if (auto *var = as<const VarExpr>(*stmt.lhs))
    {
        auto storage = lowerer_.resolveVariableStorage(var->name, stmt.loc);
        if (storage)
        {
            assignScalarSlot(storage->slotInfo, storage->pointer, rhsVal, stmt.loc);
        }
    }
    else if (auto *arr = as<const ArrayExpr>(*stmt.lhs))
    {
        assignArrayElement(*arr, rhsVal, stmt.loc);
    }

    // Assign temp to rhs
    Value tempVal = lowerer_.emitLoad(lhsVal.type, tempSlot);
    Lowerer::RVal tempRVal{tempVal, lhsVal.type};
    if (auto *var = as<const VarExpr>(*stmt.rhs))
    {
        auto storage = lowerer_.resolveVariableStorage(var->name, stmt.loc);
        if (storage)
        {
            assignScalarSlot(storage->slotInfo, storage->pointer, tempRVal, stmt.loc);
        }
    }
    else if (auto *arr = as<const ArrayExpr>(*stmt.rhs))
    {
        assignArrayElement(*arr, tempRVal, stmt.loc);
    }
}

} // namespace il::frontends::basic
