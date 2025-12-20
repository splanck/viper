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

    if (stmt->initializer)
    {
        auto result = lowerExpr(stmt->initializer.get());
        initValue = result.value;
        ilType = result.type;
    }
    else
    {
        // Default initialization
        TypeRef varType = stmt->type ? sema_.resolveType(stmt->type.get()) : types::unknown();
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
    // For now, only support range iteration
    auto *rangeExpr = dynamic_cast<RangeExpr *>(stmt->iterable.get());
    if (!rangeExpr)
    {
        // TODO: Support collection iteration
        return;
    }

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
}

void Lowerer::lowerReturnStmt(ReturnStmt *stmt)
{
    if (stmt->value)
    {
        auto result = lowerExpr(stmt->value.get());
        emitRet(result.value);
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

        // In the current block, test the pattern
        Value scrutineeVal = loadFromSlot(scrutineeSlot, scrutinee.type);

        switch (arm.pattern.kind)
        {
            case MatchArm::Pattern::Kind::Wildcard:
                // Wildcard always matches - just jump to arm body
                emitBr(armBlocks[i]);
                break;

            case MatchArm::Pattern::Kind::Literal:
            {
                // Compare scrutinee with literal
                auto litResult = lowerExpr(arm.pattern.literal.get());
                Value cond;
                if (scrutinee.type.kind == Type::Kind::Str)
                {
                    // String comparison
                    cond = emitCallRet(
                        Type(Type::Kind::I1), kStringEquals, {scrutineeVal, litResult.value});
                }
                else
                {
                    // Integer comparison
                    cond = emitBinary(
                        Opcode::ICmpEq, Type(Type::Kind::I1), scrutineeVal, litResult.value);
                }

                // If guard is present, check it too
                if (arm.pattern.guard)
                {
                    size_t guardBlock = createBlock("match_guard_" + std::to_string(i));
                    emitCBr(cond, guardBlock, nextTestBlocks[i]);
                    setBlock(guardBlock);

                    auto guardResult = lowerExpr(arm.pattern.guard.get());
                    emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
                }
                else
                {
                    emitCBr(cond, armBlocks[i], nextTestBlocks[i]);
                }
                break;
            }

            case MatchArm::Pattern::Kind::Binding:
            {
                // Binding pattern - bind the scrutinee to a local variable
                // and execute the arm (always matches if reached)
                defineLocal(arm.pattern.binding, scrutineeVal);

                // If guard is present, check it
                if (arm.pattern.guard)
                {
                    auto guardResult = lowerExpr(arm.pattern.guard.get());
                    emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
                }
                else
                {
                    emitBr(armBlocks[i]);
                }
                break;
            }

            case MatchArm::Pattern::Kind::Constructor:
            case MatchArm::Pattern::Kind::Tuple:
                // TODO: Implement constructor and tuple patterns
                emitBr(nextTestBlocks[i]);
                break;
        }

        // Lower the arm body (arm.body is an expression)
        setBlock(armBlocks[i]);
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
