//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Stmt.cpp
/// @brief Statement lowering for the ViperLang IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"
#include "frontends/viperlang/RuntimeNames.hpp"

namespace il::frontends::viperlang
{

using namespace runtime;

//=============================================================================
// Statement Lowering
//=============================================================================

void Lowerer::lowerStmt(Stmt *stmt)
{
    if (!stmt)
        return;

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
    }
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

        // Handle integer-to-number conversion when declaring Number with Integer initializer
        if (varType && varType->kind == TypeKindSem::Number && ilType.kind == Type::Kind::I64)
        {
            // Convert i64 to f64 using sitofp
            initValue = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), initValue);
            ilType = Type(Type::Kind::F64);
        }

        if (varType && varType->kind == TypeKindSem::Optional)
        {
            TypeRef initType = sema_.typeOf(stmt->initializer.get());
            TypeRef innerType = varType->innerType();
            if (initType && initType->kind == TypeKindSem::Optional)
            {
                ilType = Type(Type::Kind::Ptr);
            }
            else if (initType && initType->kind == TypeKindSem::Unit)
            {
                initValue = Value::null();
                ilType = Type(Type::Kind::Ptr);
            }
            else if (innerType)
            {
                initValue = emitOptionalWrap(initValue, innerType);
                ilType = Type(Type::Kind::Ptr);
            }
        }
    }
    else
    {
        // Default initialization
        ilType = mapType(varType);

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

    // Use slot-based storage for all mutable variables (enables cross-block SSA)
    if (!stmt->isFinal)
    {
        createSlot(stmt->name, ilType);
        storeToSlot(stmt->name, initValue, ilType);
    }
    else
    {
        // Final/immutable variables can use direct SSA values
        defineLocal(stmt->name, initValue);
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
        createSlot(stmt->variable, elemIlType);
        localTypes_[stmt->variable] = elemType;

        auto listValue = lowerExpr(stmt->iterable.get());

        std::string indexVar = "__forin_idx_" + std::to_string(nextTempId());
        std::string lenVar = "__forin_len_" + std::to_string(nextTempId());

        createSlot(indexVar, Type(Type::Kind::I64));
        createSlot(lenVar, Type(Type::Kind::I64));
        storeToSlot(indexVar, Value::constInt(0), Type(Type::Kind::I64));
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
        Value boxed = emitCallRet(Type(Type::Kind::Ptr), kListGet, {listValue.value, idxVal});
        auto elemValue = emitUnbox(boxed, elemIlType);
        storeToSlot(stmt->variable, elemValue.value, elemIlType);
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

        createSlot(indexVar, Type(Type::Kind::I64));
        createSlot(lenVar, Type(Type::Kind::I64));
        storeToSlot(indexVar, Value::constInt(0), Type(Type::Kind::I64));
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
        Value keyVal = emitCallRet(keyIlType, kSeqGet, {keysSeq, idxVal});
        storeToSlot(stmt->variable, keyVal, keyIlType);

        if (stmt->isTuple)
        {
            Value boxed = emitCallRet(Type(Type::Kind::Ptr), kMapGet, {mapValue.value, keyVal});
            auto unboxed = emitUnbox(boxed, valueIlType);
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
                // Emit cast.fp_to_si.rte.chk to convert f64 -> i64 (rounds-to-nearest-even, overflow-checked)
                unsigned convId = nextTempId();
                il::core::Instr convInstr;
                convInstr.result = convId;
                convInstr.op = Opcode::CastFpToSiRteChk;
                convInstr.type = Type(Type::Kind::I64);
                convInstr.operands = {returnValue};
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

        emitRet(returnValue);
    }
    else
    {
        emitRetVoid();
    }
}

void Lowerer::lowerBreakStmt(BreakStmt * /*stmt*/)
{
    if (!loopStack_.empty())
    {
        emitBr(loopStack_.breakTarget());
    }
}

void Lowerer::lowerContinueStmt(ContinueStmt * /*stmt*/)
{
    if (!loopStack_.empty())
    {
        emitBr(loopStack_.continueTarget());
    }
}

void Lowerer::lowerGuardStmt(GuardStmt *stmt)
{
    size_t elseIdx = createBlock("guard_else");
    size_t contIdx = createBlock("guard_cont");

    // Lower condition
    auto cond = lowerExpr(stmt->condition.get());

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
        emitPatternTest(arm.pattern, scrutineeValue, matchBlock, nextTestBlocks[i]);

        if (guardBlock)
        {
            setBlock(guardBlock);
            emitPatternBindings(arm.pattern, scrutineeValue);
            auto guardResult = lowerExpr(arm.pattern.guard.get());
            emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
        }

        // Lower the arm body (arm.body is an expression)
        setBlock(armBlocks[i]);
        if (!guardBlock)
            emitPatternBindings(arm.pattern, scrutineeValue);
        if (arm.body)
        {
            // Check if it's a block expression
            if (auto *blockExpr = dynamic_cast<BlockExpr *>(arm.body.get()))
            {
                // Lower each statement in the block
                for (auto &stmt : blockExpr->statements)
                {
                    lowerStmt(stmt.get());
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


} // namespace il::frontends::viperlang
