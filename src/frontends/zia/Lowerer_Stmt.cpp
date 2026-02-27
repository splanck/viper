//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Stmt.cpp
/// @brief Statement lowering for the Zia IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "frontends/zia/ZiaLocationScope.hpp"

namespace il::frontends::zia
{

using namespace runtime;

//=============================================================================
// Statement Lowering
//=============================================================================

void Lowerer::lowerStmt(Stmt *stmt)
{
    if (!stmt)
        return;

    if (++stmtLowerDepth_ > kMaxLowerDepth)
    {
        --stmtLowerDepth_;
        if (stmt)
            diag_.report({il::support::Severity::Error,
                          "statement nesting too deep during lowering (limit: 512)",
                          stmt->loc, "V3201"});
        return;
    }
    struct DepthGuard { unsigned &d; ~DepthGuard() { --d; } } stmtGuard_{stmtLowerDepth_};

    ZiaLocationScope locScope(*this, stmt->loc);

    switch (stmt->kind)
    {
        case StmtKind::Block:
            lowerBlockStmt(static_cast<BlockStmt *>(stmt));
            break;
        case StmtKind::Expr:
            lowerExprStmt(static_cast<ExprStmt *>(stmt));
            break;
        case StmtKind::Var:
            lowerVarStmt(static_cast<VarStmt *>(stmt));
            break;
        case StmtKind::If:
            lowerIfStmt(static_cast<IfStmt *>(stmt));
            break;
        case StmtKind::While:
            lowerWhileStmt(static_cast<WhileStmt *>(stmt));
            break;
        case StmtKind::For:
            lowerForStmt(static_cast<ForStmt *>(stmt));
            break;
        case StmtKind::ForIn:
            lowerForInStmt(static_cast<ForInStmt *>(stmt));
            break;
        case StmtKind::Return:
            lowerReturnStmt(static_cast<ReturnStmt *>(stmt));
            break;
        case StmtKind::Break:
            lowerBreakStmt(static_cast<BreakStmt *>(stmt));
            break;
        case StmtKind::Continue:
            lowerContinueStmt(static_cast<ContinueStmt *>(stmt));
            break;
        case StmtKind::Guard:
            lowerGuardStmt(static_cast<GuardStmt *>(stmt));
            break;
        case StmtKind::Match:
            lowerMatchStmt(static_cast<MatchStmt *>(stmt));
            break;
        case StmtKind::Try:
            lowerTryStmt(static_cast<TryStmt *>(stmt));
            break;
        case StmtKind::Throw:
            lowerThrowStmt(static_cast<ThrowStmt *>(stmt));
            break;
    }

    // Release any deferred temporaries from this statement.
    // Temps consumed by stores or returns have already been removed.
    releaseDeferredTemps();
}

void Lowerer::lowerBlockStmt(BlockStmt *stmt)
{
    for (auto &s : stmt->statements)
    {
        lowerStmt(s.get());
    }
}

void Lowerer::lowerExprStmt(ExprStmt *stmt)
{
    lowerExpr(stmt->expr.get());
}

void Lowerer::lowerVarStmt(VarStmt *stmt)
{
    Value initValue;
    Type ilType;
    TypeRef varType =
        stmt->type ? sema_.resolveType(stmt->type.get())
                   : (stmt->initializer ? sema_.typeOf(stmt->initializer.get()) : types::unknown());

    if (stmt->initializer)
    {
        auto result = lowerExpr(stmt->initializer.get());
        initValue = result.value;
        ilType = result.type;

        // In generic contexts, semantic types may be unknown because generic
        // function bodies aren't fully analyzed. Use the lowered expression type.
        if (!stmt->type && (!varType || varType->kind == TypeKindSem::Unknown))
        {
            varType = reverseMapType(ilType);
        }

        // Handle integer-to-number conversion when declaring Number with Integer initializer
        if (varType && varType->kind == TypeKindSem::Number && ilType.kind == Type::Kind::I64)
        {
            // Convert i64 to f64 using sitofp
            initValue = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), initValue);
            ilType = Type(Type::Kind::F64);
        }

        // Handle value type copy semantics - deep copy on assignment
        TypeRef initType = sema_.typeOf(stmt->initializer.get());
        if (initType && initType->kind == TypeKindSem::Value)
        {
            const ValueTypeInfo *info = getOrCreateValueTypeInfo(initType->name);
            if (info)
            {
                // Deep copy the value type
                initValue = emitValueTypeCopy(*info, initValue);
            }
        }

        if (varType && varType->kind == TypeKindSem::Optional)
        {
            TypeRef optInitType = sema_.typeOf(stmt->initializer.get());
            TypeRef innerType = varType->innerType();
            Type optILType = mapType(varType);
            if (optInitType && optInitType->kind == TypeKindSem::Optional)
            {
                ilType = optILType;
            }
            else if (optInitType && optInitType->kind == TypeKindSem::Unit)
            {
                initValue = Value::null();
                ilType = optILType;
            }
            else if (innerType)
            {
                initValue = emitOptionalWrap(initValue, innerType);
                ilType = optILType;
            }
        }
    }
    else
    {
        // Default initialization
        ilType = mapType(varType);

        // Special handling for value types - allocate proper stack space
        if (varType && varType->kind == TypeKindSem::Value)
        {
            const ValueTypeInfo *info = getOrCreateValueTypeInfo(varType->name);
            if (info)
            {
                // Allocate and zero-initialize the value type
                initValue = emitValueTypeAlloc(*info);
            }
            else
            {
                // Fallback if type info not found
                initValue = Value::null();
            }
        }
        else
        {
            switch (ilType.kind)
            {
                case Type::Kind::I64:
                case Type::Kind::I32:
                case Type::Kind::I16:
                case Type::Kind::I1:
                    initValue = Value::constInt(0);
                    break;
                case Type::Kind::F64:
                    initValue = Value::constFloat(0.0);
                    break;
                case Type::Kind::Str:
                    initValue = Value::constStr("");
                    break;
                case Type::Kind::Ptr:
                    initValue = Value::null();
                    break;
                default:
                    initValue = Value::constInt(0);
                    break;
            }
        }
    }

    // Use slot-based storage for all mutable variables (enables cross-block SSA)
    if (!stmt->isFinal)
    {
        createSlot(stmt->name, ilType);
        storeToSlot(stmt->name, initValue, ilType);
        // The init value is consumed by the slot — don't release it at statement boundary
        consumeDeferred(initValue);
    }
    else
    {
        // Final/immutable variables can use direct SSA values
        defineLocal(stmt->name, initValue);
        // The init value is consumed by the local — don't release it at statement boundary
        consumeDeferred(initValue);
    }

    if (varType)
    {
        localTypes_[stmt->name] = varType;
    }
}

void Lowerer::lowerIfStmt(IfStmt *stmt)
{
    size_t thenIdx = createBlock("if_then");
    size_t elseIdx = stmt->elseBranch ? createBlock("if_else") : 0;
    size_t mergeIdx = createBlock("if_end");

    // Lower condition
    auto cond = lowerExpr(stmt->condition.get());

    // Release condition temps before branch (SSA: temps are scoped to this block)
    releaseDeferredTemps();

    // Emit branch
    if (stmt->elseBranch)
    {
        emitCBr(cond.value, thenIdx, elseIdx);
    }
    else
    {
        emitCBr(cond.value, thenIdx, mergeIdx);
    }

    // Lower then branch
    setBlock(thenIdx);
    lowerStmt(stmt->thenBranch.get());
    if (!isTerminated())
    {
        emitBr(mergeIdx);
    }

    // Lower else branch
    if (stmt->elseBranch)
    {
        setBlock(elseIdx);
        lowerStmt(stmt->elseBranch.get());
        if (!isTerminated())
        {
            emitBr(mergeIdx);
        }
    }

    setBlock(mergeIdx);
}

void Lowerer::lowerWhileStmt(WhileStmt *stmt)
{
    size_t condIdx = createBlock("while_cond");
    size_t bodyIdx = createBlock("while_body");
    size_t endIdx = createBlock("while_end");

    // Push loop context
    loopStack_.push(endIdx, condIdx);

    // Branch to condition
    emitBr(condIdx);

    // Lower condition
    setBlock(condIdx);
    auto cond = lowerExpr(stmt->condition.get());
    releaseDeferredTemps(); // Release condition temps before branch
    emitCBr(cond.value, bodyIdx, endIdx);

    // Lower body
    setBlock(bodyIdx);
    lowerStmt(stmt->body.get());
    if (!isTerminated())
    {
        emitBr(condIdx);
    }

    // Pop loop context
    loopStack_.pop();

    setBlock(endIdx);
}

void Lowerer::lowerForStmt(ForStmt *stmt)
{
    size_t condIdx = createBlock("for_cond");
    size_t bodyIdx = createBlock("for_body");
    size_t updateIdx = createBlock("for_update");
    size_t endIdx = createBlock("for_end");

    // Push loop context
    loopStack_.push(endIdx, updateIdx);

    // Lower init
    if (stmt->init)
    {
        lowerStmt(stmt->init.get());
    }

    // Branch to condition
    emitBr(condIdx);

    // Lower condition
    setBlock(condIdx);
    if (stmt->condition)
    {
        auto cond = lowerExpr(stmt->condition.get());
        releaseDeferredTemps(); // Release condition temps before branch
        emitCBr(cond.value, bodyIdx, endIdx);
    }
    else
    {
        emitBr(bodyIdx);
    }

    // Lower body
    setBlock(bodyIdx);
    lowerStmt(stmt->body.get());
    if (!isTerminated())
    {
        emitBr(updateIdx);
    }

    // Lower update
    setBlock(updateIdx);
    if (stmt->update)
    {
        lowerExpr(stmt->update.get());
    }
    emitBr(condIdx);

    // Pop loop context
    loopStack_.pop();

    setBlock(endIdx);
}

void Lowerer::lowerForInStmt(ForInStmt *stmt)
{
    auto localsBackup = locals_;
    auto slotsBackup = slots_;
    auto localTypesBackup = localTypes_;

    // For now, only support range iteration
    auto *rangeExpr = dynamic_cast<RangeExpr *>(stmt->iterable.get());
    if (rangeExpr)
    {
        size_t condIdx = createBlock("forin_cond");
        size_t bodyIdx = createBlock("forin_body");
        size_t updateIdx = createBlock("forin_update");
        size_t endIdx = createBlock("forin_end");

        loopStack_.push(endIdx, updateIdx);

        // Lower range bounds
        auto startResult = lowerExpr(rangeExpr->start.get());
        auto endResult = lowerExpr(rangeExpr->end.get());

        // Create slot-based loop variable (alloca + initial store)
        // This enables proper SSA across basic block boundaries
        createSlot(stmt->variable, Type(Type::Kind::I64));
        storeToSlot(stmt->variable, startResult.value, Type(Type::Kind::I64));
        localTypes_[stmt->variable] = types::integer();

        // Also store the end value in a slot so it's available in other blocks
        std::string endVar = stmt->variable + "_end";
        createSlot(endVar, Type(Type::Kind::I64));
        storeToSlot(endVar, endResult.value, Type(Type::Kind::I64));

        // Branch to condition
        emitBr(condIdx);

        // Condition: i < end (or <= for inclusive)
        setBlock(condIdx);
        Value loopVar = loadFromSlot(stmt->variable, Type(Type::Kind::I64));
        Value endVal = loadFromSlot(endVar, Type(Type::Kind::I64));
        Value cond;
        if (rangeExpr->inclusive)
        {
            cond = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), loopVar, endVal);
        }
        else
        {
            cond = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), loopVar, endVal);
        }
        emitCBr(cond, bodyIdx, endIdx);

        // Body
        setBlock(bodyIdx);
        lowerStmt(stmt->body.get());
        if (!isTerminated())
        {
            emitBr(updateIdx);
        }

        // Update: i = i + 1
        setBlock(updateIdx);
        Value currentVal = loadFromSlot(stmt->variable, Type(Type::Kind::I64));
        Opcode addOp = options_.overflowChecks ? Opcode::IAddOvf : Opcode::Add;
        Value nextVal = emitBinary(addOp, Type(Type::Kind::I64), currentVal, Value::constInt(1));
        storeToSlot(stmt->variable, nextVal, Type(Type::Kind::I64));
        emitBr(condIdx);

        loopStack_.pop();
        setBlock(endIdx);

        // Clean up slots
        removeSlot(stmt->variable);
        removeSlot(endVar);
        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);
        return;
    }

    TypeRef iterableType = sema_.typeOf(stmt->iterable.get());
    if (!iterableType)
    {
        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);
        return;
    }

    // Tuple destructuring over a tuple value (single iteration)
    if (stmt->isTuple && iterableType->kind == TypeKindSem::Tuple)
    {
        const auto &elements = iterableType->tupleElementTypes();
        if (elements.size() == 2)
        {
            TypeRef firstType = elements[0];
            TypeRef secondType = elements[1];
            if (stmt->variableType)
                firstType = sema_.resolveType(stmt->variableType.get());
            if (stmt->secondVariableType)
                secondType = sema_.resolveType(stmt->secondVariableType.get());

            Type firstIl = mapType(firstType);
            Type secondIl = mapType(secondType);

            createSlot(stmt->variable, firstIl);
            createSlot(stmt->secondVariable, secondIl);
            localTypes_[stmt->variable] = firstType;
            localTypes_[stmt->secondVariable] = secondType;

            size_t bodyIdx = createBlock("forin_tuple_body");
            size_t endIdx = createBlock("forin_tuple_end");

            loopStack_.push(endIdx, endIdx);
            emitBr(bodyIdx);
            setBlock(bodyIdx);

            PatternValue tupleValue{lowerExpr(stmt->iterable.get()).value, iterableType};
            PatternValue firstVal = emitTupleElement(tupleValue, 0, firstType);
            PatternValue secondVal = emitTupleElement(tupleValue, 1, secondType);

            storeToSlot(stmt->variable, firstVal.value, firstIl);
            storeToSlot(stmt->secondVariable, secondVal.value, secondIl);

            lowerStmt(stmt->body.get());
            if (!isTerminated())
            {
                emitBr(endIdx);
            }

            loopStack_.pop();
            setBlock(endIdx);
        }

        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);
        return;
    }

    // Collection iteration (List/Map)
    if (iterableType->kind == TypeKindSem::List)
    {
        TypeRef elemType = iterableType->elementType();
        if (stmt->variableType)
            elemType = sema_.resolveType(stmt->variableType.get());

        Type elemIlType = mapType(elemType);

        // For tuple binding (for idx, val in list), first var is index, second is element
        // For single binding (for val in list), the variable is the element
        bool hasTupleBinding = stmt->isTuple && !stmt->secondVariable.empty();

        if (hasTupleBinding)
        {
            // First variable is the index
            createSlot(stmt->variable, Type(Type::Kind::I64));
            localTypes_[stmt->variable] = types::integer();
            // Second variable is the element
            createSlot(stmt->secondVariable, elemIlType);
            localTypes_[stmt->secondVariable] = elemType;
        }
        else
        {
            createSlot(stmt->variable, elemIlType);
            localTypes_[stmt->variable] = elemType;
        }

        auto listValue = lowerExpr(stmt->iterable.get());

        std::string indexVar = "__forin_idx_" + std::to_string(nextTempId());
        std::string lenVar = "__forin_len_" + std::to_string(nextTempId());
        std::string listVar = "__forin_list_" + std::to_string(nextTempId());

        createSlot(indexVar, Type(Type::Kind::I64));
        createSlot(lenVar, Type(Type::Kind::I64));
        createSlot(listVar, Type(Type::Kind::Ptr));
        storeToSlot(indexVar, Value::constInt(0), Type(Type::Kind::I64));
        storeToSlot(listVar, listValue.value, Type(Type::Kind::Ptr));
        Value lenVal = emitCallRet(Type(Type::Kind::I64), kListCount, {listValue.value});
        storeToSlot(lenVar, lenVal, Type(Type::Kind::I64));

        size_t condIdx = createBlock("forin_list_cond");
        size_t bodyIdx = createBlock("forin_list_body");
        size_t updateIdx = createBlock("forin_list_update");
        size_t endIdx = createBlock("forin_list_end");

        loopStack_.push(endIdx, updateIdx);
        emitBr(condIdx);

        setBlock(condIdx);
        Value idxVal = loadFromSlot(indexVar, Type(Type::Kind::I64));
        Value lenLoaded = loadFromSlot(lenVar, Type(Type::Kind::I64));
        Value cond = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), idxVal, lenLoaded);
        emitCBr(cond, bodyIdx, endIdx);

        setBlock(bodyIdx);
        Value listLoaded = loadFromSlot(listVar, Type(Type::Kind::Ptr));
        Value idxInBody = loadFromSlot(indexVar, Type(Type::Kind::I64));

        if (hasTupleBinding)
        {
            // Store index in first variable
            storeToSlot(stmt->variable, idxInBody, Type(Type::Kind::I64));
            // Get and store element in second variable
            Value boxed = emitCallRet(Type(Type::Kind::Ptr), kListGet, {listLoaded, idxInBody});
            auto elemValue = emitUnboxValue(boxed, elemIlType, elemType);
            storeToSlot(stmt->secondVariable, elemValue.value, elemIlType);
        }
        else
        {
            Value boxed = emitCallRet(Type(Type::Kind::Ptr), kListGet, {listLoaded, idxInBody});
            auto elemValue = emitUnboxValue(boxed, elemIlType, elemType);
            storeToSlot(stmt->variable, elemValue.value, elemIlType);
        }

        lowerStmt(stmt->body.get());
        if (!isTerminated())
        {
            emitBr(updateIdx);
        }

        setBlock(updateIdx);
        Value idxCurrent = loadFromSlot(indexVar, Type(Type::Kind::I64));
        Opcode addOp = options_.overflowChecks ? Opcode::IAddOvf : Opcode::Add;
        Value idxNext = emitBinary(addOp, Type(Type::Kind::I64), idxCurrent, Value::constInt(1));
        storeToSlot(indexVar, idxNext, Type(Type::Kind::I64));
        emitBr(condIdx);

        loopStack_.pop();
        setBlock(endIdx);

        removeSlot(stmt->variable);
        if (hasTupleBinding)
            removeSlot(stmt->secondVariable);
        removeSlot(indexVar);
        removeSlot(lenVar);
        removeSlot(listVar);

        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);
        return;
    }

    if (iterableType->kind == TypeKindSem::Map)
    {
        TypeRef keyType = iterableType->keyType() ? iterableType->keyType() : types::string();
        TypeRef valueType =
            iterableType->valueType() ? iterableType->valueType() : types::unknown();
        if (stmt->variableType)
            keyType = sema_.resolveType(stmt->variableType.get());
        if (stmt->isTuple && stmt->secondVariableType)
            valueType = sema_.resolveType(stmt->secondVariableType.get());

        Type keyIlType = mapType(keyType);
        Type valueIlType = mapType(valueType);

        createSlot(stmt->variable, keyIlType);
        localTypes_[stmt->variable] = keyType;

        if (stmt->isTuple)
        {
            createSlot(stmt->secondVariable, valueIlType);
            localTypes_[stmt->secondVariable] = valueType;
        }

        auto mapValue = lowerExpr(stmt->iterable.get());
        Value keysSeq = emitCallRet(Type(Type::Kind::Ptr), kMapKeys, {mapValue.value});

        std::string indexVar = "__forin_idx_" + std::to_string(nextTempId());
        std::string lenVar = "__forin_len_" + std::to_string(nextTempId());
        std::string keysVar = "__forin_keys_" + std::to_string(nextTempId());
        std::string mapVar = "__forin_map_" + std::to_string(nextTempId());

        createSlot(indexVar, Type(Type::Kind::I64));
        createSlot(lenVar, Type(Type::Kind::I64));
        createSlot(keysVar, Type(Type::Kind::Ptr));
        createSlot(mapVar, Type(Type::Kind::Ptr));
        storeToSlot(indexVar, Value::constInt(0), Type(Type::Kind::I64));
        storeToSlot(keysVar, keysSeq, Type(Type::Kind::Ptr));
        storeToSlot(mapVar, mapValue.value, Type(Type::Kind::Ptr));
        Value lenVal = emitCallRet(Type(Type::Kind::I64), kSeqLen, {keysSeq});
        storeToSlot(lenVar, lenVal, Type(Type::Kind::I64));

        size_t condIdx = createBlock("forin_map_cond");
        size_t bodyIdx = createBlock("forin_map_body");
        size_t updateIdx = createBlock("forin_map_update");
        size_t endIdx = createBlock("forin_map_end");

        loopStack_.push(endIdx, updateIdx);
        emitBr(condIdx);

        setBlock(condIdx);
        Value idxVal = loadFromSlot(indexVar, Type(Type::Kind::I64));
        Value lenLoaded = loadFromSlot(lenVar, Type(Type::Kind::I64));
        Value cond = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), idxVal, lenLoaded);
        emitCBr(cond, bodyIdx, endIdx);

        setBlock(bodyIdx);
        // Load keys sequence and index from slot for cross-block SSA
        Value keysLoaded = loadFromSlot(keysVar, Type(Type::Kind::Ptr));
        Value idxInBody = loadFromSlot(indexVar, Type(Type::Kind::I64));
        // Map keys are always strings stored as raw rt_string pointers in the seq
        // (rt_map_keys pushes raw rt_string, not boxed rt_box_t). Use kSeqGetStr.
        Value keyStrVal = emitCallRet(Type(Type::Kind::Str), kSeqGetStr, {keysLoaded, idxInBody});
        LowerResult keyVal = {keyStrVal, Type(Type::Kind::Str)};
        storeToSlot(stmt->variable, keyVal.value, keyIlType);

        if (stmt->isTuple)
        {
            // Load map from slot for cross-block SSA
            Value mapLoaded = loadFromSlot(mapVar, Type(Type::Kind::Ptr));
            Value boxed = emitCallRet(Type(Type::Kind::Ptr), kMapGet, {mapLoaded, keyVal.value});
            auto unboxed = emitUnboxValue(boxed, valueIlType, valueType);
            storeToSlot(stmt->secondVariable, unboxed.value, valueIlType);
        }

        lowerStmt(stmt->body.get());
        if (!isTerminated())
        {
            emitBr(updateIdx);
        }

        setBlock(updateIdx);
        Value idxCurrent = loadFromSlot(indexVar, Type(Type::Kind::I64));
        Opcode addOp = options_.overflowChecks ? Opcode::IAddOvf : Opcode::Add;
        Value idxNext = emitBinary(addOp, Type(Type::Kind::I64), idxCurrent, Value::constInt(1));
        storeToSlot(indexVar, idxNext, Type(Type::Kind::I64));
        emitBr(condIdx);

        loopStack_.pop();
        setBlock(endIdx);

        removeSlot(stmt->variable);
        if (stmt->isTuple)
            removeSlot(stmt->secondVariable);
        removeSlot(indexVar);
        removeSlot(lenVar);
        removeSlot(keysVar);
        removeSlot(mapVar);

        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);
        return;
    }

    // Seq iteration: typed rt_seq result from seq<T>-annotated runtime functions.
    // Uses kSeqLen / kSeqGet (not kListCount / kListGet) since rt_seq and rt_list
    // have incompatible internal layouts.
    if (iterableType->kind == TypeKindSem::Ptr && iterableType->name == "Viper.Collections.Seq" &&
        !iterableType->typeArgs.empty())
    {
        TypeRef elemType = iterableType->typeArgs[0];
        if (stmt->variableType)
            elemType = sema_.resolveType(stmt->variableType.get());

        Type elemIlType = mapType(elemType);

        createSlot(stmt->variable, elemIlType);
        localTypes_[stmt->variable] = elemType;

        auto seqValue = lowerExpr(stmt->iterable.get());

        std::string indexVar = "__forin_idx_" + std::to_string(nextTempId());
        std::string lenVar = "__forin_len_" + std::to_string(nextTempId());
        std::string seqVar = "__forin_seq_" + std::to_string(nextTempId());

        createSlot(indexVar, Type(Type::Kind::I64));
        createSlot(lenVar, Type(Type::Kind::I64));
        createSlot(seqVar, Type(Type::Kind::Ptr));
        storeToSlot(indexVar, Value::constInt(0), Type(Type::Kind::I64));
        storeToSlot(seqVar, seqValue.value, Type(Type::Kind::Ptr));
        Value lenVal = emitCallRet(Type(Type::Kind::I64), kSeqLen, {seqValue.value});
        storeToSlot(lenVar, lenVal, Type(Type::Kind::I64));

        size_t condIdx = createBlock("forin_seq_cond");
        size_t bodyIdx = createBlock("forin_seq_body");
        size_t updateIdx = createBlock("forin_seq_update");
        size_t endIdx = createBlock("forin_seq_end");

        loopStack_.push(endIdx, updateIdx);
        emitBr(condIdx);

        setBlock(condIdx);
        Value idxVal = loadFromSlot(indexVar, Type(Type::Kind::I64));
        Value lenLoaded = loadFromSlot(lenVar, Type(Type::Kind::I64));
        Value cond = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), idxVal, lenLoaded);
        emitCBr(cond, bodyIdx, endIdx);

        setBlock(bodyIdx);
        Value seqLoaded = loadFromSlot(seqVar, Type(Type::Kind::Ptr));
        Value idxInBody = loadFromSlot(indexVar, Type(Type::Kind::I64));
        // seq<str> sequences store raw rt_string pointers directly (not boxed).
        // Use kSeqGetStr which reinterprets void* as rt_string, avoiding rt_unbox_str.
        // For non-string element types, kSeqGet returns a boxed Ptr that needs unboxing.
        if (elemIlType.kind == Type::Kind::Str)
        {
            Value elem = emitCallRet(Type(Type::Kind::Str), kSeqGetStr, {seqLoaded, idxInBody});
            storeToSlot(stmt->variable, elem, Type(Type::Kind::Str));
        }
        else
        {
            Value boxed = emitCallRet(Type(Type::Kind::Ptr), kSeqGet, {seqLoaded, idxInBody});
            auto elemValue = emitUnbox(boxed, elemIlType);
            storeToSlot(stmt->variable, elemValue.value, elemIlType);
        }

        lowerStmt(stmt->body.get());
        if (!isTerminated())
        {
            emitBr(updateIdx);
        }

        setBlock(updateIdx);
        Value idxCurrent = loadFromSlot(indexVar, Type(Type::Kind::I64));
        Opcode addOp = options_.overflowChecks ? Opcode::IAddOvf : Opcode::Add;
        Value idxNext = emitBinary(addOp, Type(Type::Kind::I64), idxCurrent, Value::constInt(1));
        storeToSlot(indexVar, idxNext, Type(Type::Kind::I64));
        emitBr(condIdx);

        loopStack_.pop();
        setBlock(endIdx);

        removeSlot(stmt->variable);
        removeSlot(indexVar);
        removeSlot(lenVar);
        removeSlot(seqVar);

        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);
        return;
    }

    locals_ = std::move(localsBackup);
    slots_ = std::move(slotsBackup);
    localTypes_ = std::move(localTypesBackup);
}

void Lowerer::lowerReturnStmt(ReturnStmt *stmt)
{
    if (stmt->value)
    {
        auto result = lowerExpr(stmt->value.get());
        Value returnValue = result.value;

        // Handle Number -> Integer implicit conversion for return statements
        // This allows returning Viper.Math.Floor() etc. from Integer-returning functions
        if (currentReturnType_ && currentReturnType_->kind == TypeKindSem::Integer)
        {
            TypeRef valueType = sema_.typeOf(stmt->value.get());
            if (valueType && valueType->kind == TypeKindSem::Number)
            {
                // Emit cast.fp_to_si.rte.chk to convert f64 -> i64 (rounds-to-nearest-even,
                // overflow-checked)
                unsigned convId = nextTempId();
                il::core::Instr convInstr;
                convInstr.result = convId;
                convInstr.op = Opcode::CastFpToSiRteChk;
                convInstr.type = Type(Type::Kind::I64);
                convInstr.operands = {returnValue};
                convInstr.loc = curLoc_;
                blockMgr_.currentBlock()->instructions.push_back(convInstr);
                returnValue = Value::temp(convId);
            }
            else if (result.type.kind == Type::Kind::Ptr)
            {
                // Unbox a boxed obj (e.g., from untyped List.Get()) when returning as Integer.
                // This occurs when an untyped List holds integers: the runtime boxes them as heap
                // objects, so List.Get() returns Ptr. The return statement must unbox to i64.
                auto unboxed = emitUnbox(returnValue, Type(Type::Kind::I64));
                returnValue = unboxed.value;
            }
        }

        // Handle Integer -> Number implicit conversion for return statements
        // This allows returning integer literals/expressions from Number-returning functions
        if (currentReturnType_ && currentReturnType_->kind == TypeKindSem::Number)
        {
            TypeRef valueType = sema_.typeOf(stmt->value.get());
            if (valueType && valueType->kind == TypeKindSem::Integer)
            {
                // Emit sitofp to convert i64 -> f64
                unsigned convId = nextTempId();
                il::core::Instr convInstr;
                convInstr.result = convId;
                convInstr.op = Opcode::Sitofp;
                convInstr.type = Type(Type::Kind::F64);
                convInstr.operands = {returnValue};
                convInstr.loc = curLoc_;
                blockMgr_.currentBlock()->instructions.push_back(convInstr);
                returnValue = Value::temp(convId);
            }
        }

        if (currentReturnType_ && currentReturnType_->kind == TypeKindSem::Optional)
        {
            TypeRef valueType = sema_.typeOf(stmt->value.get());
            if (!valueType || valueType->kind != TypeKindSem::Optional)
            {
                TypeRef innerType = currentReturnType_->innerType();
                if (innerType)
                    returnValue = emitOptionalWrap(result.value, innerType);
            }
        }

        // The return value is transferred to the caller — don't release it.
        // But release any intermediate temps from evaluating the return expr.
        consumeDeferred(returnValue);
        releaseDeferredTemps();
        emitRet(returnValue);
    }
    else
    {
        releaseDeferredTemps();
        emitRetVoid();
    }
}

void Lowerer::lowerBreakStmt(BreakStmt * /*stmt*/)
{
    if (!loopStack_.empty())
    {
        releaseDeferredTemps(); // Release any pending temps before branch
        emitBr(loopStack_.breakTarget());
    }
}

void Lowerer::lowerContinueStmt(ContinueStmt * /*stmt*/)
{
    if (!loopStack_.empty())
    {
        releaseDeferredTemps(); // Release any pending temps before branch
        emitBr(loopStack_.continueTarget());
    }
}

void Lowerer::lowerGuardStmt(GuardStmt *stmt)
{
    size_t elseIdx = createBlock("guard_else");
    size_t contIdx = createBlock("guard_cont");

    // Lower condition
    auto cond = lowerExpr(stmt->condition.get());

    // Release condition temps before branch (SSA scoping)
    releaseDeferredTemps();

    // If condition is true, continue; else, execute else block
    emitCBr(cond.value, contIdx, elseIdx);

    // Lower else block (must exit)
    setBlock(elseIdx);
    lowerStmt(stmt->elseBlock.get());
    // Else block should have terminator (return, break, continue)

    setBlock(contIdx);
}

void Lowerer::lowerMatchStmt(MatchStmt *stmt)
{
    if (stmt->arms.empty())
        return;

    // Lower the scrutinee once and store in a slot for reuse
    auto scrutinee = lowerExpr(stmt->scrutinee.get());
    std::string scrutineeSlot = "__match_scrutinee";
    createSlot(scrutineeSlot, scrutinee.type);
    storeToSlot(scrutineeSlot, scrutinee.value, scrutinee.type);
    consumeDeferred(scrutinee.value); // Stored to slot — ownership transferred
    TypeRef scrutineeType = sema_.typeOf(stmt->scrutinee.get());

    // Create end block for the match
    size_t endIdx = createBlock("match_end");

    // Create blocks for each arm body and the next arm's test
    std::vector<size_t> armBlocks;
    std::vector<size_t> nextTestBlocks;
    for (size_t i = 0; i < stmt->arms.size(); ++i)
    {
        armBlocks.push_back(createBlock("match_arm_" + std::to_string(i)));
        if (i + 1 < stmt->arms.size())
        {
            nextTestBlocks.push_back(createBlock("match_test_" + std::to_string(i + 1)));
        }
        else
        {
            nextTestBlocks.push_back(endIdx); // Last arm falls through to end
        }
    }

    // Lower each arm
    for (size_t i = 0; i < stmt->arms.size(); ++i)
    {
        const auto &arm = stmt->arms[i];
        auto localsBackup = locals_;
        auto slotsBackup = slots_;
        auto localTypesBackup = localTypes_;

        size_t matchBlock = armBlocks[i];
        size_t guardBlock = 0;
        if (arm.pattern.guard)
        {
            guardBlock = createBlock("match_guard_" + std::to_string(i));
            matchBlock = guardBlock;
        }

        // In the current block, test the pattern
        Value scrutineeVal = loadFromSlot(scrutineeSlot, scrutinee.type);
        PatternValue scrutineeValue{scrutineeVal, scrutineeType};
        releaseDeferredTemps(); // Release temps before pattern branch
        emitPatternTest(arm.pattern, scrutineeValue, matchBlock, nextTestBlocks[i]);

        if (guardBlock)
        {
            setBlock(guardBlock);
            // Reload scrutinee from slot in this block for SSA correctness
            Value scrutineeInGuard = loadFromSlot(scrutineeSlot, scrutinee.type);
            PatternValue scrutineeValueInGuard{scrutineeInGuard, scrutineeType};
            emitPatternBindings(arm.pattern, scrutineeValueInGuard);
            auto guardResult = lowerExpr(arm.pattern.guard.get());
            releaseDeferredTemps(); // Release guard temps before branch
            emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
        }

        // Lower the arm body (arm.body is an expression)
        setBlock(armBlocks[i]);
        if (!guardBlock)
        {
            // Reload scrutinee from slot in this block for SSA correctness
            Value scrutineeInArm = loadFromSlot(scrutineeSlot, scrutinee.type);
            PatternValue scrutineeValueInArm{scrutineeInArm, scrutineeType};
            emitPatternBindings(arm.pattern, scrutineeValueInArm);
        }
        if (arm.body)
        {
            // Check if it's a block expression
            if (auto *blockExpr = dynamic_cast<BlockExpr *>(arm.body.get()))
            {
                // Lower each statement in the block
                for (auto &blockStmt : blockExpr->statements)
                {
                    lowerStmt(blockStmt.get());
                }
            }
            else
            {
                // It's a regular expression - just evaluate it
                lowerExpr(arm.body.get());
            }
        }

        // Jump to end after arm body (if not already terminated)
        if (!isTerminated())
        {
            emitBr(endIdx);
        }

        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);

        // Set up next test block for pattern matching
        if (i + 1 < stmt->arms.size())
        {
            setBlock(nextTestBlocks[i]);
        }
    }

    // Remove the scrutinee slot
    removeSlot(scrutineeSlot);

    // Continue from end block
    setBlock(endIdx);
}


} // namespace il::frontends::zia
