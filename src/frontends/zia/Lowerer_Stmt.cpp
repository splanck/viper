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

/// @brief True if @p type is stored inline by value (struct, fixed array, or
///        tuple) rather than behind a heap pointer — affects copy/load lowering.
bool isInlineAggregateType(TypeRef type) {
    return type && (type->kind == TypeKindSem::Struct || type->kind == TypeKindSem::FixedArray ||
                    type->kind == TypeKindSem::Tuple);
}

/// @brief Runtime "ToSeq" callee name for an iterable collection type used in
///        `for` lowering, or nullptr if @p type is not a known collection.
const char *runtimeCollectionToSeqCallee(TypeRef type) {
    if (!type || type->kind != TypeKindSem::Ptr || !type->elementType())
        return nullptr;
    if (type->name == kRuntimeClassCollectionsQueue)
        return kCollectionsQueueToSeq;
    if (type->name == kRuntimeClassCollectionsStack)
        return kCollectionsStackToSeq;
    if (type->name == kRuntimeClassCollectionsDeque)
        return kCollectionsDequeToSeq;
    if (type->name == kRuntimeClassCollectionsList)
        return kCollectionsListToSeq;
    if (type->name == kRuntimeClassCollectionsRing)
        return kCollectionsRingToSeq;
    if (type->name == kRuntimeClassCollectionsHeap)
        return kCollectionsHeapToSeq;
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

    if (!isTerminated()) {
        emitCleanupsFrom(cleanupStart);
        // Block-scoped string locals own their slot's reference; release it
        // as the variables go out of scope (runs once per loop iteration for
        // loop-body declarations, since their allocas are re-executed).
        releaseBlockStringSlots(slotsBackup);
    }
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
            reportLoweringInvariant(
                stmt->loc,
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
                localTypes_[names[i]] = bindingType;
                if (mapType(bindingType).kind == Type::Kind::Str) {
                    // Slot-ownership discipline: fresh slot, no displaced value.
                    storeOwnedStringToSlot(names[i], coerced.value, /*releaseDisplaced=*/false);
                } else {
                    storeToSlot(names[i], coerced.value, mapType(bindingType));
                    consumeDeferred(coerced.value);
                }
                continue;
            }
            defineLocal(names[i], coerced.value);
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
        if (ilType.kind == Type::Kind::Str) {
            // Slot-ownership discipline: the slot owns one reference. The
            // alloca is freshly zeroed, so there is no displaced value.
            storeOwnedStringToSlot(stmt->name, initValue, /*releaseDisplaced=*/false);
        } else {
            storeToSlot(stmt->name, initValue, ilType);
            // The init value is consumed by the slot — don't release it at statement boundary
            consumeDeferred(initValue);
        }
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
    if (auto *rangeExpr = hasRange ? rangeInfo.range : dynamic_cast<RangeExpr *>(iterable)) {
        lowerForInRange(stmt, rangeExpr, rangeInfo);
    } else if (TypeRef iterableType = sema_.typeOf(stmt->iterable.get())) {
        if (stmt->isTuple && iterableType->kind == TypeKindSem::Tuple) {
            lowerForInTuple(stmt, iterableType);
        } else if (iterableType->kind == TypeKindSem::List) {
            lowerForInList(stmt, iterableType);
        } else if (iterableType->kind == TypeKindSem::Map) {
            lowerForInMap(stmt, iterableType);
        } else if (iterableType->kind == TypeKindSem::Ptr &&
                   iterableType->name == kRuntimeClassCollectionsSeq &&
                   !iterableType->typeArgs.empty()) {
            lowerForInSeq(lowerExpr(stmt->iterable.get()),
                          stmt,
                          iterableType->typeArgs[0],
                          true,
                          "forin_seq");
        } else if (const char *toSeqCallee = runtimeCollectionToSeqCallee(iterableType)) {
            auto collectionValue = lowerExpr(stmt->iterable.get());
            Value seqValue =
                emitCallRet(Type(Type::Kind::Ptr), toSeqCallee, {collectionValue.value});
            lowerForInSeq({seqValue, Type(Type::Kind::Ptr)},
                          stmt,
                          iterableType->elementType(),
                          false,
                          "forin_runtime_seq");
        }
    }

    locals_ = std::move(localsBackup);
    slots_ = std::move(slotsBackup);
    localTypes_ = std::move(localTypesBackup);
}

void Lowerer::lowerForInRange(ForInStmt *stmt,
                              RangeExpr *rangeExpr,
                              const RangeModifierInfo &rangeInfo) {
    size_t condIdx = createBlock("forin_cond");
    size_t bodyIdx = createBlock("forin_body");
    size_t updateIdx = createBlock("forin_update");
    size_t updateDoIdx = createBlock("forin_update_do");
    size_t endIdx = createBlock("forin_end");

    loopStack_.push(endIdx, updateIdx);

    auto startResult = lowerExpr(rangeExpr->start.get());
    auto endResult = lowerExpr(rangeExpr->end.get());
    Value startVal = widenIntegralToI64(startResult.value, startResult.type);
    Value rangeEndVal = widenIntegralToI64(endResult.value, endResult.type);

    Value stepVal = Value::constInt(1);
    if (rangeInfo.stepArg) {
        auto stepResult = lowerExpr(rangeInfo.stepArg);
        stepVal = widenIntegralToI64(stepResult.value, stepResult.type);
        stepVal = emitPositiveStepCheck(stepVal);
    }

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

    emitBr(condIdx);

    // Condition block.
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

    // Body.
    setBlock(bodyIdx);
    Value elemVal = loadFromSlot(cursorVar, Type(Type::Kind::I64));
    if (rangeInfo.reversed && !rangeExpr->inclusive)
        elemVal = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), elemVal, Value::constInt(1));
    storeToSlot(stmt->variable, elemVal, Type(Type::Kind::I64));
    lowerStmt(stmt->body.get());
    if (!isTerminated())
        emitBr(updateIdx);

    // Update: cursor += step, gated on (terminal-value || overflow-risk).
    setBlock(updateIdx);
    Value currentVal = loadFromSlot(cursorVar, Type(Type::Kind::I64));
    Value currentBound = loadFromSlot(endVar, Type(Type::Kind::I64));
    Value currentStep = loadFromSlot(stepVar, Type(Type::Kind::I64));
    Value terminal =
        rangeExpr->inclusive
            ? emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), currentVal, currentBound)
            : Value::constBool(false);
    Value overflowRisk;
    if (rangeInfo.reversed) {
        Value minPlusStep = emitBinary(Opcode::IAddOvf,
                                       Type(Type::Kind::I64),
                                       Value::constInt(std::numeric_limits<int64_t>::min()),
                                       currentStep);
        overflowRisk = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), currentVal, minPlusStep);
    } else {
        Value maxMinusStep = emitBinary(Opcode::ISubOvf,
                                        Type(Type::Kind::I64),
                                        Value::constInt(std::numeric_limits<int64_t>::max()),
                                        currentStep);
        overflowRisk = emitBinary(Opcode::SCmpGT, Type(Type::Kind::I1), currentVal, maxMinusStep);
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

    removeSlot(stmt->variable);
    removeSlot(cursorVar);
    removeSlot(endVar);
    removeSlot(stepVar);
}

void Lowerer::lowerForInTuple(ForInStmt *stmt, TypeRef iterableType) {
    const auto &elements = iterableType->tupleElementTypes();
    if (elements.size() != 2)
        return;

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

    // Null-init string slots so the first store's releaseDisplaced frees null
    // (safe) instead of the uninitialized slot — native codegen leaves it garbage.
    if (firstIl.kind == Type::Kind::Str)
        storeToSlot(stmt->variable, Value::null(), Type(Type::Kind::Ptr));
    if (secondIl.kind == Type::Kind::Str)
        storeToSlot(stmt->secondVariable, Value::null(), Type(Type::Kind::Ptr));

    size_t bodyIdx = createBlock("forin_tuple_body");
    size_t endIdx = createBlock("forin_tuple_end");

    loopStack_.push(endIdx, endIdx);
    emitBr(bodyIdx);
    setBlock(bodyIdx);

    PatternValue tupleValue{lowerExpr(stmt->iterable.get()).value, iterableType};
    PatternValue firstVal = emitTupleElement(tupleValue, 0, firstType);
    PatternValue secondVal = emitTupleElement(tupleValue, 1, secondType);

    if (firstIl.kind == Type::Kind::Str)
        storeOwnedStringToSlot(stmt->variable, firstVal.value, /*releaseDisplaced=*/true);
    else
        storeToSlot(stmt->variable, firstVal.value, firstIl);
    if (secondIl.kind == Type::Kind::Str)
        storeOwnedStringToSlot(stmt->secondVariable, secondVal.value, /*releaseDisplaced=*/true);
    else
        storeToSlot(stmt->secondVariable, secondVal.value, secondIl);

    lowerStmt(stmt->body.get());
    if (!isTerminated())
        emitBr(endIdx);

    loopStack_.pop();
    setBlock(endIdx);
}

void Lowerer::lowerForInList(ForInStmt *stmt, TypeRef iterableType) {
    TypeRef elemType = iterableType->elementType();
    if (stmt->variableType)
        elemType = sema_.resolveType(stmt->variableType.get());

    Type elemIlType = mapType(elemType);

    // For tuple binding (for idx, val in list), first var is index, second is element;
    // for single binding (for val in list), the variable is the element.
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

    // Null-init a string element slot (see lowerForInSeq): the per-iteration
    // store releases the displaced value, which is uninitialized on iteration 1.
    if (elemIlType.kind == Type::Kind::Str) {
        const std::string &elemSlot = hasTupleBinding ? stmt->secondVariable : stmt->variable;
        storeToSlot(elemSlot, Value::null(), Type(Type::Kind::Ptr));
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
        storeToSlot(stmt->variable, idxInBody, Type(Type::Kind::I64));
        Value boxed = emitCallRet(Type(Type::Kind::Ptr), kListGet, {listLoaded, idxInBody});
        auto elemValue = emitUnboxValue(boxed, elemIlType, elemType);
        if (elemIlType.kind == Type::Kind::Str)
            storeOwnedStringToSlot(
                stmt->secondVariable, elemValue.value, /*releaseDisplaced=*/true);
        else
            storeToSlot(stmt->secondVariable, elemValue.value, elemIlType);
    } else {
        Value boxed = emitCallRet(Type(Type::Kind::Ptr), kListGet, {listLoaded, idxInBody});
        auto elemValue = emitUnboxValue(boxed, elemIlType, elemType);
        if (elemIlType.kind == Type::Kind::Str)
            storeOwnedStringToSlot(stmt->variable, elemValue.value, /*releaseDisplaced=*/true);
        else
            storeToSlot(stmt->variable, elemValue.value, elemIlType);
    }

    lowerStmt(stmt->body.get());
    if (!isTerminated())
        emitBr(updateIdx);

    setBlock(updateIdx);
    Value idxCurrent = loadFromSlot(indexVar, Type(Type::Kind::I64));
    Value idxNext =
        emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), idxCurrent, Value::constInt(1));
    storeToSlot(indexVar, idxNext, Type(Type::Kind::I64));
    emitBr(condIdx);

    loopStack_.pop();
    setBlock(endIdx);

    releaseOwnedStringSlot(stmt->variable);
    if (hasTupleBinding)
        releaseOwnedStringSlot(stmt->secondVariable);
    removeSlot(stmt->variable);
    if (hasTupleBinding)
        removeSlot(stmt->secondVariable);
    removeSlot(indexVar);
    removeSlot(lenVar);
    removeSlot(listVar);
}

void Lowerer::lowerForInMap(ForInStmt *stmt, TypeRef iterableType) {
    const bool integerKeyed = usesIntegerMapRuntime(iterableType);
    TypeRef keyType = iterableType->keyType() ? iterableType->keyType() : types::string();
    TypeRef valueType = iterableType->valueType() ? iterableType->valueType() : types::unknown();
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

    // Null-init string key/value slots (see lowerForInSeq): the per-iteration
    // store releases the displaced value, uninitialized on the first iteration.
    if (keyIlType.kind == Type::Kind::Str)
        storeToSlot(stmt->variable, Value::null(), Type(Type::Kind::Ptr));
    if (stmt->isTuple && valueIlType.kind == Type::Kind::Str)
        storeToSlot(stmt->secondVariable, Value::null(), Type(Type::Kind::Ptr));

    auto mapValue = lowerExpr(stmt->iterable.get());
    Value keysSeq =
        emitCallRet(Type(Type::Kind::Ptr), integerKeyed ? kIntMapKeys : kMapKeys, {mapValue.value});

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
    Value keysLoaded = loadFromSlot(keysVar, Type(Type::Kind::Ptr));
    Value idxInBody = loadFromSlot(indexVar, Type(Type::Kind::I64));
    LowerResult keyVal;
    if (integerKeyed) {
        Value boxedKey = emitCallRet(Type(Type::Kind::Ptr), kSeqGet, {keysLoaded, idxInBody});
        keyVal = emitUnboxValue(boxedKey, keyIlType, keyType);
    } else {
        // String Map keys are stored as raw rt_string pointers in the seq, so use kSeqGetStr.
        Value keyStrVal = emitCallRet(Type(Type::Kind::Str), kSeqGetStr, {keysLoaded, idxInBody});
        keyVal = {keyStrVal, Type(Type::Kind::Str)};
    }
    if (keyIlType.kind == Type::Kind::Str)
        storeOwnedStringToSlot(stmt->variable, keyVal.value, /*releaseDisplaced=*/true);
    else
        storeToSlot(stmt->variable, keyVal.value, keyIlType);

    if (stmt->isTuple) {
        Value mapLoaded = loadFromSlot(mapVar, Type(Type::Kind::Ptr));
        Value runtimeKey = coerceMapKeyForRuntime(keyVal.value, keyVal.type, iterableType);
        Value boxed = emitCallRet(
            Type(Type::Kind::Ptr), integerKeyed ? kIntMapGet : kMapGet, {mapLoaded, runtimeKey});
        auto unboxed = emitUnboxValue(boxed, valueIlType, valueType);
        if (valueIlType.kind == Type::Kind::Str)
            storeOwnedStringToSlot(stmt->secondVariable, unboxed.value, /*releaseDisplaced=*/true);
        else
            storeToSlot(stmt->secondVariable, unboxed.value, valueIlType);
    }

    lowerStmt(stmt->body.get());
    if (!isTerminated())
        emitBr(updateIdx);

    setBlock(updateIdx);
    Value idxCurrent = loadFromSlot(indexVar, Type(Type::Kind::I64));
    Value idxNext =
        emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), idxCurrent, Value::constInt(1));
    storeToSlot(indexVar, idxNext, Type(Type::Kind::I64));
    emitBr(condIdx);

    loopStack_.pop();
    setBlock(endIdx);

    releaseOwnedStringSlot(stmt->variable);
    if (stmt->isTuple)
        releaseOwnedStringSlot(stmt->secondVariable);
    removeSlot(stmt->variable);
    if (stmt->isTuple)
        removeSlot(stmt->secondVariable);
    removeSlot(indexVar);
    removeSlot(lenVar);
    removeSlot(keysVar);
    removeSlot(mapVar);
}

void Lowerer::lowerForInSeq(LowerResult seqValue,
                            ForInStmt *stmt,
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

    // Zero-initialize a string element slot to null before the loop. Each
    // iteration stores the new element with releaseDisplaced=true, which releases
    // the slot's previous value; on the first iteration that previous value is
    // the freshly-allocated (uninitialized) slot. The VM zero-inits allocas so
    // rt_str_release_maybe(null) is a safe no-op, but native codegen leaves stack
    // garbage there — releasing it traps "invalid string handle". A null init
    // makes the first release safe on every backend.
    if (elemIlType.kind == Type::Kind::Str) {
        // A str slot is a pointer; zero it with a null Ptr store (the same shape
        // emitInlineValueZero uses for Str), which the verifier accepts for a str
        // alloca while a str-typed null operand would not.
        const std::string &elemSlot = hasTupleBinding ? stmt->secondVariable : stmt->variable;
        storeToSlot(elemSlot, Value::null(), Type(Type::Kind::Ptr));
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
        storeOwnedStringToSlot(hasTupleBinding ? stmt->secondVariable : stmt->variable,
                               elem,
                               /*releaseDisplaced=*/true);
    } else {
        Value boxed = emitCallRet(Type(Type::Kind::Ptr), kSeqGet, {seqLoaded, idxInBody});
        auto elemValue = emitUnboxValue(boxed, elemIlType, elemType);
        if (elemIlType.kind == Type::Kind::Str)
            storeOwnedStringToSlot(hasTupleBinding ? stmt->secondVariable : stmt->variable,
                                   elemValue.value,
                                   /*releaseDisplaced=*/true);
        else
            storeToSlot(hasTupleBinding ? stmt->secondVariable : stmt->variable,
                        elemValue.value,
                        elemIlType);
    }

    lowerStmt(stmt->body.get());
    if (!isTerminated())
        emitBr(updateIdx);

    setBlock(updateIdx);
    Value idxCurrent = loadFromSlot(indexVar, Type(Type::Kind::I64));
    Value idxNext =
        emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), idxCurrent, Value::constInt(1));
    storeToSlot(indexVar, idxNext, Type(Type::Kind::I64));
    emitBr(condIdx);

    loopStack_.pop();
    setBlock(endIdx);

    releaseOwnedStringSlot(stmt->variable);
    if (hasTupleBinding)
        releaseOwnedStringSlot(stmt->secondVariable);
    removeSlot(stmt->variable);
    if (hasTupleBinding)
        removeSlot(stmt->secondVariable);
    removeSlot(indexVar);
    removeSlot(lenVar);
    removeSlot(seqVar);
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
            releaseLocalStringSlots();
            emitRetVoid();
            return;
        }
        auto coerced = coerceValueToType(result.value, result.type, valueType, currentReturnType_);
        Value returnValue = coerced.value;
        Type returnIlType = coerced.type;

        // Whether returnValue already carries the caller's owned reference.
        bool returnValueOwned = false;

        if (!cleanupStack_.empty()) {
            const std::string slotName = "__zia_return_" + std::to_string(nextTempId());
            createSlot(slotName, returnIlType);
            storeToSlot(slotName, returnValue, returnIlType);
            if (returnIlType.kind == Type::Kind::Str) {
                // Normalize ownership into the return slot: owned temps move
                // in; borrowed values are retained so the slot owns the
                // caller's reference either way.
                if (!consumeDeferred(returnValue))
                    emitCall(runtime::kStrRetainMaybe, {returnValue});
                returnValueOwned = true;
            } else {
                consumeDeferred(returnValue);
            }
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
        // For strings, mint the caller's reference when the value is borrowed
        // (e.g. `return s` loads from a slot that is released just below).
        if (returnIlType.kind == Type::Kind::Str && !returnValueOwned) {
            if (!consumeDeferred(returnValue))
                emitCall(runtime::kStrRetainMaybe, {returnValue});
        } else {
            consumeDeferred(returnValue);
        }
        releaseDeferredTemps();
        releaseLocalStringSlots();
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
        releaseLocalStringSlots();
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
