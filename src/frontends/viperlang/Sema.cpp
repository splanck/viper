//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema.cpp
/// @brief Implementation of ViperLang semantic analyzer.
///
/// @details This file implements the Sema class which performs type checking
/// and name resolution on ViperLang ASTs. Key implementation details:
///
/// ## Two-Pass Analysis
///
/// 1. **First pass**: Register all top-level declarations (functions, types,
///    global variables) in the global scope without analyzing bodies
/// 2. **Second pass**: Analyze declaration bodies with full symbol visibility
///
/// ## Scope Management
///
/// Scopes are dynamically allocated and linked via parent pointers:
/// - Global scope: Functions, types, global variables
/// - Type scope: Fields and methods of value/entity/interface
/// - Function scope: Parameters
/// - Block scope: Local variables
///
/// ## Expression Type Inference
///
/// Expression types are computed bottom-up and cached in exprTypes_ map.
/// Each analyzeXxx method returns the inferred type and stores it.
///
/// ## Runtime Function Resolution
///
/// Calls to runtime functions (Viper.Terminal.Say, etc.) are detected by
/// extracting dotted names from field access chains and looking them up
/// in runtimeFunctions_. Resolved calls are stored in runtimeCallees_.
///
/// @see Sema.hpp for the class interface
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Sema.hpp"
#include <functional>
#include <set>
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
            TypeRef returnType =
                func->returnType ? resolveTypeNode(func->returnType.get()) : types::voidType();

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
        else if (auto *gvar = dynamic_cast<GlobalVarDecl *>(decl.get()))
        {
            // Determine the variable type
            TypeRef varType;
            if (gvar->type)
            {
                varType = resolveTypeNode(gvar->type.get());
            }
            else if (gvar->initializer)
            {
                // Type inference from initializer - defer to second pass
                varType = types::unknown();
            }
            else
            {
                varType = types::unknown();
            }

            Symbol sym;
            sym.kind = Symbol::Kind::Variable;
            sym.name = gvar->name;
            sym.type = varType;
            sym.isFinal = gvar->isFinal;
            sym.decl = gvar;
            defineSymbol(gvar->name, sym);
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
        else if (auto *gvar = dynamic_cast<GlobalVarDecl *>(decl.get()))
        {
            analyzeGlobalVarDecl(*gvar);
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

void Sema::analyzeGlobalVarDecl(GlobalVarDecl &decl)
{
    // Analyze initializer if present
    if (decl.initializer)
    {
        TypeRef initType = analyzeExpr(decl.initializer.get());

        // If type was inferred, update the symbol
        Symbol *sym = lookupSymbol(decl.name);
        if (sym && sym->type->isUnknown())
        {
            sym->type = initType;
        }
        else if (sym && !sym->type->isAssignableFrom(*initType))
        {
            errorTypeMismatch(decl.initializer->loc, sym->type, initType);
        }
    }
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
            TypeRef returnType =
                method->returnType ? resolveTypeNode(method->returnType.get()) : types::voidType();
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
    expectedReturnType_ =
        decl.returnType ? resolveTypeNode(decl.returnType.get()) : types::voidType();

    pushScope();

    // Define parameters
    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();

        Symbol sym;
        sym.kind = Symbol::Kind::Parameter;
        sym.name = param.name;
        sym.type = paramType;
        sym.isFinal = true; // Parameters are immutable by default
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

void Sema::analyzeFieldDecl(FieldDecl &decl, TypeRef ownerType)
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

    // Store field type and visibility for access checking
    if (ownerType)
    {
        std::string fieldKey = ownerType->name + "." + decl.name;
        fieldTypes_[fieldKey] = fieldType;
        memberVisibility_[fieldKey] = decl.visibility;
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
    TypeRef returnType =
        decl.returnType ? resolveTypeNode(decl.returnType.get()) : types::voidType();
    expectedReturnType_ = returnType;

    // Build parameter types
    std::vector<TypeRef> paramTypes;
    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();
        paramTypes.push_back(paramType);
    }

    // Register method type: "TypeName.methodName" -> function type
    std::string methodKey = ownerType->name + "." + decl.name;
    methodTypes_[methodKey] = types::function(paramTypes, returnType);
    memberVisibility_[methodKey] = decl.visibility;

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
    bool hasElseArm = false;
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
            if (auto *intLit = dynamic_cast<IntLiteralExpr *>(pattern.literal.get()))
            {
                coveredIntegers.insert(intLit->value);
            }
            else if (auto *boolLit = dynamic_cast<BoolLiteralExpr *>(pattern.literal.get()))
            {
                coveredBooleans.insert(boolLit->value);
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
                error(stmt->loc, "Non-exhaustive patterns: match on Boolean must cover both true "
                                 "and false, or use a wildcard (_)");
            }
        }
        else if (scrutineeType && scrutineeType->isIntegral())
        {
            // Integer types need a wildcard since we can't enumerate all values
            error(stmt->loc, "Non-exhaustive patterns: match on Integer requires a wildcard (_) or "
                             "else case to be exhaustive");
        }
        else if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional)
        {
            // Optional types need to handle both Some and None cases
            // For now, just warn that a wildcard is recommended
            error(stmt->loc, "Non-exhaustive patterns: match on optional type should use a "
                             "wildcard (_) or handle all cases");
        }
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
        case ExprKind::Match:
            result = analyzeMatchExpr(static_cast<MatchExpr *>(expr));
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
        case ExprKind::Tuple:
            result = analyzeTuple(static_cast<TupleExpr *>(expr));
            break;
        case ExprKind::TupleIndex:
            result = analyzeTupleIndex(static_cast<TupleIndexExpr *>(expr));
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

    // Check if this is a field or method access on a value or entity type
    if (baseType && (baseType->kind == TypeKindSem::Value || baseType->kind == TypeKindSem::Entity))
    {
        std::string memberKey = baseType->name + "." + expr->field;

        // Check if accessing from inside or outside the type
        bool isInsideType = currentSelfType_ && currentSelfType_->name == baseType->name;

        // Check visibility
        auto visIt = memberVisibility_.find(memberKey);
        if (visIt != memberVisibility_.end())
        {
            if (visIt->second == Visibility::Private && !isInsideType)
            {
                error(expr->loc, "Cannot access private member '" + expr->field + "' of type '" +
                                     baseType->name + "'");
            }
        }

        // Check if it's a method
        auto methodIt = methodTypes_.find(memberKey);
        if (methodIt != methodTypes_.end())
        {
            return methodIt->second;
        }

        // Check if it's a field
        auto fieldIt = fieldTypes_.find(memberKey);
        if (fieldIt != fieldTypes_.end())
        {
            return fieldIt->second;
        }
    }

    // Handle built-in properties like .count on lists
    if (baseType && baseType->kind == TypeKindSem::List)
    {
        if (expr->field == "count" || expr->field == "size")
        {
            return types::integer();
        }
    }

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

TypeRef Sema::analyzeMatchExpr(MatchExpr *expr)
{
    TypeRef scrutineeType = analyzeExpr(expr->scrutinee.get());

    // Track if we have a wildcard or exhaustive pattern coverage
    bool hasWildcard = false;
    std::set<int64_t> coveredIntegers;
    std::set<bool> coveredBooleans;

    TypeRef resultType = nullptr;

    for (auto &arm : expr->arms)
    {
        // Analyze the pattern
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
            if (auto *intLit = dynamic_cast<IntLiteralExpr *>(pattern.literal.get()))
            {
                coveredIntegers.insert(intLit->value);
            }
            else if (auto *boolLit = dynamic_cast<BoolLiteralExpr *>(pattern.literal.get()))
            {
                coveredBooleans.insert(boolLit->value);
            }
        }

        // Analyze the body and track result type
        TypeRef bodyType = analyzeExpr(arm.body.get());
        if (!resultType)
        {
            resultType = bodyType;
        }
        else if (bodyType && !resultType->equals(*bodyType))
        {
            // Types differ - for now, use the first type
            // TODO: Find common supertype
        }
    }

    // Check exhaustiveness based on scrutinee type
    if (!hasWildcard)
    {
        if (scrutineeType && scrutineeType->kind == TypeKindSem::Boolean)
        {
            // Boolean must cover both true and false
            if (coveredBooleans.size() < 2)
            {
                error(expr->loc, "Non-exhaustive patterns: match on Boolean must cover both true "
                                 "and false, or use a wildcard (_)");
            }
        }
        else if (scrutineeType && scrutineeType->isIntegral())
        {
            // Integer types need a wildcard since we can't enumerate all values
            error(expr->loc, "Non-exhaustive patterns: match on Integer requires a wildcard (_) or "
                             "else case to be exhaustive");
        }
        else if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional)
        {
            // Optional types need to handle both Some and None cases
            error(expr->loc, "Non-exhaustive patterns: match on optional type should use a "
                             "wildcard (_) or handle all cases");
        }
    }

    return resultType ? resultType : types::unknown();
}

TypeRef Sema::analyzeNew(NewExpr *expr)
{
    TypeRef type = resolveTypeNode(expr->type.get());

    // Allow new for entity types and collection types (List, Set, Map)
    if (type->kind != TypeKindSem::Entity && type->kind != TypeKindSem::List &&
        type->kind != TypeKindSem::Set && type->kind != TypeKindSem::Map)
    {
        error(expr->loc, "'new' can only be used with entity or collection types");
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
    // Collect names that are local to the lambda (params)
    std::set<std::string> lambdaLocals;
    for (const auto &param : expr->params)
    {
        lambdaLocals.insert(param.name);
    }

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

    // Collect captured variables (free variables referenced in the body)
    collectCaptures(expr->body.get(), lambdaLocals, expr->captures);

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

TypeRef Sema::analyzeTuple(TupleExpr *expr)
{
    std::vector<TypeRef> elementTypes;
    for (auto &elem : expr->elements)
    {
        elementTypes.push_back(analyzeExpr(elem.get()));
    }
    return types::tuple(std::move(elementTypes));
}

TypeRef Sema::analyzeTupleIndex(TupleIndexExpr *expr)
{
    TypeRef tupleType = analyzeExpr(expr->tuple.get());

    if (!tupleType->isTuple())
    {
        error(expr->loc, "tuple index access requires a tuple type, got '" + tupleType->toString() +
                             "'");
        return types::unknown();
    }

    if (expr->index >= tupleType->tupleElementTypes().size())
    {
        error(expr->loc, "tuple index " + std::to_string(expr->index) + " is out of bounds for " +
                             tupleType->toString());
        return types::unknown();
    }

    return tupleType->tupleElementType(expr->index);
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
            TypeRef ret =
                func->returnType ? resolveTypeNode(func->returnType.get()) : types::voidType();
            return types::function(params, ret);
        }

        case TypeKind::Tuple:
        {
            const auto *tupleType = static_cast<const TupleType *>(node);
            std::vector<TypeRef> elementTypes;
            for (const auto &elem : tupleType->elements)
            {
                elementTypes.push_back(resolveType(elem.get()));
            }
            return types::tuple(std::move(elementTypes));
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

TypeRef Sema::lookupVarType(const std::string &name)
{
    Symbol *sym = currentScope_->lookup(name);
    if (sym && (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Parameter))
    {
        return sym->type;
    }
    return nullptr;
}

//=============================================================================
// Closure Capture Collection
//=============================================================================

void Sema::collectCaptures(const Expr *expr, const std::set<std::string> &lambdaLocals,
                           std::vector<CapturedVar> &captures)
{
    if (!expr)
        return;

    std::set<std::string> captured;

    // Helper to recursively collect identifiers
    std::function<void(const Expr *)> collect = [&](const Expr *e) {
        if (!e)
            return;

        switch (e->kind)
        {
            case ExprKind::Ident:
            {
                auto *ident = static_cast<const IdentExpr *>(e);
                // Check if this is a local variable (not a lambda param, not a function)
                if (lambdaLocals.find(ident->name) == lambdaLocals.end())
                {
                    Symbol *sym = lookupSymbol(ident->name);
                    if (sym && (sym->kind == Symbol::Kind::Variable ||
                                sym->kind == Symbol::Kind::Parameter))
                    {
                        if (captured.find(ident->name) == captured.end())
                        {
                            captured.insert(ident->name);
                            CapturedVar cv;
                            cv.name = ident->name;
                            cv.byReference = !sym->isFinal; // Mutable vars by reference
                            captures.push_back(cv);
                        }
                    }
                }
                break;
            }
            case ExprKind::Binary:
            {
                auto *bin = static_cast<const BinaryExpr *>(e);
                collect(bin->left.get());
                collect(bin->right.get());
                break;
            }
            case ExprKind::Unary:
            {
                auto *unary = static_cast<const UnaryExpr *>(e);
                collect(unary->operand.get());
                break;
            }
            case ExprKind::Call:
            {
                auto *call = static_cast<const CallExpr *>(e);
                collect(call->callee.get());
                for (const auto &arg : call->args)
                    collect(arg.value.get());
                break;
            }
            case ExprKind::Field:
            {
                auto *field = static_cast<const FieldExpr *>(e);
                collect(field->base.get());
                break;
            }
            case ExprKind::Index:
            {
                auto *idx = static_cast<const IndexExpr *>(e);
                collect(idx->base.get());
                collect(idx->index.get());
                break;
            }
            case ExprKind::Block:
            {
                auto *block = static_cast<const BlockExpr *>(e);
                // Would need to handle statements - skip for now
                break;
            }
            case ExprKind::If:
            {
                auto *ifExpr = static_cast<const IfExpr *>(e);
                collect(ifExpr->condition.get());
                collect(ifExpr->thenBranch.get());
                if (ifExpr->elseBranch)
                    collect(ifExpr->elseBranch.get());
                break;
            }
            case ExprKind::Match:
            {
                auto *match = static_cast<const MatchExpr *>(e);
                collect(match->scrutinee.get());
                for (const auto &arm : match->arms)
                    collect(arm.body.get());
                break;
            }
            case ExprKind::Tuple:
            {
                auto *tuple = static_cast<const TupleExpr *>(e);
                for (const auto &elem : tuple->elements)
                    collect(elem.get());
                break;
            }
            case ExprKind::TupleIndex:
            {
                auto *ti = static_cast<const TupleIndexExpr *>(e);
                collect(ti->tuple.get());
                break;
            }
            case ExprKind::ListLiteral:
            {
                auto *list = static_cast<const ListLiteralExpr *>(e);
                for (const auto &elem : list->elements)
                    collect(elem.get());
                break;
            }
            case ExprKind::Lambda:
            {
                // Nested lambda - don't descend, it will handle its own captures
                break;
            }
            default:
                // Literals and other expressions don't reference variables
                break;
        }
    };

    collect(expr);
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
    runtimeFunctions_["Viper.Terminal.SetColor"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SetPosition"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SetCursorVisible"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SetAltScreen"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.BeginBatch"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.EndBatch"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.GetKeyTimeout"] = types::string();

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

    // Register Viper.Random/Viper.Math runtime functions for random numbers
    runtimeFunctions_["Viper.Math.Rnd"] = types::number();
    runtimeFunctions_["Viper.Math.Randomize"] = types::voidType();
    runtimeFunctions_["Viper.Random.Next"] = types::number();
    runtimeFunctions_["Viper.Random.NextInt"] = types::integer();
    runtimeFunctions_["Viper.Random.Seed"] = types::voidType();

    // Register Viper.Environment runtime functions
    runtimeFunctions_["Viper.Environment.GetArgument"] = types::string();
    runtimeFunctions_["Viper.Environment.GetArgumentCount"] = types::integer();
    runtimeFunctions_["Viper.Environment.GetCommandLine"] = types::string();

    // Register Viper.Time runtime functions
    runtimeFunctions_["Viper.Time.Clock.Sleep"] = types::voidType();
    runtimeFunctions_["Viper.Time.Clock.Millis"] = types::integer();
    runtimeFunctions_["Viper.Time.SleepMs"] = types::voidType();

    // Note: Viper.Collections.List and Viper.Box functions are handled internally
    // by the lowerer for list literals and boxing operations, not as user-callable functions.
}

} // namespace il::frontends::viperlang
