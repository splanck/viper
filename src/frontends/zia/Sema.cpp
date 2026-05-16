//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema.cpp
/// @brief Implementation of Zia semantic analyzer.
///
/// @details This file implements the core Sema class: constructor, multi-pass
/// analyze() orchestrator, scope management, type narrowing, error reporting,
/// built-in registration, and namespace support.
///
/// Other Sema method groups are split into separate files:
/// - Sema_Generics.cpp: Generic type/function substitution and instantiation
/// - Sema_TypeResolution.cpp: Type resolution, extern functions, captures
/// - Sema_Runtime.cpp: Runtime function registration
/// - Sema_Decl.cpp: Declaration analysis (bind, struct, class, interface, etc.)
/// - Sema_Stmt.cpp: Statement analysis
/// - Sema_Expr.cpp: Expression analysis
///
/// @see Sema.hpp for the class interface
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"
#include "frontends/zia/Types.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <limits>
#include <map>
#include <sstream>

namespace il::frontends::zia {

namespace {

int compareLoc(const SourceLoc &a, const SourceLoc &b) {
    if (a.file_id != b.file_id)
        return (a.file_id < b.file_id) ? -1 : 1;
    if (a.line != b.line)
        return (a.line < b.line) ? -1 : 1;
    if (a.column != b.column)
        return (a.column < b.column) ? -1 : 1;
    return 0;
}

std::string classifySemanticError(const std::string &message) {
    auto startsWith = [&](const char *prefix) { return message.rfind(prefix, 0) == 0; };
    if (startsWith("Undefined identifier"))
        return "V-ZIA-UNDEFINED";
    if (startsWith("Type mismatch"))
        return "V-ZIA-TYPE-MISMATCH";
    if (message.find("out of bounds") != std::string::npos)
        return "V-ZIA-BOUNDS";
    if (message.find("Index") != std::string::npos || message.find("index") != std::string::npos)
        return "V-ZIA-INDEX";
    if (message.find("duplicate") != std::string::npos ||
        message.find("Duplicate") != std::string::npos ||
        message.find("already defined") != std::string::npos)
        return "V-ZIA-DUPLICATE";
    if (message.find("optional") != std::string::npos ||
        message.find("Optional") != std::string::npos)
        return "V-ZIA-OPTIONAL";
    if (message.find("return") != std::string::npos || message.find("Return") != std::string::npos)
        return "V-ZIA-RETURN";
    return "V-ZIA-SEMA";
}

bool isTopLevelModuleScopedDeclKind(DeclKind kind) {
    switch (kind) {
        case DeclKind::Function:
        case DeclKind::Struct:
        case DeclKind::Class:
        case DeclKind::Interface:
        case DeclKind::Enum:
        case DeclKind::GlobalVar:
        case DeclKind::TypeAlias:
            return true;
        default:
            return false;
    }
}

std::string topLevelModuleScopedDeclName(const Decl &decl) {
    switch (decl.kind) {
        case DeclKind::Function:
            return static_cast<const FunctionDecl &>(decl).name;
        case DeclKind::Struct:
            return static_cast<const StructDecl &>(decl).name;
        case DeclKind::Class:
            return static_cast<const ClassDecl &>(decl).name;
        case DeclKind::Interface:
            return static_cast<const InterfaceDecl &>(decl).name;
        case DeclKind::Enum:
            return static_cast<const EnumDecl &>(decl).name;
        case DeclKind::GlobalVar:
            return static_cast<const GlobalVarDecl &>(decl).name;
        case DeclKind::TypeAlias:
            return static_cast<const TypeAliasDecl &>(decl).name;
        default:
            return "";
    }
}

size_t editDistance(std::string_view lhs, std::string_view rhs) {
    std::vector<size_t> previous(rhs.size() + 1);
    std::vector<size_t> current(rhs.size() + 1);
    for (size_t j = 0; j <= rhs.size(); ++j)
        previous[j] = j;
    for (size_t i = 1; i <= lhs.size(); ++i) {
        current[0] = i;
        for (size_t j = 1; j <= rhs.size(); ++j) {
            const size_t substitution = previous[j - 1] + (lhs[i - 1] == rhs[j - 1] ? 0 : 1);
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
        }
        previous.swap(current);
    }
    return previous[rhs.size()];
}

std::string sanitizeForSymbol(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '.')
            out.push_back(ch);
        else
            out.push_back('_');
    }
    return out;
}

std::string joinTypeKeys(const std::vector<TypeRef> &types) {
    std::string result;
    bool first = true;
    for (const auto &type : types) {
        if (!first)
            result += "__";
        first = false;
        result += sanitizeForSymbol(type ? type->toString() : "?");
    }
    if (result.empty())
        result = "void";
    return result;
}

size_t requiredParamCount(const std::vector<Param> &params) {
    size_t required = 0;
    for (const auto &param : params) {
        if (!param.defaultValue && !param.isVariadic)
            ++required;
    }
    return required;
}

bool canRuntimeObjectCoerce(TypeRef argType) {
    if (!argType)
        return false;

    switch (argType->kind) {
        case TypeKindSem::Void:
        case TypeKindSem::Optional:
        case TypeKindSem::Function:
        case TypeKindSem::Tuple:
        case TypeKindSem::FixedArray:
        case TypeKindSem::Unknown:
        case TypeKindSem::Never:
        case TypeKindSem::TypeParam:
        case TypeKindSem::Module:
            return false;
        default:
            return true;
    }
}

bool allowsFunctionPointerParam(std::string_view name) {
    std::string lower;
    lower.reserve(name.size());
    for (char ch : name)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

    return lower == "fn" || lower == "func" || lower == "entry" || lower == "callback" ||
           lower == "handler" || lower == "pred" || lower == "predicate" || lower == "transform" ||
           lower == "mapper" || lower == "reducer" || lower == "action" || lower == "compare" ||
           lower == "comparator" || lower.find("callback") != std::string::npos ||
           lower.find("handler") != std::string::npos || lower.find("entry") != std::string::npos ||
           lower.find("predicate") != std::string::npos ||
           lower.find("transform") != std::string::npos ||
           lower.find("mapper") != std::string::npos ||
           lower.find("reducer") != std::string::npos ||
           lower.find("comparator") != std::string::npos;
}

bool isSafeFunctionBridgePayload(TypeRef type) {
    if (!type)
        return true;
    if (type->kind == TypeKindSem::Function)
        return false;
    if (type->kind == TypeKindSem::Ptr)
        return !type->name.empty();
    return true;
}

std::string saferRuntimePointerAlternative(std::string_view calleeName) {
    static const std::unordered_map<std::string_view, std::string_view> alternatives = {
        {"Viper.IO.Dir.List", "Viper.IO.Dir.ListSeq"},
        {"Viper.IO.Dir.Files", "Viper.IO.Dir.FilesSeq"},
        {"Viper.IO.Dir.Dirs", "Viper.IO.Dir.DirsSeq"},
        {"Viper.IO.File.ReadBytes", "Viper.IO.File.ReadAllBytes"},
        {"Viper.IO.File.ReadLines", "Viper.IO.File.ReadAllLines"},
        {"Viper.IO.File.WriteBytes", "Viper.IO.File.WriteAllBytes"},
        {"Viper.IO.File.WriteLines", "Viper.IO.File.WriteAllLines"},
        {"Viper.Core.Box.TryToI64", "Viper.Core.Box.ToI64Option"},
        {"Viper.Core.Box.TryToF64", "Viper.Core.Box.ToF64Option"},
        {"Viper.Core.Box.TryToI1", "Viper.Core.Box.ToI1Option"},
        {"Viper.Core.Box.TryToStr", "Viper.Core.Box.ToStrOption"},
        {"Viper.Core.Parse.Double", "Viper.Core.Parse.DoubleOption or Viper.Core.Parse.NumOr"},
        {"Viper.Core.Parse.Int64", "Viper.Core.Parse.Int64Option or Viper.Core.Parse.IntOr"},
        {"Viper.Core.Parse.TryInt", "Viper.Core.Parse.IntOr or Viper.Core.Parse.Int64Option"},
        {"Viper.Core.Parse.TryNum", "Viper.Core.Parse.NumOr or Viper.Core.Parse.DoubleOption"},
        {"Viper.Core.Parse.TryBool", "Viper.Core.Parse.BoolOr"},
        {"Viper.Parse.Double", "Viper.Parse.DoubleOption or Viper.Parse.NumOr"},
        {"Viper.Parse.Int64", "Viper.Parse.Int64Option or Viper.Parse.IntOr"},
        {"Viper.Parse.TryInt", "Viper.Parse.IntOr or Viper.Parse.Int64Option"},
        {"Viper.Parse.TryNum", "Viper.Parse.NumOr or Viper.Parse.DoubleOption"},
        {"Viper.Parse.TryBool", "Viper.Parse.BoolOr"},
        {"Viper.String.SplitFields", "Viper.String.SplitFieldsSeq"},
        {"Viper.Graphics.Canvas.Polyline", "Viper.Graphics.Canvas.PolylinePath"},
        {"Viper.Graphics.Canvas.Polygon", "Viper.Graphics.Canvas.PolygonPath"},
        {"Viper.Graphics.Canvas.PolygonFrame", "Viper.Graphics.Canvas.PolygonFramePath"},
        {"Viper.Game.ParticleEmitter.Get", "Viper.Game.ParticleEmitter.ParticleAt"},
        {"Viper.Threads.Pool.Submit",
         "Viper.Threads.Thread.Start or Viper.Threads.Async.Run for managed Zia callbacks"},
        {"Viper.Core.MessageBus.Callback",
         "a managed callback returned by Viper.Core.MessageBus.Callback(&handler)"},
        {"Viper.Option.Map", "ordinary Zia optional control flow or a managed function reference"},
        {"Viper.Option.AndThen",
         "ordinary Zia optional control flow or a managed function reference"},
        {"Viper.Option.OrElse",
         "ordinary Zia optional control flow or a managed function reference"},
        {"Viper.Option.Filter",
         "ordinary Zia optional control flow or a managed function reference"},
        {"Viper.Result.Map", "ordinary Zia result control flow or a managed function reference"},
        {"Viper.Result.MapErr", "ordinary Zia result control flow or a managed function reference"},
        {"Viper.Result.AndThen",
         "ordinary Zia result control flow or a managed function reference"},
        {"Viper.Result.OrElse", "ordinary Zia result control flow or a managed function reference"},
    };

    auto it = alternatives.find(calleeName);
    return it == alternatives.end() ? std::string{} : std::string(it->second);
}

int conversionCost(TypeRef paramType, TypeRef argType, bool allowRuntimeObjectCoercion) {
    if (!paramType || !argType)
        return 1000;
    if (paramType->equals(*argType))
        return 0;
    if (allowRuntimeObjectCoercion && paramType->kind == TypeKindSem::Ptr &&
        canRuntimeObjectCoerce(argType)) {
        return 2;
    }
    if (paramType->kind == TypeKindSem::Optional) {
        if (argType->kind == TypeKindSem::Optional)
            return paramType->innerType() && argType->innerType() &&
                           paramType->innerType()->equals(*argType->innerType())
                       ? 1
                       : (paramType->isAssignableFrom(*argType) ? 3 : 1000);
        if (paramType->isAssignableFrom(*argType))
            return 2;
    }
    if (paramType->kind == TypeKindSem::Number && argType->kind == TypeKindSem::Integer)
        return 2;
    if (paramType->kind == TypeKindSem::Integer && argType->kind == TypeKindSem::Byte)
        return 2;
    if (paramType->kind == TypeKindSem::Number && argType->kind == TypeKindSem::Byte)
        return 3;
    if (paramType->isAssignableFrom(*argType))
        return 4;
    return 1000;
}

} // namespace

//=============================================================================
// Scope Implementation
//=============================================================================

/// @brief Define a symbol in the current scope.
/// @param name The symbol name to register.
/// @param symbol The symbol metadata to associate with the name.
void Scope::define(const std::string &name, Symbol symbol) {
    symbols_[name] = std::move(symbol);
}

/// @brief Look up a symbol by name, walking parent scopes.
/// @param name The symbol name to search for.
/// @return Pointer to the symbol if found, nullptr otherwise.
Symbol *Scope::lookup(const std::string &name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end())
        return &it->second;
    if (parent_)
        return parent_->lookup(name);
    return nullptr;
}

/// @brief Look up a symbol only in the current scope (no parent walk).
/// @param name The symbol name to search for.
/// @return Pointer to the symbol if found in this scope, nullptr otherwise.
Symbol *Scope::lookupLocal(const std::string &name) {
    auto it = symbols_.find(name);
    return it != symbols_.end() ? &it->second : nullptr;
}

//=============================================================================
// Sema Implementation
//=============================================================================

Sema::Sema(il::support::DiagnosticEngine &diag) : diag_(diag) {
    scopes_.push_back(std::make_unique<Scope>(nullptr, nextScopeId_, 0));
    currentScope_ = scopes_.back().get();
    narrowedTypes_.push_back({});
    scopeSnapshots_[nextScopeId_] = ScopeSnapshot{nextScopeId_, 0, 0, {}, {}};
    nextScopeId_++;
    types::clearInterfaceImplementations();
    types::clearClassInheritance();
    registerBuiltins();
}

TypeRef Sema::functionTypeForDecl(const FunctionDecl &decl) const {
    TypeRef declaredReturn =
        decl.returnType ? resolveType(decl.returnType.get()) : types::voidType();
    TypeRef returnType = decl.isAsync ? types::futureOf(declaredReturn) : declaredReturn;
    std::vector<TypeRef> paramTypes;
    paramTypes.reserve(decl.params.size());
    for (const auto &param : decl.params) {
        TypeRef resolved = param.type ? resolveType(param.type.get()) : types::unknown();
        if (param.isVariadic)
            resolved = types::list(resolved); // ...Integer → List[Integer]
        paramTypes.push_back(resolved);
    }
    return types::function(paramTypes, returnType);
}

TypeRef Sema::methodTypeForDecl(const MethodDecl &decl) const {
    bool pushedParams = false;
    if (!decl.genericParams.empty()) {
        std::map<std::string, TypeRef> params;
        for (const auto &param : decl.genericParams)
            params[param] = types::typeParam(param);
        const_cast<Sema *>(this)->pushTypeParams(params);
        pushedParams = true;
    }

    TypeRef returnType = decl.returnType ? resolveType(decl.returnType.get()) : types::voidType();
    std::vector<TypeRef> paramTypes;
    paramTypes.reserve(decl.params.size());
    for (const auto &param : decl.params)
        paramTypes.push_back(param.type ? resolveType(param.type.get()) : types::unknown());

    if (pushedParams)
        const_cast<Sema *>(this)->popTypeParams();
    return types::function(paramTypes, returnType);
}

std::string Sema::functionSignatureKey(const FunctionDecl &decl) const {
    TypeRef funcType = functionTypeForDecl(decl);
    return decl.name + "#" + joinTypeKeys(funcType->paramTypes());
}

std::string Sema::methodSignatureKey(const MethodDecl &decl) const {
    TypeRef methodType = methodTypeForDecl(decl);
    return methodSignatureKey(decl, methodType);
}

std::string Sema::methodSignatureKey(const MethodDecl &decl, TypeRef methodType) const {
    const auto paramTypes = methodType && methodType->kind == TypeKindSem::Function
                                ? methodType->paramTypes()
                                : std::vector<TypeRef>{};
    return decl.name + "#" + (decl.isStatic ? "static#" : "inst#") + joinTypeKeys(paramTypes);
}

std::string Sema::methodDispatchKey(const MethodDecl &decl) const {
    TypeRef methodType = methodTypeForDecl(decl);
    return methodDispatchKey(decl, methodType);
}

std::string Sema::methodDispatchKey(const MethodDecl &decl, TypeRef methodType) const {
    return methodSignatureKey(decl, methodType);
}

bool Sema::registerFunctionOverload(const std::string &name,
                                    FunctionDecl *decl,
                                    TypeRef funcType,
                                    SourceLoc loc) {
    auto &family = functionOverloads_[name];
    const std::string sigKey = functionSignatureKey(*decl);
    for (auto *existing : family) {
        if (functionSignatureKey(*existing) == sigKey) {
            error(loc, "Duplicate definition of '" + name + "' with the same signature");
            return false;
        }
    }

    family.push_back(decl);
    functionDeclTypes_[decl] = funcType;

    const bool overloaded = family.size() > 1;
    const bool isEntryStart =
        decl && decl->name == "start" && currentModule_ &&
        (currentModule_->loc.file_id == 0 || decl->loc.file_id == currentModule_->loc.file_id);
    if (isEntryStart) {
        loweredFunctionNames_[decl] = "main";
    } else if (overloaded) {
        loweredFunctionNames_[decl] = name + "__ov__" +
                                      std::to_string(funcType->paramTypes().size()) + "__" +
                                      joinTypeKeys(funcType->paramTypes());
    } else {
        loweredFunctionNames_[decl] = name;
    }
    functionDecls_[loweredFunctionNames_[decl]] = decl;
    if (family.size() == 1)
        functionDecls_[name] = decl;
    return true;
}

bool Sema::registerMethodOverload(const std::string &ownerType,
                                  MethodDecl *decl,
                                  TypeRef methodType,
                                  SourceLoc loc) {
    const std::string familyKey = ownerType + "." + decl->name;
    auto &family = methodOverloads_[familyKey];
    const MethodInstanceKey instanceKey{ownerType, decl};
    const std::string sigKey = methodSignatureKey(*decl, methodType);
    for (auto *existing : family) {
        if (methodSignatureKey(ownerType, existing) == sigKey) {
            error(loc,
                  "Duplicate definition of '" + decl->name + "' in type '" + ownerType +
                      "' with the same signature");
            return false;
        }
    }

    family.push_back(decl);
    ownerMethodTypes_[instanceKey] = methodType;
    ownerMethodSignatureKeys_[instanceKey] = sigKey;
    ownerMethodDispatchKeys_[instanceKey] = methodDispatchKey(*decl, methodType);
    if (!methodDeclTypes_.count(decl))
        methodDeclTypes_[decl] = methodType;
    methodSignatureKeys_.try_emplace(decl, sigKey);
    methodDispatchKeys_.try_emplace(decl, methodDispatchKey(*decl, methodType));

    if (family.size() > 1) {
        ownerLoweredMethodNames_[instanceKey] = ownerType + "." + decl->name + "__ov__" +
                                                std::to_string(methodType->paramTypes().size()) +
                                                "__" + joinTypeKeys(methodType->paramTypes());
    } else {
        ownerLoweredMethodNames_[instanceKey] = ownerType + "." + decl->name;
    }
    loweredMethodNames_.try_emplace(decl, ownerLoweredMethodNames_[instanceKey]);
    return true;
}

std::vector<MethodDecl *> Sema::collectMethodOverloads(const std::string &typeName,
                                                       const std::string &methodName,
                                                       bool includeInherited) const {
    std::vector<MethodDecl *> result;
    std::unordered_set<std::string> seenSignatures;

    auto addFamily = [&](const std::string &owner) {
        auto it = methodOverloads_.find(owner + "." + methodName);
        if (it == methodOverloads_.end())
            return;
        for (auto *method : it->second) {
            std::string sigKey = methodSignatureKey(owner, method);
            if (sigKey.empty())
                sigKey = methodSignatureKey(*method);
            if (seenSignatures.insert(sigKey).second)
                result.push_back(method);
        }
    };

    addFamily(typeName);
    if (!includeInherited)
        return result;

    auto classIt = lookupClassDeclForType(typeName);
    while (classIt && !classIt->baseClass.empty()) {
        const std::string &parentName = classIt->baseClass;
        addFamily(parentName);
        classIt = lookupClassDeclForType(parentName);
    }

    return result;
}

FunctionDecl *Sema::resolveFunctionOverload(const std::string &name,
                                            const std::vector<TypeRef> &argTypes,
                                            SourceLoc loc,
                                            std::string *loweredName,
                                            bool viaQualifiedModule) {
    auto it = functionOverloads_.find(name);
    if (it == functionOverloads_.end())
        return nullptr;

    FunctionDecl *best = nullptr;
    FunctionDecl *firstInaccessible = nullptr;
    int bestScore = std::numeric_limits<int>::max();
    bool ambiguous = false;
    std::vector<std::string> candidateSigs;

    for (auto *decl : it->second) {
        Symbol accessSym;
        accessSym.kind = Symbol::Kind::Function;
        accessSym.name = name;
        accessSym.type = functionDeclTypes_[decl];
        accessSym.decl = decl;
        accessSym.loc = decl->loc;
        accessSym.isExported = decl->isExported;
        if (!canAccessSymbol(accessSym, loc, name, viaQualifiedModule)) {
            if (!firstInaccessible)
                firstInaccessible = decl;
            continue;
        }

        TypeRef funcType = functionDeclTypes_[decl];
        const auto &params = decl->params;
        const size_t required = requiredParamCount(params);
        const size_t total = params.size();
        if (argTypes.size() < required || argTypes.size() > total)
            continue;

        int score = 0;
        bool viable = true;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            int cost = conversionCost(funcType->paramTypes()[i], argTypes[i], false);
            if (cost >= 1000) {
                viable = false;
                break;
            }
            score += cost;
        }
        if (!viable)
            continue;
        score += static_cast<int>(total - argTypes.size()) * 3;
        candidateSigs.push_back(loweredFunctionNames_[decl]);

        if (score < bestScore) {
            best = decl;
            bestScore = score;
            ambiguous = false;
        } else if (score == bestScore) {
            ambiguous = true;
        }
    }

    if (!best) {
        if (firstInaccessible) {
            Symbol accessSym;
            accessSym.kind = Symbol::Kind::Function;
            accessSym.name = name;
            accessSym.type = functionDeclTypes_[firstInaccessible];
            accessSym.decl = firstInaccessible;
            accessSym.loc = firstInaccessible->loc;
            accessSym.isExported = firstInaccessible->isExported;
            reportInaccessibleSymbol(loc, name, accessSym, viaQualifiedModule);
        }
        return nullptr;
    }
    if (ambiguous) {
        error(loc, "Ambiguous call to '" + name + "': " + formatOverloadCandidates(candidateSigs));
        return nullptr;
    }
    if (loweredName)
        *loweredName = loweredFunctionNames_[best];
    return best;
}

MethodDecl *Sema::resolveMethodOverload(const std::string &ownerType,
                                        const std::string &methodName,
                                        const std::vector<TypeRef> &argTypes,
                                        SourceLoc loc,
                                        std::string *resolvedOwnerType,
                                        bool includeInherited) {
    MethodDecl *best = nullptr;
    std::string bestOwner;
    int bestScore = std::numeric_limits<int>::max();
    bool ambiguous = false;
    std::vector<std::string> candidates;
    std::unordered_set<std::string> seenSignatures;

    auto considerOwner = [&](const std::string &candidateOwner) {
        auto it = methodOverloads_.find(candidateOwner + "." + methodName);
        if (it == methodOverloads_.end())
            return;
        for (auto *decl : it->second) {
            std::string sigKey = methodSignatureKey(candidateOwner, decl);
            if (sigKey.empty())
                sigKey = methodSignatureKey(*decl);
            if (!seenSignatures.insert(sigKey).second)
                continue;
            TypeRef methodType = getMethodType(candidateOwner, decl);
            if (!methodType)
                continue;
            const auto &params = decl->params;
            const size_t required = requiredParamCount(params);
            const size_t total = params.size();
            if (argTypes.size() < required || argTypes.size() > total)
                continue;
            int score = 0;
            bool viable = true;
            for (size_t i = 0; i < argTypes.size(); ++i) {
                int cost = conversionCost(methodType->paramTypes()[i], argTypes[i], false);
                if (cost >= 1000) {
                    viable = false;
                    break;
                }
                score += cost;
            }
            if (!viable)
                continue;
            score += static_cast<int>(total - argTypes.size()) * 3;
            std::string lowered = loweredMethodName(candidateOwner, decl);
            candidates.push_back(lowered.empty() ? (candidateOwner + "." + decl->name) : lowered);
            if (score < bestScore) {
                best = decl;
                bestOwner = candidateOwner;
                bestScore = score;
                ambiguous = false;
            } else if (score == bestScore) {
                ambiguous = true;
            }
        }
    };

    considerOwner(ownerType);
    if (includeInherited) {
        auto classIt = lookupClassDeclForType(ownerType);
        while (classIt && !classIt->baseClass.empty()) {
            considerOwner(classIt->baseClass);
            classIt = lookupClassDeclForType(classIt->baseClass);
        }
    }

    if (!best)
        return nullptr;
    if (ambiguous) {
        error(loc,
              "Ambiguous call to method '" + methodName +
                  "': " + formatOverloadCandidates(candidates));
        return nullptr;
    }
    if (resolvedOwnerType)
        *resolvedOwnerType = bestOwner;
    return best;
}

std::vector<Sema::CallParamSpec> Sema::makeParamSpecs(
    const std::vector<Param> &params, const std::vector<TypeRef> &paramTypes) const {
    std::vector<CallParamSpec> specs;
    specs.reserve(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        CallParamSpec spec;
        spec.name = params[i].name;
        spec.type = i < paramTypes.size() ? paramTypes[i] : nullptr;
        spec.hasDefault = params[i].defaultValue != nullptr;
        spec.isVariadic = params[i].isVariadic;
        specs.push_back(std::move(spec));
    }
    return specs;
}

std::vector<Sema::CallParamSpec> Sema::makeExternParamSpecs(const Symbol &sym,
                                                            size_t skipLeadingParams) const {
    std::vector<CallParamSpec> specs;
    if (!sym.type || sym.type->kind != TypeKindSem::Function)
        return specs;

    const auto paramTypes = sym.type->paramTypes();
    if (skipLeadingParams >= paramTypes.size())
        return specs;

    const size_t paramCount = paramTypes.size() - skipLeadingParams;
    specs.reserve(paramCount);

    size_t nameOffset = 0;
    if (sym.paramNames.size() > paramTypes.size())
        nameOffset = sym.paramNames.size() - paramTypes.size();

    for (size_t i = skipLeadingParams; i < paramTypes.size(); ++i) {
        CallParamSpec spec;
        if (nameOffset + i < sym.paramNames.size() && !sym.paramNames[nameOffset + i].empty()) {
            spec.name = sym.paramNames[nameOffset + i];
        } else {
            spec.name = "arg" + std::to_string((i - skipLeadingParams) + 1);
        }
        spec.type = paramTypes[i];
        specs.push_back(std::move(spec));
    }

    return specs;
}

void Sema::appendClassFieldSpecs(const std::string &typeName,
                                 std::vector<CallParamSpec> &out) const {
    auto classIt = classDecls_.find(typeName);
    if (classIt == classDecls_.end())
        return;

    ClassDecl *decl = classIt->second;
    if (!decl->baseClass.empty())
        appendClassFieldSpecs(decl->baseClass, out);

    for (const auto &member : decl->members) {
        if (member->kind != DeclKind::Field)
            continue;
        auto *field = static_cast<FieldDecl *>(member.get());
        if (field->isStatic)
            continue;
        auto visIt = memberVisibility_.find(typeName + "." + field->name);
        const bool externallyPrivate = visIt != memberVisibility_.end() &&
                                       visIt->second == Visibility::Private &&
                                       (!currentSelfType_ || currentSelfType_->name != typeName);

        CallParamSpec spec;
        spec.name = externallyPrivate ? "" : field->name;
        spec.type = getFieldType(typeName, field->name);
        if (!spec.type && field->type)
            spec.type = resolveType(field->type.get());
        spec.hasDefault = true;
        out.push_back(std::move(spec));
    }
}

std::vector<Sema::CallParamSpec> Sema::makeClassFieldSpecs(const std::string &typeName) const {
    std::vector<CallParamSpec> specs;
    appendClassFieldSpecs(typeName, specs);
    return specs;
}

std::vector<Sema::CallParamSpec> Sema::makeStructFieldSpecs(const std::string &typeName) const {
    std::vector<CallParamSpec> specs;
    auto structIt = structDecls_.find(typeName);
    if (structIt == structDecls_.end())
        return specs;

    for (const auto &member : structIt->second->members) {
        if (member->kind != DeclKind::Field)
            continue;
        auto *field = static_cast<FieldDecl *>(member.get());
        if (field->isStatic)
            continue;
        auto visIt = memberVisibility_.find(typeName + "." + field->name);
        const bool externallyPrivate = visIt != memberVisibility_.end() &&
                                       visIt->second == Visibility::Private &&
                                       (!currentSelfType_ || currentSelfType_->name != typeName);

        CallParamSpec spec;
        spec.name = externallyPrivate ? "" : field->name;
        spec.type = getFieldType(typeName, field->name);
        if (!spec.type && field->type)
            spec.type = resolveType(field->type.get());
        spec.hasDefault = true;
        specs.push_back(std::move(spec));
    }
    return specs;
}

bool Sema::bindCallArgs(const std::vector<CallArg> &args,
                        const std::vector<CallParamSpec> &params,
                        SourceLoc loc,
                        const std::string &calleeName,
                        CallArgBinding &binding,
                        int *score,
                        bool reportErrors,
                        bool allowRuntimeObjectCoercion) const {
    binding.fixedParamSources.assign(params.size(), -1);
    binding.variadicSources.clear();
    if (score)
        *score = 0;

    const bool hasVariadic = !params.empty() && params.back().isVariadic;
    const size_t fixedCount = hasVariadic ? params.size() - 1 : params.size();

    auto fail = [&](const std::string &message, SourceLoc errLoc) {
        if (reportErrors)
            const_cast<Sema *>(this)->error(errLoc, message);
        return false;
    };

    bool sawNamed = false;
    size_t nextPositional = 0;

    for (size_t argIndex = 0; argIndex < args.size(); ++argIndex) {
        const auto &arg = args[argIndex];
        if (arg.name) {
            sawNamed = true;
            int targetIndex = -1;
            for (size_t i = 0; i < fixedCount; ++i) {
                if (params[i].name == *arg.name) {
                    targetIndex = static_cast<int>(i);
                    break;
                }
            }

            if (targetIndex < 0) {
                if (hasVariadic && params.back().name == *arg.name) {
                    return fail("Named binding for variadic parameter '" + *arg.name +
                                    "' is not supported in call to '" + calleeName + "'",
                                arg.value ? arg.value->loc : loc);
                }
                return fail("Unknown argument '" + *arg.name + "' in call to '" + calleeName + "'",
                            arg.value ? arg.value->loc : loc);
            }

            if (binding.fixedParamSources[static_cast<size_t>(targetIndex)] >= 0) {
                return fail("Argument '" + *arg.name + "' is provided more than once in call to '" +
                                calleeName + "'",
                            arg.value ? arg.value->loc : loc);
            }

            binding.fixedParamSources[static_cast<size_t>(targetIndex)] =
                static_cast<int>(argIndex);
        } else {
            if (sawNamed) {
                return fail("Positional arguments cannot follow named arguments in call to '" +
                                calleeName + "'",
                            arg.value ? arg.value->loc : loc);
            }

            while (nextPositional < fixedCount && binding.fixedParamSources[nextPositional] >= 0) {
                ++nextPositional;
            }

            if (nextPositional < fixedCount) {
                binding.fixedParamSources[nextPositional] = static_cast<int>(argIndex);
                ++nextPositional;
            } else if (hasVariadic) {
                binding.variadicSources.push_back(static_cast<int>(argIndex));
            } else {
                return fail("Too many arguments to '" + calleeName + "'",
                            arg.value ? arg.value->loc : loc);
            }
        }
    }

    for (size_t i = 0; i < fixedCount; ++i) {
        if (binding.fixedParamSources[i] < 0 && !params[i].hasDefault) {
            return fail("Missing required argument '" + params[i].name + "' in call to '" +
                            calleeName + "'",
                        loc);
        }
    }

    int totalScore = 0;
    for (size_t i = 0; i < fixedCount; ++i) {
        int sourceIndex = binding.fixedParamSources[i];
        if (sourceIndex < 0) {
            totalScore += 3;
            continue;
        }

        TypeRef paramType = params[i].type;
        TypeRef argType = exprTypes_.count(args[static_cast<size_t>(sourceIndex)].value.get())
                              ? exprTypes_.at(args[static_cast<size_t>(sourceIndex)].value.get())
                              : nullptr;
        int cost = conversionCost(paramType, argType, allowRuntimeObjectCoercion);
        if (allowRuntimeObjectCoercion && paramType && paramType->kind == TypeKindSem::Ptr &&
            argType && argType->kind == TypeKindSem::Function &&
            allowsFunctionPointerParam(params[i].name)) {
            cost = 2;
        }
        if (cost >= 1000) {
            if (reportErrors) {
                const_cast<Sema *>(this)->errorTypeMismatch(
                    args[static_cast<size_t>(sourceIndex)].value->loc, paramType, argType);
            }
            return false;
        }
        totalScore += cost;
    }

    if (hasVariadic) {
        TypeRef listType = params.back().type;
        TypeRef elemType = listType ? listType->elementType() : nullptr;
        for (int sourceIndex : binding.variadicSources) {
            TypeRef argType =
                exprTypes_.count(args[static_cast<size_t>(sourceIndex)].value.get())
                    ? exprTypes_.at(args[static_cast<size_t>(sourceIndex)].value.get())
                    : nullptr;
            int cost = conversionCost(elemType, argType, allowRuntimeObjectCoercion);
            if (allowRuntimeObjectCoercion && elemType && elemType->kind == TypeKindSem::Ptr &&
                argType && argType->kind == TypeKindSem::Function &&
                allowsFunctionPointerParam(params.back().name)) {
                cost = 2;
            }
            if (cost >= 1000) {
                if (reportErrors) {
                    const_cast<Sema *>(this)->errorTypeMismatch(
                        args[static_cast<size_t>(sourceIndex)].value->loc, elemType, argType);
                }
                return false;
            }
            totalScore += cost;
        }
    }

    if (score)
        *score = totalScore;
    return true;
}

bool Sema::checkRuntimePointerSafety(const std::string &calleeName,
                                     const std::vector<CallArg> &args,
                                     const std::vector<CallParamSpec> &params,
                                     const CallArgBinding &binding,
                                     size_t skipLeadingParams,
                                     SourceLoc loc) const {
    RuntimePointerSafety safety;
    bool hasSafety = false;
    if (auto it = runtimePointerSafety_.find(calleeName); it != runtimePointerSafety_.end()) {
        safety = it->second;
        hasSafety = true;
    } else {
        const auto &registry = il::runtime::RuntimeRegistry::instance();
        if (auto sig = registry.findFunction(calleeName)) {
            safety.rawPointerReturn = sig->rawPointerReturn;
            safety.rawPointerParams = sig->rawPointerParams;
            hasSafety = true;
        }
    }

    if (!hasSafety)
        return true;

    auto diagnosticMessage = [&](std::string detail) {
        std::string message = "Runtime API '" + calleeName + "' exposes " + detail +
                              " and is unavailable in safe Zia";
        if (std::string alternative = saferRuntimePointerAlternative(calleeName);
            !alternative.empty()) {
            message += "; use " + alternative;
        } else {
            message += "; use a typed runtime class/API";
        }
        return message;
    };

    if (safety.rawPointerReturn) {
        const_cast<Sema *>(this)->error(loc, diagnosticMessage("a runtime-internal return handle"));
        return false;
    }

    auto isRawParam = [&](size_t exposedParamIndex) -> bool {
        size_t fullIndex = skipLeadingParams + exposedParamIndex;
        if (fullIndex < safety.rawPointerParams.size())
            return static_cast<bool>(safety.rawPointerParams[fullIndex]);
        if (skipLeadingParams > 0 && exposedParamIndex < safety.rawPointerParams.size())
            return static_cast<bool>(safety.rawPointerParams[exposedParamIndex]);
        return false;
    };

    auto bridgeRoleFor = [&](size_t exposedParamIndex) -> RuntimePointerBridgeRole {
        size_t fullIndex = skipLeadingParams + exposedParamIndex;
        if (fullIndex < safety.bridgeRoles.size())
            return safety.bridgeRoles[fullIndex];
        if (skipLeadingParams > 0 && exposedParamIndex < safety.bridgeRoles.size())
            return safety.bridgeRoles[exposedParamIndex];
        return RuntimePointerBridgeRole::None;
    };

    std::vector<bool> safeFunctionBridge(params.size(), false);
    for (size_t i = 0; i < params.size(); ++i) {
        RuntimePointerBridgeRole bridgeRole = bridgeRoleFor(i);
        const bool rawParam = isRawParam(i);
        if (!rawParam && bridgeRole == RuntimePointerBridgeRole::None)
            continue;

        int sourceIndex = i < binding.fixedParamSources.size() ? binding.fixedParamSources[i] : -1;
        TypeRef argType = nullptr;
        const CallArg *sourceArg = nullptr;
        SourceLoc errLoc = loc;
        if (sourceIndex >= 0 && static_cast<size_t>(sourceIndex) < args.size()) {
            sourceArg = &args[static_cast<size_t>(sourceIndex)];
            if (sourceArg->value) {
                errLoc = sourceArg->value->loc;
                auto typeIt = exprTypes_.find(sourceArg->value.get());
                if (typeIt != exprTypes_.end())
                    argType = typeIt->second;
            }
        }

        const std::string paramName = params[i].name.empty() ? "argument " + std::to_string(i + 1)
                                                             : "parameter '" + params[i].name + "'";

        if (bridgeRole == RuntimePointerBridgeRole::Callback) {
            if (sourceArg && argType && argType->kind == TypeKindSem::Function) {
                safeFunctionBridge[i] = true;
                continue;
            }

            const_cast<Sema *>(this)->error(
                errLoc,
                "Runtime API '" + calleeName + "' callback " + paramName +
                    " requires a function reference such as 'handler' or '&handler'");
            return false;
        }

        bool pairedWithFunctionBridge = false;
        if (bridgeRole == RuntimePointerBridgeRole::Payload) {
            for (size_t prior = 0; prior < i; ++prior) {
                if (safeFunctionBridge[prior]) {
                    pairedWithFunctionBridge = true;
                    break;
                }
            }
        }
        if (pairedWithFunctionBridge && isSafeFunctionBridgePayload(argType)) {
            continue;
        }
        if (bridgeRole == RuntimePointerBridgeRole::Payload && !rawParam) {
            continue;
        }

        const_cast<Sema *>(this)->error(errLoc,
                                        diagnosticMessage("runtime-internal handle " + paramName));
        return false;
    }

    return true;
}

FunctionDecl *Sema::resolveFunctionArgOverload(const std::string &name,
                                               const std::vector<CallArg> &args,
                                               SourceLoc loc,
                                               std::string *loweredName,
                                               CallArgBinding *bindingOut,
                                               bool viaQualifiedModule) {
    auto it = functionOverloads_.find(name);
    if (it == functionOverloads_.end())
        return nullptr;

    FunctionDecl *best = nullptr;
    FunctionDecl *firstInaccessible = nullptr;
    CallArgBinding bestBinding;
    int bestScore = std::numeric_limits<int>::max();
    bool ambiguous = false;
    std::vector<std::string> candidateSigs;

    for (auto *decl : it->second) {
        Symbol accessSym;
        accessSym.kind = Symbol::Kind::Function;
        accessSym.name = name;
        accessSym.type = functionDeclTypes_[decl];
        accessSym.decl = decl;
        accessSym.loc = decl->loc;
        accessSym.isExported = decl->isExported;
        if (!canAccessSymbol(accessSym, loc, name, viaQualifiedModule)) {
            if (!firstInaccessible)
                firstInaccessible = decl;
            continue;
        }

        TypeRef funcType = getFunctionType(decl);
        if (!funcType || funcType->kind != TypeKindSem::Function)
            continue;

        CallArgBinding binding;
        int score = 0;
        auto specs = makeParamSpecs(decl->params, funcType->paramTypes());
        if (!bindCallArgs(args, specs, loc, name, binding, &score, false))
            continue;

        std::string lowered = loweredFunctionNames_[decl];
        candidateSigs.push_back(lowered);
        if (score < bestScore) {
            best = decl;
            bestBinding = binding;
            bestScore = score;
            ambiguous = false;
        } else if (score == bestScore) {
            ambiguous = true;
        }
    }

    if (!best) {
        if (firstInaccessible) {
            Symbol accessSym;
            accessSym.kind = Symbol::Kind::Function;
            accessSym.name = name;
            accessSym.type = functionDeclTypes_[firstInaccessible];
            accessSym.decl = firstInaccessible;
            accessSym.loc = firstInaccessible->loc;
            accessSym.isExported = firstInaccessible->isExported;
            reportInaccessibleSymbol(loc, name, accessSym, viaQualifiedModule);
        }
        return nullptr;
    }
    if (ambiguous) {
        error(loc, "Ambiguous call to '" + name + "': " + formatOverloadCandidates(candidateSigs));
        return nullptr;
    }
    if (loweredName)
        *loweredName = loweredFunctionNames_[best];
    if (bindingOut)
        *bindingOut = bestBinding;
    return best;
}

MethodDecl *Sema::resolveMethodArgOverload(const std::string &ownerType,
                                           const std::string &methodName,
                                           const std::vector<CallArg> &args,
                                           SourceLoc loc,
                                           std::string *resolvedOwnerType,
                                           bool includeInherited,
                                           CallArgBinding *bindingOut) {
    MethodDecl *best = nullptr;
    CallArgBinding bestBinding;
    std::string bestOwner;
    int bestScore = std::numeric_limits<int>::max();
    bool ambiguous = false;
    std::vector<std::string> candidates;
    std::unordered_set<std::string> seenSignatures;

    auto considerOwner = [&](const std::string &candidateOwner) {
        auto it = methodOverloads_.find(candidateOwner + "." + methodName);
        if (it == methodOverloads_.end())
            return;

        for (auto *decl : it->second) {
            std::string sigKey = methodSignatureKey(candidateOwner, decl);
            if (sigKey.empty())
                sigKey = methodSignatureKey(*decl);
            if (!seenSignatures.insert(sigKey).second)
                continue;

            TypeRef methodType = getMethodType(candidateOwner, decl);
            if (!methodType || methodType->kind != TypeKindSem::Function)
                continue;

            CallArgBinding binding;
            int score = 0;
            auto specs = makeParamSpecs(decl->params, methodType->paramTypes());
            if (!bindCallArgs(
                    args, specs, loc, candidateOwner + "." + methodName, binding, &score, false))
                continue;

            std::string lowered = loweredMethodName(candidateOwner, decl);
            candidates.push_back(lowered.empty() ? (candidateOwner + "." + decl->name) : lowered);
            if (score < bestScore) {
                best = decl;
                bestBinding = binding;
                bestOwner = candidateOwner;
                bestScore = score;
                ambiguous = false;
            } else if (score == bestScore) {
                ambiguous = true;
            }
        }
    };

    considerOwner(ownerType);
    if (includeInherited) {
        auto classIt = lookupClassDeclForType(ownerType);
        while (classIt && !classIt->baseClass.empty()) {
            considerOwner(classIt->baseClass);
            classIt = lookupClassDeclForType(classIt->baseClass);
        }
    }

    if (!best)
        return nullptr;
    if (ambiguous) {
        error(loc,
              "Ambiguous call to method '" + methodName +
                  "': " + formatOverloadCandidates(candidates));
        return nullptr;
    }
    if (resolvedOwnerType)
        *resolvedOwnerType = bestOwner;
    if (bindingOut)
        *bindingOut = bestBinding;
    return best;
}

FunctionDecl *Sema::resolveFunctionCallOverload(const std::string &name,
                                                CallExpr *expr,
                                                SourceLoc loc,
                                                std::string *loweredName,
                                                CallArgBinding *bindingOut,
                                                bool viaQualifiedModule) {
    return resolveFunctionArgOverload(
        name, expr->args, loc, loweredName, bindingOut, viaQualifiedModule);
}

MethodDecl *Sema::resolveMethodCallOverload(const std::string &ownerType,
                                            const std::string &methodName,
                                            CallExpr *expr,
                                            SourceLoc loc,
                                            std::string *resolvedOwnerType,
                                            bool includeInherited,
                                            CallArgBinding *bindingOut) {
    return resolveMethodArgOverload(
        ownerType, methodName, expr->args, loc, resolvedOwnerType, includeInherited, bindingOut);
}

MethodDecl *Sema::findInheritedExactMethod(const std::string &ownerType,
                                           const MethodDecl &decl) const {
    auto classIt = lookupClassDeclForType(ownerType);
    if (!classIt)
        return nullptr;
    const std::string wanted = methodSignatureKey(ownerType, &decl);
    std::string parentName = classIt->baseClass;
    while (!parentName.empty()) {
        auto famIt = methodOverloads_.find(parentName + "." + decl.name);
        if (famIt != methodOverloads_.end()) {
            for (auto *candidate : famIt->second) {
                std::string candidateKey = methodSignatureKey(parentName, candidate);
                if (candidateKey.empty())
                    candidateKey = methodSignatureKey(*candidate);
                if (candidateKey == wanted)
                    return candidate;
            }
        }
        ClassDecl *nextIt = lookupClassDeclForType(parentName);
        if (!nextIt)
            break;
        parentName = nextIt->baseClass;
    }
    return nullptr;
}

ClassDecl *Sema::lookupClassDeclForType(const std::string &typeName) const {
    auto it = classDecls_.find(typeName);
    if (it != classDecls_.end())
        return it->second;
    if (Decl *genericDecl = getGenericDeclForInstantiation(typeName)) {
        if (genericDecl->kind == DeclKind::Class)
            return static_cast<ClassDecl *>(genericDecl);
    }
    return nullptr;
}

StructDecl *Sema::lookupStructDeclForType(const std::string &typeName) const {
    auto it = structDecls_.find(typeName);
    if (it != structDecls_.end())
        return it->second;
    if (Decl *genericDecl = getGenericDeclForInstantiation(typeName)) {
        if (genericDecl->kind == DeclKind::Struct)
            return static_cast<StructDecl *>(genericDecl);
    }
    return nullptr;
}

bool Sema::hasOverloadedFunctionName(const std::string &name) const {
    auto it = functionOverloads_.find(name);
    return it != functionOverloads_.end() && it->second.size() > 1;
}

std::string Sema::formatOverloadCandidates(const std::vector<std::string> &candidates) const {
    std::string result;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i != 0)
            result += ", ";
        result += candidates[i];
    }
    return result;
}

void Sema::initWarnings(const WarningPolicy &policy) {
    warningPolicy_ = &policy;
    suppressions_.clear();
}

void Sema::addWarningSuppressions(uint32_t fileId, std::string_view source) {
    suppressions_.scan(fileId, source);
}

bool Sema::registerTypeDeclarationSymbol(Decl &decl, const std::string &semanticName) {
    TypeRef type;
    Symbol sym;
    sym.kind = Symbol::Kind::Type;
    sym.name = semanticName;
    sym.decl = &decl;
    sym.isExported = decl.isExported;

    auto makeGenericArgs = [](const std::vector<std::string> &params) {
        std::vector<TypeRef> args;
        args.reserve(params.size());
        for (const auto &param : params)
            args.push_back(types::typeParam(param));
        return args;
    };

    switch (decl.kind) {
        case DeclKind::Struct: {
            auto &value = static_cast<StructDecl &>(decl);
            if (!value.genericParams.empty()) {
                registerGenericType(semanticName, &value);
                type = std::make_shared<ViperType>(
                    TypeKindSem::Struct, semanticName, makeGenericArgs(value.genericParams));
            } else {
                type = types::structType(semanticName);
            }
            structDecls_[semanticName] = &value;
            break;
        }
        case DeclKind::Class: {
            auto &cls = static_cast<ClassDecl &>(decl);
            if (!cls.genericParams.empty()) {
                registerGenericType(semanticName, &cls);
                type = std::make_shared<ViperType>(
                    TypeKindSem::Class, semanticName, makeGenericArgs(cls.genericParams));
            } else {
                type = types::classType(semanticName);
            }
            classDecls_[semanticName] = &cls;
            break;
        }
        case DeclKind::Interface: {
            auto &iface = static_cast<InterfaceDecl &>(decl);
            if (!iface.genericParams.empty()) {
                registerGenericType(semanticName, &iface);
                type = std::make_shared<ViperType>(
                    TypeKindSem::Interface, semanticName, makeGenericArgs(iface.genericParams));
            } else {
                type = types::interface(semanticName);
            }
            interfaceDecls_[semanticName] = &iface;
            break;
        }
        case DeclKind::Enum: {
            auto &enumDecl = static_cast<EnumDecl &>(decl);
            type = types::enumType(semanticName);
            enumDecls_[semanticName] = &enumDecl;
            break;
        }
        default:
            return false;
    }

    sym.type = type;
    if (Symbol *existing = currentScope_->lookupLocal(semanticName)) {
        if (existing->kind == Symbol::Kind::Type && existing->decl == &decl) {
            existing->type = type;
            typeRegistry_[semanticName] = type;
            return true;
        }
        return defineSymbol(semanticName, sym);
    }

    if (defineSymbol(semanticName, sym)) {
        typeRegistry_[semanticName] = type;
        return true;
    }
    return false;
}

void Sema::registerTypeAliasPlaceholder(TypeAliasDecl &decl, const std::string &semanticName) {
    Symbol sym;
    sym.kind = Symbol::Kind::Type;
    sym.name = semanticName;
    sym.type = types::unknown();
    sym.decl = &decl;
    sym.isExported = decl.isExported;

    if (Symbol *existing = currentScope_->lookupLocal(semanticName)) {
        if (existing->kind == Symbol::Kind::Type && existing->decl == &decl) {
            existing->type = types::unknown();
        } else {
            defineSymbol(semanticName, sym);
        }
    } else {
        defineSymbol(semanticName, sym);
    }
    typeAliases_[semanticName] = types::unknown();
}

void Sema::resolvePendingTypeAliases(
    const std::vector<std::pair<TypeAliasDecl *, std::string>> &aliases) {
    for (size_t pass = 0; pass <= aliases.size(); ++pass) {
        bool changed = false;
        for (const auto &[alias, semanticName] : aliases) {
            auto currentIt = typeAliases_.find(semanticName);
            if (currentIt != typeAliases_.end() && currentIt->second &&
                currentIt->second->kind != TypeKindSem::Unknown) {
                continue;
            }

            TypeRef resolved = resolveTypeNode(alias->targetType.get());
            if (!resolved || resolved->kind == TypeKindSem::Unknown)
                continue;

            typeAliases_[semanticName] = resolved;
            if (Symbol *sym = currentScope_->lookupLocal(semanticName))
                sym->type = resolved;
            changed = true;
        }
        if (!changed)
            break;
    }

    for (const auto &[alias, semanticName] : aliases) {
        auto it = typeAliases_.find(semanticName);
        if (it == typeAliases_.end() || !it->second || it->second->kind == TypeKindSem::Unknown) {
            error(alias->loc, "Cannot resolve type alias target for '" + alias->name + "'");
        }
    }
}

void Sema::registerNominalTypeRelationships(std::vector<DeclPtr> &declarations) {
    auto registerInterfaces = [this](const std::string &ownerName,
                                     const SourceLoc &loc,
                                     const std::vector<std::string> &interfaces) {
        for (const auto &ifaceName : interfaces) {
            TypeRef ifaceType = resolveNamedType(ifaceName, loc);
            if (ifaceType && ifaceType->kind == TypeKindSem::Interface)
                types::registerInterfaceImplementation(ownerName, ifaceType->name);
        }
    };

    for (auto &decl : declarations) {
        switch (decl->kind) {
            case DeclKind::Struct: {
                auto *value = static_cast<StructDecl *>(decl.get());
                if (value->genericParams.empty()) {
                    const std::string ownerName = semanticNameForDecl(*value, value->name);
                    registerInterfaces(ownerName, value->loc, value->interfaces);
                }
                break;
            }
            case DeclKind::Class: {
                auto *cls = static_cast<ClassDecl *>(decl.get());
                if (!cls->genericParams.empty())
                    break;

                const std::string ownerName = semanticNameForDecl(*cls, cls->name);
                if (!cls->baseClass.empty()) {
                    TypeRef baseType = resolveNamedType(cls->baseClass, cls->loc);
                    if (baseType && baseType->kind == TypeKindSem::Class) {
                        cls->baseClass = baseType->name;
                        types::registerClassInheritance(ownerName, cls->baseClass);
                    }
                }
                registerInterfaces(ownerName, cls->loc, cls->interfaces);
                break;
            }
            default:
                break;
        }
    }
}

/// @brief Run multi-pass semantic analysis on a module.
/// @details Pass 1: Register all top-level declarations (types, functions, globals).
///          Pass 1b: Process namespace declarations (recursive multi-pass).
///          Pass 2: Register member signatures (fields, method types) for type declarations.
///          Pass 3: Analyze declaration bodies (function bodies, method bodies, initializers).
/// @param module The module AST to analyze.
/// @return True if analysis succeeded without errors, false otherwise.
bool Sema::analyze(ModuleDecl &module) {
    currentModule_ = &module;
    fileModuleNames_.clear();

    for (auto &bind : module.binds) {
        analyzeBind(bind);
    }

    prepareModuleScopedTypeNames(module);

    std::vector<std::pair<TypeAliasDecl *, std::string>> pendingTypeAliases;

    for (auto &decl : module.declarations) {
        switch (decl->kind) {
            case DeclKind::Struct: {
                auto *value = static_cast<StructDecl *>(decl.get());
                registerTypeDeclarationSymbol(*value, semanticNameForDecl(*value, value->name));
                break;
            }
            case DeclKind::Class: {
                auto *cls = static_cast<ClassDecl *>(decl.get());
                registerTypeDeclarationSymbol(*cls, semanticNameForDecl(*cls, cls->name));
                break;
            }
            case DeclKind::Interface: {
                auto *iface = static_cast<InterfaceDecl *>(decl.get());
                registerTypeDeclarationSymbol(*iface, semanticNameForDecl(*iface, iface->name));
                break;
            }
            case DeclKind::Enum: {
                auto *enumDecl = static_cast<EnumDecl *>(decl.get());
                registerTypeDeclarationSymbol(
                    *enumDecl, semanticNameForDecl(*enumDecl, enumDecl->name));
                break;
            }
            case DeclKind::TypeAlias: {
                auto *alias = static_cast<TypeAliasDecl *>(decl.get());
                const std::string semanticName = semanticNameForDecl(*alias, alias->name);
                registerTypeAliasPlaceholder(*alias, semanticName);
                pendingTypeAliases.emplace_back(alias, semanticName);
                break;
            }
            default:
                break;
        }
    }

    resolvePendingTypeAliases(pendingTypeAliases);
    registerNominalTypeRelationships(module.declarations);

    // First pass: register all top-level declarations
    for (auto &decl : module.declarations) {
        switch (decl->kind) {
            case DeclKind::Function: {
                auto *func = static_cast<FunctionDecl *>(decl.get());
                const std::string semanticName = semanticNameForDecl(*func, func->name);

                if (!func->genericParams.empty()) {
                    // Generic function: register for later instantiation
                    registerGenericFunction(semanticName, func);

                    // Create a placeholder type with type parameters as param types
                    // The actual function type will be created when instantiated
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : func->genericParams) {
                        paramTypes.push_back(types::typeParam(param));
                    }
                    auto placeholderType = types::function(paramTypes, types::unknown());

                    Symbol sym;
                    sym.kind = Symbol::Kind::Function;
                    sym.name = semanticName;
                    sym.type = placeholderType;
                    sym.decl = func;
                    sym.isExported = func->isExported;
                    if (!currentScope_->lookupLocal(semanticName))
                        defineSymbol(semanticName, sym);
                } else {
                    auto funcType = functionTypeForDecl(*func);

                    Symbol sym;
                    sym.kind = Symbol::Kind::Function;
                    sym.name = semanticName;
                    sym.type = funcType;
                    sym.decl = func;
                    sym.isExported = func->isExported;
                    Symbol *existing = currentScope_->lookupLocal(semanticName);
                    if (!existing) {
                        defineSymbol(semanticName, sym);
                    } else if (existing->kind != Symbol::Kind::Function) {
                        reportDuplicateDefinition(semanticName, func->loc);
                    }
                    registerFunctionOverload(semanticName, func, funcType, func->loc);
                }
                break;
            }
            case DeclKind::Struct: {
                auto *value = static_cast<StructDecl *>(decl.get());
                registerTypeDeclarationSymbol(*value, semanticNameForDecl(*value, value->name));
                break;
            }
            case DeclKind::Class: {
                auto *cls = static_cast<ClassDecl *>(decl.get());
                registerTypeDeclarationSymbol(*cls, semanticNameForDecl(*cls, cls->name));
                break;
            }
            case DeclKind::Interface: {
                auto *iface = static_cast<InterfaceDecl *>(decl.get());
                registerTypeDeclarationSymbol(*iface, semanticNameForDecl(*iface, iface->name));
                break;
            }
            case DeclKind::Enum: {
                auto *enumDecl = static_cast<EnumDecl *>(decl.get());
                registerTypeDeclarationSymbol(
                    *enumDecl, semanticNameForDecl(*enumDecl, enumDecl->name));
                break;
            }
            case DeclKind::GlobalVar: {
                auto *gvar = static_cast<GlobalVarDecl *>(decl.get());
                const std::string semanticName = semanticNameForDecl(*gvar, gvar->name);
                // Determine the variable type
                TypeRef varType;
                if (gvar->type) {
                    varType = resolveTypeNode(gvar->type.get());
                } else if (gvar->initializer) {
                    // Type inference from initializer - defer to second pass
                    varType = types::unknown();
                } else {
                    varType = types::unknown();
                }

                Symbol sym;
                sym.kind = Symbol::Kind::Variable;
                sym.name = semanticName;
                sym.type = varType;
                sym.isFinal = gvar->isFinal;
                sym.decl = gvar;
                sym.isExported = gvar->isExported;
                if (defineSymbol(semanticName, sym)) {
                    // Global variables are always considered initialized
                    // (either explicitly or default-initialized)
                    markInitialized(semanticName);
                }
                break;
            }
            case DeclKind::Namespace: {
                auto *ns = static_cast<NamespaceDecl *>(decl.get());
                Symbol sym;
                sym.kind = Symbol::Kind::Module;
                sym.name = ns->name;
                sym.type = types::module(ns->name);
                sym.decl = ns;
                sym.isFinal = true;
                defineSymbol(ns->name, sym);
                break;
            }
            case DeclKind::TypeAlias: {
                break;
            }
            default:
                break;
        }
    }

    // Process namespace declarations (they handle their own multi-pass analysis)
    for (auto &decl : module.declarations) {
        if (decl->kind == DeclKind::Namespace) {
            analyzeNamespaceDecl(*static_cast<NamespaceDecl *>(decl.get()));
        }
    }

    // Pre-pass: eagerly resolve types of final constants from literal initializers
    // This allows forward references to final constants in class/function bodies
    registerFinalConstantTypes(module.declarations);

    // File-module export maps snapshot top-level symbol types. Build them only
    // after final constants have had their literal types registered.
    buildBoundFileExports(module.binds, module.declarations);

    // Second pass: register all method/field signatures (before analyzing bodies)
    // This ensures cross-module method calls can be resolved regardless of declaration order
    registerMemberSignatures(module.declarations);

    // Third pass: analyze declarations (bodies)
    analyzeDeclarationBodies(module.declarations);

    return !hasError_;
}

/// @brief Get the resolved semantic type of an expression.
/// @details Returns the cached type from exprTypes_, applying type parameter
///          substitution if currently in a generic context.
/// @param expr The expression to query.
/// @return The resolved type, or unknown() if the expression has not been analyzed.
TypeRef Sema::typeOf(const Expr *expr) const {
    auto it = exprTypes_.find(expr);
    if (it == exprTypes_.end())
        return types::unknown();
    // Apply type parameter substitution if in generic context
    return substituteTypeParams(it->second);
}

/// @brief Resolve a type AST node to a semantic type reference.
/// @param node The type node to resolve.
/// @return The resolved semantic type.
TypeRef Sema::resolveType(const TypeNode *node) const {
    return const_cast<Sema *>(this)->resolveTypeNode(node);
}

//=============================================================================
// Scope Management
//=============================================================================

/// @brief Push a new child scope onto the scope stack.
void Sema::pushScope(SourceLoc startLoc) {
    const uint32_t scopeId = nextScopeId_++;
    const size_t depth = currentScope_ ? currentScope_->depth() + 1 : 0;
    const uint32_t parentId = currentScope_ ? currentScope_->id() : 0;
    scopes_.push_back(std::make_unique<Scope>(currentScope_, scopeId, depth));
    currentScope_ = scopes_.back().get();
    narrowedTypes_.push_back({});
    scopeSnapshots_[scopeId] = ScopeSnapshot{scopeId, parentId, depth, startLoc, {}};
}

/// @brief Pop the current scope, restoring its parent as the active scope.
/// @pre There must be more than the global scope remaining.
/// @details Checks for unused variables (W001) in the scope before popping.
void Sema::popScope(SourceLoc endLoc) {
    assert(scopes_.size() > 1 && "cannot pop global scope");

    // W001: Check for unused variables/parameters in the scope being popped
    checkUnusedVariables(*currentScope_);

    auto snapIt = scopeSnapshots_.find(currentScope_->id());
    if (snapIt != scopeSnapshots_.end() && endLoc.isValid())
        snapIt->second.endLoc = endLoc;

    if (!narrowedTypes_.empty())
        narrowedTypes_.pop_back();
    currentScope_ = currentScope_->parent();
    scopes_.pop_back();
    assert(currentScope_ == scopes_.back().get() && "scope stack corrupted");
}

/// @brief Define a symbol in the current scope.
/// @param name The symbol name to register.
/// @param symbol The symbol metadata to associate with the name.
/// @param locOverride Optional source location for symbols without decl (locals, params).
bool Sema::defineSymbol(const std::string &name, Symbol symbol, SourceLoc locOverride) {
    SourceLoc defLoc =
        locOverride.isValid() ? locOverride : (symbol.decl ? symbol.decl->loc : SourceLoc{});
    if (Symbol *existing = currentScope_->lookupLocal(name)) {
        if (existing->decl == nullptr && symbol.decl == nullptr && existing->isExtern &&
            symbol.isExtern) {
            symbol.loc = defLoc;
            currentScope_->define(name, std::move(symbol));
            return true;
        }
        if (existing->decl == nullptr && symbol.decl == nullptr &&
            existing->kind == Symbol::Kind::Variable && symbol.kind == Symbol::Kind::Variable) {
            symbol.loc = defLoc;
            currentScope_->define(name, std::move(symbol));
            return true;
        }
    }
    if (!reportDuplicateDefinition(name, defLoc))
        return false;

    symbol.loc = defLoc;
    currentScope_->define(name, std::move(symbol));

    // Capture a snapshot for position-based hover queries.
    Symbol *defined = currentScope_->lookupLocal(name);
    if (defined) {
        ScopedSymbol ss;
        ss.symbol = *defined;
        ss.loc = defLoc;
        ss.ownerType = currentSelfType_ ? currentSelfType_->name : "";
        ss.scopeId = currentScope_->id();
        scopedSymbols_.push_back(std::move(ss));
    }
    return true;
}

/// @brief Find the most relevant symbol at a given cursor position.
const ScopedSymbol *Sema::findSymbolAtPosition(const std::string &name,
                                               uint32_t fileId,
                                               uint32_t line,
                                               uint32_t col) const {
    const ScopedSymbol *best = nullptr;
    const SourceLoc cursor{fileId, line, col};
    for (const auto &ss : scopedSymbols_) {
        if (ss.symbol.name != name)
            continue;
        if (!ss.loc.isValid())
            continue;
        if (fileId != 0 && ss.loc.file_id != fileId)
            continue;
        // Symbol must be defined at or before the cursor position.
        if (compareLoc(ss.loc, cursor) > 0)
            continue;

        auto scopeIt = scopeSnapshots_.find(ss.scopeId);
        if (scopeIt != scopeSnapshots_.end()) {
            const auto &scope = scopeIt->second;
            if (fileId != 0 && scope.startLoc.hasFile() && scope.startLoc.file_id != fileId)
                continue;
            if (scope.startLoc.isValid() && compareLoc(scope.startLoc, cursor) > 0)
                continue;
            if (scope.endLoc.isValid() && cursor.file_id == scope.endLoc.file_id &&
                cursor.line > scope.endLoc.line)
                continue;
        }

        if (!best) {
            best = &ss;
            continue;
        }

        const auto bestScopeIt = scopeSnapshots_.find(best->scopeId);
        const size_t bestDepth =
            bestScopeIt != scopeSnapshots_.end() ? bestScopeIt->second.depth : 0;
        const size_t thisDepth = scopeIt != scopeSnapshots_.end() ? scopeIt->second.depth : 0;
        if (thisDepth > bestDepth ||
            (thisDepth == bestDepth && compareLoc(ss.loc, best->loc) > 0)) {
            best = &ss;
        }
    }
    return best;
}

/// @brief Look up a symbol by name in the current scope chain.
/// @param name The symbol name to search for.
/// @return Pointer to the symbol if found, nullptr otherwise.
Symbol *Sema::lookupSymbol(const std::string &name) {
    return currentScope_->lookup(name);
}

TypeRef Sema::declaredOptionalSurfaceType(Expr *expr, TypeRef analyzedType) {
    if (analyzedType && analyzedType->kind == TypeKindSem::Optional)
        return analyzedType;

    auto *ident = dynamic_cast<IdentExpr *>(expr);
    if (!ident)
        return analyzedType;

    Symbol *sym = lookupSymbol(ident->name);
    if (!sym)
        return analyzedType;

    if (sym->kind != Symbol::Kind::Variable && sym->kind != Symbol::Kind::Parameter)
        return analyzedType;

    if (sym->type && sym->type->kind == TypeKindSem::Optional)
        return sym->type;

    return analyzedType;
}

bool Sema::canAccessSymbol(const Symbol &sym,
                           SourceLoc useLoc,
                           const std::string &name,
                           bool viaQualifiedModule) const {
    (void)name;
    (void)viaQualifiedModule;

    if (sym.isExtern || !useLoc.isValid() || !sym.loc.isValid())
        return true;

    if (useLoc.file_id == 0 || sym.loc.file_id == 0 || useLoc.file_id == sym.loc.file_id)
        return true;

    if (!sym.decl)
        return true;

    switch (sym.decl->kind) {
        case DeclKind::Function:
        case DeclKind::Struct:
        case DeclKind::Class:
        case DeclKind::Interface:
        case DeclKind::GlobalVar:
        case DeclKind::Namespace:
        case DeclKind::Enum:
        case DeclKind::TypeAlias:
            break;
        default:
            return true;
    }

    if (!sym.isExported)
        return false;
    return true;
}

void Sema::reportInaccessibleSymbol(SourceLoc useLoc,
                                    const std::string &name,
                                    const Symbol &sym,
                                    bool viaQualifiedModule) {
    (void)viaQualifiedModule;
    if (!sym.isExported) {
        error(useLoc,
              "Cannot access private top-level declaration '" + name + "' from another file");
        return;
    }
}

Symbol *Sema::lookupAccessibleSymbol(const std::string &name,
                                     SourceLoc useLoc,
                                     bool viaQualifiedModule) {
    Symbol *sym = lookupSymbol(name);
    if (!sym)
        return nullptr;
    if (canAccessSymbol(*sym, useLoc, name, viaQualifiedModule))
        return sym;
    reportInaccessibleSymbol(useLoc, name, *sym, viaQualifiedModule);
    return nullptr;
}

/// @brief Look up the type of a variable, respecting flow-sensitive type narrowing.
/// @details Checks narrowed types first (from null-check analysis), then falls back
///          to the declared type in scope.
/// @param name The variable name to look up.
/// @return The narrowed or declared type, or nullptr if not found.
TypeRef Sema::lookupVarType(const std::string &name) {
    if (TypeRef narrowed = lookupNarrowedType(name))
        return narrowed;

    // Fall back to declared type
    Symbol *sym = currentScope_->lookup(name);
    if (sym && (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Parameter ||
                sym->kind == Symbol::Kind::Field)) {
        return sym->type;
    }
    return nullptr;
}

TypeRef Sema::lookupNarrowedType(const std::string &key) const {
    if (key.empty())
        return nullptr;

    for (auto it = narrowedTypes_.rbegin(); it != narrowedTypes_.rend(); ++it) {
        auto found = it->find(key);
        if (found != it->end())
            return found->second;
    }
    return nullptr;
}

//=============================================================================
// Type Narrowing (Flow-Sensitive Type Analysis)
//=============================================================================

/// @brief Push a new type narrowing scope for flow-sensitive analysis.
void Sema::pushNarrowingScope() {
    narrowedTypes_.push_back({});
}

/// @brief Pop the current type narrowing scope.
void Sema::popNarrowingScope() {
    if (!narrowedTypes_.empty()) {
        narrowedTypes_.pop_back();
    }
}

/// @brief Narrow the type of a variable in the current narrowing scope.
/// @param name The variable whose type is being narrowed.
/// @param narrowedType The narrowed type to record.
void Sema::narrowType(const std::string &name, TypeRef narrowedType) {
    if (!narrowedTypes_.empty()) {
        narrowedTypes_.back()[name] = narrowedType;
    }
}

/// @brief Mark a variable as definitely initialized.
void Sema::markInitialized(const std::string &name) {
    initializedVars_.insert(name);
}

/// @brief Check if a variable has been definitely initialized.
bool Sema::isInitialized(const std::string &name) const {
    return initializedVars_.count(name) > 0;
}

/// @brief Save the current initialization state for branching analysis.
std::unordered_set<std::string> Sema::saveInitState() const {
    return initializedVars_;
}

/// @brief Intersect two branch initialization states.
/// Only variables initialized in BOTH branches remain initialized.
void Sema::intersectInitState(const std::unordered_set<std::string> &branchA,
                              const std::unordered_set<std::string> &branchB) {
    std::unordered_set<std::string> result;
    for (const auto &name : branchA) {
        if (branchB.count(name) > 0)
            result.insert(name);
    }
    initializedVars_ = std::move(result);
}

/// @brief Try to extract a null-check pattern from a condition expression.
/// @details Recognizes patterns: x != null, x == null, null != x, null == x.
/// @param[in] cond The condition expression to analyze.
/// @param[out] varName The variable name being null-checked.
/// @param[out] isNotNull True if the pattern is != null, false if == null.
/// @return True if a null-check pattern was recognized.
bool Sema::tryExtractNullCheck(Expr *cond,
                               std::string &varName,
                               bool &isNotNull,
                               TypeRef *checkedType) {
    // Pattern: x != null or x == null
    if (cond->kind != ExprKind::Binary)
        return false;

    auto *binary = static_cast<BinaryExpr *>(cond);
    if (binary->op != BinaryOp::Ne && binary->op != BinaryOp::Eq)
        return false;

    isNotNull = (binary->op == BinaryOp::Ne);

    auto captureOperand = [&](Expr *operand) {
        varName = narrowingKeyForExpr(operand);
        if (varName.empty())
            return false;
        if (checkedType) {
            TypeRef ty = typeOf(operand);
            *checkedType = declaredOptionalSurfaceType(operand, ty);
        }
        return true;
    };

    // Check for "x != null" or "obj.field != null" pattern
    if (binary->right->kind == ExprKind::NullLiteral) {
        return captureOperand(binary->left.get());
    }

    // Check for "null != x" or "null != obj.field" pattern
    if (binary->left->kind == ExprKind::NullLiteral) {
        return captureOperand(binary->right.get());
    }

    return false;
}

std::string Sema::narrowingKeyForExpr(Expr *expr) const {
    if (!expr)
        return {};

    if (auto *ident = dynamic_cast<IdentExpr *>(expr))
        return ident->name;

    if (dynamic_cast<SelfExpr *>(expr))
        return "self";

    if (auto *field = dynamic_cast<FieldExpr *>(expr)) {
        std::string base = narrowingKeyForExpr(field->base.get());
        if (base.empty())
            return {};
        return base + "." + field->field;
    }

    if (auto *index = dynamic_cast<IndexExpr *>(expr)) {
        std::string base = narrowingKeyForExpr(index->base.get());
        if (base.empty())
            return {};
        if (auto *intLit = dynamic_cast<IntLiteralExpr *>(index->index.get()))
            return base + "[" + std::to_string(intLit->value) + "]";
        if (auto *strLit = dynamic_cast<StringLiteralExpr *>(index->index.get()))
            return base + "[\"" + strLit->value + "\"]";
        return {};
    }

    return {};
}

//=============================================================================
// Error Reporting
//=============================================================================

/// @brief Report a semantic warning at a source location (legacy).
void Sema::warning(SourceLoc loc, const std::string &message) {
    il::support::Diagnostic diag{il::support::Severity::Warning, message, loc, "V3001"};
    diag.stage = "sema";
    diag_.report(std::move(diag));
}

/// @brief Report a coded warning with policy and suppression checks.
void Sema::warn(WarningCode code, SourceLoc loc, const std::string &message) {
    // Check policy: is this warning enabled?
    if (warningPolicy_) {
        if (!warningPolicy_->isEnabled(code))
            return;
    } else {
        // No policy set — use default conservative set
        if (WarningPolicy::defaultEnabled().count(code) == 0)
            return;
    }

    // Check inline suppression
    if (suppressions_.isSuppressed(code, loc))
        return;

    auto isSafetyCritical = [](WarningCode warning) {
        switch (warning) {
            case WarningCode::W008_MissingReturn:
            case WarningCode::W010_DivisionByZero:
            case WarningCode::W015_UninitializedVariable:
            case WarningCode::W016_OptionalWithoutCheck:
            case WarningCode::W019_NonExhaustiveMatch:
                return true;
            default:
                return false;
        }
    };

    // Determine severity: Warning or Error (-Werror or strict safety diagnostics).
    auto sev =
        (warningPolicy_ && (warningPolicy_->warningsAsErrors ||
                            (warningPolicy_->strictSafetyWarnings && isSafetyCritical(code))))
            ? il::support::Severity::Error
            : il::support::Severity::Warning;

    if (sev == il::support::Severity::Error)
        hasError_ = true;

    il::support::Diagnostic diag{sev, message, loc, warningCodeStr(code)};
    diag.stage = "sema";
    diag_.report(std::move(diag));
}

/// @brief Check for unused variables in a scope and emit W001 warnings.
void Sema::checkUnusedVariables(const Scope &scope) {
    for (const auto &[name, sym] : scope.getSymbols()) {
        // Only check variables and parameters
        if (sym.kind != Symbol::Kind::Variable && sym.kind != Symbol::Kind::Parameter)
            continue;

        // Skip the discard name "_"
        if (name == "_")
            continue;

        // Instance methods permit implicit field/member access without writing
        // `self.`, so warning on the synthetic receiver parameter is noise.
        if (sym.kind == Symbol::Kind::Parameter && name == "self")
            continue;

        // Skip extern/runtime symbols
        if (sym.isExtern)
            continue;

        if (!sym.used) {
            std::string what = (sym.kind == Symbol::Kind::Parameter) ? "Parameter" : "Variable";
            SourceLoc loc = sym.loc.isValid() ? sym.loc : (sym.decl ? sym.decl->loc : SourceLoc{});
            warn(WarningCode::W001_UnusedVariable,
                 loc,
                 what + " '" + name + "' is declared but never used");
        }
    }
}

/// @brief Report a semantic error at a source location.
void Sema::error(SourceLoc loc, const std::string &message) {
    errorWithCode(loc, classifySemanticError(message), message);
}

void Sema::errorWithCode(SourceLoc loc,
                         std::string code,
                         std::string message,
                         il::support::SourceRange range,
                         std::vector<il::support::DiagnosticNote> notes,
                         std::string help) {
    hasError_ = true;
    if (!range.isValid() && loc.isValid()) {
        range = il::support::SourceRange{
            loc,
            il::support::SourceLoc{loc.file_id, loc.line, loc.column + 1},
        };
    }
    il::support::Diagnostic diag{
        il::support::Severity::Error, std::move(message), loc, std::move(code)};
    diag.range = range;
    diag.notes = std::move(notes);
    diag.stage = "sema";
    diag.help = std::move(help);
    diag_.report(std::move(diag));
}

std::optional<std::string> Sema::suggestSymbolName(const std::string &name) const {
    std::optional<std::string> best;
    size_t bestDistance = std::numeric_limits<size_t>::max();

    auto consider = [&](const std::string &candidate) {
        if (candidate.empty() || candidate == name)
            return;
        const size_t distance = editDistance(name, candidate);
        const size_t limit = name.size() <= 4 ? 1 : 2;
        if (distance <= limit && distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    };

    for (const Scope *scope = currentScope_; scope != nullptr; scope = scope->parent()) {
        for (const auto &[candidate, _] : scope->getSymbols())
            consider(candidate);
    }
    for (const auto &[candidate, _] : importedSymbols_)
        consider(candidate);
    for (const auto &[candidate, _] : typeRegistry_)
        consider(candidate);
    return best;
}

bool Sema::reportDuplicateDefinition(const std::string &name, SourceLoc loc) {
    if (!currentScope_)
        return true;

    Symbol *existing = currentScope_->lookupLocal(name);
    if (!existing)
        return true;

    SourceLoc existingLoc = existing->loc.isValid()
                                ? existing->loc
                                : (existing->decl ? existing->decl->loc : SourceLoc{});
    if (!existingLoc.isValid()) {
        for (auto it = scopedSymbols_.rbegin(); it != scopedSymbols_.rend(); ++it) {
            if (it->scopeId == currentScope_->id() && it->symbol.name == name) {
                existingLoc = it->loc;
                break;
            }
        }
    }

    std::string message = "Duplicate definition of '" + name + "'";
    std::vector<il::support::DiagnosticNote> notes;
    if (existingLoc.isValid()) {
        message += " (previous definition at line " + std::to_string(existingLoc.line) +
                   ", column " + std::to_string(existingLoc.column) + ")";
        notes.push_back({existingLoc, "previous definition of '" + name + "' is here"});
    }
    errorWithCode(loc,
                  "V-ZIA-DUPLICATE",
                  std::move(message),
                  {},
                  std::move(notes),
                  "Rename one declaration or move it to a different scope.");
    return false;
}

SourceLoc Sema::scopeEndForStmt(const Stmt *stmt) {
    if (!stmt)
        return {};

    switch (stmt->kind) {
        case StmtKind::Block: {
            auto *block = static_cast<const BlockStmt *>(stmt);
            if (block->statements.empty())
                return stmt->loc;
            return scopeEndForStmt(block->statements.back().get());
        }
        case StmtKind::If: {
            auto *ifStmt = static_cast<const IfStmt *>(stmt);
            SourceLoc end = scopeEndForStmt(ifStmt->thenBranch.get());
            if (ifStmt->elseBranch) {
                SourceLoc elseEnd = scopeEndForStmt(ifStmt->elseBranch.get());
                if (!end.isValid() || (elseEnd.isValid() && compareLoc(elseEnd, end) > 0))
                    end = elseEnd;
            }
            return end.isValid() ? end : stmt->loc;
        }
        case StmtKind::While:
            return scopeEndForStmt(static_cast<const WhileStmt *>(stmt)->body.get());
        case StmtKind::For:
            return scopeEndForStmt(static_cast<const ForStmt *>(stmt)->body.get());
        case StmtKind::ForIn:
            return scopeEndForStmt(static_cast<const ForInStmt *>(stmt)->body.get());
        case StmtKind::Defer:
            return scopeEndForStmt(static_cast<const DeferStmt *>(stmt)->action.get());
        case StmtKind::Try: {
            auto *tryStmt = static_cast<const TryStmt *>(stmt);
            SourceLoc end = scopeEndForStmt(tryStmt->tryBody.get());
            for (const auto &catchClause : tryStmt->catches) {
                SourceLoc catchEnd = scopeEndForStmt(catchClause.body.get());
                if (!end.isValid() || (catchEnd.isValid() && compareLoc(catchEnd, end) > 0))
                    end = catchEnd;
            }
            if (tryStmt->finallyBody) {
                SourceLoc finallyEnd = scopeEndForStmt(tryStmt->finallyBody.get());
                if (!end.isValid() || (finallyEnd.isValid() && compareLoc(finallyEnd, end) > 0))
                    end = finallyEnd;
            }
            return end.isValid() ? end : stmt->loc;
        }
        default:
            return stmt->loc;
    }
}

/// @brief Report an "undefined identifier" error for the given name.
void Sema::errorUndefined(SourceLoc loc, const std::string &name) {
    std::string message = "Undefined identifier: " + name;
    std::vector<il::support::DiagnosticNote> notes;
    std::vector<il::support::DiagnosticFixIt> fixits;
    il::support::SourceRange range{};
    if (loc.isValid()) {
        range = il::support::SourceRange{
            loc,
            il::support::SourceLoc{loc.file_id,
                                   loc.line,
                                   loc.column +
                                       static_cast<uint32_t>(std::max<size_t>(1, name.size()))},
        };
    }
    if (auto suggestion = suggestSymbolName(name)) {
        message += "; did you mean '" + *suggestion + "'?";
        notes.push_back({loc, "candidate symbol '" + *suggestion + "' is visible here"});
        fixits.push_back({range, *suggestion, "replace with '" + *suggestion + "'"});
    }

    hasError_ = true;
    il::support::Diagnostic diag{
        il::support::Severity::Error,
        std::move(message),
        loc,
        "V-ZIA-UNDEFINED",
    };
    diag.range = range;
    diag.notes = std::move(notes);
    diag.stage = "sema";
    diag.help = "Declare the symbol, import it, or correct the spelling.";
    diag.fixits = std::move(fixits);
    diag_.report(std::move(diag));
}

/// @brief Report a type mismatch error showing expected vs actual types.
void Sema::errorTypeMismatch(SourceLoc loc, TypeRef expected, TypeRef actual) {
    std::string expectedStr = expected ? expected->toDisplayString() : "unknown";
    std::string actualStr = actual ? actual->toDisplayString() : "unknown";
    error(loc, "Type mismatch: expected " + expectedStr + ", got " + actualStr);
}

//=============================================================================
// Built-in Functions
//=============================================================================

/// @brief Register built-in functions and runtime library functions.
/// @details Registers print, println, input, toString as built-in symbols,
///          then loads all Viper.* runtime functions from runtime.def.
void Sema::registerBuiltins() {
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

    // Register all Viper.* runtime functions from runtime.def
    // Generated from src/il/runtime/runtime.def (1002 functions)
    initRuntimeFunctions();
}

//===----------------------------------------------------------------------===//
// Namespace Support
//===----------------------------------------------------------------------===//

void Sema::prepareModuleScopedTypeNames(const ModuleDecl &module) {
    semanticDeclNames_.clear();
    fileScopedDeclNames_.clear();

    if (module.loc.file_id != 0)
        fileModuleNames_[module.loc.file_id] = module.name;

    std::unordered_map<std::string, std::unordered_set<uint32_t>> filesByName;
    for (const auto &decl : module.declarations) {
        if (!decl || !isTopLevelModuleScopedDeclKind(decl->kind))
            continue;
        filesByName[topLevelModuleScopedDeclName(*decl)].insert(decl->loc.file_id);
    }

    for (const auto &decl : module.declarations) {
        if (!decl || !isTopLevelModuleScopedDeclKind(decl->kind))
            continue;

        const std::string shortName = topLevelModuleScopedDeclName(*decl);
        std::string semanticName = shortName;
        auto collisionIt = filesByName.find(shortName);
        if (collisionIt != filesByName.end() && collisionIt->second.size() > 1) {
            std::string moduleName = moduleNameForFile(decl->loc.file_id);
            if (!moduleName.empty())
                semanticName = moduleName + "." + shortName;
        }

        semanticDeclNames_[decl.get()] = semanticName;
        fileScopedDeclNames_[decl->loc.file_id][shortName] = semanticName;
    }
}

std::string Sema::moduleNameForFile(uint32_t fileId) const {
    if (fileId == 0)
        return "";
    auto it = fileModuleNames_.find(fileId);
    if (it != fileModuleNames_.end())
        return it->second;
    if (currentModule_ && currentModule_->loc.file_id == fileId)
        return currentModule_->name;
    return "";
}

std::string Sema::semanticNameForDecl(const Decl &decl, const std::string &name) const {
    auto it = semanticDeclNames_.find(&decl);
    if (it != semanticDeclNames_.end())
        return it->second;
    return qualifyName(name);
}

std::string Sema::fileScopedDeclName(uint32_t fileId, const std::string &name) const {
    auto fileIt = fileScopedDeclNames_.find(fileId);
    if (fileIt == fileScopedDeclNames_.end())
        return name;
    auto nameIt = fileIt->second.find(name);
    return nameIt != fileIt->second.end() ? nameIt->second : name;
}

std::string Sema::fileScopedTypeName(uint32_t fileId, const std::string &name) const {
    return fileScopedDeclName(fileId, name);
}

/// @brief Qualify a name with the current namespace prefix.
/// @param name The unqualified name.
/// @return The qualified name (prefix.name), or the original name if no namespace is active.
std::string Sema::qualifyName(const std::string &name) const {
    if (namespacePrefix_.empty())
        return name;
    return namespacePrefix_ + "." + name;
}

/// @brief Pass 2: Register member signatures (fields, methods) for type declarations.
/// @param declarations The declaration list to process.
void Sema::registerMemberSignatures(std::vector<DeclPtr> &declarations) {
    for (auto &decl : declarations) {
        switch (decl->kind) {
            case DeclKind::Struct:
                registerStructMembers(*static_cast<StructDecl *>(decl.get()));
                break;
            case DeclKind::Class:
                registerClassMembers(*static_cast<ClassDecl *>(decl.get()));
                break;
            case DeclKind::Interface:
                registerInterfaceMembers(*static_cast<InterfaceDecl *>(decl.get()));
                break;
            case DeclKind::Enum:
                analyzeEnumDecl(*static_cast<EnumDecl *>(decl.get()));
                break;
            default:
                break;
        }
    }
}

/// @brief Pre-pass: Eagerly resolve types of final constants from literal initializers.
/// @details Scans declarations for final globals with literal initializers and updates
///          the registered symbol type from unknown() to the concrete literal type.
///          This allows forward references to final constants in class/function bodies.
/// @param declarations The declaration list to process.
void Sema::registerFinalConstantTypes(std::vector<DeclPtr> &declarations) {
    for (auto &decl : declarations) {
        if (decl->kind == DeclKind::GlobalVar) {
            auto *gvar = static_cast<GlobalVarDecl *>(decl.get());
            if (!gvar->isFinal || !gvar->initializer)
                continue;

            // Look up the symbol — it was registered in Pass 1 with unknown type
            std::string name = semanticNameForDecl(*gvar, gvar->name);
            Symbol *sym = lookupSymbol(name);
            if (!sym || !sym->type->isUnknown())
                continue;

            // Infer type directly from literal initializer
            Expr *init = gvar->initializer.get();
            TypeRef inferredType = nullptr;
            if (dynamic_cast<IntLiteralExpr *>(init))
                inferredType = types::integer();
            else if (dynamic_cast<NumberLiteralExpr *>(init))
                inferredType = types::number();
            else if (dynamic_cast<BoolLiteralExpr *>(init))
                inferredType = types::boolean();
            else if (dynamic_cast<StringLiteralExpr *>(init))
                inferredType = types::string();
            else if (auto *unary = dynamic_cast<UnaryExpr *>(init)) {
                // Handle negated literals: final X = -42
                if (unary->op == UnaryOp::Neg) {
                    if (dynamic_cast<IntLiteralExpr *>(unary->operand.get()))
                        inferredType = types::integer();
                    else if (dynamic_cast<NumberLiteralExpr *>(unary->operand.get()))
                        inferredType = types::number();
                }
            }

            if (inferredType)
                sym->type = inferredType;
        } else if (decl->kind == DeclKind::Namespace) {
            // Recurse into namespace declarations
            auto *ns = static_cast<NamespaceDecl *>(decl.get());
            std::string savedPrefix = namespacePrefix_;
            if (namespacePrefix_.empty())
                namespacePrefix_ = ns->name;
            else
                namespacePrefix_ = namespacePrefix_ + "." + ns->name;

            registerFinalConstantTypes(ns->declarations);

            namespacePrefix_ = savedPrefix;
        }
    }
}

/// @brief Pass 3: Analyze declaration bodies (functions, types, globals).
/// @param declarations The declaration list to process.
void Sema::analyzeDeclarationBodies(std::vector<DeclPtr> &declarations) {
    for (auto &decl : declarations) {
        switch (decl->kind) {
            case DeclKind::Function:
                analyzeFunctionDecl(*static_cast<FunctionDecl *>(decl.get()));
                break;
            case DeclKind::Struct:
                analyzeStructDecl(*static_cast<StructDecl *>(decl.get()));
                break;
            case DeclKind::Class:
                analyzeClassDecl(*static_cast<ClassDecl *>(decl.get()));
                break;
            case DeclKind::Interface:
                analyzeInterfaceDecl(*static_cast<InterfaceDecl *>(decl.get()));
                break;
            case DeclKind::GlobalVar:
                analyzeGlobalVarDecl(*static_cast<GlobalVarDecl *>(decl.get()));
                break;
            default:
                break;
        }
    }
}

/// @brief Analyze a namespace declaration with recursive multi-pass processing.
/// @details Saves the current namespace prefix, computes a new qualified prefix,
///          then runs the same three-pass strategy (register, member sigs, bodies)
///          on the namespace's nested declarations. Handles nested namespaces recursively.
/// @param decl The namespace declaration to analyze.
void Sema::analyzeNamespaceDecl(NamespaceDecl &decl) {
    // Save current namespace prefix
    std::string savedPrefix = namespacePrefix_;

    // Compute new prefix: append this namespace's name
    if (namespacePrefix_.empty())
        namespacePrefix_ = decl.name;
    else
        namespacePrefix_ = namespacePrefix_ + "." + decl.name;

    std::vector<std::pair<TypeAliasDecl *, std::string>> pendingTypeAliases;

    for (auto &innerDecl : decl.declarations) {
        switch (innerDecl->kind) {
            case DeclKind::Struct: {
                auto *value = static_cast<StructDecl *>(innerDecl.get());
                registerTypeDeclarationSymbol(*value, qualifyName(value->name));
                break;
            }
            case DeclKind::Class: {
                auto *cls = static_cast<ClassDecl *>(innerDecl.get());
                registerTypeDeclarationSymbol(*cls, qualifyName(cls->name));
                break;
            }
            case DeclKind::Interface: {
                auto *iface = static_cast<InterfaceDecl *>(innerDecl.get());
                registerTypeDeclarationSymbol(*iface, qualifyName(iface->name));
                break;
            }
            case DeclKind::Enum: {
                auto *enumDecl = static_cast<EnumDecl *>(innerDecl.get());
                registerTypeDeclarationSymbol(*enumDecl, qualifyName(enumDecl->name));
                break;
            }
            case DeclKind::TypeAlias: {
                auto *alias = static_cast<TypeAliasDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(alias->name);
                registerTypeAliasPlaceholder(*alias, qualifiedName);
                pendingTypeAliases.emplace_back(alias, qualifiedName);
                break;
            }
            default:
                break;
        }
    }

    resolvePendingTypeAliases(pendingTypeAliases);
    registerNominalTypeRelationships(decl.declarations);

    // Process declarations inside this namespace
    // First pass: register declarations
    for (auto &innerDecl : decl.declarations) {
        switch (innerDecl->kind) {
            case DeclKind::Function: {
                auto *func = static_cast<FunctionDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(func->name);
                auto funcType = functionTypeForDecl(*func);

                Symbol sym;
                sym.kind = Symbol::Kind::Function;
                sym.name = qualifiedName;
                sym.type = funcType;
                sym.decl = func;
                sym.isExported = func->isExported;
                Symbol *existing = currentScope_->lookupLocal(qualifiedName);
                if (!existing) {
                    defineSymbol(qualifiedName, sym);
                } else if (existing->kind != Symbol::Kind::Function) {
                    reportDuplicateDefinition(qualifiedName, func->loc);
                }
                registerFunctionOverload(qualifiedName, func, funcType, func->loc);
                break;
            }
            case DeclKind::Struct: {
                auto *value = static_cast<StructDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(value->name);
                registerTypeDeclarationSymbol(*value, qualifiedName);
                break;
            }
            case DeclKind::Class: {
                auto *cls = static_cast<ClassDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(cls->name);
                registerTypeDeclarationSymbol(*cls, qualifiedName);
                break;
            }
            case DeclKind::Interface: {
                auto *iface = static_cast<InterfaceDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(iface->name);
                registerTypeDeclarationSymbol(*iface, qualifiedName);
                break;
            }
            case DeclKind::Enum: {
                auto *enumDecl = static_cast<EnumDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(enumDecl->name);
                registerTypeDeclarationSymbol(*enumDecl, qualifiedName);
                break;
            }
            case DeclKind::GlobalVar: {
                auto *gvar = static_cast<GlobalVarDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(gvar->name);

                TypeRef varType;
                if (gvar->type)
                    varType = resolveTypeNode(gvar->type.get());
                else
                    varType = types::unknown();

                Symbol sym;
                sym.kind = Symbol::Kind::Variable;
                sym.name = qualifiedName;
                sym.type = varType;
                sym.isFinal = gvar->isFinal;
                sym.decl = gvar;
                sym.isExported = gvar->isExported;
                defineSymbol(qualifiedName, sym);
                break;
            }
            case DeclKind::TypeAlias: {
                break;
            }
            case DeclKind::Namespace: {
                auto *ns = static_cast<NamespaceDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(ns->name);
                Symbol sym;
                sym.kind = Symbol::Kind::Module;
                sym.name = qualifiedName;
                sym.type = types::module(qualifiedName);
                sym.decl = ns;
                sym.isFinal = true;
                defineSymbol(qualifiedName, sym);
                analyzeNamespaceDecl(*ns);
                break;
            }
            default:
                break;
        }
    }

    // Pre-pass: resolve final constant types for forward references
    registerFinalConstantTypes(decl.declarations);

    // Second pass: register members for types
    registerMemberSignatures(decl.declarations);

    // Third pass: analyze bodies
    analyzeDeclarationBodies(decl.declarations);

    // Restore previous namespace prefix
    namespacePrefix_ = savedPrefix;
}

// initRuntimeFunctions() implementation moved to Sema_Runtime.cpp
} // namespace il::frontends::zia
