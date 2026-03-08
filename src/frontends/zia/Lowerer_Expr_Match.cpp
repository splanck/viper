//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr_Match.cpp
/// @brief Pattern matching expression lowering for the Zia IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"

namespace il::frontends::zia
{

using namespace runtime;

//=============================================================================
// Pattern Matching Helpers
//=============================================================================

Lowerer::PatternValue Lowerer::emitTupleElement(const PatternValue &tuple,
                                                size_t index,
                                                TypeRef elemType)
{
    Type ilType = mapType(elemType);
    size_t offset = index * 8;
    Value elemPtr = tuple.value;
    if (offset > 0)
    {
        elemPtr = emitGEP(tuple.value, static_cast<int64_t>(offset));
    }
    Value elemVal = emitLoad(elemPtr, ilType);
    return {elemVal, elemType};
}

void Lowerer::emitPatternTest(const MatchArm::Pattern &pattern,
                              const PatternValue &scrutinee,
                              size_t successBlock,
                              size_t failureBlock)
{
    switch (pattern.kind)
    {
        case MatchArm::Pattern::Kind::Wildcard:
        case MatchArm::Pattern::Kind::Binding:
            emitBr(successBlock);
            return;

        case MatchArm::Pattern::Kind::Literal:
        {
            if (!pattern.literal)
            {
                emitBr(failureBlock);
                return;
            }
            if (scrutinee.type && scrutinee.type->kind == TypeKindSem::Optional)
            {
                auto emitPtrCompare = [&](Opcode op) -> Value
                {
                    unsigned ptrSlotId = nextTempId();
                    il::core::Instr ptrSlotInstr;
                    ptrSlotInstr.result = ptrSlotId;
                    ptrSlotInstr.op = Opcode::Alloca;
                    ptrSlotInstr.type = Type(Type::Kind::Ptr);
                    ptrSlotInstr.operands = {Value::constInt(8)};
                    ptrSlotInstr.loc = curLoc_;
                    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
                    Value ptrSlot = Value::temp(ptrSlotId);

                    il::core::Instr storePtrInstr;
                    storePtrInstr.op = Opcode::Store;
                    storePtrInstr.type = Type(Type::Kind::Ptr);
                    storePtrInstr.operands = {ptrSlot, scrutinee.value};
                    storePtrInstr.loc = curLoc_;
                    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

                    unsigned ptrAsI64Id = nextTempId();
                    il::core::Instr loadAsI64Instr;
                    loadAsI64Instr.result = ptrAsI64Id;
                    loadAsI64Instr.op = Opcode::Load;
                    loadAsI64Instr.type = Type(Type::Kind::I64);
                    loadAsI64Instr.operands = {ptrSlot};
                    loadAsI64Instr.loc = curLoc_;
                    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
                    Value ptrAsI64 = Value::temp(ptrAsI64Id);

                    return emitBinary(op, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
                };

                if (pattern.literal->kind == ExprKind::NullLiteral)
                {
                    Value isNull = emitPtrCompare(Opcode::ICmpEq);
                    emitCBr(isNull, successBlock, failureBlock);
                    return;
                }

                Value isNotNull = emitPtrCompare(Opcode::ICmpNe);
                size_t someBlock = createBlock("match_opt_lit");
                emitCBr(isNotNull, someBlock, failureBlock);
                setBlock(someBlock);

                TypeRef innerType = scrutinee.type->innerType();
                auto innerValue = emitOptionalUnwrap(scrutinee.value, innerType);
                PatternValue inner{innerValue.value, innerType};
                emitPatternTest(pattern, inner, successBlock, failureBlock);
                return;
            }
            auto litResult = lowerExpr(pattern.literal.get());
            Value cond;
            if (scrutinee.type && scrutinee.type->kind == TypeKindSem::String)
            {
                cond = emitCallRet(
                    Type(Type::Kind::I1), kStringEquals, {scrutinee.value, litResult.value});
            }
            else if (scrutinee.type && scrutinee.type->kind == TypeKindSem::Number)
            {
                cond = emitBinary(
                    Opcode::FCmpEQ, Type(Type::Kind::I1), scrutinee.value, litResult.value);
            }
            else if (scrutinee.type && scrutinee.type->kind == TypeKindSem::Boolean)
            {
                // Booleans are i1 — extend to i64 for ICmpEq comparison
                Value lhsExt = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), scrutinee.value);
                Value rhsExt = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), litResult.value);
                cond = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), lhsExt, rhsExt);
            }
            else
            {
                cond = emitBinary(
                    Opcode::ICmpEq, Type(Type::Kind::I1), scrutinee.value, litResult.value);
            }
            emitCBr(cond, successBlock, failureBlock);
            return;
        }

        case MatchArm::Pattern::Kind::Expression:
        {
            if (!pattern.literal)
            {
                emitBr(failureBlock);
                return;
            }
            auto exprResult = lowerExpr(pattern.literal.get());
            Value cond = exprResult.value;
            if (exprResult.type.kind != Type::Kind::I1)
            {
                cond = emitBinary(
                    Opcode::ICmpNe, Type(Type::Kind::I1), exprResult.value, Value::constInt(0));
            }
            emitCBr(cond, successBlock, failureBlock);
            return;
        }

        case MatchArm::Pattern::Kind::Tuple:
        {
            if (!scrutinee.type || scrutinee.type->kind != TypeKindSem::Tuple)
            {
                emitBr(failureBlock);
                return;
            }

            const auto &elements = scrutinee.type->tupleElementTypes();
            if (elements.size() != pattern.subpatterns.size())
            {
                emitBr(failureBlock);
                return;
            }

            for (size_t i = 0; i < elements.size(); ++i)
            {
                size_t nextBlock = (i + 1 < elements.size())
                                       ? createBlock("match_tuple_" + std::to_string(i))
                                       : successBlock;
                PatternValue elemValue = emitTupleElement(scrutinee, i, elements[i]);
                emitPatternTest(pattern.subpatterns[i], elemValue, nextBlock, failureBlock);
                if (i + 1 < elements.size())
                {
                    setBlock(nextBlock);
                }
            }
            return;
        }

        case MatchArm::Pattern::Kind::Constructor:
        {
            if (scrutinee.type && scrutinee.type->kind == TypeKindSem::Optional)
            {
                auto emitPtrCompare = [&](Opcode op) -> Value
                {
                    unsigned ptrSlotId = nextTempId();
                    il::core::Instr ptrSlotInstr;
                    ptrSlotInstr.result = ptrSlotId;
                    ptrSlotInstr.op = Opcode::Alloca;
                    ptrSlotInstr.type = Type(Type::Kind::Ptr);
                    ptrSlotInstr.operands = {Value::constInt(8)};
                    ptrSlotInstr.loc = curLoc_;
                    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
                    Value ptrSlot = Value::temp(ptrSlotId);

                    il::core::Instr storePtrInstr;
                    storePtrInstr.op = Opcode::Store;
                    storePtrInstr.type = Type(Type::Kind::Ptr);
                    storePtrInstr.operands = {ptrSlot, scrutinee.value};
                    storePtrInstr.loc = curLoc_;
                    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

                    unsigned ptrAsI64Id = nextTempId();
                    il::core::Instr loadAsI64Instr;
                    loadAsI64Instr.result = ptrAsI64Id;
                    loadAsI64Instr.op = Opcode::Load;
                    loadAsI64Instr.type = Type(Type::Kind::I64);
                    loadAsI64Instr.operands = {ptrSlot};
                    loadAsI64Instr.loc = curLoc_;
                    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
                    Value ptrAsI64 = Value::temp(ptrAsI64Id);

                    return emitBinary(op, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
                };

                if (pattern.binding == "None")
                {
                    Value isNull = emitPtrCompare(Opcode::ICmpEq);
                    emitCBr(isNull, successBlock, failureBlock);
                    return;
                }

                if (pattern.binding == "Some")
                {
                    if (pattern.subpatterns.empty())
                    {
                        emitBr(failureBlock);
                        return;
                    }
                    Value isNotNull = emitPtrCompare(Opcode::ICmpNe);
                    size_t someBlock = createBlock("match_some");
                    emitCBr(isNotNull, someBlock, failureBlock);
                    setBlock(someBlock);

                    TypeRef innerType = scrutinee.type->innerType();
                    auto innerValue = emitOptionalUnwrap(scrutinee.value, innerType);
                    PatternValue inner{innerValue.value, innerType};
                    emitPatternTest(pattern.subpatterns[0], inner, successBlock, failureBlock);
                    return;
                }

                emitBr(failureBlock);
                return;
            }

            if (!scrutinee.type)
            {
                emitBr(failureBlock);
                return;
            }

            const std::vector<FieldLayout> *fields = nullptr;
            if (scrutinee.type->kind == TypeKindSem::Value)
            {
                const ValueTypeInfo *valueInfo = getOrCreateValueTypeInfo(scrutinee.type->name);
                if (valueInfo)
                    fields = &valueInfo->fields;
            }
            else if (scrutinee.type->kind == TypeKindSem::Entity)
            {
                const EntityTypeInfo *entityInfo = getOrCreateEntityTypeInfo(scrutinee.type->name);
                if (entityInfo)
                    fields = &entityInfo->fields;
            }

            if (!fields || fields->size() != pattern.subpatterns.size())
            {
                emitBr(failureBlock);
                return;
            }

            for (size_t i = 0; i < fields->size(); ++i)
            {
                const FieldLayout &field = (*fields)[i];
                PatternValue fieldValue{emitFieldLoad(&field, scrutinee.value), field.type};
                size_t nextBlock = (i + 1 < fields->size())
                                       ? createBlock("match_ctor_" + std::to_string(i))
                                       : successBlock;
                emitPatternTest(pattern.subpatterns[i], fieldValue, nextBlock, failureBlock);
                if (i + 1 < fields->size())
                {
                    setBlock(nextBlock);
                }
            }
            return;
        }
    }
}

void Lowerer::emitPatternBindings(const MatchArm::Pattern &pattern, const PatternValue &scrutinee)
{
    switch (pattern.kind)
    {
        case MatchArm::Pattern::Kind::Binding:
            defineLocal(pattern.binding, scrutinee.value);
            if (scrutinee.type)
                localTypes_[pattern.binding] = scrutinee.type;
            return;

        case MatchArm::Pattern::Kind::Tuple:
        {
            if (!scrutinee.type || scrutinee.type->kind != TypeKindSem::Tuple)
                return;
            const auto &elements = scrutinee.type->tupleElementTypes();
            if (elements.size() != pattern.subpatterns.size())
                return;
            for (size_t i = 0; i < elements.size(); ++i)
            {
                PatternValue elemValue = emitTupleElement(scrutinee, i, elements[i]);
                emitPatternBindings(pattern.subpatterns[i], elemValue);
            }
            return;
        }

        case MatchArm::Pattern::Kind::Constructor:
        {
            if (scrutinee.type && scrutinee.type->kind == TypeKindSem::Optional)
            {
                if (pattern.binding != "Some" || pattern.subpatterns.empty())
                    return;
                TypeRef innerType = scrutinee.type->innerType();
                auto innerValue = emitOptionalUnwrap(scrutinee.value, innerType);
                PatternValue inner{innerValue.value, innerType};
                emitPatternBindings(pattern.subpatterns[0], inner);
                return;
            }

            if (!scrutinee.type)
                return;

            const std::vector<FieldLayout> *fields = nullptr;
            if (scrutinee.type->kind == TypeKindSem::Value)
            {
                const ValueTypeInfo *valueInfo = getOrCreateValueTypeInfo(scrutinee.type->name);
                if (valueInfo)
                    fields = &valueInfo->fields;
            }
            else if (scrutinee.type->kind == TypeKindSem::Entity)
            {
                const EntityTypeInfo *entityInfo = getOrCreateEntityTypeInfo(scrutinee.type->name);
                if (entityInfo)
                    fields = &entityInfo->fields;
            }

            if (!fields || fields->size() != pattern.subpatterns.size())
                return;

            for (size_t i = 0; i < fields->size(); ++i)
            {
                const FieldLayout &field = (*fields)[i];
                PatternValue fieldValue{emitFieldLoad(&field, scrutinee.value), field.type};
                emitPatternBindings(pattern.subpatterns[i], fieldValue);
            }
            return;
        }

        default:
            return;
    }
}

//=============================================================================
// Match Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerMatchExpr(MatchExpr *expr)
{
    if (expr->arms.empty())
    {
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }

    // Lower the scrutinee and store it for reuse in pattern tests
    auto scrutinee = lowerExpr(expr->scrutinee.get());
    std::string scrutineeSlot = "__match_scrutinee";
    createSlot(scrutineeSlot, scrutinee.type);
    storeToSlot(scrutineeSlot, scrutinee.value, scrutinee.type);

    TypeRef scrutineeType = sema_.typeOf(expr->scrutinee.get());

    // Determine the result type from the first arm body
    TypeRef resultType = sema_.typeOf(expr);
    Type ilResultType = mapType(resultType);
    bool expectsOptional = resultType && resultType->kind == TypeKindSem::Optional;
    TypeRef optionalInner = expectsOptional ? resultType->innerType() : nullptr;

    // Create a result slot to store the match result
    std::string resultSlot = "__match_result";
    bool hasResult = ilResultType.kind != Type::Kind::Void;
    if (hasResult)
        createSlot(resultSlot, ilResultType);

    // ---- SwitchI32 fast path for integer-only match ----
    // When the scrutinee is Integer and every arm is either an integer literal
    // (without guard) or a wildcard/binding (the default), emit a single
    // SwitchI32 instruction instead of an O(N) if-else chain.
    if (scrutineeType && scrutineeType->kind == TypeKindSem::Integer)
    {
        bool canSwitch = true;
        size_t defaultArmIdx = SIZE_MAX;
        std::vector<std::pair<int64_t, size_t>> caseValues; // (value, arm index)

        for (size_t i = 0; i < expr->arms.size(); ++i)
        {
            const auto &arm = expr->arms[i];
            if (arm.pattern.guard)
            {
                canSwitch = false;
                break;
            }
            if (arm.pattern.kind == MatchArm::Pattern::Kind::Wildcard ||
                arm.pattern.kind == MatchArm::Pattern::Kind::Binding)
            {
                defaultArmIdx = i;
                // Only the last arm can be wildcard/binding for switch.
                if (i + 1 != expr->arms.size())
                {
                    canSwitch = false;
                    break;
                }
            }
            else if (arm.pattern.kind == MatchArm::Pattern::Kind::Literal && arm.pattern.literal &&
                     arm.pattern.literal->kind == ExprKind::IntLiteral)
            {
                auto *lit = static_cast<IntLiteralExpr *>(arm.pattern.literal.get());
                caseValues.emplace_back(lit->value, i);
            }
            else
            {
                canSwitch = false;
                break;
            }
        }

        // Need at least 2 cases to justify a switch (1 case → just use the generic path).
        if (canSwitch && caseValues.size() >= 2)
        {
            size_t endIdx = createBlock("match_end");
            size_t nocaseIdx = createBlock("match_nocase");

            // Narrow I64 scrutinee to I32 for SwitchI32.
            Value scrutineeI32 =
                emitUnary(Opcode::CastUiNarrowChk, Type(Type::Kind::I32), scrutinee.value);

            // Create arm body blocks.
            std::vector<size_t> armBlocks;
            armBlocks.reserve(expr->arms.size());
            for (size_t i = 0; i < expr->arms.size(); ++i)
                armBlocks.push_back(createBlock("match_arm_" + std::to_string(i)));

            // Determine default target: either the wildcard arm or the trap block.
            size_t defaultTarget =
                (defaultArmIdx != SIZE_MAX) ? armBlocks[defaultArmIdx] : nocaseIdx;

            // Emit SwitchI32.
            {
                il::core::Instr sw;
                sw.op = Opcode::SwitchI32;
                sw.type = Type(Type::Kind::Void);
                sw.operands.push_back(scrutineeI32);

                // Default target.
                sw.labels.push_back(currentFunc_->blocks[defaultTarget].label);
                sw.brArgs.push_back({});

                // Cases.
                for (const auto &[val, armIdx] : caseValues)
                {
                    sw.operands.push_back(Value::constInt(static_cast<int64_t>(val)));
                    sw.labels.push_back(currentFunc_->blocks[armBlocks[armIdx]].label);
                    sw.brArgs.push_back({});
                }

                sw.loc = curLoc_;
                blockMgr_.currentBlock()->instructions.push_back(std::move(sw));
                blockMgr_.currentBlock()->terminated = true;
            }

            // Emit each arm body block.
            for (size_t i = 0; i < expr->arms.size(); ++i)
            {
                const auto &arm = expr->arms[i];
                auto localsBackup = locals_;
                auto slotsBackup = slots_;
                auto localTypesBackup = localTypes_;

                setBlock(armBlocks[i]);

                // Emit bindings (wildcard/binding arms bind the scrutinee).
                Value scrutineeInArm = loadFromSlot(scrutineeSlot, scrutinee.type);
                PatternValue scrutineeValueInArm{scrutineeInArm, scrutineeType};
                emitPatternBindings(arm.pattern, scrutineeValueInArm);

                if (arm.body)
                {
                    auto bodyResult = lowerExpr(arm.body.get());
                    if (hasResult)
                    {
                        Value bodyValue = bodyResult.value;
                        if (expectsOptional)
                        {
                            TypeRef bodyType = sema_.typeOf(arm.body.get());
                            if (!bodyType || bodyType->kind != TypeKindSem::Optional)
                            {
                                if (optionalInner)
                                    bodyValue = emitOptionalWrap(bodyResult.value, optionalInner);
                            }
                        }
                        storeToSlot(resultSlot, bodyValue, ilResultType);
                    }
                }

                if (!isTerminated())
                    emitBr(endIdx);

                locals_ = std::move(localsBackup);
                slots_ = std::move(slotsBackup);
                localTypes_ = std::move(localTypesBackup);
            }

            // Trap block for non-exhaustive match.
            setBlock(nocaseIdx);
            {
                il::core::Instr trapInstr;
                trapInstr.op = Opcode::Trap;
                trapInstr.type = Type(Type::Kind::Void);
                trapInstr.loc = curLoc_;
                blockMgr_.currentBlock()->instructions.push_back(trapInstr);
            }

            // Continue from end block.
            setBlock(endIdx);
            removeSlot(scrutineeSlot);
            if (hasResult)
            {
                Value result = loadFromSlot(resultSlot, ilResultType);
                removeSlot(resultSlot);
                return {result, ilResultType};
            }
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
    }

    // Create end block for the match
    size_t endIdx = createBlock("match_end");

    // Create a trap block for non-exhaustive match fallthrough
    size_t nocaseIdx = createBlock("match_nocase");

    // Create blocks for each arm body and the next arm's test
    std::vector<size_t> armBlocks;
    std::vector<size_t> nextTestBlocks;
    for (size_t i = 0; i < expr->arms.size(); ++i)
    {
        armBlocks.push_back(createBlock("match_arm_" + std::to_string(i)));
        if (i + 1 < expr->arms.size())
        {
            nextTestBlocks.push_back(createBlock("match_test_" + std::to_string(i + 1)));
        }
        else
        {
            nextTestBlocks.push_back(nocaseIdx); // Last arm falls through to trap
        }
    }

    // Lower each arm
    for (size_t i = 0; i < expr->arms.size(); ++i)
    {
        const auto &arm = expr->arms[i];
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
            // Reload scrutinee from slot in this block for SSA correctness
            Value scrutineeInGuard = loadFromSlot(scrutineeSlot, scrutinee.type);
            PatternValue scrutineeValueInGuard{scrutineeInGuard, scrutineeType};
            emitPatternBindings(arm.pattern, scrutineeValueInGuard);
            auto guardResult = lowerExpr(arm.pattern.guard.get());
            emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
        }

        // Lower the arm body and store result
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
            auto bodyResult = lowerExpr(arm.body.get());
            if (hasResult)
            {
                Value bodyValue = bodyResult.value;
                if (expectsOptional)
                {
                    TypeRef bodyType = sema_.typeOf(arm.body.get());
                    if (!bodyType || bodyType->kind != TypeKindSem::Optional)
                    {
                        if (optionalInner)
                            bodyValue = emitOptionalWrap(bodyResult.value, optionalInner);
                    }
                }
                storeToSlot(resultSlot, bodyValue, ilResultType);
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
        if (i + 1 < expr->arms.size())
        {
            setBlock(nextTestBlocks[i]);
        }
    }

    // Remove the scrutinee slot
    removeSlot(scrutineeSlot);

    // Emit a trap for non-exhaustive match fallthrough.
    // If all arms had a wildcard/irrefutable pattern, the last arm's failure
    // branch is unreachable and the trap will be eliminated by DCE.
    setBlock(nocaseIdx);
    {
        il::core::Instr trapInstr;
        trapInstr.op = Opcode::Trap;
        trapInstr.type = Type(Type::Kind::Void);
        trapInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(trapInstr);
    }

    // Continue from end block
    setBlock(endIdx);

    // Load and return the result
    if (hasResult)
    {
        Value result = loadFromSlot(resultSlot, ilResultType);
        removeSlot(resultSlot);
        return {result, ilResultType};
    }

    return {Value::constInt(0), Type(Type::Kind::Void)};
}

} // namespace il::frontends::zia
