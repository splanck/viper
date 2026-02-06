//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Stmt.cpp
/// @brief Statement analysis for the Zia semantic analyzer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"

namespace il::frontends::zia
{

//=============================================================================
// Statement Analysis
//=============================================================================

void Sema::analyzeStmt(Stmt *stmt)
{
    if (!stmt)
        return;

    switch (stmt->kind)
    {
        case StmtKind::Block:
            analyzeBlockStmt(static_cast<BlockStmt *>(stmt));
            break;
        case StmtKind::Expr:
            analyzeExpr(static_cast<ExprStmt *>(stmt)->expr.get());
            break;
        case StmtKind::Var:
            analyzeVarStmt(static_cast<VarStmt *>(stmt));
            break;
        case StmtKind::If:
            analyzeIfStmt(static_cast<IfStmt *>(stmt));
            break;
        case StmtKind::While:
            analyzeWhileStmt(static_cast<WhileStmt *>(stmt));
            break;
        case StmtKind::For:
            analyzeForStmt(static_cast<ForStmt *>(stmt));
            break;
        case StmtKind::ForIn:
            analyzeForInStmt(static_cast<ForInStmt *>(stmt));
            break;
        case StmtKind::Return:
            analyzeReturnStmt(static_cast<ReturnStmt *>(stmt));
            break;
        case StmtKind::Break:
        case StmtKind::Continue:
            if (loopDepth_ == 0)
            {
                error(stmt->loc,
                      stmt->kind == StmtKind::Break ? "break used outside of loop"
                                                    : "continue used outside of loop");
            }
            break;
        case StmtKind::Guard:
            analyzeGuardStmt(static_cast<GuardStmt *>(stmt));
            break;
        case StmtKind::Match:
            analyzeMatchStmt(static_cast<MatchStmt *>(stmt));
            break;
    }
}

void Sema::analyzeBlockStmt(BlockStmt *stmt)
{
    pushScope();
    for (auto &s : stmt->statements)
    {
        analyzeStmt(s.get());
    }
    popScope();
}

void Sema::analyzeVarStmt(VarStmt *stmt)
{
    TypeRef declaredType = stmt->type ? resolveTypeNode(stmt->type.get()) : nullptr;
    TypeRef initType = stmt->initializer ? analyzeExpr(stmt->initializer.get()) : nullptr;

    TypeRef varType;
    if (declaredType && initType)
    {
        // BUG-VL-001: Allow integer literals in Byte range (0-255) to be assigned to Byte
        if (declaredType->kind == TypeKindSem::Byte && initType->kind == TypeKindSem::Integer)
        {
            if (stmt->initializer->kind == ExprKind::IntLiteral)
            {
                auto *lit = static_cast<IntLiteralExpr *>(stmt->initializer.get());
                if (lit->value >= 0 && lit->value <= 255)
                {
                    initType = types::byte(); // Treat as Byte literal
                }
            }
        }

        // Both declared and inferred - check compatibility
        if (!declaredType->isAssignableFrom(*initType))
        {
            errorTypeMismatch(stmt->loc, declaredType, initType);
        }
        varType = declaredType;
    }
    else if (declaredType)
    {
        varType = declaredType;
    }
    else if (initType)
    {
        varType = initType;
    }
    else
    {
        error(stmt->loc, "Cannot infer type without initializer");
        varType = types::unknown();
    }

    Symbol sym;
    sym.kind = Symbol::Kind::Variable;
    sym.name = stmt->name;
    sym.type = varType;
    sym.isFinal = stmt->isFinal;
    defineSymbol(stmt->name, sym);
}

void Sema::analyzeIfStmt(IfStmt *stmt)
{
    TypeRef condType = analyzeExpr(stmt->condition.get());
    if (condType->kind != TypeKindSem::Boolean)
    {
        error(stmt->condition->loc, "Condition must be Boolean");
    }

    // Check for null check pattern for type narrowing
    std::string nullCheckVar;
    bool isNotNull = false;
    bool hasNullCheck = tryExtractNullCheck(stmt->condition.get(), nullCheckVar, isNotNull);

    TypeRef narrowedType = nullptr;
    if (hasNullCheck)
    {
        // Look up the variable's current type
        TypeRef varType = lookupVarType(nullCheckVar);
        if (varType && varType->kind == TypeKindSem::Optional)
        {
            // Get the inner (non-optional) type
            narrowedType = varType->innerType();
        }
    }

    // Analyze then-branch with narrowing if condition is "x != null"
    if (hasNullCheck && isNotNull && narrowedType)
    {
        pushNarrowingScope();
        narrowType(nullCheckVar, narrowedType);
        analyzeStmt(stmt->thenBranch.get());
        popNarrowingScope();
    }
    else
    {
        analyzeStmt(stmt->thenBranch.get());
    }

    // Analyze else-branch with narrowing if condition is "x == null"
    if (stmt->elseBranch)
    {
        if (hasNullCheck && !isNotNull && narrowedType)
        {
            // In else branch of "x == null", x is not null
            pushNarrowingScope();
            narrowType(nullCheckVar, narrowedType);
            analyzeStmt(stmt->elseBranch.get());
            popNarrowingScope();
        }
        else
        {
            analyzeStmt(stmt->elseBranch.get());
        }
    }
}

void Sema::analyzeWhileStmt(WhileStmt *stmt)
{
    TypeRef condType = analyzeExpr(stmt->condition.get());
    if (condType->kind != TypeKindSem::Boolean)
    {
        error(stmt->condition->loc, "Condition must be Boolean");
    }

    loopDepth_++;
    analyzeStmt(stmt->body.get());
    loopDepth_--;
}

void Sema::analyzeForStmt(ForStmt *stmt)
{
    pushScope();
    if (stmt->init)
        analyzeStmt(stmt->init.get());
    if (stmt->condition)
    {
        TypeRef condType = analyzeExpr(stmt->condition.get());
        if (condType->kind != TypeKindSem::Boolean)
        {
            error(stmt->condition->loc, "Condition must be Boolean");
        }
    }
    if (stmt->update)
        analyzeExpr(stmt->update.get());
    loopDepth_++;
    analyzeStmt(stmt->body.get());
    loopDepth_--;
    popScope();
}

void Sema::analyzeForInStmt(ForInStmt *stmt)
{
    pushScope();

    TypeRef iterableType = analyzeExpr(stmt->iterable.get());

    // Determine element types from iterable
    TypeRef elementType = types::unknown();
    TypeRef secondType = types::unknown();

    if (iterableType->kind == TypeKindSem::List || iterableType->kind == TypeKindSem::Set)
    {
        elementType = iterableType->elementType();
    }
    else if (iterableType->kind == TypeKindSem::Map)
    {
        elementType = iterableType->keyType() ? iterableType->keyType() : types::string();
        secondType = iterableType->valueType();
    }
    else if (stmt->iterable && stmt->iterable->kind == ExprKind::Range)
    {
        elementType = types::integer();
    }
    else
    {
        error(stmt->iterable->loc, "Expression is not iterable");
    }

    if (stmt->isTuple)
    {
        if (iterableType->kind == TypeKindSem::Map)
        {
            // Map iteration binds (key, value)
        }
        else if (iterableType->kind == TypeKindSem::List || iterableType->kind == TypeKindSem::Set)
        {
            // List/Set iteration with tuple binding: (index, element)
            secondType = elementType;       // Element goes to second variable
            elementType = types::integer(); // Index goes to first variable
        }
        else if (iterableType->kind == TypeKindSem::Tuple)
        {
            const auto &elements = iterableType->tupleElementTypes();
            if (elements.size() == 2)
            {
                elementType = elements[0];
                secondType = elements[1];
            }
        }
        else
        {
            error(stmt->loc, "Tuple binding requires Map, List, Set, or Tuple elements");
        }
    }

    if (stmt->variableType)
    {
        TypeRef explicitType = resolveTypeNode(stmt->variableType.get());
        if (elementType && !explicitType->isAssignableFrom(*elementType))
        {
            error(stmt->loc, "Loop variable type does not match iterable element type");
        }
        elementType = explicitType;
    }

    if (stmt->isTuple && stmt->secondVariableType)
    {
        TypeRef explicitType = resolveTypeNode(stmt->secondVariableType.get());
        if (secondType && !explicitType->isAssignableFrom(*secondType))
        {
            error(stmt->loc, "Loop variable type does not match iterable element type");
        }
        secondType = explicitType;
    }

    Symbol sym;
    sym.kind = Symbol::Kind::Variable;
    sym.name = stmt->variable;
    sym.type = elementType ? elementType : types::unknown();
    sym.isFinal = true;
    defineSymbol(stmt->variable, sym);

    if (stmt->isTuple)
    {
        Symbol secondSym;
        secondSym.kind = Symbol::Kind::Variable;
        secondSym.name = stmt->secondVariable;
        secondSym.type = secondType ? secondType : types::unknown();
        secondSym.isFinal = true;
        defineSymbol(stmt->secondVariable, secondSym);
    }

    loopDepth_++;
    analyzeStmt(stmt->body.get());
    loopDepth_--;
    popScope();
}

void Sema::analyzeReturnStmt(ReturnStmt *stmt)
{
    if (stmt->value)
    {
        TypeRef valueType = analyzeExpr(stmt->value.get());
        if (expectedReturnType_ && !expectedReturnType_->isAssignableFrom(*valueType))
        {
            // Allow implicit Number -> Integer conversion in return statements
            // This enables returning Floor/Ceil/Round/Trunc results from Integer functions
            bool allowedNarrowing = (expectedReturnType_->kind == TypeKindSem::Integer &&
                                     valueType->kind == TypeKindSem::Number);
            if (!allowedNarrowing)
            {
                errorTypeMismatch(stmt->value->loc, expectedReturnType_, valueType);
            }
        }
    }
    else
    {
        // No value - must be void return
        if (expectedReturnType_ && expectedReturnType_->kind != TypeKindSem::Void)
        {
            error(stmt->loc, "Expected return value");
        }
    }
}

void Sema::analyzeGuardStmt(GuardStmt *stmt)
{
    TypeRef condType = analyzeExpr(stmt->condition.get());
    if (condType->kind != TypeKindSem::Boolean)
    {
        error(stmt->condition->loc, "Condition must be Boolean");
    }

    analyzeStmt(stmt->elseBlock.get());
    if (!stmtAlwaysExits(stmt->elseBlock.get()))
    {
        error(stmt->loc, "Guard else block must exit the scope");
    }
}

void Sema::analyzeMatchStmt(MatchStmt *stmt)
{
    TypeRef scrutineeType = analyzeExpr(stmt->scrutinee.get());

    MatchCoverage coverage;
    for (auto &arm : stmt->arms)
    {
        std::unordered_map<std::string, TypeRef> bindings;
        pushScope();

        analyzeMatchPattern(arm.pattern, scrutineeType, coverage, bindings);

        for (const auto &binding : bindings)
        {
            Symbol sym;
            sym.kind = Symbol::Kind::Variable;
            sym.name = binding.first;
            sym.type = binding.second;
            sym.isFinal = true;
            defineSymbol(binding.first, sym);
        }

        if (arm.pattern.guard)
        {
            TypeRef guardType = analyzeExpr(arm.pattern.guard.get());
            if (guardType->kind != TypeKindSem::Boolean)
            {
                error(arm.pattern.guard->loc, "Match guard must be Boolean");
            }
        }

        analyzeExpr(arm.body.get());
        popScope();
    }

    if (!coverage.hasIrrefutable)
    {
        if (scrutineeType && scrutineeType->kind == TypeKindSem::Boolean)
        {
            if (coverage.coveredBooleans.size() < 2)
            {
                error(stmt->loc,
                      "Non-exhaustive patterns: match on Boolean must cover both true "
                      "and false, or use a wildcard (_)");
            }
        }
        else if (scrutineeType && scrutineeType->isIntegral())
        {
            error(stmt->loc,
                  "Non-exhaustive patterns: match on Integer requires a wildcard (_) or "
                  "else case to be exhaustive");
        }
        else if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional)
        {
            if (!(coverage.coversNull && coverage.coversSome))
            {
                error(stmt->loc,
                      "Non-exhaustive patterns: match on optional type should use a "
                      "wildcard (_) or handle all cases");
            }
        }
    }
}

bool Sema::stmtAlwaysExits(Stmt *stmt)
{
    if (!stmt)
        return false;

    switch (stmt->kind)
    {
        case StmtKind::Return:
        case StmtKind::Break:
        case StmtKind::Continue:
            return true;

        case StmtKind::Block:
        {
            auto *block = static_cast<BlockStmt *>(stmt);
            for (auto &inner : block->statements)
            {
                if (stmtAlwaysExits(inner.get()))
                    return true;
            }
            return false;
        }

        case StmtKind::If:
        {
            auto *ifStmt = static_cast<IfStmt *>(stmt);
            if (!ifStmt->elseBranch)
                return false;
            return stmtAlwaysExits(ifStmt->thenBranch.get()) &&
                   stmtAlwaysExits(ifStmt->elseBranch.get());
        }

        default:
            return false;
    }
}

} // namespace il::frontends::zia
