//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Sema.cpp
// Purpose: Implementation of ViperLang semantic analyzer.
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Sema.hpp"
#include <sstream>

namespace il::frontends::viperlang
{

//=============================================================================
// Scope Implementation
//=============================================================================

void Scope::define(const std::string &name, Symbol symbol)
{
    symbols_[name] = std::move(symbol);
}

Symbol *Scope::lookup(const std::string &name)
{
    auto it = symbols_.find(name);
    if (it != symbols_.end())
        return &it->second;
    if (parent_)
        return parent_->lookup(name);
    return nullptr;
}

Symbol *Scope::lookupLocal(const std::string &name)
{
    auto it = symbols_.find(name);
    return it != symbols_.end() ? &it->second : nullptr;
}

//=============================================================================
// Sema Implementation
//=============================================================================

Sema::Sema(il::support::DiagnosticEngine &diag)
    : diag_(diag), globalScope_(std::make_unique<Scope>()), currentScope_(globalScope_.get())
{
    registerBuiltins();
}

bool Sema::analyze(ModuleDecl &module)
{
    currentModule_ = &module;

    // First pass: register all top-level declarations
    for (auto &decl : module.declarations)
    {
        if (auto *func = dynamic_cast<FunctionDecl *>(decl.get()))
        {
            // Determine return type
            TypeRef returnType = func->returnType ? resolveTypeNode(func->returnType.get())
                                                  : types::voidType();

            // Build function type
            std::vector<TypeRef> paramTypes;
            for (const auto &param : func->params)
            {
                paramTypes.push_back(param.type ? resolveTypeNode(param.type.get())
                                                : types::unknown());
            }
            auto funcType = types::function(paramTypes, returnType);

            Symbol sym;
            sym.kind = Symbol::Kind::Function;
            sym.name = func->name;
            sym.type = funcType;
            sym.decl = func;
            defineSymbol(func->name, sym);
        }
        else if (auto *value = dynamic_cast<ValueDecl *>(decl.get()))
        {
            auto valueType = types::value(value->name);
            typeRegistry_[value->name] = valueType;

            Symbol sym;
            sym.kind = Symbol::Kind::Type;
            sym.name = value->name;
            sym.type = valueType;
            sym.decl = value;
            defineSymbol(value->name, sym);
        }
        else if (auto *entity = dynamic_cast<EntityDecl *>(decl.get()))
        {
            auto entityType = types::entity(entity->name);
            typeRegistry_[entity->name] = entityType;

            Symbol sym;
            sym.kind = Symbol::Kind::Type;
            sym.name = entity->name;
            sym.type = entityType;
            sym.decl = entity;
            defineSymbol(entity->name, sym);
        }
        else if (auto *iface = dynamic_cast<InterfaceDecl *>(decl.get()))
        {
            auto ifaceType = types::interface(iface->name);
            typeRegistry_[iface->name] = ifaceType;

            Symbol sym;
            sym.kind = Symbol::Kind::Type;
            sym.name = iface->name;
            sym.type = ifaceType;
            sym.decl = iface;
            defineSymbol(iface->name, sym);
        }
    }

    // Second pass: analyze declarations
    for (auto &decl : module.declarations)
    {
        if (auto *func = dynamic_cast<FunctionDecl *>(decl.get()))
        {
            analyzeFunctionDecl(*func);
        }
        else if (auto *value = dynamic_cast<ValueDecl *>(decl.get()))
        {
            analyzeValueDecl(*value);
        }
        else if (auto *entity = dynamic_cast<EntityDecl *>(decl.get()))
        {
            analyzeEntityDecl(*entity);
        }
        else if (auto *iface = dynamic_cast<InterfaceDecl *>(decl.get()))
        {
            analyzeInterfaceDecl(*iface);
        }
    }

    return !hasError_;
}

TypeRef Sema::typeOf(const Expr *expr) const
{
    auto it = exprTypes_.find(expr);
    return it != exprTypes_.end() ? it->second : types::unknown();
}

TypeRef Sema::resolveType(const TypeNode *node) const
{
    return const_cast<Sema *>(this)->resolveTypeNode(node);
}

//=============================================================================
// Declaration Analysis
//=============================================================================

void Sema::analyzeImport(ImportDecl & /*decl*/)
{
    // TODO: Implement import resolution
}

void Sema::analyzeValueDecl(ValueDecl &decl)
{
    auto selfType = types::value(decl.name);
    currentSelfType_ = selfType;

    pushScope();

    // Analyze fields
    for (auto &member : decl.members)
    {
        if (auto *field = dynamic_cast<FieldDecl *>(member.get()))
        {
            analyzeFieldDecl(*field, selfType);
        }
    }

    // Analyze methods
    for (auto &member : decl.members)
    {
        if (auto *method = dynamic_cast<MethodDecl *>(member.get()))
        {
            analyzeMethodDecl(*method, selfType);
        }
    }

    popScope();
    currentSelfType_ = nullptr;
}

void Sema::analyzeEntityDecl(EntityDecl &decl)
{
    auto selfType = types::entity(decl.name);
    currentSelfType_ = selfType;

    pushScope();

    // Analyze fields
    for (auto &member : decl.members)
    {
        if (auto *field = dynamic_cast<FieldDecl *>(member.get()))
        {
            analyzeFieldDecl(*field, selfType);
        }
    }

    // Analyze methods
    for (auto &member : decl.members)
    {
        if (auto *method = dynamic_cast<MethodDecl *>(member.get()))
        {
            analyzeMethodDecl(*method, selfType);
        }
    }

    popScope();
    currentSelfType_ = nullptr;
}

void Sema::analyzeInterfaceDecl(InterfaceDecl &decl)
{
    auto selfType = types::interface(decl.name);
    currentSelfType_ = selfType;

    pushScope();

    // Analyze method signatures
    for (auto &member : decl.members)
    {
        if (auto *method = dynamic_cast<MethodDecl *>(member.get()))
        {
            // Just register the method signature, no body analysis
            TypeRef returnType = method->returnType ? resolveTypeNode(method->returnType.get())
                                                    : types::voidType();
            std::vector<TypeRef> paramTypes;
            for (const auto &param : method->params)
            {
                paramTypes.push_back(param.type ? resolveTypeNode(param.type.get())
                                                : types::unknown());
            }
            auto methodType = types::function(paramTypes, returnType);

            Symbol sym;
            sym.kind = Symbol::Kind::Method;
            sym.name = method->name;
            sym.type = methodType;
            sym.decl = method;
            defineSymbol(method->name, sym);
        }
    }

    popScope();
    currentSelfType_ = nullptr;
}

void Sema::analyzeFunctionDecl(FunctionDecl &decl)
{
    currentFunction_ = &decl;
    expectedReturnType_ = decl.returnType ? resolveTypeNode(decl.returnType.get())
                                          : types::voidType();

    pushScope();

    // Define parameters
    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();

        Symbol sym;
        sym.kind = Symbol::Kind::Parameter;
        sym.name = param.name;
        sym.type = paramType;
        sym.isFinal = true;  // Parameters are immutable by default
        defineSymbol(param.name, sym);
    }

    // Analyze body
    if (decl.body)
    {
        analyzeStmt(decl.body.get());
    }

    popScope();

    currentFunction_ = nullptr;
    expectedReturnType_ = nullptr;
}

void Sema::analyzeFieldDecl(FieldDecl &decl, TypeRef /*ownerType*/)
{
    TypeRef fieldType = decl.type ? resolveTypeNode(decl.type.get()) : types::unknown();

    // Check initializer type
    if (decl.initializer)
    {
        TypeRef initType = analyzeExpr(decl.initializer.get());
        if (!fieldType->isAssignableFrom(*initType))
        {
            errorTypeMismatch(decl.initializer->loc, fieldType, initType);
        }
    }

    Symbol sym;
    sym.kind = Symbol::Kind::Field;
    sym.name = decl.name;
    sym.type = fieldType;
    sym.isFinal = decl.isFinal;
    sym.decl = &decl;
    defineSymbol(decl.name, sym);
}

void Sema::analyzeMethodDecl(MethodDecl &decl, TypeRef ownerType)
{
    currentSelfType_ = ownerType;
    expectedReturnType_ = decl.returnType ? resolveTypeNode(decl.returnType.get())
                                          : types::voidType();

    pushScope();

    // Define 'self' parameter implicitly
    Symbol selfSym;
    selfSym.kind = Symbol::Kind::Parameter;
    selfSym.name = "self";
    selfSym.type = ownerType;
    selfSym.isFinal = true;
    defineSymbol("self", selfSym);

    // Define explicit parameters
    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();

        Symbol sym;
        sym.kind = Symbol::Kind::Parameter;
        sym.name = param.name;
        sym.type = paramType;
        sym.isFinal = true;
        defineSymbol(param.name, sym);
    }

    // Analyze body
    if (decl.body)
    {
        analyzeStmt(decl.body.get());
    }

    popScope();

    expectedReturnType_ = nullptr;
}

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
    else if (auto *rangeExpr = dynamic_cast<RangeExpr *>(stmt->iterable.get()))
    {
        // Range produces integers
        elementType = types::integer();
    }

    // Define loop variable
    Symbol sym;
    sym.kind = Symbol::Kind::Variable;
    sym.name = stmt->variable;
    sym.type = elementType ? elementType : types::unknown();
    sym.isFinal = true;  // Loop variable is immutable
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
    analyzeExpr(stmt->scrutinee.get());
    // TODO: Implement pattern matching analysis
    for (auto &arm : stmt->arms)
    {
        analyzeExpr(arm.body.get());
    }
}

//=============================================================================
// Expression Analysis
//=============================================================================

TypeRef Sema::analyzeExpr(Expr *expr)
{
    if (!expr)
        return types::unknown();

    TypeRef result;

    switch (expr->kind)
    {
        case ExprKind::IntLiteral:
            result = analyzeIntLiteral(static_cast<IntLiteralExpr *>(expr));
            break;
        case ExprKind::NumberLiteral:
            result = analyzeNumberLiteral(static_cast<NumberLiteralExpr *>(expr));
            break;
        case ExprKind::StringLiteral:
            result = analyzeStringLiteral(static_cast<StringLiteralExpr *>(expr));
            break;
        case ExprKind::BoolLiteral:
            result = analyzeBoolLiteral(static_cast<BoolLiteralExpr *>(expr));
            break;
        case ExprKind::NullLiteral:
            result = analyzeNullLiteral(static_cast<NullLiteralExpr *>(expr));
            break;
        case ExprKind::UnitLiteral:
            result = analyzeUnitLiteral(static_cast<UnitLiteralExpr *>(expr));
            break;
        case ExprKind::Ident:
            result = analyzeIdent(static_cast<IdentExpr *>(expr));
            break;
        case ExprKind::SelfExpr:
            result = analyzeSelf(static_cast<SelfExpr *>(expr));
            break;
        case ExprKind::Binary:
            result = analyzeBinary(static_cast<BinaryExpr *>(expr));
            break;
        case ExprKind::Unary:
            result = analyzeUnary(static_cast<UnaryExpr *>(expr));
            break;
        case ExprKind::Ternary:
            result = analyzeTernary(static_cast<TernaryExpr *>(expr));
            break;
        case ExprKind::Call:
            result = analyzeCall(static_cast<CallExpr *>(expr));
            break;
        case ExprKind::Index:
            result = analyzeIndex(static_cast<IndexExpr *>(expr));
            break;
        case ExprKind::Field:
            result = analyzeField(static_cast<FieldExpr *>(expr));
            break;
        case ExprKind::OptionalChain:
            result = analyzeOptionalChain(static_cast<OptionalChainExpr *>(expr));
            break;
        case ExprKind::Coalesce:
            result = analyzeCoalesce(static_cast<CoalesceExpr *>(expr));
            break;
        case ExprKind::Is:
            result = analyzeIs(static_cast<IsExpr *>(expr));
            break;
        case ExprKind::As:
            result = analyzeAs(static_cast<AsExpr *>(expr));
            break;
        case ExprKind::Range:
            result = analyzeRange(static_cast<RangeExpr *>(expr));
            break;
        case ExprKind::New:
            result = analyzeNew(static_cast<NewExpr *>(expr));
            break;
        case ExprKind::Lambda:
            result = analyzeLambda(static_cast<LambdaExpr *>(expr));
            break;
        case ExprKind::ListLiteral:
            result = analyzeListLiteral(static_cast<ListLiteralExpr *>(expr));
            break;
        case ExprKind::MapLiteral:
            result = analyzeMapLiteral(static_cast<MapLiteralExpr *>(expr));
            break;
        case ExprKind::SetLiteral:
            result = analyzeSetLiteral(static_cast<SetLiteralExpr *>(expr));
            break;
        default:
            result = types::unknown();
            break;
    }

    exprTypes_[expr] = result;
    return result;
}

TypeRef Sema::analyzeIntLiteral(IntLiteralExpr * /*expr*/)
{
    return types::integer();
}

TypeRef Sema::analyzeNumberLiteral(NumberLiteralExpr * /*expr*/)
{
    return types::number();
}

TypeRef Sema::analyzeStringLiteral(StringLiteralExpr * /*expr*/)
{
    return types::string();
}

TypeRef Sema::analyzeBoolLiteral(BoolLiteralExpr * /*expr*/)
{
    return types::boolean();
}

TypeRef Sema::analyzeNullLiteral(NullLiteralExpr * /*expr*/)
{
    // null is Optional[Unknown] - needs context to determine actual type
    return types::optional(types::unknown());
}

TypeRef Sema::analyzeUnitLiteral(UnitLiteralExpr * /*expr*/)
{
    return types::unit();
}

TypeRef Sema::analyzeIdent(IdentExpr *expr)
{
    Symbol *sym = lookupSymbol(expr->name);
    if (!sym)
    {
        errorUndefined(expr->loc, expr->name);
        return types::unknown();
    }
    return sym->type;
}

TypeRef Sema::analyzeSelf(SelfExpr *expr)
{
    if (!currentSelfType_)
    {
        error(expr->loc, "'self' can only be used inside a method");
        return types::unknown();
    }
    return currentSelfType_;
}

TypeRef Sema::analyzeBinary(BinaryExpr *expr)
{
    TypeRef leftType = analyzeExpr(expr->left.get());
    TypeRef rightType = analyzeExpr(expr->right.get());

    switch (expr->op)
    {
        case BinaryOp::Add:
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div:
        case BinaryOp::Mod:
            // Numeric operations
            if (leftType->kind == TypeKindSem::String && expr->op == BinaryOp::Add)
            {
                // String concatenation
                return types::string();
            }
            if (leftType->isNumeric() && rightType->isNumeric())
            {
                // Return wider type
                if (leftType->kind == TypeKindSem::Number || rightType->kind == TypeKindSem::Number)
                    return types::number();
                return types::integer();
            }
            error(expr->loc, "Invalid operands for arithmetic operation");
            return types::unknown();

        case BinaryOp::Eq:
        case BinaryOp::Ne:
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge:
            // Comparison operations
            return types::boolean();

        case BinaryOp::And:
        case BinaryOp::Or:
            // Logical operations
            if (leftType->kind != TypeKindSem::Boolean || rightType->kind != TypeKindSem::Boolean)
            {
                error(expr->loc, "Logical operators require Boolean operands");
            }
            return types::boolean();

        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
            // Bitwise operations
            if (!leftType->isIntegral() || !rightType->isIntegral())
            {
                error(expr->loc, "Bitwise operators require integral operands");
            }
            return types::integer();

        case BinaryOp::Assign:
            // Assignment - LHS must be assignable, types must be compatible
            // For now, just check that the types are compatible
            if (!rightType->isConvertibleTo(*leftType))
            {
                errorTypeMismatch(expr->loc, leftType, rightType);
            }
            // Assignment expression returns the assigned value
            return leftType;
    }

    return types::unknown();
}

TypeRef Sema::analyzeUnary(UnaryExpr *expr)
{
    TypeRef operandType = analyzeExpr(expr->operand.get());

    switch (expr->op)
    {
        case UnaryOp::Neg:
            if (!operandType->isNumeric())
            {
                error(expr->loc, "Negation requires numeric operand");
            }
            return operandType;

        case UnaryOp::Not:
            if (operandType->kind != TypeKindSem::Boolean)
            {
                error(expr->loc, "Logical not requires Boolean operand");
            }
            return types::boolean();

        case UnaryOp::BitNot:
            if (!operandType->isIntegral())
            {
                error(expr->loc, "Bitwise not requires integral operand");
            }
            return types::integer();
    }

    return types::unknown();
}

TypeRef Sema::analyzeTernary(TernaryExpr *expr)
{
    TypeRef condType = analyzeExpr(expr->condition.get());
    if (condType->kind != TypeKindSem::Boolean)
    {
        error(expr->condition->loc, "Condition must be Boolean");
    }

    TypeRef thenType = analyzeExpr(expr->thenExpr.get());
    TypeRef elseType = analyzeExpr(expr->elseExpr.get());

    // TODO: Compute common type
    if (thenType->equals(*elseType))
        return thenType;
    if (thenType->isAssignableFrom(*elseType))
        return thenType;
    if (elseType->isAssignableFrom(*thenType))
        return elseType;

    error(expr->loc, "Incompatible types in ternary expression");
    return types::unknown();
}

/// @brief Try to extract a dotted name from a field access chain.
/// @param expr The expression to extract from.
/// @param out The output string to append to.
/// @return True if successful, false otherwise.
static bool extractDottedName(Expr *expr, std::string &out)
{
    if (auto *ident = dynamic_cast<IdentExpr *>(expr))
    {
        out = ident->name;
        return true;
    }
    if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr))
    {
        if (!extractDottedName(fieldExpr->base.get(), out))
            return false;
        out += ".";
        out += fieldExpr->field;
        return true;
    }
    return false;
}

TypeRef Sema::analyzeCall(CallExpr *expr)
{
    // First, try to resolve dotted runtime function names like Viper.Terminal.Say
    std::string dottedName;
    if (extractDottedName(expr->callee.get(), dottedName))
    {
        // Check if it's a known runtime function
        auto it = runtimeFunctions_.find(dottedName);
        if (it != runtimeFunctions_.end())
        {
            // Analyze arguments
            for (auto &arg : expr->args)
            {
                analyzeExpr(arg.value.get());
            }
            // Store the resolved runtime call info
            runtimeCallees_[expr] = dottedName;
            return it->second;
        }
    }

    TypeRef calleeType = analyzeExpr(expr->callee.get());

    // Analyze arguments
    for (auto &arg : expr->args)
    {
        analyzeExpr(arg.value.get());
    }

    // If callee is a function type, return its return type
    if (calleeType->kind == TypeKindSem::Function)
    {
        return calleeType->returnType();
    }

    // If callee is unknown, return unknown
    if (calleeType->kind == TypeKindSem::Unknown)
    {
        return types::unknown();
    }

    // Could be a constructor call (Type(args))
    if (calleeType->kind == TypeKindSem::Value || calleeType->kind == TypeKindSem::Entity)
    {
        return calleeType;
    }

    error(expr->loc, "Expression is not callable");
    return types::unknown();
}

TypeRef Sema::analyzeIndex(IndexExpr *expr)
{
    TypeRef baseType = analyzeExpr(expr->base.get());
    TypeRef indexType = analyzeExpr(expr->index.get());

    if (baseType->kind == TypeKindSem::List || baseType->kind == TypeKindSem::String)
    {
        if (!indexType->isIntegral())
        {
            error(expr->index->loc, "Index must be an integer");
        }
        if (baseType->kind == TypeKindSem::String)
            return types::string();
        return baseType->elementType() ? baseType->elementType() : types::unknown();
    }

    if (baseType->kind == TypeKindSem::Map)
    {
        return baseType->valueType() ? baseType->valueType() : types::unknown();
    }

    error(expr->loc, "Expression is not indexable");
    return types::unknown();
}

TypeRef Sema::analyzeField(FieldExpr *expr)
{
    TypeRef baseType = analyzeExpr(expr->base.get());

    // TODO: Look up field in type's member table
    // For now, return unknown
    return types::unknown();
}

TypeRef Sema::analyzeOptionalChain(OptionalChainExpr *expr)
{
    TypeRef baseType = analyzeExpr(expr->base.get());

    // Result is always optional
    // TODO: Look up field type
    return types::optional(types::unknown());
}

TypeRef Sema::analyzeCoalesce(CoalesceExpr *expr)
{
    TypeRef leftType = analyzeExpr(expr->left.get());
    TypeRef rightType = analyzeExpr(expr->right.get());

    // Left should be optional
    if (leftType->kind != TypeKindSem::Optional)
    {
        error(expr->left->loc, "Left side of ?? must be optional");
    }

    // Result is the unwrapped type
    TypeRef innerType = leftType->innerType();
    return innerType ? innerType : rightType;
}

TypeRef Sema::analyzeIs(IsExpr *expr)
{
    analyzeExpr(expr->value.get());
    resolveTypeNode(expr->type.get());
    return types::boolean();
}

TypeRef Sema::analyzeAs(AsExpr *expr)
{
    analyzeExpr(expr->value.get());
    return resolveTypeNode(expr->type.get());
}

TypeRef Sema::analyzeRange(RangeExpr *expr)
{
    TypeRef startType = analyzeExpr(expr->start.get());
    TypeRef endType = analyzeExpr(expr->end.get());

    if (!startType->isIntegral() || !endType->isIntegral())
    {
        error(expr->loc, "Range bounds must be integers");
    }

    // Range type is internal - used for iteration
    return types::list(types::integer());
}

TypeRef Sema::analyzeNew(NewExpr *expr)
{
    TypeRef type = resolveTypeNode(expr->type.get());

    if (type->kind != TypeKindSem::Entity)
    {
        error(expr->loc, "'new' can only be used with entity types");
    }

    // Analyze constructor arguments
    for (auto &arg : expr->args)
    {
        analyzeExpr(arg.value.get());
    }

    return type;
}

TypeRef Sema::analyzeLambda(LambdaExpr *expr)
{
    pushScope();

    std::vector<TypeRef> paramTypes;
    for (const auto &param : expr->params)
    {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();
        paramTypes.push_back(paramType);

        Symbol sym;
        sym.kind = Symbol::Kind::Parameter;
        sym.name = param.name;
        sym.type = paramType;
        sym.isFinal = true;
        defineSymbol(param.name, sym);
    }

    TypeRef bodyType = analyzeExpr(expr->body.get());

    popScope();

    TypeRef returnType = expr->returnType ? resolveTypeNode(expr->returnType.get()) : bodyType;
    return types::function(paramTypes, returnType);
}

TypeRef Sema::analyzeListLiteral(ListLiteralExpr *expr)
{
    TypeRef elementType = types::unknown();

    for (auto &elem : expr->elements)
    {
        TypeRef elemType = analyzeExpr(elem.get());
        if (elementType->kind == TypeKindSem::Unknown)
        {
            elementType = elemType;
        }
        else if (!elementType->equals(*elemType))
        {
            // TODO: Find common type
        }
    }

    return types::list(elementType);
}

TypeRef Sema::analyzeMapLiteral(MapLiteralExpr *expr)
{
    TypeRef keyType = types::unknown();
    TypeRef valueType = types::unknown();

    for (auto &entry : expr->entries)
    {
        TypeRef kType = analyzeExpr(entry.key.get());
        TypeRef vType = analyzeExpr(entry.value.get());

        if (keyType->kind == TypeKindSem::Unknown)
            keyType = kType;
        if (valueType->kind == TypeKindSem::Unknown)
            valueType = vType;
    }

    return types::map(keyType, valueType);
}

TypeRef Sema::analyzeSetLiteral(SetLiteralExpr *expr)
{
    TypeRef elementType = types::unknown();

    for (auto &elem : expr->elements)
    {
        TypeRef elemType = analyzeExpr(elem.get());
        if (elementType->kind == TypeKindSem::Unknown)
        {
            elementType = elemType;
        }
    }

    return types::set(elementType);
}

//=============================================================================
// Type Resolution
//=============================================================================

TypeRef Sema::resolveNamedType(const std::string &name) const
{
    // Built-in types
    if (name == "Integer")
        return types::integer();
    if (name == "Number")
        return types::number();
    if (name == "Boolean")
        return types::boolean();
    if (name == "String")
        return types::string();
    if (name == "Byte")
        return types::byte();
    if (name == "Unit")
        return types::unit();
    if (name == "Void")
        return types::voidType();
    if (name == "Error")
        return types::error();
    if (name == "Ptr")
        return types::ptr();

    // Look up in registry
    auto it = typeRegistry_.find(name);
    if (it != typeRegistry_.end())
        return it->second;

    return nullptr;
}

TypeRef Sema::resolveTypeNode(const TypeNode *node)
{
    if (!node)
        return types::unknown();

    switch (node->kind)
    {
        case TypeKind::Named:
        {
            auto *named = static_cast<const NamedType *>(node);
            TypeRef resolved = resolveNamedType(named->name);
            if (!resolved)
            {
                error(node->loc, "Unknown type: " + named->name);
                return types::unknown();
            }
            return resolved;
        }

        case TypeKind::Generic:
        {
            auto *generic = static_cast<const GenericType *>(node);
            std::vector<TypeRef> args;
            for (const auto &arg : generic->args)
            {
                args.push_back(resolveTypeNode(arg.get()));
            }

            // Built-in generic types
            if (generic->name == "List")
            {
                return types::list(args.empty() ? types::unknown() : args[0]);
            }
            if (generic->name == "Set")
            {
                return types::set(args.empty() ? types::unknown() : args[0]);
            }
            if (generic->name == "Map")
            {
                return types::map(args.size() > 0 ? args[0] : types::unknown(),
                                  args.size() > 1 ? args[1] : types::unknown());
            }
            if (generic->name == "Result")
            {
                return types::result(args.empty() ? types::unit() : args[0]);
            }

            // User-defined generic type
            TypeRef baseType = resolveNamedType(generic->name);
            if (!baseType)
            {
                error(node->loc, "Unknown type: " + generic->name);
                return types::unknown();
            }

            // Create instantiated type
            return std::make_shared<ViperType>(baseType->kind, baseType->name, args);
        }

        case TypeKind::Optional:
        {
            auto *opt = static_cast<const OptionalType *>(node);
            TypeRef inner = resolveTypeNode(opt->inner.get());
            return types::optional(inner);
        }

        case TypeKind::Function:
        {
            auto *func = static_cast<const FunctionType *>(node);
            std::vector<TypeRef> params;
            for (const auto &param : func->params)
            {
                params.push_back(resolveTypeNode(param.get()));
            }
            TypeRef ret = func->returnType ? resolveTypeNode(func->returnType.get())
                                           : types::voidType();
            return types::function(params, ret);
        }

        case TypeKind::Tuple:
        {
            // TODO: Implement tuple types
            return types::unknown();
        }
    }

    return types::unknown();
}

//=============================================================================
// Scope Management
//=============================================================================

void Sema::pushScope()
{
    currentScope_ = new Scope(currentScope_);
}

void Sema::popScope()
{
    Scope *old = currentScope_;
    currentScope_ = currentScope_->parent();
    delete old;
}

void Sema::defineSymbol(const std::string &name, Symbol symbol)
{
    currentScope_->define(name, std::move(symbol));
}

Symbol *Sema::lookupSymbol(const std::string &name)
{
    return currentScope_->lookup(name);
}

//=============================================================================
// Error Reporting
//=============================================================================

void Sema::error(SourceLoc loc, const std::string &message)
{
    hasError_ = true;
    diag_.report({il::support::Severity::Error, message, loc, ""});
}

void Sema::errorUndefined(SourceLoc loc, const std::string &name)
{
    error(loc, "Undefined identifier: " + name);
}

void Sema::errorTypeMismatch(SourceLoc loc, TypeRef expected, TypeRef actual)
{
    error(loc, "Type mismatch: expected " + expected->toString() + ", got " + actual->toString());
}

//=============================================================================
// Built-in Functions
//=============================================================================

void Sema::registerBuiltins()
{
    // print(String) -> Void
    {
        auto printType = types::function({types::string()}, types::voidType());
        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.name = "print";
        sym.type = printType;
        defineSymbol("print", sym);
    }

    // println(String) -> Void (alias for print with newline)
    {
        auto printlnType = types::function({types::string()}, types::voidType());
        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.name = "println";
        sym.type = printlnType;
        defineSymbol("println", sym);
    }

    // input() -> String
    {
        auto inputType = types::function({}, types::string());
        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.name = "input";
        sym.type = inputType;
        defineSymbol("input", sym);
    }

    // toString(Any) -> String
    {
        auto toStringType = types::function({types::any()}, types::string());
        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.name = "toString";
        sym.type = toStringType;
        defineSymbol("toString", sym);
    }

    // Register Viper.Terminal runtime functions
    runtimeFunctions_["Viper.Terminal.Say"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SayInt"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SayNum"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SayBool"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.Print"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.PrintInt"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.PrintNum"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.ReadLine"] = types::string();
    runtimeFunctions_["Viper.Terminal.GetKey"] = types::string();
    runtimeFunctions_["Viper.Terminal.InKey"] = types::string();
    runtimeFunctions_["Viper.Terminal.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.Bell"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.Flush"] = types::voidType();

    // Register Viper.String runtime functions
    runtimeFunctions_["Viper.String.Concat"] = types::string();
    runtimeFunctions_["Viper.String.Length"] = types::integer();
    runtimeFunctions_["Viper.String.Left"] = types::string();
    runtimeFunctions_["Viper.String.Right"] = types::string();
    runtimeFunctions_["Viper.String.Mid"] = types::string();
    runtimeFunctions_["Viper.String.Trim"] = types::string();
    runtimeFunctions_["Viper.String.ToUpper"] = types::string();
    runtimeFunctions_["Viper.String.ToLower"] = types::string();
    runtimeFunctions_["Viper.String.IndexOf"] = types::integer();
    runtimeFunctions_["Viper.String.Chr"] = types::string();
    runtimeFunctions_["Viper.String.Asc"] = types::integer();

    // Register Viper.Math runtime functions
    runtimeFunctions_["Viper.Math.Abs"] = types::number();
    runtimeFunctions_["Viper.Math.AbsInt"] = types::integer();
    runtimeFunctions_["Viper.Math.Sin"] = types::number();
    runtimeFunctions_["Viper.Math.Cos"] = types::number();
    runtimeFunctions_["Viper.Math.Tan"] = types::number();
    runtimeFunctions_["Viper.Math.Sqrt"] = types::number();
    runtimeFunctions_["Viper.Math.Log"] = types::number();
    runtimeFunctions_["Viper.Math.Exp"] = types::number();
    runtimeFunctions_["Viper.Math.Floor"] = types::number();
    runtimeFunctions_["Viper.Math.Ceil"] = types::number();
    runtimeFunctions_["Viper.Math.Round"] = types::number();
    runtimeFunctions_["Viper.Math.Min"] = types::number();
    runtimeFunctions_["Viper.Math.Max"] = types::number();
    runtimeFunctions_["Viper.Math.MinInt"] = types::integer();
    runtimeFunctions_["Viper.Math.MaxInt"] = types::integer();

    // Register Viper.Random runtime functions
    runtimeFunctions_["Viper.Random.Next"] = types::number();
    runtimeFunctions_["Viper.Random.Seed"] = types::voidType();

    // Register Viper.Environment runtime functions
    runtimeFunctions_["Viper.Environment.GetArgument"] = types::string();
    runtimeFunctions_["Viper.Environment.GetArgumentCount"] = types::integer();
    runtimeFunctions_["Viper.Environment.GetCommandLine"] = types::string();
}

} // namespace il::frontends::viperlang
