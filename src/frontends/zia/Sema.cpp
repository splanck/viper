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
#include <cassert>
#include <cctype>
#include <limits>
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

bool isFunctionPointerArg(const CallArg &arg) {
    if (!arg.value)
        return false;

    if (auto *unary = dynamic_cast<const UnaryExpr *>(arg.value.get())) {
        return unary->op == UnaryOp::AddressOf;
    }

    return arg.value->kind == ExprKind::Ident || arg.value->kind == ExprKind::Field;
}

bool allowsFunctionPointerParam(std::string_view name) {
    std::string lower;
    lower.reserve(name.size());
    for (char ch : name)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

    return lower == "fn" || lower == "func" || lower == "entry" || lower == "callback" ||
           lower == "handler" || lower.find("callback") != std::string::npos ||
           lower.find("handler") != std::string::npos || lower.find("entry") != std::string::npos;
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
        if (argType->kind == TypeKindSem::Unit)
            return 1;
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
    registerBuiltins();
}

TypeRef Sema::functionTypeForDecl(const FunctionDecl &decl) const {
    TypeRef returnType =
        decl.isAsync ? types::runtimeClass("Viper.Threads.Future")
                     : (decl.returnType ? resolveType(decl.returnType.get()) : types::voidType());
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
    TypeRef returnType = decl.returnType ? resolveType(decl.returnType.get()) : types::voidType();
    std::vector<TypeRef> paramTypes;
    paramTypes.reserve(decl.params.size());
    for (const auto &param : decl.params)
        paramTypes.push_back(param.type ? resolveType(param.type.get()) : types::unknown());
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
    if (name == "start") {
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

        CallParamSpec spec;
        spec.name = field->name;
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

        CallParamSpec spec;
        spec.name = field->name;
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
        if (cost >= 1000 && allowRuntimeObjectCoercion && paramType &&
            paramType->kind == TypeKindSem::Ptr && argType &&
            argType->kind == TypeKindSem::Function &&
            allowsFunctionPointerParam(params[i].name) &&
            isFunctionPointerArg(args[static_cast<size_t>(sourceIndex)])) {
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
            if (cost >= 1000 && allowRuntimeObjectCoercion && elemType &&
                elemType->kind == TypeKindSem::Ptr && argType &&
                argType->kind == TypeKindSem::Function &&
                allowsFunctionPointerParam(params.back().name) &&
                isFunctionPointerArg(args[static_cast<size_t>(sourceIndex)])) {
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

/// @brief Run multi-pass semantic analysis on a module.
/// @details Pass 1: Register all top-level declarations (types, functions, globals).
///          Pass 1b: Process namespace declarations (recursive multi-pass).
///          Pass 2: Register member signatures (fields, method types) for type declarations.
///          Pass 3: Analyze declaration bodies (function bodies, method bodies, initializers).
/// @param module The module AST to analyze.
/// @return True if analysis succeeded without errors, false otherwise.
bool Sema::analyze(ModuleDecl &module) {
    currentModule_ = &module;

    for (auto &bind : module.binds) {
        analyzeBind(bind);
    }

    // First pass: register all top-level declarations
    for (auto &decl : module.declarations) {
        switch (decl->kind) {
            case DeclKind::Function: {
                auto *func = static_cast<FunctionDecl *>(decl.get());

                if (!func->genericParams.empty()) {
                    // Generic function: register for later instantiation
                    registerGenericFunction(func->name, func);

                    // Create a placeholder type with type parameters as param types
                    // The actual function type will be created when instantiated
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : func->genericParams) {
                        paramTypes.push_back(types::typeParam(param));
                    }
                    auto placeholderType = types::function(paramTypes, types::unknown());

                    Symbol sym;
                    sym.kind = Symbol::Kind::Function;
                    sym.name = func->name;
                    sym.type = placeholderType;
                    sym.decl = func;
                    sym.isExported = func->isExported;
                    if (!currentScope_->lookupLocal(func->name))
                        defineSymbol(func->name, sym);
                } else {
                    auto funcType = functionTypeForDecl(*func);

                    Symbol sym;
                    sym.kind = Symbol::Kind::Function;
                    sym.name = func->name;
                    sym.type = funcType;
                    sym.decl = func;
                    sym.isExported = func->isExported;
                    Symbol *existing = currentScope_->lookupLocal(func->name);
                    if (!existing) {
                        defineSymbol(func->name, sym);
                    } else if (existing->kind != Symbol::Kind::Function) {
                        reportDuplicateDefinition(func->name, func->loc);
                    }
                    registerFunctionOverload(func->name, func, funcType, func->loc);
                }
                break;
            }
            case DeclKind::Struct: {
                auto *value = static_cast<StructDecl *>(decl.get());

                TypeRef valueType;
                if (!value->genericParams.empty()) {
                    // Generic type: register for later instantiation
                    registerGenericType(value->name, value);
                    // Create uninstantiated type placeholder with type parameters
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : value->genericParams) {
                        paramTypes.push_back(types::typeParam(param));
                    }
                    valueType =
                        std::make_shared<ViperType>(TypeKindSem::Struct, value->name, paramTypes);
                } else {
                    valueType = types::structType(value->name);
                }
                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = value->name;
                sym.type = valueType;
                sym.decl = value;
                sym.isExported = value->isExported;
                if (defineSymbol(value->name, sym)) {
                    structDecls_[value->name] = value;
                    typeRegistry_[value->name] = valueType;
                }
                break;
            }
            case DeclKind::Class: {
                auto *cls = static_cast<ClassDecl *>(decl.get());

                TypeRef classT;
                if (!cls->genericParams.empty()) {
                    // Generic type: register for later instantiation
                    registerGenericType(cls->name, cls);
                    // Create uninstantiated type placeholder with type parameters
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : cls->genericParams) {
                        paramTypes.push_back(types::typeParam(param));
                    }
                    classT = std::make_shared<ViperType>(TypeKindSem::Class, cls->name, paramTypes);
                } else {
                    classT = types::classType(cls->name);
                }
                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = cls->name;
                sym.type = classT;
                sym.decl = cls;
                sym.isExported = cls->isExported;
                if (defineSymbol(cls->name, sym)) {
                    classDecls_[cls->name] = cls;
                    typeRegistry_[cls->name] = classT;
                }
                break;
            }
            case DeclKind::Interface: {
                auto *iface = static_cast<InterfaceDecl *>(decl.get());
                auto ifaceType = types::interface(iface->name);

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = iface->name;
                sym.type = ifaceType;
                sym.decl = iface;
                sym.isExported = iface->isExported;
                if (defineSymbol(iface->name, sym)) {
                    interfaceDecls_[iface->name] = iface;
                    typeRegistry_[iface->name] = ifaceType;
                }
                break;
            }
            case DeclKind::Enum: {
                auto *enumDecl = static_cast<EnumDecl *>(decl.get());
                auto enumT = types::enumType(enumDecl->name);

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = enumDecl->name;
                sym.type = enumT;
                sym.decl = enumDecl;
                sym.isExported = enumDecl->isExported;
                if (defineSymbol(enumDecl->name, sym))
                    typeRegistry_[enumDecl->name] = enumT;
                break;
            }
            case DeclKind::GlobalVar: {
                auto *gvar = static_cast<GlobalVarDecl *>(decl.get());
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
                sym.name = gvar->name;
                sym.type = varType;
                sym.isFinal = gvar->isFinal;
                sym.decl = gvar;
                sym.isExported = gvar->isExported;
                if (defineSymbol(gvar->name, sym)) {
                    // Global variables are always considered initialized
                    // (either explicitly or default-initialized)
                    markInitialized(gvar->name);
                }
                break;
            }
            case DeclKind::Namespace: {
                // Namespaces are processed in a separate pass to handle their
                // nested declarations properly
                break;
            }
            case DeclKind::TypeAlias: {
                auto *alias = static_cast<TypeAliasDecl *>(decl.get());
                TypeRef resolved = resolveTypeNode(alias->targetType.get());
                if (resolved) {
                    Symbol sym;
                    sym.kind = Symbol::Kind::Type;
                    sym.name = alias->name;
                    sym.type = resolved;
                    sym.decl = alias;
                    sym.isExported = alias->isExported;
                    defineSymbol(alias->name, sym);
                    typeAliases_[alias->name] = resolved;
                } else {
                    error(alias->loc, "Cannot resolve type alias target for '" + alias->name + "'");
                }
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
        if (existing->decl == nullptr && symbol.decl == nullptr) {
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
    // Check narrowed types first (for flow-sensitive type analysis)
    for (auto it = narrowedTypes_.rbegin(); it != narrowedTypes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }

    // Fall back to declared type
    Symbol *sym = currentScope_->lookup(name);
    if (sym && (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Parameter)) {
        return sym->type;
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
bool Sema::tryExtractNullCheck(Expr *cond, std::string &varName, bool &isNotNull) {
    // Pattern: x != null or x == null
    if (cond->kind != ExprKind::Binary)
        return false;

    auto *binary = static_cast<BinaryExpr *>(cond);
    if (binary->op != BinaryOp::Ne && binary->op != BinaryOp::Eq)
        return false;

    isNotNull = (binary->op == BinaryOp::Ne);

    // Check for "x != null" pattern
    if (binary->left->kind == ExprKind::Ident && binary->right->kind == ExprKind::NullLiteral) {
        varName = static_cast<IdentExpr *>(binary->left.get())->name;
        return true;
    }

    // Check for "null != x" pattern
    if (binary->left->kind == ExprKind::NullLiteral && binary->right->kind == ExprKind::Ident) {
        varName = static_cast<IdentExpr *>(binary->right.get())->name;
        return true;
    }

    return false;
}

//=============================================================================
// Error Reporting
//=============================================================================

/// @brief Report a semantic warning at a source location (legacy).
void Sema::warning(SourceLoc loc, const std::string &message) {
    diag_.report({il::support::Severity::Warning, message, loc, "V3001"});
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

    // Determine severity: Warning or Error (if -Werror)
    auto sev = (warningPolicy_ && warningPolicy_->warningsAsErrors)
                   ? il::support::Severity::Error
                   : il::support::Severity::Warning;

    if (sev == il::support::Severity::Error)
        hasError_ = true;

    diag_.report({sev, message, loc, warningCodeStr(code)});
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
    hasError_ = true;
    diag_.report({il::support::Severity::Error, message, loc, "V3000"});
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
    if (existingLoc.isValid()) {
        message += " (previous definition at line " + std::to_string(existingLoc.line) +
                   ", column " + std::to_string(existingLoc.column) + ")";
    }
    error(loc, message);
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
        case StmtKind::Try: {
            auto *tryStmt = static_cast<const TryStmt *>(stmt);
            SourceLoc end = scopeEndForStmt(tryStmt->tryBody.get());
            if (tryStmt->catchBody) {
                SourceLoc catchEnd = scopeEndForStmt(tryStmt->catchBody.get());
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
    error(loc, "Undefined identifier: " + name);
}

/// @brief Report a type mismatch error showing expected vs actual types.
void Sema::errorTypeMismatch(SourceLoc loc, TypeRef expected, TypeRef actual) {
    error(loc, "Type mismatch: expected " + expected->toString() + ", got " + actual->toString());
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
            std::string name = qualifyName(gvar->name);
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
                auto valueType = types::structType(qualifiedName);

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = qualifiedName;
                sym.type = valueType;
                sym.decl = value;
                sym.isExported = value->isExported;
                if (defineSymbol(qualifiedName, sym)) {
                    structDecls_[qualifiedName] = value;
                    typeRegistry_[qualifiedName] = valueType;
                }
                break;
            }
            case DeclKind::Class: {
                auto *cls = static_cast<ClassDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(cls->name);
                auto classT = types::classType(qualifiedName);

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = qualifiedName;
                sym.type = classT;
                sym.decl = cls;
                sym.isExported = cls->isExported;
                if (defineSymbol(qualifiedName, sym)) {
                    classDecls_[qualifiedName] = cls;
                    typeRegistry_[qualifiedName] = classT;
                }
                break;
            }
            case DeclKind::Interface: {
                auto *iface = static_cast<InterfaceDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(iface->name);
                auto ifaceType = types::interface(qualifiedName);

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = qualifiedName;
                sym.type = ifaceType;
                sym.decl = iface;
                sym.isExported = iface->isExported;
                if (defineSymbol(qualifiedName, sym)) {
                    interfaceDecls_[qualifiedName] = iface;
                    typeRegistry_[qualifiedName] = ifaceType;
                }
                break;
            }
            case DeclKind::Enum: {
                auto *enumDecl = static_cast<EnumDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(enumDecl->name);
                auto enumType = types::enumType(qualifiedName);

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = qualifiedName;
                sym.type = enumType;
                sym.decl = enumDecl;
                sym.isExported = enumDecl->isExported;
                if (defineSymbol(qualifiedName, sym)) {
                    enumDecls_[qualifiedName] = enumDecl;
                    typeRegistry_[qualifiedName] = enumType;
                }
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
                auto *alias = static_cast<TypeAliasDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(alias->name);
                TypeRef resolved = resolveTypeNode(alias->targetType.get());

                if (resolved) {
                    Symbol sym;
                    sym.kind = Symbol::Kind::Type;
                    sym.name = qualifiedName;
                    sym.type = resolved;
                    sym.decl = alias;
                    sym.isExported = alias->isExported;
                    defineSymbol(qualifiedName, sym);
                    typeAliases_[qualifiedName] = resolved;
                } else {
                    error(alias->loc,
                          "Cannot resolve type alias target for '" + qualifiedName + "'");
                }
                break;
            }
            case DeclKind::Namespace: {
                // Nested namespace - recursively process
                analyzeNamespaceDecl(*static_cast<NamespaceDecl *>(innerDecl.get()));
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
