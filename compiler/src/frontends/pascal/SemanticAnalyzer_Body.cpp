//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/SemanticAnalyzer_Body.cpp
// Purpose: Procedure/function body analysis.
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
// Body Analysis (Pass 2)
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::analyzeBodies(Program &prog)
{
    // Analyze procedure/function/constructor/destructor bodies
    for (auto &decl : prog.decls)
    {
        if (!decl)
            continue;
        if (decl->kind == DeclKind::Procedure)
        {
            analyzeProcedureBody(static_cast<ProcedureDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Function)
        {
            analyzeFunctionBody(static_cast<FunctionDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Constructor)
        {
            analyzeConstructorBody(static_cast<ConstructorDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Destructor)
        {
            analyzeDestructorBody(static_cast<DestructorDecl &>(*decl));
        }
    }

    // Analyze main program body
    if (prog.body)
    {
        analyzeBlock(*prog.body);
    }
}

void SemanticAnalyzer::analyzeBodies(Unit &unit)
{
    // Analyze implementation bodies
    for (auto &decl : unit.implDecls)
    {
        if (!decl)
            continue;
        if (decl->kind == DeclKind::Procedure)
        {
            analyzeProcedureBody(static_cast<ProcedureDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Function)
        {
            analyzeFunctionBody(static_cast<FunctionDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Constructor)
        {
            analyzeConstructorBody(static_cast<ConstructorDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Destructor)
        {
            analyzeDestructorBody(static_cast<DestructorDecl &>(*decl));
        }
    }

    // Analyze init section
    if (unit.initSection)
    {
        analyzeBlock(*unit.initSection);
    }

    // Analyze final section
    if (unit.finalSection)
    {
        analyzeBlock(*unit.finalSection);
    }
}

void SemanticAnalyzer::analyzeProcedureBody(ProcedureDecl &decl)
{
    if (!decl.body)
        return;

    // Set current class if this is a method
    std::string savedClassName = currentClassName_;
    if (decl.isMethod())
    {
        currentClassName_ = decl.className;
    }

    // Push scope for procedure
    pushScope();
    ++routineDepth_;

    // Register Self for methods
    if (decl.isMethod())
    {
        addVariable("self", PasType::classType(decl.className));
    }

    // Register parameters
    for (const auto &param : decl.params)
    {
        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
        addVariable(toLower(param.name), paramType);
    }

    // v0.1: Reject nested procedures/functions
    for (auto &localDecl : decl.localDecls)
    {
        if (localDecl)
        {
            if (localDecl->kind == DeclKind::Procedure || localDecl->kind == DeclKind::Function)
            {
                error(localDecl->loc,
                      "nested procedures/functions are not supported in Viper Pascal v0.1; "
                      "move declarations to the enclosing scope");
            }
            else
            {
                collectDecl(*localDecl);
            }
        }
    }

    // For methods, bind visible fields of current class (and base classes) into scope
    if (decl.isMethod())
    {
        auto bindFields = [&](const std::string &className)
        {
            std::string cur = toLower(className);
            while (!cur.empty())
            {
                auto *classInfo = lookupClass(cur);
                if (!classInfo)
                    break;
                for (const auto &pair : classInfo->fields)
                {
                    const std::string &fname = pair.first; // already lowercase
                    // Do not shadow existing locals/params
                    if (!varScopes_.empty() && !varScopes_.back().contains(fname))
                    {
                        addVariable(fname, pair.second.type);
                    }
                }
                if (classInfo->baseClass.empty())
                    break;
                cur = toLower(classInfo->baseClass);
            }
        };
        bindFields(decl.className);
    }

    // Save/assign current method name for inherited resolution
    std::string savedMethodName = currentMethodName_;
    if (decl.isMethod())
        currentMethodName_ = decl.name;

    // Analyze body
    analyzeBlock(*decl.body);

    --routineDepth_;
    popScope();
    currentMethodName_ = savedMethodName;
    currentClassName_ = savedClassName;
}

void SemanticAnalyzer::analyzeFunctionBody(FunctionDecl &decl)
{
    if (!decl.body)
        return;

    // Set current function for return type checking
    std::string key = toLower(decl.name);
    auto it = functions_.find(key);
    if (it != functions_.end())
    {
        currentFunction_ = &it->second;
    }

    // Set current class if this is a method
    std::string savedClassName = currentClassName_;
    if (decl.isMethod())
    {
        currentClassName_ = decl.className;
    }

    // Push scope for function
    pushScope();
    ++routineDepth_;

    // Register Self for methods
    if (decl.isMethod())
    {
        addVariable("self", PasType::classType(decl.className));
    }

    // Register parameters
    for (const auto &param : decl.params)
    {
        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
        addVariable(toLower(param.name), paramType);
    }

    // Register Result variable with function's return type
    // Note: Per spec, assigning to function name is NOT supported - only Result
    PasType retType = decl.returnType ? resolveType(*decl.returnType) : PasType::unknown();
    addVariable("result", retType);

    // v0.1: Reject nested procedures/functions
    for (auto &localDecl : decl.localDecls)
    {
        if (localDecl)
        {
            if (localDecl->kind == DeclKind::Procedure || localDecl->kind == DeclKind::Function)
            {
                error(localDecl->loc,
                      "nested procedures/functions are not supported in Viper Pascal v0.1; "
                      "move declarations to the enclosing scope");
            }
            else
            {
                collectDecl(*localDecl);
            }
        }
    }

    // For methods, bind visible fields of current class (and base classes) into scope
    if (decl.isMethod())
    {
        auto bindFields = [&](const std::string &className)
        {
            std::string cur = toLower(className);
            while (!cur.empty())
            {
                auto *classInfo = lookupClass(cur);
                if (!classInfo)
                    break;
                for (const auto &pair : classInfo->fields)
                {
                    const std::string &fname = pair.first; // already lowercase
                    if (!varScopes_.empty() && !varScopes_.back().contains(fname))
                    {
                        addVariable(fname, pair.second.type);
                    }
                }
                if (classInfo->baseClass.empty())
                    break;
                cur = toLower(classInfo->baseClass);
            }
        };
        bindFields(decl.className);
    }

    // Save/assign current method name for inherited resolution
    std::string savedMethodName = currentMethodName_;
    if (decl.isMethod())
        currentMethodName_ = decl.name;

    // Analyze body
    analyzeBlock(*decl.body);

    --routineDepth_;
    popScope();
    currentFunction_ = nullptr;
    currentMethodName_ = savedMethodName;
    currentClassName_ = savedClassName;
}

void SemanticAnalyzer::analyzeConstructorBody(ConstructorDecl &decl)
{
    if (!decl.body)
        return;

    // Set current class
    std::string savedClassName = currentClassName_;
    currentClassName_ = decl.className;

    // Push scope for constructor
    pushScope();

    // Register Self with the class type
    addVariable("self", PasType::classType(decl.className));

    // Register parameters
    for (const auto &param : decl.params)
    {
        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
        addVariable(toLower(param.name), paramType);
    }

    // Register local declarations
    for (auto &localDecl : decl.localDecls)
    {
        if (localDecl)
            collectDecl(*localDecl);
    }

    // Bind fields of the current class (and bases) into scope
    {
        auto bindFields = [&](const std::string &className)
        {
            std::string cur = toLower(className);
            while (!cur.empty())
            {
                auto *classInfo = lookupClass(cur);
                if (!classInfo)
                    break;
                for (const auto &pair : classInfo->fields)
                {
                    const std::string &fname = pair.first; // already lowercase
                    if (!varScopes_.empty() && !varScopes_.back().contains(fname))
                    {
                        addVariable(fname, pair.second.type);
                    }
                }
                if (classInfo->baseClass.empty())
                    break;
                cur = toLower(classInfo->baseClass);
            }
        };
        bindFields(decl.className);
    }

    // Analyze body
    analyzeBlock(*decl.body);

    popScope();
    currentClassName_ = savedClassName;
}

void SemanticAnalyzer::analyzeDestructorBody(DestructorDecl &decl)
{
    if (!decl.body)
        return;

    // Set current class
    std::string savedClassName = currentClassName_;
    currentClassName_ = decl.className;

    // Push scope for destructor
    pushScope();

    // Register Self with the class type
    addVariable("self", PasType::classType(decl.className));

    // Register local declarations (destructors have no parameters)
    for (auto &localDecl : decl.localDecls)
    {
        if (localDecl)
            collectDecl(*localDecl);
    }

    // Bind fields into scope for destructor as well
    {
        auto bindFields = [&](const std::string &className)
        {
            std::string cur = toLower(className);
            while (!cur.empty())
            {
                auto *classInfo = lookupClass(cur);
                if (!classInfo)
                    break;
                for (const auto &pair : classInfo->fields)
                {
                    const std::string &fname = pair.first; // already lowercase
                    if (!varScopes_.empty() && !varScopes_.back().contains(fname))
                    {
                        addVariable(fname, pair.second.type);
                    }
                }
                if (classInfo->baseClass.empty())
                    break;
                cur = toLower(classInfo->baseClass);
            }
        };
        bindFields(decl.className);
    }

    // Analyze body
    analyzeBlock(*decl.body);

    popScope();
    currentClassName_ = savedClassName;
}

} // namespace il::frontends::pascal
