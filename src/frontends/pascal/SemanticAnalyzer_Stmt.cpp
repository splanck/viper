//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/SemanticAnalyzer_Stmt.cpp
// Purpose: Statement analysis for Viper Pascal.
// Key invariants: Two-pass analysis; error recovery returns Unknown type.
// Ownership/Lifetime: Borrows DiagnosticEngine; AST not owned.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/common/CharUtils.hpp"
#include "frontends/pascal/BuiltinRegistry.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace il::frontends::pascal
{

// Use common toLowercase for case-insensitive comparison
using common::char_utils::toLowercase;

// Alias for compatibility with existing code
inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// Statement Analysis
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::analyzeStmt(Stmt &stmt)
{
    switch (stmt.kind)
    {
        case StmtKind::Block:
            analyzeBlock(static_cast<BlockStmt &>(stmt));
            break;
        case StmtKind::Assign:
            analyzeAssign(static_cast<AssignStmt &>(stmt));
            break;
        case StmtKind::Call:
            analyzeCall(static_cast<CallStmt &>(stmt));
            break;
        case StmtKind::If:
            analyzeIf(static_cast<IfStmt &>(stmt));
            break;
        case StmtKind::While:
            analyzeWhile(static_cast<WhileStmt &>(stmt));
            break;
        case StmtKind::Repeat:
            analyzeRepeat(static_cast<RepeatStmt &>(stmt));
            break;
        case StmtKind::For:
            analyzeFor(static_cast<ForStmt &>(stmt));
            break;
        case StmtKind::ForIn:
            analyzeForIn(static_cast<ForInStmt &>(stmt));
            break;
        case StmtKind::Case:
            analyzeCase(static_cast<CaseStmt &>(stmt));
            break;
        case StmtKind::Break:
            if (loopDepth_ == 0)
            {
                error(stmt, "break statement outside of loop");
            }
            break;
        case StmtKind::Continue:
            if (loopDepth_ == 0)
            {
                error(stmt, "continue statement outside of loop");
            }
            break;
        case StmtKind::Exit:
            analyzeExit(static_cast<ExitStmt &>(stmt));
            break;
        case StmtKind::Raise:
            analyzeRaise(static_cast<RaiseStmt &>(stmt));
            break;
        case StmtKind::TryExcept:
            analyzeTryExcept(static_cast<TryExceptStmt &>(stmt));
            break;
        case StmtKind::TryFinally:
            analyzeTryFinally(static_cast<TryFinallyStmt &>(stmt));
            break;
        case StmtKind::With:
            analyzeWith(static_cast<WithStmt &>(stmt));
            break;
        case StmtKind::Inherited:
            analyzeInherited(static_cast<InheritedStmt &>(stmt));
            break;
        case StmtKind::Empty:
            // Nothing to analyze
            break;
    }
}

void SemanticAnalyzer::analyzeBlock(BlockStmt &block)
{
    for (auto &stmt : block.stmts)
    {
        if (stmt)
            analyzeStmt(*stmt);
    }
}

void SemanticAnalyzer::analyzeAssign(AssignStmt &stmt)
{
    if (!stmt.target || !stmt.value)
        return;

    // Check for assignment to read-only loop variable
    if (stmt.target->kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(*stmt.target);
        std::string key = toLower(nameExpr.name);
        if (readOnlyLoopVars_.count(key))
        {
            error(stmt, "cannot assign to loop variable '" + nameExpr.name + "' inside loop body");
            return;
        }

        // Check for assignment to function name (not allowed - use Result instead)
        // But first, check if there's a local variable with this name (shadows function)
        bool isLocalVar = lookupVariable(key).has_value();

        // Also check if we're in a class method and this is a field name
        bool isClassField = false;
        if (!currentClassName_.empty())
        {
            auto *classInfo = lookupClass(toLower(currentClassName_));
            if (classInfo && classInfo->fields.find(key) != classInfo->fields.end())
            {
                isClassField = true;
            }
        }

        // Only error if it's a function name AND not a local var AND not a class field
        if (!isLocalVar && !isClassField && functions_.count(key))
        {
            error(stmt,
                  "cannot assign to function name '" + nameExpr.name +
                      "'; use 'Result' to return a value");
            return;
        }
    }

    // For assignment targets, use the declared type (not narrowed type)
    // Narrowing only affects reads, not assignment targets
    PasType targetType;
    if (stmt.target->kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(*stmt.target);
        // Use lookupVariable to get declared type, not lookupEffectiveType
        auto declaredType = lookupVariable(nameExpr.name);
        if (declaredType)
        {
            targetType = *declaredType;
        }
        else
        {
            targetType = typeOf(*stmt.target);
        }
    }
    else
    {
        targetType = typeOf(*stmt.target);
    }

    PasType valueType = typeOf(*stmt.value);

    // Special check: non-optional class/interface cannot be assigned nil
    if (valueType.kind == PasTypeKind::Nil &&
        (targetType.kind == PasTypeKind::Class || targetType.kind == PasTypeKind::Interface) &&
        !targetType.isOptional())
    {
        error(stmt, "cannot assign nil to non-optional " + targetType.toString());
        return;
    }

    // Check assignability
    if (!isAssignableFrom(targetType, valueType))
    {
        error(stmt, "cannot assign " + valueType.toString() + " to " + targetType.toString());
    }

    // Invalidate narrowing and mark as definitely assigned if assigning to a variable
    if (stmt.target->kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(*stmt.target);
        invalidateNarrowing(nameExpr.name);

        // Mark as definitely assigned (removes from uninitializedNonNullableVars_)
        markDefinitelyAssigned(nameExpr.name);
    }
}

void SemanticAnalyzer::analyzeCall(CallStmt &stmt)
{
    if (!stmt.call)
        return;

    // The call expression must be a CallExpr
    if (stmt.call->kind != ExprKind::Call)
    {
        error(stmt, "statement must be a procedure call, not a bare expression");
        return;
    }

    // Type-check the call expression
    typeOf(*stmt.call);
}

void SemanticAnalyzer::analyzeIf(IfStmt &stmt)
{
    std::string narrowedVar;
    bool isNotNil = false;
    bool hasNilCheck = false;
    PasType unwrappedType;

    if (stmt.condition)
    {
        PasType condType = typeOf(*stmt.condition);
        if (condType.kind != PasTypeKind::Boolean && !condType.isError())
        {
            error(*stmt.condition, "condition must be Boolean, got " + condType.toString());
        }

        // Check for nil check pattern for flow narrowing
        if (isNilCheck(*stmt.condition, narrowedVar, isNotNil))
        {
            // Look up the variable's type
            if (auto varType = lookupVariable(narrowedVar))
            {
                if (varType->isOptional())
                {
                    hasNilCheck = true;
                    unwrappedType = varType->unwrap();
                }
            }
        }
    }

    // Save state before branches for definite assignment tracking
    std::set<std::string> uninitBeforeIf = uninitializedNonNullableVars_;

    // Analyze then-branch with narrowing if applicable
    if (stmt.thenBranch)
    {
        if (hasNilCheck && isNotNil)
        {
            // "x <> nil" - narrow x to T in then-branch
            std::unordered_map<std::string, PasType> narrowed;
            narrowed[narrowedVar] = unwrappedType;
            pushNarrowing(narrowed);
            analyzeStmt(*stmt.thenBranch);
            popNarrowing();
        }
        else
        {
            analyzeStmt(*stmt.thenBranch);
        }
    }

    // Save what was initialized in the then branch
    std::set<std::string> uninitAfterThen = uninitializedNonNullableVars_;

    // Restore state before analyzing else branch
    uninitializedNonNullableVars_ = uninitBeforeIf;

    // Analyze else-branch with narrowing if applicable
    if (stmt.elseBranch)
    {
        if (hasNilCheck && !isNotNil)
        {
            // "x = nil" - narrow x to T in else-branch
            std::unordered_map<std::string, PasType> narrowed;
            narrowed[narrowedVar] = unwrappedType;
            pushNarrowing(narrowed);
            analyzeStmt(*stmt.elseBranch);
            popNarrowing();
        }
        else
        {
            analyzeStmt(*stmt.elseBranch);
        }
    }

    std::set<std::string> uninitAfterElse = uninitializedNonNullableVars_;

    // Compute union: a variable is NOT definitely assigned after the if
    // if it was NOT assigned in at least one branch.
    // In other words: a variable is definitely assigned only if assigned in BOTH branches.
    if (stmt.thenBranch && stmt.elseBranch)
    {
        // A variable remains uninitialized if it's uninitialized after EITHER branch
        // (union of uninitAfterThen and uninitAfterElse)
        uninitializedNonNullableVars_ = uninitAfterThen;
        for (const auto &var : uninitAfterElse)
        {
            uninitializedNonNullableVars_.insert(var);
        }
    }
    else if (stmt.thenBranch)
    {
        // No else branch: can't assume then-branch executed
        // Conservatively keep the original uninitialized set
        uninitializedNonNullableVars_ = uninitBeforeIf;
    }
    else
    {
        // No branches at all (shouldn't happen but handle it)
        uninitializedNonNullableVars_ = uninitBeforeIf;
    }
}

void SemanticAnalyzer::analyzeWhile(WhileStmt &stmt)
{
    std::string narrowedVar;
    bool isNotNil = false;
    bool hasNilCheck = false;
    PasType unwrappedType;

    if (stmt.condition)
    {
        PasType condType = typeOf(*stmt.condition);
        if (condType.kind != PasTypeKind::Boolean && !condType.isError())
        {
            error(*stmt.condition, "condition must be Boolean, got " + condType.toString());
        }

        // Check for nil check pattern for flow narrowing
        if (isNilCheck(*stmt.condition, narrowedVar, isNotNil))
        {
            if (auto varType = lookupVariable(narrowedVar))
            {
                if (varType->isOptional())
                {
                    hasNilCheck = true;
                    unwrappedType = varType->unwrap();
                }
            }
        }
    }

    ++loopDepth_;
    if (stmt.body)
    {
        if (hasNilCheck && isNotNil)
        {
            // "while x <> nil" - narrow x to T in body
            std::unordered_map<std::string, PasType> narrowed;
            narrowed[narrowedVar] = unwrappedType;
            pushNarrowing(narrowed);
            analyzeStmt(*stmt.body);
            popNarrowing();
        }
        else
        {
            analyzeStmt(*stmt.body);
        }
    }
    --loopDepth_;
}

void SemanticAnalyzer::analyzeRepeat(RepeatStmt &stmt)
{
    ++loopDepth_;
    if (stmt.body)
        analyzeStmt(*stmt.body);
    --loopDepth_;

    if (stmt.condition)
    {
        PasType condType = typeOf(*stmt.condition);
        if (condType.kind != PasTypeKind::Boolean && !condType.isError())
        {
            error(*stmt.condition, "condition must be Boolean, got " + condType.toString());
        }
    }
}

void SemanticAnalyzer::analyzeFor(ForStmt &stmt)
{
    // Look up or declare the loop variable
    std::string varKey = toLower(stmt.loopVar);
    auto varType = lookupVariable(varKey);

    if (!varType)
    {
        // Implicitly declare as Integer
        addVariable(varKey, PasType::integer());
        varType = PasType::integer();
    }

    // Loop variable must be ordinal (Integer or enum), not Real
    if (!varType->isOrdinal())
    {
        error(stmt, "for loop variable must be Integer or enum type (not Real)");
    }

    // Check start and bound expressions
    if (stmt.start)
    {
        PasType startType = typeOf(*stmt.start);
        if (!isAssignableFrom(*varType, startType) && !startType.isError())
        {
            error(*stmt.start, "start value type mismatch");
        }
    }

    if (stmt.bound)
    {
        PasType boundType = typeOf(*stmt.bound);
        if (!isAssignableFrom(*varType, boundType) && !boundType.isError())
        {
            error(*stmt.bound, "bound value type mismatch");
        }
    }

    // Mark the loop variable as read-only during body analysis
    readOnlyLoopVars_.insert(varKey);
    // Remove from undefined set (it's valid inside the loop)
    undefinedVars_.erase(varKey);

    ++loopDepth_;
    if (stmt.body)
        analyzeStmt(*stmt.body);
    --loopDepth_;

    // After the loop, the loop variable becomes undefined
    readOnlyLoopVars_.erase(varKey);
    undefinedVars_.insert(varKey);
}

void SemanticAnalyzer::analyzeForIn(ForInStmt &stmt)
{
    // Type-check collection
    PasType collType = stmt.collection ? typeOf(*stmt.collection) : PasType::unknown();

    // Infer element type based on collection type
    PasType elementType = PasType::unknown();
    bool validIterable = false;

    if (!collType.isError())
    {
        if (collType.kind == PasTypeKind::Array && collType.elementType)
        {
            // Array iteration yields element type
            elementType = *collType.elementType;
            validIterable = true;
        }
        else if (collType.kind == PasTypeKind::String)
        {
            // String iteration yields 1-character strings
            elementType = PasType::string();
            validIterable = true;
        }
        else
        {
            error(stmt, "for-in requires an array or string, got " + collType.toString());
        }
    }

    // Create a new scope for the loop variable
    pushScope();

    // Declare the loop variable with inferred element type
    std::string varKey = toLower(stmt.loopVar);
    if (validIterable)
    {
        addVariable(varKey, elementType);
    }
    else
    {
        addVariable(varKey, PasType::unknown());
    }

    // Mark the loop variable as read-only during body analysis
    readOnlyLoopVars_.insert(varKey);

    ++loopDepth_;
    if (stmt.body)
        analyzeStmt(*stmt.body);
    --loopDepth_;

    // After the loop, the loop variable becomes undefined
    readOnlyLoopVars_.erase(varKey);

    // Pop the scope (the variable is no longer accessible)
    popScope();
}

void SemanticAnalyzer::analyzeCase(CaseStmt &stmt)
{
    PasType exprType = PasType::unknown();
    if (stmt.expr)
    {
        exprType = typeOf(*stmt.expr);
        // Case expression must be Integer or Enum (not String in v0.1)
        if (!exprType.isError() && exprType.kind != PasTypeKind::Integer &&
            exprType.kind != PasTypeKind::Enum)
        {
            error(*stmt.expr, "case expression must be Integer or enum type");
        }
    }

    // Track seen labels for duplicate detection
    std::set<int64_t> seenLabels;

    for (auto &arm : stmt.arms)
    {
        for (auto &label : arm.labels)
        {
            if (!label)
                continue;

            // Type-check the label
            PasType labelType = typeOf(*label);

            // Check label type matches case expression type
            if (!labelType.isError() && !exprType.isError())
            {
                if (exprType.kind == PasTypeKind::Integer && labelType.kind != PasTypeKind::Integer)
                {
                    error(*label, "case label must be Integer");
                }
                else if (exprType.kind == PasTypeKind::Enum)
                {
                    if (labelType.kind != PasTypeKind::Enum || labelType.name != exprType.name)
                    {
                        error(*label, "case label must be of type " + exprType.name);
                    }
                }
            }

            // Extract compile-time constant value for duplicate detection
            int64_t labelValue = 0;
            bool isConstant = false;
            if (label->kind == ExprKind::IntLiteral)
            {
                labelValue = static_cast<IntLiteralExpr &>(*label).value;
                isConstant = true;
            }
            else if (label->kind == ExprKind::Name)
            {
                // Check if it's an enum constant
                auto &nameExpr = static_cast<NameExpr &>(*label);
                if (auto constType = lookupConstant(toLower(nameExpr.name)))
                {
                    if (constType->kind == PasTypeKind::Enum && constType->enumOrdinal >= 0)
                    {
                        labelValue = constType->enumOrdinal;
                        isConstant = true;
                    }
                }
            }

            // Check for duplicates
            if (isConstant)
            {
                if (seenLabels.count(labelValue) > 0)
                {
                    error(*label, "duplicate case label");
                }
                else
                {
                    seenLabels.insert(labelValue);
                }
            }
        }
        if (arm.body)
            analyzeStmt(*arm.body);
    }

    if (stmt.elseBody)
        analyzeStmt(*stmt.elseBody);
}

void SemanticAnalyzer::analyzeRaise(RaiseStmt &stmt)
{
    if (stmt.exception)
    {
        // raise Expr; - type-check the exception expression
        PasType excType = typeOf(*stmt.exception);

        // Check that the expression is an exception type (class derived from Exception)
        if (!excType.isError())
        {
            if (excType.kind != PasTypeKind::Class)
            {
                error(stmt, "raise expression must be an exception object (class type)");
            }
            else
            {
                // Verify the class derives from Exception
                bool derivesFromException = false;
                std::string checkClass = toLower(excType.name);
                while (!checkClass.empty())
                {
                    if (checkClass == "exception")
                    {
                        derivesFromException = true;
                        break;
                    }
                    auto classIt = classes_.find(checkClass);
                    if (classIt == classes_.end())
                        break;
                    checkClass = toLower(classIt->second.baseClass);
                }
                if (!derivesFromException)
                {
                    error(stmt,
                          "raise expression must be of type Exception or a subclass, not '" +
                              excType.name + "'");
                }
            }
        }
    }
    else
    {
        // raise; (re-raise) - only valid inside except handler
        if (exceptHandlerDepth_ == 0)
        {
            error(stmt, "'raise' without expression is only valid inside an except handler");
        }
    }
}

void SemanticAnalyzer::analyzeExit(ExitStmt &stmt)
{
    // Exit must be inside a procedure/function
    if (routineDepth_ == 0)
    {
        error(stmt, "'Exit' statement is only valid inside a procedure or function");
        return;
    }

    if (stmt.value)
    {
        // Exit(value) - must be inside a function, and value type must match return type
        if (!currentFunction_)
        {
            error(stmt, "'Exit' with a value is only valid inside a function");
            return;
        }

        if (currentFunction_->returnType.kind == PasTypeKind::Void)
        {
            error(stmt, "'Exit' with a value is not valid in a procedure (use 'Exit;' instead)");
            return;
        }

        PasType valType = typeOf(*stmt.value);
        if (!valType.isError() && !isAssignableFrom(currentFunction_->returnType, valType))
        {
            error(stmt,
                  "Exit value type '" + valType.toString() +
                      "' is not compatible with function return type '" +
                      currentFunction_->returnType.toString() + "'");
        }
    }
    // Exit; without value is valid in both procedures and functions
    // In functions, it returns the current value of 'Result' (or undefined if not set)
}

void SemanticAnalyzer::analyzeTryExcept(TryExceptStmt &stmt)
{
    // Reject except...else syntax in v0.1
    if (stmt.elseBody)
    {
        error(stmt, "'except...else' is not supported; use 'on E: Exception do' as a catch-all");
        // Continue analysis for better error recovery
    }

    if (stmt.tryBody)
        analyzeBlock(*stmt.tryBody);

    for (auto &handler : stmt.handlers)
    {
        // Validate handler type derives from Exception
        std::string typeLower = toLower(handler.typeName);
        auto typeIt = types_.find(typeLower);
        if (typeIt == types_.end())
        {
            error(handler.loc, "unknown exception type '" + handler.typeName + "'");
        }
        else if (typeIt->second.kind != PasTypeKind::Class)
        {
            error(handler.loc,
                  "exception handler type must be a class, not '" + handler.typeName + "'");
        }
        else
        {
            // Check if type is Exception or derives from it
            // For now, we accept any class type; full inheritance checking would
            // walk the class hierarchy to verify it derives from Exception
            bool derivesFromException = false;
            std::string checkClass = typeLower;
            while (!checkClass.empty())
            {
                if (checkClass == "exception")
                {
                    derivesFromException = true;
                    break;
                }
                auto classIt = classes_.find(checkClass);
                if (classIt == classes_.end())
                    break;
                checkClass = toLower(classIt->second.baseClass);
            }
            if (!derivesFromException)
            {
                error(handler.loc,
                      "exception handler type '" + handler.typeName +
                          "' must derive from Exception");
            }
        }

        pushScope();
        if (!handler.varName.empty())
        {
            // Register exception variable
            PasType excType;
            excType.kind = PasTypeKind::Class;
            excType.name = handler.typeName;
            addVariable(toLower(handler.varName), excType);
        }

        // Track that we're inside an except handler for raise; validation
        exceptHandlerDepth_++;
        if (handler.body)
            analyzeStmt(*handler.body);
        exceptHandlerDepth_--;

        popScope();
    }

    if (stmt.elseBody)
        analyzeStmt(*stmt.elseBody);
}

void SemanticAnalyzer::analyzeTryFinally(TryFinallyStmt &stmt)
{
    if (stmt.tryBody)
        analyzeBlock(*stmt.tryBody);
    if (stmt.finallyBody)
        analyzeBlock(*stmt.finallyBody);
}

void SemanticAnalyzer::analyzeWith(WithStmt &stmt)
{
    // Process each with expression, push context, and generate temp var names
    static int withCounter = 0;

    std::vector<WithContext> pushedContexts;
    for (size_t i = 0; i < stmt.objects.size(); ++i)
    {
        auto &obj = stmt.objects[i];
        PasType objType = typeOf(*obj);

        // The with expression must be a class or record type
        if (objType.kind != PasTypeKind::Class && objType.kind != PasTypeKind::Record)
        {
            error(*obj, "'with' expression must be of class or record type");
            continue;
        }

        // Generate a temp variable name for this with context
        std::string tempName = "__with_" + std::to_string(withCounter++);

        // Add temp variable to current scope for lowering
        addVariable(tempName, objType);

        // Push context (innermost-last, so later expressions take priority)
        WithContext ctx;
        ctx.type = objType;
        ctx.tempVarName = tempName;
        withContexts_.push_back(ctx);
        pushedContexts.push_back(ctx);
    }

    // Analyze the body with the with contexts active
    if (stmt.body)
    {
        analyzeStmt(*stmt.body);
    }

    // Pop the contexts (in reverse order)
    for (size_t i = 0; i < pushedContexts.size(); ++i)
    {
        withContexts_.pop_back();
    }
}

void SemanticAnalyzer::analyzeInherited(InheritedStmt &stmt)
{
    // inherited must be used inside a method
    if (currentClassName_.empty())
    {
        error(stmt, "'inherited' can only be used inside a method");
        return;
    }

    // Look up the current class to find its base class
    std::string classKey = toLower(currentClassName_);
    auto classIt = classes_.find(classKey);
    if (classIt == classes_.end())
    {
        error(stmt, "internal error: current class '" + currentClassName_ + "' not found");
        return;
    }

    const ClassInfo &classInfo = classIt->second;
    if (classInfo.baseClass.empty())
    {
        error(stmt, "cannot use 'inherited' - class '" + currentClassName_ + "' has no base class");
        return;
    }

    // Type-check arguments
    for (auto &arg : stmt.args)
    {
        if (arg)
            typeOf(*arg);
    }

    // Resolve method in base class hierarchy
    std::string baseName = classInfo.baseClass;
    if (baseName.empty())
        return;

    auto hasMethodInHierarchy = [&](const std::string &method) -> bool
    {
        std::string cur = toLower(baseName);
        while (!cur.empty())
        {
            auto *ci = lookupClass(cur);
            if (!ci)
                break;
            std::string mkey = toLower(method);
            auto mit = ci->methods.find(mkey);
            if (mit != ci->methods.end())
            {
                if (mit->second.isAbstract)
                {
                    error(stmt, "cannot call abstract base method '" + method + "'");
                }
                return true;
            }
            if (ci->baseClass.empty())
                break;
            cur = toLower(ci->baseClass);
        }
        return false;
    };

    // Determine method name
    std::string targetMethod = stmt.methodName;
    if (targetMethod.empty())
    {
        // Use current method name; ensure we are actually in a method
        if (currentMethodName_.empty())
        {
            error(stmt, "'inherited' can only be used inside a method");
            return;
        }
        targetMethod = currentMethodName_;
    }

    if (!hasMethodInHierarchy(targetMethod))
    {
        error(stmt, "base class does not define method '" + targetMethod + "'");
    }
}


} // namespace il::frontends::pascal
