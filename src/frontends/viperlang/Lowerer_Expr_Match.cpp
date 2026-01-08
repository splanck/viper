//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr_Match.cpp
/// @brief Pattern matching expression lowering for the ViperLang IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"
#include "frontends/viperlang/RuntimeNames.hpp"

namespace il::frontends::viperlang
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
                    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
                    Value ptrSlot = Value::temp(ptrSlotId);

                    il::core::Instr storePtrInstr;
                    storePtrInstr.op = Opcode::Store;
                    storePtrInstr.type = Type(Type::Kind::Ptr);
                    storePtrInstr.operands = {ptrSlot, scrutinee.value};
                    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

                    unsigned ptrAsI64Id = nextTempId();
                    il::core::Instr loadAsI64Instr;
                    loadAsI64Instr.result = ptrAsI64Id;
                    loadAsI64Instr.op = Opcode::Load;
                    loadAsI64Instr.type = Type(Type::Kind::I64);
                    loadAsI64Instr.operands = {ptrSlot};
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
                    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
                    Value ptrSlot = Value::temp(ptrSlotId);

                    il::core::Instr storePtrInstr;
                    storePtrInstr.op = Opcode::Store;
                    storePtrInstr.type = Type(Type::Kind::Ptr);
                    storePtrInstr.operands = {ptrSlot, scrutinee.value};
                    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

                    unsigned ptrAsI64Id = nextTempId();
                    il::core::Instr loadAsI64Instr;
                    loadAsI64Instr.result = ptrAsI64Id;
                    loadAsI64Instr.op = Opcode::Load;
                    loadAsI64Instr.type = Type(Type::Kind::I64);
                    loadAsI64Instr.operands = {ptrSlot};
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
                auto it = valueTypes_.find(scrutinee.type->name);
                if (it != valueTypes_.end())
                    fields = &it->second.fields;
            }
            else if (scrutinee.type->kind == TypeKindSem::Entity)
            {
                auto it = entityTypes_.find(scrutinee.type->name);
                if (it != entityTypes_.end())
                    fields = &it->second.fields;
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
                auto it = valueTypes_.find(scrutinee.type->name);
                if (it != valueTypes_.end())
                    fields = &it->second.fields;
            }
            else if (scrutinee.type->kind == TypeKindSem::Entity)
            {
                auto it = entityTypes_.find(scrutinee.type->name);
                if (it != entityTypes_.end())
                    fields = &it->second.fields;
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

    // Create end block for the match
    size_t endIdx = createBlock("match_end");

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
            nextTestBlocks.push_back(endIdx); // Last arm falls through to end
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
            emitPatternBindings(arm.pattern, scrutineeValue);
            auto guardResult = lowerExpr(arm.pattern.guard.get());
            emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
        }

        // Lower the arm body and store result
        setBlock(armBlocks[i]);
        if (!guardBlock)
            emitPatternBindings(arm.pattern, scrutineeValue);
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

} // namespace il::frontends::viperlang
