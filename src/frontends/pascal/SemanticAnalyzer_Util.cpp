//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/SemanticAnalyzer_Util.cpp
// Purpose: Scope management, lookups, and builtins.
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
// Scope Management
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::pushScope()
{
    varScopes_.emplace_back();
}

void SemanticAnalyzer::popScope()
{
    if (!varScopes_.empty())
    {
        // Clear any tracking for variables in this scope
        for (const auto &pair : varScopes_.back())
        {
            uninitializedNonNullableVars_.erase(pair.first);
            definitelyAssignedVars_.erase(pair.first);
        }
        varScopes_.pop_back();
    }
}

void SemanticAnalyzer::addVariable(const std::string &name, const PasType &type)
{
    if (!varScopes_.empty())
    {
        varScopes_.back()[name] = type;
    }
}

void SemanticAnalyzer::addLocalVariable(const std::string &name, const PasType &type)
{
    addVariable(name, type);

    // Track non-nullable reference types that require definite assignment
    if (type.requiresDefiniteAssignment())
    {
        uninitializedNonNullableVars_.insert(name);
    }
}

void SemanticAnalyzer::markDefinitelyAssigned(const std::string &name)
{
    std::string key = toLower(name);
    uninitializedNonNullableVars_.erase(key);
    definitelyAssignedVars_.insert(key);
}

bool SemanticAnalyzer::isDefinitelyAssigned(const std::string &name) const
{
    std::string key = toLower(name);
    // If not in the uninitialized set, it's either:
    // 1. Not a non-nullable reference type
    // 2. Definitely assigned
    return uninitializedNonNullableVars_.count(key) == 0;
}

//===----------------------------------------------------------------------===//
// Lookup Functions
//===----------------------------------------------------------------------===//

std::optional<PasType> SemanticAnalyzer::lookupType(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = types_.find(key);
    if (it != types_.end())
        return it->second;
    return std::nullopt;
}

std::optional<PasType> SemanticAnalyzer::lookupVariable(const std::string &name) const
{
    std::string key = toLower(name);
    // Search from innermost scope outward
    for (auto it = varScopes_.rbegin(); it != varScopes_.rend(); ++it)
    {
        auto found = it->find(key);
        if (found != it->end())
            return found->second;
    }
    return std::nullopt;
}

std::optional<PasType> SemanticAnalyzer::lookupConstant(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = constants_.find(key);
    if (it != constants_.end())
        return it->second;
    return std::nullopt;
}

std::optional<int64_t> SemanticAnalyzer::lookupConstantInt(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = constantValues_.find(key);
    if (it != constantValues_.end())
        return it->second;
    return std::nullopt;
}

std::optional<double> SemanticAnalyzer::lookupConstantReal(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = constantRealValues_.find(key);
    if (it != constantRealValues_.end())
        return it->second;
    return std::nullopt;
}

std::optional<std::string> SemanticAnalyzer::lookupConstantStr(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = constantStrValues_.find(key);
    if (it != constantStrValues_.end())
        return it->second;
    return std::nullopt;
}

const FuncSignature *SemanticAnalyzer::lookupFunction(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = functions_.find(key);
    if (it != functions_.end())
        return &it->second;
    return nullptr;
}

const Expr *SemanticAnalyzer::getDefaultParamExpr(const std::string &funcName,
                                                  size_t paramIndex) const
{
    std::string key = toLower(funcName) + ":" + std::to_string(paramIndex);
    auto it = defaultParamExprs_.find(key);
    return (it != defaultParamExprs_.end()) ? it->second : nullptr;
}

const Expr *SemanticAnalyzer::getDefaultMethodParamExpr(const std::string &className,
                                                        const std::string &methodName,
                                                        size_t paramIndex) const
{
    // Method defaults are stored with key "classname.methodname:paramindex"
    std::string key =
        toLower(className) + "." + toLower(methodName) + ":" + std::to_string(paramIndex);
    auto it = defaultParamExprs_.find(key);
    return (it != defaultParamExprs_.end()) ? it->second : nullptr;
}

//===----------------------------------------------------------------------===//
// Error Reporting
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::error(il::support::SourceLoc loc, const std::string &message)
{
    hasError_ = true;
    diag_.report({il::support::Severity::Error, message, loc, ""});
}

void SemanticAnalyzer::error(const Expr &expr, const std::string &message)
{
    error(expr.loc, message);
}

void SemanticAnalyzer::error(const Stmt &stmt, const std::string &message)
{
    error(stmt.loc, message);
}

//===----------------------------------------------------------------------===//
// Built-in Registration
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::registerPrimitives()
{
    types_["integer"] = PasType::integer();
    types_["real"] = PasType::real();
    types_["double"] = PasType::real();
    types_["boolean"] = PasType::boolean();
    types_["string"] = PasType::string();

    // Register Exception as predefined class type
    types_["exception"] = PasType::classType("Exception");

    // Register Exception class info for inheritance checking and constructor resolution
    ClassInfo excInfo;
    excInfo.name = "Exception";
    excInfo.baseClass = "";
    excInfo.hasConstructor = true;

    // Add Message field (String property)
    FieldInfo msgField;
    msgField.name = "Message";
    msgField.type = PasType::string();
    msgField.visibility = Visibility::Public;
    excInfo.fields["message"] = msgField;

    // Add Create constructor: constructor Create(msg: String)
    MethodInfo createCtor;
    createCtor.name = "Create";
    createCtor.params.push_back({"msg", PasType::string()});
    createCtor.isVarParam.push_back(false);
    createCtor.hasDefault.push_back(false);
    createCtor.returnType = PasType::voidType(); // Constructors return void
    createCtor.visibility = Visibility::Public;
    createCtor.requiredParams = 1;
    excInfo.methods["create"].push_back(createCtor);

    classes_["exception"] = excInfo;
}

//===----------------------------------------------------------------------===//
// Flow-Sensitive Narrowing
//===----------------------------------------------------------------------===//

bool SemanticAnalyzer::isNilCheck(const Expr &expr, std::string &varName, bool &isNotNil) const
{
    // Check if expr is a binary comparison with nil
    if (expr.kind != ExprKind::Binary)
        return false;

    const auto &binExpr = static_cast<const BinaryExpr &>(expr);

    // Must be = or <> comparison
    if (binExpr.op != BinaryExpr::Op::Eq && binExpr.op != BinaryExpr::Op::Ne)
        return false;

    // One side must be nil, the other a simple name
    const Expr *nameExpr = nullptr;
    const Expr *nilExpr = nullptr;

    if (binExpr.left && binExpr.left->kind == ExprKind::Name && binExpr.right &&
        binExpr.right->kind == ExprKind::NilLiteral)
    {
        nameExpr = binExpr.left.get();
        nilExpr = binExpr.right.get();
    }
    else if (binExpr.left && binExpr.left->kind == ExprKind::NilLiteral && binExpr.right &&
             binExpr.right->kind == ExprKind::Name)
    {
        nilExpr = binExpr.left.get();
        nameExpr = binExpr.right.get();
    }
    else
    {
        return false;
    }

    varName = toLower(static_cast<const NameExpr *>(nameExpr)->name);
    isNotNil = (binExpr.op == BinaryExpr::Op::Ne); // <> nil means "is not nil"
    return true;
}

void SemanticAnalyzer::pushNarrowing(const std::unordered_map<std::string, PasType> &narrowed)
{
    narrowingScopes_.push_back(narrowed);
}

void SemanticAnalyzer::popNarrowing()
{
    if (!narrowingScopes_.empty())
        narrowingScopes_.pop_back();
}

void SemanticAnalyzer::invalidateNarrowing(const std::string &varName)
{
    std::string key = toLower(varName);
    // Remove from all narrowing scopes
    for (auto &scope : narrowingScopes_)
    {
        scope.erase(key);
    }
}

std::optional<PasType> SemanticAnalyzer::lookupEffectiveType(const std::string &name) const
{
    std::string key = toLower(name);

    // Check narrowing scopes first (from innermost to outermost)
    for (auto it = narrowingScopes_.rbegin(); it != narrowingScopes_.rend(); ++it)
    {
        auto found = it->find(key);
        if (found != it->end())
            return found->second;
    }

    // Fall back to regular variable lookup
    return lookupVariable(key);
}

void SemanticAnalyzer::registerBuiltins()
{
    // Helper to map ResultKind to PasType
    auto resultTypeToPassType = [](ResultKind kind) -> PasType
    {
        switch (kind)
        {
            case ResultKind::Void:
                return PasType::voidType();
            case ResultKind::Integer:
                return PasType::integer();
            case ResultKind::Real:
                return PasType::real();
            case ResultKind::String:
                return PasType::string();
            case ResultKind::Boolean:
                return PasType::boolean();
            case ResultKind::FromArg:
                // Default to Integer for FromArg builtins
                return PasType::integer();
        }
        return PasType::unknown();
    };

    // Helper to map ArgTypeMask to PasType (for signature purposes)
    // For Numeric, prefer Real since integers auto-promote to real
    // For multi-type args (String|Array), return an Any type marker
    auto maskToType = [](ArgTypeMask mask) -> PasType
    {
        // For Numeric (both Int + Real allowed), use Real to allow promotion
        if ((mask & ArgTypeMask::Integer) && (mask & ArgTypeMask::Real))
            return PasType::real();
        // Ordinal includes Integer and Boolean - use Integer for compatibility
        if ((mask & ArgTypeMask::Integer) && (mask & ArgTypeMask::Boolean))
            return PasType::integer(); // Ordinal - accept integers (booleans are ordinal too)
        // String|Array - mark as Any so type checking is deferred
        if ((mask & ArgTypeMask::String) && (mask & ArgTypeMask::Array))
            return PasType::unknown(); // Use unknown as "Any" marker
        // Otherwise, order of preference: Integer, Real, String, Boolean, Array
        if (mask & ArgTypeMask::Integer)
            return PasType::integer();
        if (mask & ArgTypeMask::Real)
            return PasType::real();
        if (mask & ArgTypeMask::String)
            return PasType::string();
        if (mask & ArgTypeMask::Boolean)
            return PasType::boolean();
        if (mask & ArgTypeMask::Array)
            return PasType::array(PasType::unknown(), 0); // Generic array type
        return PasType::unknown();
    };

    // Register all builtins from the registry (only core builtins, not unit-specific)
    for (size_t i = 0; i < static_cast<size_t>(PascalBuiltin::Count); ++i)
    {
        auto id = static_cast<PascalBuiltin>(i);
        const auto &desc = getBuiltinDescriptor(id);

        // Skip if no name (unused entry)
        if (!desc.name)
            continue;

        // Skip Viper unit builtins (they require uses clause)
        if (desc.category != BuiltinCategory::Builtin)
            continue;

        FuncSignature sig;
        sig.name = desc.name;
        sig.returnType = resultTypeToPassType(desc.result);

        // For variadic builtins, leave params empty (checked specially in typeOfCall)
        if (!desc.variadic)
        {
            // Add ALL parameters (both required and optional) to sig.params
            // Bug #19 fix: previously only non-optional params were added
            for (const auto &arg : desc.args)
            {
                sig.params.emplace_back("arg", maskToType(arg.allowed));
                sig.isVarParam.push_back(arg.isVar);
                // Count required params (non-optional ones)
                if (!arg.optional)
                {
                    sig.requiredParams++;
                }
            }
        }

        functions_[toLower(desc.name)] = sig;
    }

    // Register built-in units (Viper.Strings, Viper.Math)
    registerBuiltinUnits();
}

void SemanticAnalyzer::registerBuiltinUnits()
{
    // Helper to map ResultKind to PasType
    auto resultTypeToPassType = [](ResultKind kind) -> PasType
    {
        switch (kind)
        {
            case ResultKind::Void:
                return PasType::voidType();
            case ResultKind::Integer:
                return PasType::integer();
            case ResultKind::Real:
                return PasType::real();
            case ResultKind::String:
                return PasType::string();
            case ResultKind::Boolean:
                return PasType::boolean();
            case ResultKind::FromArg:
                return PasType::integer();
        }
        return PasType::unknown();
    };

    // Helper to map ArgTypeMask to PasType
    auto maskToType = [](ArgTypeMask mask) -> PasType
    {
        if ((mask & ArgTypeMask::Integer) && (mask & ArgTypeMask::Real))
            return PasType::real();
        if ((mask & ArgTypeMask::Integer) && (mask & ArgTypeMask::Boolean))
            return PasType::integer();
        if ((mask & ArgTypeMask::String) && (mask & ArgTypeMask::Array))
            return PasType::unknown();
        if (mask & ArgTypeMask::Integer)
            return PasType::integer();
        if (mask & ArgTypeMask::Real)
            return PasType::real();
        if (mask & ArgTypeMask::String)
            return PasType::string();
        if (mask & ArgTypeMask::Boolean)
            return PasType::boolean();
        if (mask & ArgTypeMask::Array)
            return PasType::array(PasType::unknown(), 0);
        return PasType::unknown();
    };

    //=========================================================================
    // Viper.Strings Unit
    //=========================================================================
    {
        UnitInfo unit;
        unit.name = "Viper.Strings";

        // Register all Viper.Strings builtins
        for (size_t i = 0; i < static_cast<size_t>(PascalBuiltin::Count); ++i)
        {
            auto id = static_cast<PascalBuiltin>(i);
            const auto &desc = getBuiltinDescriptor(id);

            if (!desc.name || desc.category != BuiltinCategory::ViperStrings)
                continue;

            FuncSignature sig;
            sig.name = desc.name;
            sig.returnType = resultTypeToPassType(desc.result);

            if (!desc.variadic)
            {
                for (const auto &arg : desc.args)
                {
                    if (!arg.optional)
                    {
                        sig.params.emplace_back("arg", maskToType(arg.allowed));
                        sig.isVarParam.push_back(arg.isVar);
                    }
                }
            }

            unit.functions[toLower(desc.name)] = sig;
        }

        registerUnit(unit);
    }

    //=========================================================================
    // Viper.Math Unit
    //=========================================================================
    {
        UnitInfo unit;
        unit.name = "Viper.Math";

        // Register constants Pi and E with their actual values
        ConstantValue piConst;
        piConst.type = PasType::real();
        piConst.realVal = 3.14159265358979323846;
        piConst.hasValue = true;

        ConstantValue eConst;
        eConst.type = PasType::real();
        eConst.realVal = 2.71828182845904523536;
        eConst.hasValue = true;

        unit.constants["pi"] = piConst;
        unit.constants["e"] = eConst;

        // Register all Viper.Math builtins
        for (size_t i = 0; i < static_cast<size_t>(PascalBuiltin::Count); ++i)
        {
            auto id = static_cast<PascalBuiltin>(i);
            const auto &desc = getBuiltinDescriptor(id);

            if (!desc.name || desc.category != BuiltinCategory::ViperMath)
                continue;

            FuncSignature sig;
            sig.name = desc.name;
            sig.returnType = resultTypeToPassType(desc.result);

            if (!desc.variadic)
            {
                for (const auto &arg : desc.args)
                {
                    if (!arg.optional)
                    {
                        sig.params.emplace_back("arg", maskToType(arg.allowed));
                        sig.isVarParam.push_back(arg.isVar);
                    }
                }
            }

            unit.functions[toLower(desc.name)] = sig;
        }

        // Also register core math functions that should be in the unit per spec:
        // Sqrt, Abs, Floor, Ceil, Sin, Cos, Tan, Exp, Ln (already in core)
        // These are available in core, but the unit re-exports them for consistency
        auto addCoreFunc = [&](const char *name, ResultKind res, ArgTypeMask argMask)
        {
            FuncSignature sig;
            sig.name = name;
            sig.returnType = resultTypeToPassType(res);
            sig.params.emplace_back("arg", maskToType(argMask));
            sig.isVarParam.push_back(false);
            unit.functions[toLower(name)] = sig;
        };

        // Re-export core math functions in the unit
        addCoreFunc("Sqrt", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Abs", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Floor", ResultKind::Integer, ArgTypeMask::Numeric);
        addCoreFunc("Ceil", ResultKind::Integer, ArgTypeMask::Numeric);
        addCoreFunc("Sin", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Cos", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Tan", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Exp", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Ln", ResultKind::Real, ArgTypeMask::Numeric);

        registerUnit(unit);
    }
}

//===----------------------------------------------------------------------===//
// Unit Management
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::registerUnit(const UnitInfo &unitInfo)
{
    std::string key = toLower(unitInfo.name);
    units_[key] = unitInfo;
}

const UnitInfo *SemanticAnalyzer::getUnit(const std::string &name) const
{
    auto it = units_.find(toLower(name));
    if (it != units_.end())
        return &it->second;
    return nullptr;
}

bool SemanticAnalyzer::importUnits(const std::vector<std::string> &unitNames)
{
    bool allFound = true;

    for (const auto &unitName : unitNames)
    {
        // Check if this is a Viper standard unit (Viper.Strings, Viper.Math, Crt, etc.)
        if (isViperUnit(unitName))
        {
            // Import builtins from this Viper unit
            auto builtins = getUnitBuiltins(unitName);
            for (auto builtin : builtins)
            {
                const auto &desc = getBuiltinDescriptor(builtin);
                if (!desc.name)
                    continue;

                // Register the builtin as a function
                FuncSignature sig;
                sig.name = desc.name;

                // Set up parameters
                for (size_t i = 0; i < desc.args.size() && i < desc.maxArgs; ++i)
                {
                    const auto &argSpec = desc.args[i];
                    PasType paramType; // default to unknown

                    // Determine parameter type from allowed mask
                    // Check Numeric (Integer|Real) first - use Real to accept both
                    if ((argSpec.allowed & ArgTypeMask::Integer) &&
                        (argSpec.allowed & ArgTypeMask::Real))
                        paramType = PasType::real();
                    else if (argSpec.allowed & ArgTypeMask::Integer)
                        paramType = PasType::integer();
                    else if (argSpec.allowed & ArgTypeMask::Real)
                        paramType = PasType::real();
                    else if (argSpec.allowed & ArgTypeMask::String)
                        paramType = PasType::string();
                    else if (argSpec.allowed & ArgTypeMask::Boolean)
                        paramType = PasType::boolean();

                    sig.params.push_back({"arg" + std::to_string(i), paramType});
                    sig.isVarParam.push_back(argSpec.isVar);
                    sig.hasDefault.push_back(argSpec.optional);
                }

                // Set return type
                sig.returnType = getBuiltinResultType(builtin);
                sig.requiredParams = desc.minArgs;

                std::string key = toLower(desc.name);
                functions_[key] = sig;
            }

            // Also import constants and functions from the registered unit
            const UnitInfo *unit = getUnit(unitName);
            if (unit)
            {
                for (const auto &[key, constVal] : unit->constants)
                {
                    constants_[key] = constVal.type;
                    // Import actual values too
                    if (constVal.hasValue)
                    {
                        if (constVal.type.kind == PasTypeKind::Integer)
                            constantValues_[key] = constVal.intVal;
                        else if (constVal.type.kind == PasTypeKind::Real)
                            constantRealValues_[key] = constVal.realVal;
                        else if (constVal.type.kind == PasTypeKind::String)
                            constantStrValues_[key] = constVal.strVal;
                    }
                }
                for (const auto &[key, sig] : unit->functions)
                {
                    // Don't overwrite builtins we already registered
                    if (functions_.find(key) == functions_.end())
                    {
                        functions_[key] = sig;
                    }
                }
            }
            continue;
        }

        const UnitInfo *unit = getUnit(unitName);
        if (!unit)
        {
            error({}, "unit '" + unitName + "' not found");
            allFound = false;
            continue;
        }

        // Import all exported symbols into the current scope
        for (const auto &[key, type] : unit->types)
        {
            types_[key] = type;
        }

        for (const auto &[key, constVal] : unit->constants)
        {
            constants_[key] = constVal.type;
            // Import actual values too
            if (constVal.hasValue)
            {
                if (constVal.type.kind == PasTypeKind::Integer)
                    constantValues_[key] = constVal.intVal;
                else if (constVal.type.kind == PasTypeKind::Real)
                    constantRealValues_[key] = constVal.realVal;
                else if (constVal.type.kind == PasTypeKind::String)
                    constantStrValues_[key] = constVal.strVal;
            }
        }

        for (const auto &[key, sig] : unit->functions)
        {
            functions_[key] = sig;
        }

        for (const auto &[key, classInfo] : unit->classes)
        {
            classes_[key] = classInfo;
        }

        for (const auto &[key, ifaceInfo] : unit->interfaces)
        {
            interfaces_[key] = ifaceInfo;
        }
    }

    return allFound;
}

UnitInfo SemanticAnalyzer::extractUnitExports(const Unit &unit)
{
    UnitInfo info;
    info.name = unit.name;

    // Process each interface declaration
    for (const auto &decl : unit.interfaceDecls)
    {
        if (!decl)
            continue;

        switch (decl->kind)
        {
            case DeclKind::Type:
            {
                auto &td = static_cast<const TypeDecl &>(*decl);
                std::string key = toLower(td.name);
                // Type must be resolved - look it up from current types_
                auto it = types_.find(key);
                if (it != types_.end())
                    info.types[key] = it->second;
                break;
            }
            case DeclKind::Const:
            {
                auto &cd = static_cast<const ConstDecl &>(*decl);
                std::string key = toLower(cd.name);
                auto it = constants_.find(key);
                if (it != constants_.end())
                {
                    ConstantValue cv;
                    cv.type = it->second;
                    cv.hasValue = true;
                    // Get actual value based on type
                    if (it->second.kind == PasTypeKind::Integer)
                    {
                        auto valIt = constantValues_.find(key);
                        if (valIt != constantValues_.end())
                            cv.intVal = valIt->second;
                    }
                    else if (it->second.kind == PasTypeKind::Real)
                    {
                        auto valIt = constantRealValues_.find(key);
                        if (valIt != constantRealValues_.end())
                            cv.realVal = valIt->second;
                    }
                    else if (it->second.kind == PasTypeKind::String)
                    {
                        auto valIt = constantStrValues_.find(key);
                        if (valIt != constantStrValues_.end())
                            cv.strVal = valIt->second;
                    }
                    info.constants[key] = cv;
                }
                break;
            }
            case DeclKind::Procedure:
            case DeclKind::Function:
            {
                std::string name;
                if (decl->kind == DeclKind::Procedure)
                    name = static_cast<const ProcedureDecl &>(*decl).name;
                else
                    name = static_cast<const FunctionDecl &>(*decl).name;
                std::string key = toLower(name);
                auto it = functions_.find(key);
                if (it != functions_.end())
                    info.functions[key] = it->second;
                break;
            }
            case DeclKind::Class:
            {
                auto &cd = static_cast<const ClassDecl &>(*decl);
                std::string key = toLower(cd.name);
                auto it = classes_.find(key);
                if (it != classes_.end())
                    info.classes[key] = it->second;
                break;
            }
            case DeclKind::Interface:
            {
                auto &id = static_cast<const InterfaceDecl &>(*decl);
                std::string key = toLower(id.name);
                auto it = interfaces_.find(key);
                if (it != interfaces_.end())
                    info.interfaces[key] = it->second;
                break;
            }
            default:
                break;
        }
    }

    return info;
}


} // namespace il::frontends::pascal
