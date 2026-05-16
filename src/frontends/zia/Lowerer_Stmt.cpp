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
#include <limits>

namespace il::frontends::zia {

using namespace runtime;

namespace {

bool isInlineAggregateType(TypeRef type) {
    return type && (type->kind == TypeKindSem::Struct || type->kind == TypeKindSem::FixedArray ||
                    type->kind == TypeKindSem::Tuple);
}

const char *runtimeCollectionToSeqCallee(TypeRef type) {
    if (!type || type->kind != TypeKindSem::Ptr || !type->elementType())
        return nullptr;
    if (type->name == "Viper.Collections.Queue")
        return "Viper.Collections.Queue.ToSeq";
    if (type->name == "Viper.Collections.Stack")
        return "Viper.Collections.Stack.ToSeq";
    if (type->name == "Viper.Collections.Deque")
        return "Viper.Collections.Deque.ToSeq";
    if (type->name == "Viper.Collections.List")
        return "Viper.Collections.List.ToSeq";
    if (type->name == "Viper.Collections.Ring")
        return "Viper.Collections.Ring.ToSeq";
    if (type->name == "Viper.Collections.Heap")
        return "Viper.Collections.Heap.ToSeq";
    return nullptr;
}

} // namespace

//=============================================================================
// Statement Lowering
//=============================================================================

void Lowerer::lowerStmt(Stmt *stmt) {
    if (!stmt) {
        reportLoweringInvariant({}, "V-ZIA-LOWER-NULL-STMT", "null statement reached lowering");
        return;
    }

    if (++stmtLowerDepth_ > kMaxLowerDepth) {
        --stmtLowerDepth_;
        reportLoweringInvariant(stmt->loc,
                                "V-ZIA-LOWER-DEPTH",
                                "statement nesting too deep during lowering (limit: 512)");
        return;
    }

    struct DepthGuard {
        unsigned &d;

        ~DepthGuard() {
            --d;
        }
    } stmtGuard_{stmtLowerDepth_};

    ZiaLocationScope locScope(*this, stmt->loc);

    switch (stmt->kind) {
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
        case StmtKind::Defer:
            lowerDeferStmt(static_cast<DeferStmt *>(stmt));
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

void Lowerer::lowerBlockStmt(BlockStmt *stmt) {
    auto localsBackup = locals_;
    auto slotsBackup = slots_;
    auto localTypesBackup = localTypes_;
    const size_t cleanupStart = cleanupStack_.size();

    for (auto &s : stmt->statements) {
        if (isTerminated())
            break;
        lowerStmt(s.get());
    }

    if (!isTerminated())
        emitCleanupsFrom(cleanupStart);
    cleanupStack_.resize(cleanupStart);

    locals_ = std::move(localsBackup);
    slots_ = std::move(slotsBackup);
    localTypes_ = std::move(localTypesBackup);
}

void Lowerer::lowerExprStmt(ExprStmt *stmt) {
    lowerExpr(stmt->expr.get());
}

void Lowerer::lowerVarStmt(VarStmt *stmt) {
    if (stmt->isTupleDestructure) {
        TypeRef initType =
            stmt->initializer ? sema_.typeOf(stmt->initializer.get()) : types::unknown();
        if (!stmt->initializer || !initType || initType->kind != TypeKindSem::Tuple) {
            reportLoweringInvariant(stmt->loc,
                                    "V-ZIA-LOWER-TUPLE-DESTRUCTURE",
                                    "tuple destructuring reached lowering without tuple initializer");
            return;
        }

        auto init = lowerExpr(stmt->initializer.get());
        const auto &elements = initType->tupleElementTypes();
        std::vector<std::string> names = stmt->tupleNames;
        if (names.empty()) {
            names.push_back(stmt->name);
            if (!stmt->secondName.empty())
                names.push_back(stmt->secondName);
        }

        if (elements.size() != names.size()) {
            reportLoweringInvariant(stmt->loc,
                                    "V-ZIA-LOWER-TUPLE-DESTRUCTURE-ARITY",
                                    "tuple destructuring reached lowering with mismatched arity");
            return;
        }

        PatternValue tupleValue{init.value, initType};
        for (size_t i = 0; i < elements.size(); ++i) {
            TypeNode *annotation = nullptr;
            if (!stmt->tupleTypes.empty() && i < stmt->tupleTypes.size()) {
                annotation = stmt->tupleTypes[i].get();
            } else if (i == 0) {
                annotation = stmt->type.get();
            } else if (i == 1) {
                annotation = stmt->secondType.get();
            }

            TypeRef bindingType = annotation ? sema_.resolveType(annotation) : elements[i];
            PatternValue element = emitTupleElement(tupleValue, i, elements[i]);
            auto coerced =
                coerceValueToType(element.value, mapType(elements[i]), elements[i], bindingType);

            if (!stmt->isFinal) {
                createSlot(names[i], mapType(bindingType));
                storeToSlot(names[i], coerced.value, mapType(bindingType));
            } else {
                defineLocal(names[i], coerced.value);
            }
            consumeDeferred(coerced.value);
            localTypes_[names[i]] = bindingType;
        }
        return;
    }

    Value initValue;
    Type ilType;
    TypeRef varType =
        stmt->type ? sema_.resolveType(stmt->type.get())
                   : (stmt->initializer ? sema_.typeOf(stmt->initializer.get()) : types::unknown());

    if (stmt->initializer) {
        auto result = lowerExpr(stmt->initializer.get());
        initValue = result.value;
        ilType = result.type;
        TypeRef initType = sema_.typeOf(stmt->initializer.get());

        // In generic contexts, semantic types may be unknown because generic
        // function bodies aren't fully analyzed. Use the lowered expression type.
        if (!stmt->type && (!varType || varType->kind == TypeKindSem::Unknown)) {
            varType = reverseMapType(ilType);
        }

        // Inline aggregate locals own their own storage. Initializers are copied
        // so later writes cannot alias literal/temporary storage.
        if (initType && isInlineAggregateType(initType)) {
            Value copy = emitInlineValueAlloc(initType);
            emitInlineValueCopy(initType, copy, initValue, true);
            initValue = copy;
            ilType = Type(Type::Kind::Ptr);
        }

        auto coerced = coerceValueToType(initValue, ilType, initType, varType);
        initValue = coerced.value;
        ilType = coerced.type;
    } else {
        // Default initialization
        ilType = mapType(varType);

        // Inline aggregate locals need real stack storage; a null ptr would make
        // field/index access operate on invalid memory.
        if (isInlineAggregateType(varType)) {
            initValue = emitInlineValueAlloc(varType);
            ilType = Type(Type::Kind::Ptr);
        } else {
            switch (ilType.kind) {
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
    if (!stmt->isFinal) {
        createSlot(stmt->name, ilType);
        storeToSlot(stmt->name, initValue, ilType);
        // The init value is consumed by the slot — don't release it at statement boundary
        consumeDeferred(initValue);
    } else {
        // Final/immutable variables can use direct SSA values
        defineLocal(stmt->name, initValue);
        // The init value is consumed by the local — don't release it at statement boundary
        consumeDeferred(initValue);
    }

    if (varType) {
        localTypes_[stmt->name] = varType;
    }
}

void Lowerer::lowerIfStmt(IfStmt *stmt) {
    size_t thenIdx = createBlock("if_then");
    size_t elseIdx = stmt->elseBranch ? createBlock("if_else") : 0;
    size_t mergeIdx = createBlock("if_end");
    bool mergeReachable = !stmt->elseBranch;

    // Lower condition
    auto cond = lowerExpr(stmt->condition.get());

    // Release condition temps before branch (SSA: temps are scoped to this block)
    releaseDeferredTemps();

    // Emit branch
    if (stmt->elseBranch) {
        emitCBr(cond.value, thenIdx, elseIdx);
    } else {
        emitCBr(cond.value, thenIdx, mergeIdx);
    }

    // Lower then branch
    setBlock(thenIdx);
    lowerStmt(stmt->thenBranch.get());
    if (!isTerminated()) {
        emitBr(mergeIdx);
        mergeReachable = true;
    }

    // Lower else branch
    if (stmt->elseBranch) {
        setBlock(elseIdx);
        lowerStmt(stmt->elseBranch.get());
        if (!isTerminated()) {
            emitBr(mergeIdx);
            mergeReachable = true;
        }
    }

    setBlock(mergeIdx);
    if (!mergeReachable) {
        il::core::Instr trap;
        trap.op = il::core::Opcode::Trap;
        trap.type = il::core::Type(il::core::Type::Kind::Void);
        trap.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(trap);
        blockMgr_.currentBlock()->terminated = true;
    }
}

void Lowerer::lowerWhileStmt(WhileStmt *stmt) {
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
    if (!isTerminated()) {
        emitBr(condIdx);
    }

    // Pop loop context
    loopStack_.pop();

    setBlock(endIdx);
}

void Lowerer::lowerForStmt(ForStmt *stmt) {
    size_t condIdx = createBlock("for_cond");
    size_t bodyIdx = createBlock("for_body");
    size_t updateIdx = createBlock("for_update");
    size_t endIdx = createBlock("for_end");

    // Push loop context
    loopStack_.push(endIdx, updateIdx);

    // Lower init
    if (stmt->init) {
        lowerStmt(stmt->init.get());
    }

    // Branch to condition
    emitBr(condIdx);

    // Lower condition
    setBlock(condIdx);
    if (stmt->condition) {
        auto cond = lowerExpr(stmt->condition.get());
        releaseDeferredTemps(); // Release condition temps before branch
        emitCBr(cond.value, bodyIdx, endIdx);
    } else {
        emitBr(bodyIdx);
    }

    // Lower body
    setBlock(bodyIdx);
    lowerStmt(stmt->body.get());
    if (!isTerminated()) {
        emitBr(updateIdx);
    }

    // Lower update
    setBlock(updateIdx);
    if (stmt->update) {
        lowerExpr(stmt->update.get());
    }
    emitBr(condIdx);

    // Pop loop context
    loopStack_.pop();

    setBlock(endIdx);
}

void Lowerer::lowerForInStmt(ForInStmt *stmt) {
    auto localsBackup = locals_;
    auto slotsBackup = slots_;
    auto localTypesBackup = localTypes_;

    RangeModifierInfo rangeInfo;
    Expr *iterable = stmt->iterable.get();
    bool hasRange = collectRangeModifierChain(iterable, rangeInfo);
    auto *rangeExpr = hasRange ? rangeInfo.range : dynamic_cast<RangeExpr *>(iterable);
    if (rangeExpr) {
        size_t condIdx = createBlock("forin_cond");
        size_t bodyIdx = createBlock("forin_body");
        size_t updateIdx = createBlock("forin_update");
        size_t updateDoIdx = createBlock("forin_update_do");
        size_t endIdx = createBlock("forin_end");

        loopStack_.push(endIdx, updateIdx);

        // Lower range bounds
        auto startResult = lowerExpr(rangeExpr->start.get());
        auto endResult = lowerExpr(rangeExpr->end.get());
        Value startVal = widenIntegralToI64(startResult.value, startResult.type);
        Value rangeEndVal = widenIntegralToI64(endResult.value, endResult.type);

        // Lower step value (default 1)
        Value stepVal = Value::constInt(1);
        if (rangeInfo.stepArg) {
            auto stepResult = lowerExpr(rangeInfo.stepArg);
            stepVal = widenIntegralToI64(stepResult.value, stepResult.type);
            stepVal = emitPositiveStepCheck(stepVal);
        }

        // Create slot-based loop variable (alloca + initial store)
        createSlot(stmt->variable, Type(Type::Kind::I64));
        localTypes_[stmt->variable] = types::integer();

        std::string cursorVar = stmt->variable + "_cursor";
        createSlot(cursorVar, Type(Type::Kind::I64));

        std::string endVar = stmt->variable + "_end";
        createSlot(endVar, Type(Type::Kind::I64));

        std::string stepVar = stmt->variable + "_step";
        createSlot(stepVar, Type(Type::Kind::I64));
        storeToSlot(stepVar, stepVal, Type(Type::Kind::I64));

        if (rangeInfo.reversed) {
            storeToSlot(cursorVar, rangeEndVal, Type(Type::Kind::I64));
            storeToSlot(endVar, startVal, Type(Type::Kind::I64));
        } else {
            storeToSlot(cursorVar, startVal, Type(Type::Kind::I64));
            storeToSlot(endVar, rangeEndVal, Type(Type::Kind::I64));
        }

        // Branch to condition
        emitBr(condIdx);

        // Condition block
        setBlock(condIdx);
        Value loopVar = loadFromSlot(cursorVar, Type(Type::Kind::I64));
        Value endVal = loadFromSlot(endVar, Type(Type::Kind::I64));
        Value cond;
        if (rangeInfo.reversed) {
            cond = emitBinary(rangeExpr->inclusive ? Opcode::SCmpGE : Opcode::SCmpGT,
                              Type(Type::Kind::I1),
                              loopVar,
                              endVal);
        } else if (rangeExpr->inclusive) {
            cond = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), loopVar, endVal);
        } else {
            cond = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), loopVar, endVal);
        }
        emitCBr(cond, bodyIdx, endIdx);

        // Body
        setBlock(bodyIdx);
        Value elemVal = loadFromSlot(cursorVar, Type(Type::Kind::I64));
        if (rangeInfo.reversed && !rangeExpr->inclusive)
            elemVal = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), elemVal, Value::constInt(1));
        storeToSlot(stmt->variable, elemVal, Type(Type::Kind::I64));
        lowerStmt(stmt->body.get());
        if (!isTerminated()) {
            emitBr(updateIdx);
        }

        // Update: cursor = cursor +/- step, unless this was the terminal value or would overflow.
        setBlock(updateIdx);
        Value currentVal = loadFromSlot(cursorVar, Type(Type::Kind::I64));
        Value currentBound = loadFromSlot(endVar, Type(Type::Kind::I64));
        Value currentStep = loadFromSlot(stepVar, Type(Type::Kind::I64));
        Value terminal = rangeExpr->inclusive
                             ? emitBinary(
                                   Opcode::ICmpEq, Type(Type::Kind::I1), currentVal, currentBound)
                             : Value::constBool(false);
        Value overflowRisk;
        if (rangeInfo.reversed) {
            Value minPlusStep = emitBinary(Opcode::IAddOvf,
                                           Type(Type::Kind::I64),
                                           Value::constInt(std::numeric_limits<int64_t>::min()),
                                           currentStep);
            overflowRisk =
                emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), currentVal, minPlusStep);
        } else {
            Value maxMinusStep = emitBinary(Opcode::ISubOvf,
                                            Type(Type::Kind::I64),
                                            Value::constInt(std::numeric_limits<int64_t>::max()),
                                            currentStep);
            overflowRisk =
                emitBinary(Opcode::SCmpGT, Type(Type::Kind::I1), currentVal, maxMinusStep);
        }
        Value terminalWide = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), terminal);
        Value overflowWide = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), overflowRisk);
        Value exitWide = emitBinary(Opcode::Or, Type(Type::Kind::I64), terminalWide, overflowWide);
        Value exitNow = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), exitWide);
        emitCBr(exitNow, endIdx, updateDoIdx);

        setBlock(updateDoIdx);
        Value nextCur = loadFromSlot(cursorVar, Type(Type::Kind::I64));
        Value nextStep = loadFromSlot(stepVar, Type(Type::Kind::I64));
        Opcode updateOp = rangeInfo.reversed ? Opcode::ISubOvf : Opcode::IAddOvf;
        Value nextVal = emitBinary(updateOp, Type(Type::Kind::I64), nextCur, nextStep);
        storeToSlot(cursorVar, nextVal, Type(Type::Kind::I64));
        emitBr(condIdx);

        loopStack_.pop();
        setBlock(endIdx);

        // Clean up slots
        removeSlot(stmt->variable);
        removeSlot(cursorVar);
        removeSlot(endVar);
        removeSlot(stepVar);
        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);
        return;
    }

    TypeRef iterableType = sema_.typeOf(stmt->iterable.get());
    if (!iterableType) {
        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);
        return;
    }

    // Tuple destructuring over a tuple value (single iteration)
    if (stmt->isTuple && iterableType->kind == TypeKindSem::Tuple) {
        const auto &elements = iterableType->tupleElementTypes();
        if (elements.size() == 2) {
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
            if (!isTerminated()) {
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
    if (iterableType->kind == TypeKindSem::List) {
        TypeRef elemType = iterableType->elementType();
        if (stmt->variableType)
            elemType = sema_.resolveType(stmt->variableType.get());

        Type elemIlType = mapType(elemType);

        // For tuple binding (for idx, val in list), first var is index, second is element
        // For single binding (for val in list), the variable is the element
        bool hasTupleBinding = stmt->isTuple && !stmt->secondVariable.empty();

        if (hasTupleBinding) {
            // First variable is the index
            createSlot(stmt->variable, Type(Type::Kind::I64));
            localTypes_[stmt->variable] = types::integer();
            // Second variable is the element
            createSlot(stmt->secondVariable, elemIlType);
            localTypes_[stmt->secondVariable] = elemType;
        } else {
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

        if (hasTupleBinding) {
            // Store index in first variable
            storeToSlot(stmt->variable, idxInBody, Type(Type::Kind::I64));
            // Get and store element in second variable
            Value boxed = emitCallRet(Type(Type::Kind::Ptr), kListGet, {listLoaded, idxInBody});
            auto elemValue = emitUnboxValue(boxed, elemIlType, elemType);
            storeToSlot(stmt->secondVariable, elemValue.value, elemIlType);
        } else {
            Value boxed = emitCallRet(Type(Type::Kind::Ptr), kListGet, {listLoaded, idxInBody});
            auto elemValue = emitUnboxValue(boxed, elemIlType, elemType);
            storeToSlot(stmt->variable, elemValue.value, elemIlType);
        }

        lowerStmt(stmt->body.get());
        if (!isTerminated()) {
            emitBr(updateIdx);
        }

        setBlock(updateIdx);
        Value idxCurrent = loadFromSlot(indexVar, Type(Type::Kind::I64));
        Value idxNext =
            emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), idxCurrent, Value::constInt(1));
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

    if (iterableType->kind == TypeKindSem::Map) {
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

        if (stmt->isTuple) {
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

        if (stmt->isTuple) {
            // Load map from slot for cross-block SSA
            Value mapLoaded = loadFromSlot(mapVar, Type(Type::Kind::Ptr));
            Value boxed = emitCallRet(Type(Type::Kind::Ptr), kMapGet, {mapLoaded, keyVal.value});
            auto unboxed = emitUnboxValue(boxed, valueIlType, valueType);
            storeToSlot(stmt->secondVariable, unboxed.value, valueIlType);
        }

        lowerStmt(stmt->body.get());
        if (!isTerminated()) {
            emitBr(updateIdx);
        }

        setBlock(updateIdx);
        Value idxCurrent = loadFromSlot(indexVar, Type(Type::Kind::I64));
        Value idxNext =
            emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), idxCurrent, Value::constInt(1));
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

    auto lowerSeqForIn = [&](LowerResult seqValue,
                             TypeRef elemType,
                             bool rawStringElements,
                             const std::string &labelPrefix) {
        if (stmt->variableType)
            elemType = sema_.resolveType(stmt->variableType.get());

        Type elemIlType = mapType(elemType);
        bool hasTupleBinding = stmt->isTuple && !stmt->secondVariable.empty();

        if (hasTupleBinding) {
            createSlot(stmt->variable, Type(Type::Kind::I64));
            localTypes_[stmt->variable] = types::integer();
            createSlot(stmt->secondVariable, elemIlType);
            localTypes_[stmt->secondVariable] = elemType;
        } else {
            createSlot(stmt->variable, elemIlType);
            localTypes_[stmt->variable] = elemType;
        }

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

        size_t condIdx = createBlock(labelPrefix + "_cond");
        size_t bodyIdx = createBlock(labelPrefix + "_body");
        size_t updateIdx = createBlock(labelPrefix + "_update");
        size_t endIdx = createBlock(labelPrefix + "_end");

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

        if (hasTupleBinding)
            storeToSlot(stmt->variable, idxInBody, Type(Type::Kind::I64));

        if (rawStringElements && elemIlType.kind == Type::Kind::Str) {
            Value elem = emitCallRet(Type(Type::Kind::Str), kSeqGetStr, {seqLoaded, idxInBody});
            storeToSlot(hasTupleBinding ? stmt->secondVariable : stmt->variable,
                        elem,
                        Type(Type::Kind::Str));
        } else {
            Value boxed = emitCallRet(Type(Type::Kind::Ptr), kSeqGet, {seqLoaded, idxInBody});
            auto elemValue = emitUnboxValue(boxed, elemIlType, elemType);
            storeToSlot(hasTupleBinding ? stmt->secondVariable : stmt->variable,
                        elemValue.value,
                        elemIlType);
        }

        lowerStmt(stmt->body.get());
        if (!isTerminated()) {
            emitBr(updateIdx);
        }

        setBlock(updateIdx);
        Value idxCurrent = loadFromSlot(indexVar, Type(Type::Kind::I64));
        Value idxNext =
            emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), idxCurrent, Value::constInt(1));
        storeToSlot(indexVar, idxNext, Type(Type::Kind::I64));
        emitBr(condIdx);

        loopStack_.pop();
        setBlock(endIdx);

        removeSlot(stmt->variable);
        if (hasTupleBinding)
            removeSlot(stmt->secondVariable);
        removeSlot(indexVar);
        removeSlot(lenVar);
        removeSlot(seqVar);
    };

    // Seq iteration: typed rt_seq result from seq<T>-annotated runtime functions.
    // Uses kSeqLen / kSeqGet (not kListCount / kListGet) since rt_seq and rt_list
    // have incompatible internal layouts.
    if (iterableType->kind == TypeKindSem::Ptr && iterableType->name == "Viper.Collections.Seq" &&
        !iterableType->typeArgs.empty()) {
        lowerSeqForIn(lowerExpr(stmt->iterable.get()),
                      iterableType->typeArgs[0],
                      true,
                      "forin_seq");
        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);
        return;
    }

    if (const char *toSeqCallee = runtimeCollectionToSeqCallee(iterableType)) {
        auto collectionValue = lowerExpr(stmt->iterable.get());
        Value seqValue = emitCallRet(Type(Type::Kind::Ptr), toSeqCallee, {collectionValue.value});
        lowerSeqForIn({seqValue, Type(Type::Kind::Ptr)},
                      iterableType->elementType(),
                      false,
                      "forin_runtime_seq");
        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);
        return;
    }

    locals_ = std::move(localsBackup);
    slots_ = std::move(slotsBackup);
    localTypes_ = std::move(localTypesBackup);
}

void Lowerer::lowerReturnStmt(ReturnStmt *stmt) {
    if (stmt->value) {
        auto result = lowerExpr(stmt->value.get());
        TypeRef valueType = sema_.typeOf(stmt->value.get());
        if (valueType && valueType->kind == TypeKindSem::Unit && currentReturnType_ &&
            currentReturnType_->kind == TypeKindSem::Void) {
            releaseDeferredTemps();
            if (!cleanupStack_.empty()) {
                emitActiveCleanups();
                if (isTerminated())
                    return;
            }
            emitRetVoid();
            return;
        }
        auto coerced = coerceValueToType(result.value, result.type, valueType, currentReturnType_);
        Value returnValue = coerced.value;
        Type returnIlType = coerced.type;

        if (!cleanupStack_.empty()) {
            const std::string slotName = "__zia_return_" + std::to_string(nextTempId());
            createSlot(slotName, returnIlType);
            storeToSlot(slotName, returnValue, returnIlType);
            consumeDeferred(returnValue);
            releaseDeferredTemps();

            emitActiveCleanups();
            if (isTerminated())
                return;

            returnValue = loadFromSlot(slotName, returnIlType);
        }

        if (currentAsyncWorker_) {
            Type payloadIlType = mapType(currentReturnType_);
            Value futureValue = returnValue;
            if (currentReturnType_ && (currentReturnType_->kind == TypeKindSem::Struct ||
                                       payloadIlType.kind != Type::Kind::Ptr)) {
                futureValue = emitBoxValue(returnValue, payloadIlType, currentReturnType_);
            } else {
                emitCall("rt_obj_retain_maybe", {futureValue});
            }

            for (const auto &owned : asyncOwnedValues_)
                emitManagedRelease(owned, /*isString=*/false);
            asyncOwnedValues_.clear();

            // The async runtime consumes the returned object.
            consumeDeferred(futureValue);
            releaseDeferredTemps();
            emitRet(futureValue);
            return;
        }

        if (currentReturnType_ && currentReturnType_->kind == TypeKindSem::Struct) {
            returnValue =
                emitBoxValue(returnValue, mapType(currentReturnType_), currentReturnType_);
        }

        // The return value is transferred to the caller — don't release it.
        // But release any intermediate temps from evaluating the return expr.
        consumeDeferred(returnValue);
        releaseDeferredTemps();
        emitRet(returnValue);
    } else {
        if (!cleanupStack_.empty()) {
            releaseDeferredTemps();
            emitActiveCleanups();
            if (isTerminated())
                return;
        }

        if (currentAsyncWorker_) {
            for (const auto &owned : asyncOwnedValues_)
                emitManagedRelease(owned, /*isString=*/false);
            asyncOwnedValues_.clear();
            releaseDeferredTemps();
            emitRet(Value::null());
            return;
        }

        releaseDeferredTemps();
        emitRetVoid();
    }
}

void Lowerer::lowerBreakStmt(BreakStmt * /*stmt*/) {
    if (!loopStack_.empty()) {
        releaseDeferredTemps(); // Release any pending temps before branch
        if (!cleanupStack_.empty()) {
            emitActiveCleanups();
            if (isTerminated())
                return;
        }
        emitBr(loopStack_.breakTarget());
    } else {
        // Defensive: semantic analysis should reject this; emit trap for safety.
        il::core::Instr trap;
        trap.op = il::core::Opcode::Trap;
        trap.type = il::core::Type(il::core::Type::Kind::Void);
        trap.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(trap);
        blockMgr_.currentBlock()->terminated = true;
    }
}

void Lowerer::lowerContinueStmt(ContinueStmt * /*stmt*/) {
    if (!loopStack_.empty()) {
        releaseDeferredTemps(); // Release any pending temps before branch
        if (!cleanupStack_.empty()) {
            emitActiveCleanups();
            if (isTerminated())
                return;
        }
        emitBr(loopStack_.continueTarget());
    } else {
        // Defensive: semantic analysis should reject this; emit trap for safety.
        il::core::Instr trap;
        trap.op = il::core::Opcode::Trap;
        trap.type = il::core::Type(il::core::Type::Kind::Void);
        trap.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(trap);
        blockMgr_.currentBlock()->terminated = true;
    }
}

void Lowerer::lowerDeferStmt(DeferStmt *stmt) {
    if (stmt && stmt->action)
        cleanupStack_.push_back({stmt->action.get(), false});
}

void Lowerer::lowerGuardStmt(GuardStmt *stmt) {
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

void Lowerer::lowerMatchStmt(MatchStmt *stmt) {
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
    for (size_t i = 0; i < stmt->arms.size(); ++i) {
        armBlocks.push_back(createBlock("match_arm_" + std::to_string(i)));
        if (i + 1 < stmt->arms.size()) {
            nextTestBlocks.push_back(createBlock("match_test_" + std::to_string(i + 1)));
        } else {
            nextTestBlocks.push_back(endIdx); // Last arm falls through to end
        }
    }

    // Lower each arm
    for (size_t i = 0; i < stmt->arms.size(); ++i) {
        const auto &arm = stmt->arms[i];
        auto localsBackup = locals_;
        auto slotsBackup = slots_;
        auto localTypesBackup = localTypes_;

        size_t matchBlock = armBlocks[i];
        size_t guardBlock = 0;
        if (arm.pattern.guard) {
            guardBlock = createBlock("match_guard_" + std::to_string(i));
            matchBlock = guardBlock;
        }

        // In the current block, test the pattern
        Value scrutineeVal = loadFromSlot(scrutineeSlot, scrutinee.type);
        PatternValue scrutineeValue{scrutineeVal, scrutineeType};
        releaseDeferredTemps(); // Release temps before pattern branch
        emitPatternTest(arm.pattern, scrutineeValue, matchBlock, nextTestBlocks[i]);

        if (guardBlock) {
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
        if (!guardBlock) {
            // Reload scrutinee from slot in this block for SSA correctness
            Value scrutineeInArm = loadFromSlot(scrutineeSlot, scrutinee.type);
            PatternValue scrutineeValueInArm{scrutineeInArm, scrutineeType};
            emitPatternBindings(arm.pattern, scrutineeValueInArm);
        }
        if (arm.body) {
            // Check if it's a block expression
            if (auto *blockExpr = dynamic_cast<BlockExpr *>(arm.body.get())) {
                lowerBlockExpr(blockExpr);
            } else {
                // It's a regular expression - just evaluate it
                lowerExpr(arm.body.get());
            }
        }

        // Jump to end after arm body (if not already terminated)
        if (!isTerminated()) {
            emitBr(endIdx);
        }

        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);

        // Set up next test block for pattern matching
        if (i + 1 < stmt->arms.size()) {
            setBlock(nextTestBlocks[i]);
        }
    }

    // Remove the scrutinee slot
    removeSlot(scrutineeSlot);

    // Continue from end block
    setBlock(endIdx);
}


} // namespace il::frontends::zia
