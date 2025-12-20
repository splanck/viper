//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Stmt.cpp
/// @brief Statement analysis for the ViperLang semantic analyzer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Sema.hpp"

namespace il::frontends::viperlang
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
            // TODO: Check if inside loop
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

    analyzeStmt(stmt->thenBranch.get());
    if (stmt->elseBranch)
    {
        analyzeStmt(stmt->elseBranch.get());
    }
}

void Sema::analyzeWhileStmt(WhileStmt *stmt)
{
    TypeRef condType = analyzeExpr(stmt->condition.get());
    if (condType->kind != TypeKindSem::Boolean)
    {
        error(stmt->condition->loc, "Condition must be Boolean");
    }

    analyzeStmt(stmt->body.get());
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
    analyzeStmt(stmt->body.get());
    popScope();
}

void Sema::analyzeForInStmt(ForInStmt *stmt)
{
    pushScope();

    TypeRef iterableType = analyzeExpr(stmt->iterable.get());

    // Determine element type from iterable
    TypeRef elementType = types::unknown();
    if (iterableType->kind == TypeKindSem::List || iterableType->kind == TypeKindSem::Set)
    {
        elementType = iterableType->elementType();
    }
    else if (stmt->iterable && stmt->iterable->kind == ExprKind::Range)
    {
        // Range produces integers
        elementType = types::integer();
    }

    // Define loop variable
    Symbol sym;
    sym.kind = Symbol::Kind::Variable;
    sym.name = stmt->variable;
    sym.type = elementType ? elementType : types::unknown();
    sym.isFinal = true; // Loop variable is immutable
    defineSymbol(stmt->variable, sym);

    analyzeStmt(stmt->body.get());
    popScope();
}

void Sema::analyzeReturnStmt(ReturnStmt *stmt)
{
    if (stmt->value)
    {
        TypeRef valueType = analyzeExpr(stmt->value.get());
        if (expectedReturnType_ && !expectedReturnType_->isAssignableFrom(*valueType))
        {
            errorTypeMismatch(stmt->value->loc, expectedReturnType_, valueType);
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
    // TODO: Check that else block always exits (return, break, continue, trap)
}

void Sema::analyzeMatchStmt(MatchStmt *stmt)
{
    TypeRef scrutineeType = analyzeExpr(stmt->scrutinee.get());

    // Track if we have a wildcard or exhaustive pattern coverage
    bool hasWildcard = false;
    std::set<int64_t> coveredIntegers;
    std::set<bool> coveredBooleans;

    for (auto &arm : stmt->arms)
    {
        // Analyze the pattern and body
        const auto &pattern = arm.pattern;

        if (pattern.kind == MatchArm::Pattern::Kind::Wildcard)
        {
            hasWildcard = true;
        }
        else if (pattern.kind == MatchArm::Pattern::Kind::Binding)
        {
            // A binding without a guard acts as a wildcard
            if (!pattern.guard)
            {
                hasWildcard = true;
            }
        }
        else if (pattern.kind == MatchArm::Pattern::Kind::Literal && pattern.literal)
        {
            // Track which literals are covered
            if (pattern.literal->kind == ExprKind::IntLiteral)
            {
                coveredIntegers.insert(static_cast<IntLiteralExpr *>(pattern.literal.get())->value);
            }
            else if (pattern.literal->kind == ExprKind::BoolLiteral)
            {
                coveredBooleans.insert(
                    static_cast<BoolLiteralExpr *>(pattern.literal.get())->value);
            }
        }

        analyzeExpr(arm.body.get());
    }

    // Check exhaustiveness based on scrutinee type
    if (!hasWildcard)
    {
        if (scrutineeType && scrutineeType->kind == TypeKindSem::Boolean)
        {
            // Boolean must cover both true and false
            if (coveredBooleans.size() < 2)
            {
                error(stmt->loc,
                      "Non-exhaustive patterns: match on Boolean must cover both true "
                      "and false, or use a wildcard (_)");
            }
        }
        else if (scrutineeType && scrutineeType->isIntegral())
        {
            // Integer types need a wildcard since we can't enumerate all values
            error(stmt->loc,
                  "Non-exhaustive patterns: match on Integer requires a wildcard (_) or "
                  "else case to be exhaustive");
        }
        else if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional)
        {
            // Optional types need to handle both Some and None cases
            // For now, just warn that a wildcard is recommended
            error(stmt->loc,
                  "Non-exhaustive patterns: match on optional type should use a "
                  "wildcard (_) or handle all cases");
        }
    }
}


} // namespace il::frontends::viperlang
