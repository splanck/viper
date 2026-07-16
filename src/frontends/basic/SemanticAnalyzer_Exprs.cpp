//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/SemanticAnalyzer_Exprs.cpp
// Purpose: Implement expression analysis for the BASIC semantic analyser,
//          including variable resolution, operator checking, and array access
//          validation.
// Key invariants: Expression analysis reports type mismatches and symbol
//                 resolution issues while visitor overrides defer to
//                 SemanticAnalyzer helpers.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; AST nodes owned
//                     externally.
// Links: docs/codemap.md, docs/basic-language.md#expressions
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/SemanticAnalyzer_Internal.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "frontends/basic/sem/OverloadResolution.hpp"
#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "frontends/basic/sem/TypeRegistry.hpp"
#include "il/runtime/RuntimeClassNames.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <algorithm>
#include <limits>

namespace il::frontends::basic::semantic_analyzer_detail {

/// @brief Check if an expression represents a runtime namespace chain (starting with "Viper").
/// @param expr The expression to check.
/// @return True if the expression is a qualified name chain starting with "Viper".
static bool isRuntimeNamespaceChain(const Expr &expr) {
    std::vector<std::string> parts;
    const Expr *cur = &expr;
    while (cur) {
        if (auto *var = as<const VarExpr>(*cur)) {
            parts.push_back(var->name);
            break;
        }
        if (auto *mem = as<const MemberAccessExpr>(*cur)) {
            parts.push_back(mem->member);
            cur = mem->base.get();
            continue;
        }
        return false;
    }

    if (parts.empty())
        return false;
    std::reverse(parts.begin(), parts.end());
    return string_utils::iequals(parts.front(), "Viper");
}

/// @brief Collect qualified name segments from an expression chain.
/// @param expr The expression to traverse (VarExpr or MemberAccessExpr chain).
/// @param out Output vector to store name segments in order.
/// @return True if successful, false if expression is not a valid name chain.
static bool collectQualifiedChain(const Expr &expr, std::vector<std::string> &out) {
    const Expr *cur = &expr;
    while (cur) {
        if (auto *var = as<const VarExpr>(*cur)) {
            out.push_back(var->name);
            break;
        }
        if (auto *mem = as<const MemberAccessExpr>(*cur)) {
            out.push_back(mem->member);
            cur = mem->base.get();
            continue;
        }
        return false;
    }
    if (out.empty())
        return false;
    std::reverse(out.begin(), out.end());
    return true;
}

/// @brief Extract the fully-qualified runtime class name from an expression.
/// @param expr The expression representing a runtime class reference.
/// @return The qualified name (e.g., "Viper.Graphics.Canvas") if valid, nullopt otherwise.
static std::optional<std::string> runtimeClassQNameFromExpr(const Expr &expr) {
    std::vector<std::string> parts;
    if (!collectQualifiedChain(expr, parts))
        return std::nullopt;
    if (!string_utils::iequals(parts.front(), "Viper"))
        return std::nullopt;
    return JoinDots(parts);
}

enum class RuntimePointerBridgeRole { None, Callback, Payload };

static RuntimePointerBridgeRole runtimePointerBridgeRole(std::string_view target,
                                                         std::size_t argIndex) {
    auto is = [&](std::string_view name) { return target == name; };

    if (is("Viper.Threads.Thread.Start") || is("Viper.Threads.Thread.StartSafe") ||
        is("Viper.Threads.Async.Run")) {
        if (argIndex == 0)
            return RuntimePointerBridgeRole::Callback;
        if (argIndex == 1)
            return RuntimePointerBridgeRole::Payload;
    }

    if (is("Viper.Threads.Thread.StartOwned") || is("Viper.Threads.Thread.StartSafeOwned") ||
        is("Viper.Threads.Async.RunOwned")) {
        return argIndex == 0 ? RuntimePointerBridgeRole::Callback : RuntimePointerBridgeRole::None;
    }

    if (is("Viper.Threads.Async.RunCancellable")) {
        if (argIndex == 0)
            return RuntimePointerBridgeRole::Callback;
        if (argIndex == 1)
            return RuntimePointerBridgeRole::Payload;
    }

    if (is("Viper.Threads.Async.RunCancellableOwned"))
        return argIndex == 0 ? RuntimePointerBridgeRole::Callback : RuntimePointerBridgeRole::None;

    if (is("Viper.Threads.Async.Map")) {
        if (argIndex == 1)
            return RuntimePointerBridgeRole::Callback;
        if (argIndex == 2)
            return RuntimePointerBridgeRole::Payload;
    }

    if (is("Viper.Threads.Async.MapOwned"))
        return argIndex == 1 ? RuntimePointerBridgeRole::Callback : RuntimePointerBridgeRole::None;

    if (is("Viper.Network.HttpServer.BindHandler") || is("Viper.Network.HttpsServer.BindHandler")) {
        return argIndex == 1 ? RuntimePointerBridgeRole::Callback : RuntimePointerBridgeRole::None;
    }

    return RuntimePointerBridgeRole::None;
}

static std::string saferRuntimePointerAlternative(std::string_view target) {
    if (target == "Viper.Core.Parse.TryInt")
        return "Viper.Core.Parse.IntOr";
    if (target == "Viper.Core.Parse.TryDouble")
        return "Viper.Core.Parse.DoubleOr";
    if (target == "Viper.Core.Parse.TryBool")
        return "Viper.Core.Parse.BoolOr";
    return {};
}

static bool isAddressOfArg(const ExprPtr &expr) {
    return expr && expr->kind() == Expr::Kind::AddressOf;
}

/// @brief Map runtime IL type names to BASIC semantic types.
/// @param ty The IL type string (e.g., "i64", "f64", "str").
/// @return The corresponding SemanticAnalyzer::Type, or nullopt if unknown.
static std::optional<SemanticAnalyzer::Type> semanticTypeFromRuntimeType(std::string_view ty) {
    auto trimRuntimeToken = [](std::string_view token) {
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t' ||
                                  token.front() == '\n' || token.front() == '\r'))
            token.remove_prefix(1);
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t' ||
                                  token.back() == '\n' || token.back() == '\r'))
            token.remove_suffix(1);
        if (!token.empty() && token.back() == '?')
            token.remove_suffix(1);
        return token;
    };
    ty = trimRuntimeToken(ty);
    if (ty == "i64")
        return SemanticAnalyzer::Type::Int;
    if (ty == "f64")
        return SemanticAnalyzer::Type::Float;
    if (ty == "i1")
        return SemanticAnalyzer::Type::Bool;
    if (ty == "str")
        return SemanticAnalyzer::Type::String;
    if (ty == "obj" || ty == "ptr" || ty.rfind("obj<", 0) == 0 || ty.rfind("ptr<", 0) == 0 ||
        ty == "seq" || ty.rfind("seq<", 0) == 0 || ty == "list" || ty.rfind("list<", 0) == 0)
        return SemanticAnalyzer::Type::Object;
    return std::nullopt;
}

static SemanticAnalyzer::Type semanticTypeFromBasicType(BasicType ty) {
    switch (ty) {
        case BasicType::Int:
            return SemanticAnalyzer::Type::Int;
        case BasicType::Float:
            return SemanticAnalyzer::Type::Float;
        case BasicType::String:
            return SemanticAnalyzer::Type::String;
        case BasicType::Bool:
            return SemanticAnalyzer::Type::Bool;
        case BasicType::Object:
            return SemanticAnalyzer::Type::Object;
        case BasicType::Void:
        case BasicType::Unknown:
        default:
            return SemanticAnalyzer::Type::Unknown;
    }
}

static BasicType basicTypeFromSemanticType(SemanticAnalyzer::Type ty) {
    switch (ty) {
        case SemanticAnalyzer::Type::Int:
            return BasicType::Int;
        case SemanticAnalyzer::Type::Float:
            return BasicType::Float;
        case SemanticAnalyzer::Type::String:
            return BasicType::String;
        case SemanticAnalyzer::Type::Bool:
            return BasicType::Bool;
        case SemanticAnalyzer::Type::Object:
            return BasicType::Object;
        case SemanticAnalyzer::Type::ArrayInt:
        case SemanticAnalyzer::Type::ArrayString:
        case SemanticAnalyzer::Type::ArrayObject:
        case SemanticAnalyzer::Type::Unknown:
        default:
            return BasicType::Unknown;
    }
}

static std::optional<::il::frontends::basic::Type> astTypeFromSemanticType(
    SemanticAnalyzer::Type ty) {
    switch (ty) {
        case SemanticAnalyzer::Type::Int:
            return ::il::frontends::basic::Type::I64;
        case SemanticAnalyzer::Type::Float:
            return ::il::frontends::basic::Type::F64;
        case SemanticAnalyzer::Type::String:
            return ::il::frontends::basic::Type::Str;
        case SemanticAnalyzer::Type::Bool:
            return ::il::frontends::basic::Type::Bool;
        default:
            return std::nullopt;
    }
}

SemanticAnalyzer::Type semanticTypeFromOopField(const ClassInfo::FieldInfo &field) {
    if (!field.objectClassName.empty())
        return SemanticAnalyzer::Type::Object;

    switch (field.type) {
        case ::il::frontends::basic::Type::I64:
            return SemanticAnalyzer::Type::Int;
        case ::il::frontends::basic::Type::F64:
            return SemanticAnalyzer::Type::Float;
        case ::il::frontends::basic::Type::Str:
            return SemanticAnalyzer::Type::String;
        case ::il::frontends::basic::Type::Bool:
            return SemanticAnalyzer::Type::Bool;
    }
    return SemanticAnalyzer::Type::Unknown;
}

const ClassInfo::FieldInfo *findActiveInstanceField(const SemanticAnalyzer &analyzer,
                                                    std::string_view name) {
    auto className = analyzer.activeInstanceClassQName();
    if (!className)
        return nullptr;
    return analyzer.oopIndex().findFieldInHierarchy(*className, name);
}

static std::optional<std::string> resolveUserClassQNameFromExpr(SemanticAnalyzer &analyzer,
                                                                const Expr &expr) {
    std::vector<std::string> parts;
    if (collectQualifiedChain(expr, parts)) {
        std::string dotted = JoinDots(parts);
        if (analyzer.oopIndex().findClass(dotted))
            return dotted;
    }

    if (auto inferred = inferObjectClassQName(analyzer, expr)) {
        if (analyzer.oopIndex().findClass(*inferred))
            return inferred;
    }

    return std::nullopt;
}

static void emitNoSuchMethod(SemanticDiagnostics &diagnostics,
                             const std::string &className,
                             const std::string &method,
                             il::support::SourceLoc loc) {
    std::string msg = "no such method '" + method + "' on '" + className + "'";
    diagnostics.emit(il::support::Severity::Error,
                     "E_NO_SUCH_METHOD",
                     loc,
                     static_cast<uint32_t>(method.size()),
                     std::move(msg));
}

static std::optional<std::string> resolveRuntimeFunctionReturnClassQName(
    SemanticAnalyzer &analyzer, std::string_view calleeName) {
    const auto &registry = il::runtime::RuntimeRegistry::instance();

    auto resolveConcrete = [&](std::string_view canonicalName) -> std::optional<std::string> {
        auto sig = registry.findFunction(canonicalName);
        if (!sig)
            return std::nullopt;

        if (std::string concrete = il::runtime::concreteRuntimeReturnClassQName(*sig);
            !concrete.empty()) {
            return concrete;
        }

        if (sig->returnType != il::runtime::ILScalarType::Object)
            return std::nullopt;

        auto lastDot = canonicalName.rfind('.');
        if (lastDot == std::string_view::npos)
            return std::nullopt;

        std::string prefix(canonicalName.substr(0, lastDot));
        std::string_view method = canonicalName.substr(lastDot + 1);
        if (string_utils::iequals(method, "New") && il::runtime::findRuntimeClassByQName(prefix))
            return prefix;

        return std::nullopt;
    };

    if (auto exact = resolveConcrete(calleeName))
        return exact;

    if (calleeName.find('.') != std::string_view::npos)
        return std::nullopt;

    for (const auto &ns : analyzer.getUsingImports()) {
        std::string qualified = ns;
        if (!qualified.empty())
            qualified.push_back('.');
        qualified.append(calleeName);
        if (auto imported = resolveConcrete(qualified))
            return imported;
    }

    return std::nullopt;
}

std::optional<std::string> inferObjectClassQName(SemanticAnalyzer &analyzer, const Expr &expr) {
    if (as<const MeExpr>(expr)) {
        return analyzer.activeInstanceClassQName();
    }

    if (const auto *var = as<const VarExpr>(expr))
        return analyzer.lookupObjectClassQName(var->name);

    if (const auto *alloc = as<const NewExpr>(expr)) {
        if (!alloc->className.empty())
            return alloc->className;
        if (!alloc->qualifiedType.empty())
            return JoinDots(alloc->qualifiedType);
        return std::nullopt;
    }

    if (const auto *call = as<const CallExpr>(expr)) {
        std::string calleeName =
            !call->calleeQualified.empty() ? JoinDots(call->calleeQualified) : call->callee;
        if (calleeName.empty())
            return std::nullopt;
        return resolveRuntimeFunctionReturnClassQName(analyzer, calleeName);
    }

    if (const auto *call = as<const MethodCallExpr>(expr)) {
        if (!call->base)
            return std::nullopt;

        std::string baseClass;
        if (auto runtimeBase = runtimeClassQNameFromExpr(*call->base))
            baseClass = *runtimeBase;
        else if (auto inferredBase = inferObjectClassQName(analyzer, *call->base))
            baseClass = *inferredBase;

        if (baseClass.empty())
            return std::nullopt;

        std::vector<BasicType> argTypes(call->args.size(), BasicType::Unknown);

        if (auto entry = runtimeMethodIndex().find(baseClass, call->method, argTypes);
            entry && !entry->returnClassQName.empty()) {
            return entry->returnClassQName;
        }

        if (const auto *klass = analyzer.oopIndex().findClass(baseClass)) {
            auto it = klass->methods.find(std::string(call->method));
            if (it != klass->methods.end() && !it->second.sig.returnClassName.empty())
                return it->second.sig.returnClassName;

            if (const auto *field =
                    analyzer.oopIndex().findFieldInHierarchy(baseClass, call->method);
                field && field->isArray && !field->objectClassName.empty()) {
                return field->objectClassName;
            }
        }

        return std::nullopt;
    }

    if (const auto *access = as<const MemberAccessExpr>(expr)) {
        if (!access->base)
            return std::nullopt;

        if (auto runtimeBase = runtimeClassQNameFromExpr(*access->base)) {
            if (const auto *klass = analyzer.oopIndex().findClass(*runtimeBase)) {
                if (const auto *field =
                        analyzer.oopIndex().findField(klass->qualifiedName, access->member);
                    field && !field->objectClassName.empty()) {
                    return field->objectClassName;
                }
            }
        }

        if (auto baseClass = inferObjectClassQName(analyzer, *access->base)) {
            if (const auto *field = analyzer.oopIndex().findField(*baseClass, access->member);
                field && !field->objectClassName.empty()) {
                return field->objectClassName;
            }
        }
    }

    return std::nullopt;
}

/// @brief Resolve the type of a runtime class property access.
/// @param analyzer The semantic analyzer instance.
/// @param expr The member access expression (e.g., obj.property).
/// @return The semantic type of the property if found, nullopt otherwise.
/// @details Handles both direct class references (Canvas.Width) and variable
///          references to runtime class instances.
static std::optional<SemanticAnalyzer::Type> resolveRuntimePropertyType(
    SemanticAnalyzer &analyzer, const MemberAccessExpr &expr) {
    const il::runtime::RuntimeClass *klass = nullptr;
    if (expr.base) {
        if (auto qname = runtimeClassQNameFromExpr(*expr.base)) {
            klass = il::runtime::findRuntimeClassByQName(*qname);
        } else if (auto qname2 = inferObjectClassQName(analyzer, *expr.base)) {
            klass = il::runtime::findRuntimeClassByQName(*qname2);
        }
    }

    if (!klass)
        return std::nullopt;

    for (const auto &prop : klass->properties) {
        if (string_utils::iequals(prop.name, expr.member))
            return semanticTypeFromRuntimeType(prop.type ? prop.type : "");
    }

    return std::nullopt;
}

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic {

using semantic_analyzer_detail::astToSemanticType;
using semantic_analyzer_detail::astTypeFromSemanticType;
using semantic_analyzer_detail::emitNoSuchMethod;
using semantic_analyzer_detail::inferObjectClassQName;
using semantic_analyzer_detail::isRuntimeNamespaceChain;
using semantic_analyzer_detail::levenshtein;
using semantic_analyzer_detail::resolveRuntimePropertyType;
using semantic_analyzer_detail::resolveUserClassQNameFromExpr;
using semantic_analyzer_detail::runtimeClassQNameFromExpr;
using semantic_analyzer_detail::semanticTypeName;

bool SemanticAnalyzer::checkRuntimePointerSafety(std::string_view target,
                                                 bool rawPointerReturn,
                                                 const std::vector<bool> &rawPointerParams,
                                                 const std::vector<ExprPtr> &args,
                                                 il::support::SourceLoc loc,
                                                 std::string_view displayName) {
    bool hasRawParam = false;
    for (bool raw : rawPointerParams) {
        if (raw) {
            hasRawParam = true;
            break;
        }
    }
    if (!rawPointerReturn && !hasRawParam)
        return true;

    std::string targetName(target);
    if (targetName.empty())
        targetName = std::string(displayName);

    auto diagnosticMessage = [&](std::string detail) {
        std::string message = "Runtime API '" + targetName + "' exposes " + detail +
                              " and is unavailable in safe BASIC";
        if (std::string alternative =
                semantic_analyzer_detail::saferRuntimePointerAlternative(targetName);
            !alternative.empty()) {
            message += "; use " + alternative;
        } else {
            message += "; use a typed runtime class/API";
        }
        return message;
    };

    const uint32_t displayLength =
        static_cast<uint32_t>(std::max<std::size_t>(displayName.size(), 1));
    if (rawPointerReturn) {
        de.emit(il::support::Severity::Error,
                "B2131",
                loc,
                displayLength,
                diagnosticMessage("a raw pointer return"));
        return false;
    }

    std::vector<bool> safeCallbacks(rawPointerParams.size(), false);
    for (std::size_t i = 0; i < rawPointerParams.size(); ++i) {
        if (!rawPointerParams[i])
            continue;

        semantic_analyzer_detail::RuntimePointerBridgeRole role =
            semantic_analyzer_detail::runtimePointerBridgeRole(targetName, i);
        if (role == semantic_analyzer_detail::RuntimePointerBridgeRole::Callback &&
            i < args.size() && semantic_analyzer_detail::isAddressOfArg(args[i])) {
            safeCallbacks[i] = true;
            continue;
        }

        if (role == semantic_analyzer_detail::RuntimePointerBridgeRole::Payload &&
            i < args.size() && !semantic_analyzer_detail::isAddressOfArg(args[i])) {
            bool pairedWithCallback = false;
            for (std::size_t prior = 0; prior < i && prior < safeCallbacks.size(); ++prior) {
                if (safeCallbacks[prior]) {
                    pairedWithCallback = true;
                    break;
                }
            }
            if (pairedWithCallback)
                continue;
        }

        il::support::SourceLoc argLoc = loc;
        if (i < args.size() && args[i])
            argLoc = args[i]->loc;
        de.emit(il::support::Severity::Error,
                "B2131",
                argLoc,
                1,
                diagnosticMessage("raw pointer argument " + std::to_string(i + 1)));
        return false;
    }

    return true;
}

/// @brief Visitor that routes AST expression nodes through SemanticAnalyzer helpers.
///
/// @details Each override forwards to the corresponding SemanticAnalyzer method
///          or returns an immediate type for literals.  The visitor stores the
///          resulting semantic type so callers can retrieve it after walking an
///          expression tree.
class SemanticAnalyzerExprVisitor final : public MutExprVisitor {
  public:
    /// @brief Create a visitor bound to @p analyzer.
    explicit SemanticAnalyzerExprVisitor(SemanticAnalyzer &analyzer) noexcept
        : analyzer_(analyzer) {}

    /// @brief Literal integers yield the integer semantic type.
    void visit(IntExpr &) override {
        result_ = SemanticAnalyzer::Type::Int;
    }

    /// @brief Literal floats evaluate to floating-point semantic type.
    void visit(FloatExpr &) override {
        result_ = SemanticAnalyzer::Type::Float;
    }

    /// @brief Literal strings evaluate to string semantic type.
    void visit(StringExpr &) override {
        result_ = SemanticAnalyzer::Type::String;
    }

    /// @brief Boolean literals propagate the boolean semantic type.
    void visit(BoolExpr &) override {
        result_ = SemanticAnalyzer::Type::Bool;
    }

    /// @brief Variables defer to SemanticAnalyzer for resolution.
    void visit(VarExpr &expr) override {
        result_ = analyzer_.analyzeVar(expr);
    }

    /// @brief Array expressions trigger array-specific analysis.
    void visit(ArrayExpr &expr) override {
        result_ = analyzer_.analyzeArray(expr);
    }

    /// @brief Unary expressions are analysed via SemanticAnalyzer helpers.
    void visit(UnaryExpr &expr) override {
        result_ = analyzer_.analyzeUnary(expr);
    }

    /// @brief Binary expressions defer to SemanticAnalyzer::analyzeBinary.
    void visit(BinaryExpr &expr) override {
        result_ = analyzer_.analyzeBinary(expr);
    }

    /// @brief Builtin calls delegate to dedicated builtin analysis.
    void visit(BuiltinCallExpr &expr) override {
        result_ = analyzer_.analyzeBuiltinCall(expr);
    }

    /// @brief LBOUND expressions compute integer results via analyser logic.
    void visit(LBoundExpr &expr) override {
        result_ = analyzer_.analyzeLBound(expr);
    }

    /// @brief UBOUND expressions compute integer results via analyser logic.
    void visit(UBoundExpr &expr) override {
        result_ = analyzer_.analyzeUBound(expr);
    }

    /// @brief Procedure calls re-use general call analysis.
    void visit(CallExpr &expr) override {
        result_ = analyzer_.analyzeCall(expr);
    }

    /// @brief NEW expressions analyse constructor signatures before returning Unknown.
    void visit(NewExpr &expr) override {
        result_ = analyzer_.analyzeNew(expr);
    }

    /// @brief ME references evaluate to the active instance object.
    void visit(MeExpr &expr) override {
        if (analyzer_.activeMemberHasMe_ && !analyzer_.activeClassQName_.empty()) {
            result_ = SemanticAnalyzer::Type::Object;
            return;
        }

        analyzer_.de.emit(il::support::Severity::Error,
                          "B2130",
                          expr.loc,
                          2,
                          "ME can only be used inside an instance class member");
        result_ = SemanticAnalyzer::Type::Unknown;
    }

    /// @brief Member access expressions validate the base and return known field/property types.
    void visit(MemberAccessExpr &expr) override {
        // Validate base expression (catches undefined variables like 'A' in 'A.B')
        if (expr.base && !isRuntimeNamespaceChain(*expr.base)) {
            analyzer_.visitExpr(*expr.base);
        }
        if (auto rtType = resolveRuntimePropertyType(analyzer_, expr)) {
            result_ = *rtType;
            return;
        }
        if (expr.base) {
            if (auto className = resolveUserClassQNameFromExpr(analyzer_, *expr.base)) {
                if (const auto *field =
                        analyzer_.oopIndex().findFieldInHierarchy(*className, expr.member)) {
                    result_ = semantic_analyzer_detail::semanticTypeFromOopField(*field);
                    return;
                }
            }
        }
        result_ = SemanticAnalyzer::Type::Unknown;
    }

    /// @brief Reject primitive arguments bound to object parameters of a runtime method.
    /// @details Runtime object parameters are pointers at the IL level; an INTEGER,
    ///          FLOAT, or BOOLEAN argument would otherwise reach IL verification as an
    ///          invalid i64/f64 operand. Strings are permitted (the lowerer passes them
    ///          as managed references), as are OBJECT and untyped/unknown arguments and
    ///          the literal `0` null-object idiom.
    /// @return True when every argument is acceptable; false after emitting B2001.
    bool checkObjectParamArgs(const RuntimeMethodInfo &info,
                              const std::vector<BasicType> &argTypes,
                              const std::vector<ExprPtr> &args,
                              const std::string &method,
                              il::support::SourceLoc loc) {
        const std::size_t count = std::min(info.args.size(), argTypes.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (info.args[i] != BasicType::Object)
                continue;
            const BasicType argTy = argTypes[i];
            if (argTy == BasicType::Int || argTy == BasicType::Float ||
                argTy == BasicType::Bool) {
                if (argTy == BasicType::Int && i < args.size() && args[i]) {
                    if (const auto *lit = as<const IntExpr>(*args[i]); lit && lit->value == 0)
                        continue; // Literal 0 is the null-object idiom.
                }
                std::string msg = "argument " + std::to_string(i + 1) + " to '" + method +
                                  "' expects an OBJECT; box primitives with Viper.Core.Box";
                analyzer_.de.emit(il::support::Severity::Error, "B2001", loc, 1, std::move(msg));
                return false;
            }
        }
        return true;
    }

    /// @brief Method calls validate runtime/OOP methods and return known result types.
    void visit(MethodCallExpr &expr) override {
        SemanticAnalyzer::Type baseType = SemanticAnalyzer::Type::Unknown;
        const bool runtimeNamespaceBase = expr.base && isRuntimeNamespaceChain(*expr.base);
        if (expr.base && !runtimeNamespaceBase)
            baseType = analyzer_.visitExpr(*expr.base);

        std::vector<SemanticAnalyzer::Type> argTypes;
        argTypes.reserve(expr.args.size());
        for (auto &arg : expr.args) {
            argTypes.push_back(arg ? analyzer_.visitExpr(*arg) : SemanticAnalyzer::Type::Unknown);
        }

        std::vector<BasicType> runtimeArgTypes;
        runtimeArgTypes.reserve(argTypes.size());
        for (auto ty : argTypes)
            runtimeArgTypes.push_back(semantic_analyzer_detail::basicTypeFromSemanticType(ty));

        std::string runtimeClass;
        if (expr.base) {
            if (auto qname = runtimeClassQNameFromExpr(*expr.base)) {
                if (il::runtime::findRuntimeClassByQName(*qname))
                    runtimeClass = *qname;
            }
            if (runtimeClass.empty()) {
                if (auto inferred = inferObjectClassQName(analyzer_, *expr.base)) {
                    if (il::runtime::findRuntimeClassByQName(*inferred))
                        runtimeClass = *inferred;
                }
            }
            if (runtimeClass.empty() && baseType == SemanticAnalyzer::Type::String)
                runtimeClass = il::runtime::RTCLASS_STRING;
        }

        if (!runtimeClass.empty()) {
            auto info = runtimeMethodIndex().find(runtimeClass, expr.method, runtimeArgTypes);
            if (!info) {
                emitNoSuchMethod(analyzer_.de, runtimeClass, expr.method, expr.loc);
                result_ = SemanticAnalyzer::Type::Unknown;
                return;
            }
            if (!analyzer_.checkRuntimePointerSafety(info->target,
                                                     info->rawPointerReturn,
                                                     info->rawPointerParams,
                                                     expr.args,
                                                     expr.loc,
                                                     expr.method)) {
                result_ = SemanticAnalyzer::Type::Unknown;
                return;
            }
            if (!checkObjectParamArgs(*info, runtimeArgTypes, expr.args, expr.method, expr.loc)) {
                result_ = SemanticAnalyzer::Type::Unknown;
                return;
            }
            result_ = semantic_analyzer_detail::semanticTypeFromBasicType(info->ret);
            return;
        }

        if (expr.base) {
            if (auto className = resolveUserClassQNameFromExpr(analyzer_, *expr.base)) {
                std::vector<::il::frontends::basic::Type> astArgTypes;
                astArgTypes.reserve(argTypes.size());
                for (auto ty : argTypes) {
                    if (auto astTy = astTypeFromSemanticType(ty)) {
                        astArgTypes.push_back(*astTy);
                    } else {
                        astArgTypes.push_back(::il::frontends::basic::Type::I64);
                    }
                }

                auto resolved = sem::resolveMethodOverload(analyzer_.oopIndex(),
                                                           *className,
                                                           expr.method,
                                                           /*isStatic*/ false,
                                                           astArgTypes,
                                                           "",
                                                           nullptr,
                                                           expr.loc);
                if (!resolved) {
                    resolved = sem::resolveMethodOverload(analyzer_.oopIndex(),
                                                          *className,
                                                          expr.method,
                                                          /*isStatic*/ true,
                                                          astArgTypes,
                                                          "",
                                                          nullptr,
                                                          expr.loc);
                }

                if (!resolved) {
                    if (const auto *field =
                            analyzer_.oopIndex().findFieldInHierarchy(*className, expr.method);
                        field && field->isArray) {
                        validateArrayFieldIndexArgs(expr, argTypes);
                        result_ = semantic_analyzer_detail::semanticTypeFromOopField(*field);
                        return;
                    }

                    auto objectInfo = runtimeMethodIndex().find(
                        std::string(il::runtime::RTCLASS_OBJECT), expr.method, runtimeArgTypes);
                    if (objectInfo) {
                        if (!analyzer_.checkRuntimePointerSafety(objectInfo->target,
                                                                 objectInfo->rawPointerReturn,
                                                                 objectInfo->rawPointerParams,
                                                                 expr.args,
                                                                 expr.loc,
                                                                 expr.method)) {
                            result_ = SemanticAnalyzer::Type::Unknown;
                            return;
                        }
                        if (!checkObjectParamArgs(
                                *objectInfo, runtimeArgTypes, expr.args, expr.method, expr.loc)) {
                            result_ = SemanticAnalyzer::Type::Unknown;
                            return;
                        }
                        result_ =
                            semantic_analyzer_detail::semanticTypeFromBasicType(objectInfo->ret);
                    } else {
                        // A method with this name may exist but be inaccessible
                        // (e.g. PRIVATE): defer so the lowering-stage access check
                        // reports the precise visibility diagnostic (B2021) instead
                        // of a misleading "no such method".
                        bool nameExists = false;
                        if (const auto *klass = analyzer_.oopIndex().findClass(*className))
                            nameExists = klass->methods.contains(expr.method);
                        if (!nameExists)
                            emitNoSuchMethod(analyzer_.de, *className, expr.method, expr.loc);
                        result_ = SemanticAnalyzer::Type::Unknown;
                    }
                    return;
                }

                if (!resolved->method->sig.returnClassName.empty()) {
                    result_ = SemanticAnalyzer::Type::Object;
                    return;
                }
                if (resolved->method->sig.returnType) {
                    result_ = astToSemanticType(*resolved->method->sig.returnType);
                    return;
                }
                result_ = SemanticAnalyzer::Type::Unknown;
                return;
            }
        }

        if (baseType == SemanticAnalyzer::Type::Object) {
            auto info = runtimeMethodIndex().find(
                std::string(il::runtime::RTCLASS_OBJECT), expr.method, runtimeArgTypes);
            if (info) {
                if (!analyzer_.checkRuntimePointerSafety(info->target,
                                                         info->rawPointerReturn,
                                                         info->rawPointerParams,
                                                         expr.args,
                                                         expr.loc,
                                                         expr.method)) {
                    result_ = SemanticAnalyzer::Type::Unknown;
                    return;
                }
                if (!checkObjectParamArgs(*info, runtimeArgTypes, expr.args, expr.method, expr.loc)) {
                    result_ = SemanticAnalyzer::Type::Unknown;
                    return;
                }
                result_ = semantic_analyzer_detail::semanticTypeFromBasicType(info->ret);
                return;
            }
        }

        result_ = SemanticAnalyzer::Type::Unknown;
    }

    /// @brief IS expressions evaluate to boolean; validate operands and type name.
    void visit(IsExpr &expr) override {
        // Check left operand type; reject obvious primitives.
        SemanticAnalyzer::Type lhsType = analyzer_.visitExpr(*expr.value);
        auto isPrimitive = [&](SemanticAnalyzer::Type t) {
            using T = SemanticAnalyzer::Type;
            return t == T::Int || t == T::Float || t == T::Bool || t == T::String ||
                   t == T::ArrayInt || t == T::ArrayString || t == T::ArrayObject;
        };

        // Resolve right-hand dotted type to class or interface.
        auto existsQ = [&](const std::string &q) -> bool {
            if (analyzer_.oopIndex_.interfacesByQname().contains(q))
                return true;
            return analyzer_.oopIndex_.findClass(q) != nullptr;
        };

        std::string dotted;
        if (!expr.typeName.empty()) {
            if (expr.typeName.size() == 1) {
                // Unqualified: resolve with parent-walk then imports.
                std::string ident = Canon(expr.typeName[0]);
                std::vector<std::string> prefix;
                for (const auto &seg : analyzer_.nsStack_)
                    prefix.push_back(Canon(seg));
                std::vector<std::string> hits;
                for (std::size_t n = prefix.size(); n > 0; --n) {
                    std::vector<std::string> parts(prefix.begin(),
                                                   prefix.begin() + static_cast<std::ptrdiff_t>(n));
                    parts.push_back(ident);
                    std::string q = JoinDots(parts);
                    if (existsQ(q))
                        hits.push_back(q);
                }
                if (existsQ(ident))
                    hits.push_back(ident);
                if (hits.size() == 1)
                    dotted = hits[0];
                else if (hits.empty()) {
                    if (!analyzer_.usingStack_.empty()) {
                        const auto &cur = analyzer_.usingStack_.back();
                        for (const auto &ns : cur.imports) {
                            std::string q = ns + "." + ident;
                            if (existsQ(q))
                                hits.push_back(q);
                        }
                    }
                    if (hits.size() == 1)
                        dotted = hits[0];
                }
                if (dotted.empty()) {
                    // Default to single ident (unknown handling occurs below)
                    dotted = ident;
                }
            } else {
                // Qualified: join as-is
                for (size_t i = 0; i < expr.typeName.size(); ++i) {
                    if (i)
                        dotted.push_back('.');
                    dotted += expr.typeName[i];
                }
            }
        }
        bool resolved = existsQ(dotted);

        if (!resolved) {
            std::string msg = std::string("unknown type '") + dotted + "'";
            analyzer_.de.emit(il::support::Severity::Error,
                              "B2111",
                              expr.loc,
                              static_cast<uint32_t>(dotted.size()),
                              std::move(msg));
        } else if (isPrimitive(lhsType)) {
            analyzer_.de.emit(il::support::Severity::Error,
                              "B2121",
                              expr.loc,
                              2,
                              "'IS' requires an object/interface value on the left");
        }

        result_ = SemanticAnalyzer::Type::Bool;
    }

    /// @brief AS expressions yield the inner value's type; validate operands and type name.
    void visit(AsExpr &expr) override {
        // Preserve operand type; runtime returns NULL on failure.
        SemanticAnalyzer::Type lhsType = analyzer_.visitExpr(*expr.value);
        auto isPrimitive = [&](SemanticAnalyzer::Type t) {
            using T = SemanticAnalyzer::Type;
            return t == T::Int || t == T::Float || t == T::Bool || t == T::String ||
                   t == T::ArrayInt || t == T::ArrayString || t == T::ArrayObject;
        };

        std::string dotted;
        if (!expr.typeName.empty()) {
            if (expr.typeName.size() == 1) {
                std::string ident = Canon(expr.typeName[0]);
                std::vector<std::string> prefix;
                for (const auto &seg : analyzer_.nsStack_)
                    prefix.push_back(Canon(seg));
                std::vector<std::string> hits;
                auto existsQ = [&](const std::string &q) -> bool {
                    if (analyzer_.oopIndex_.interfacesByQname().contains(q))
                        return true;
                    return analyzer_.oopIndex_.findClass(q) != nullptr;
                };
                for (std::size_t n = prefix.size(); n > 0; --n) {
                    std::vector<std::string> parts(prefix.begin(),
                                                   prefix.begin() + static_cast<std::ptrdiff_t>(n));
                    parts.push_back(ident);
                    std::string q = JoinDots(parts);
                    if (existsQ(q))
                        hits.push_back(q);
                }
                if (existsQ(ident))
                    hits.push_back(ident);
                if (hits.size() == 1)
                    dotted = hits[0];
                else if (hits.empty()) {
                    if (!analyzer_.usingStack_.empty()) {
                        const auto &cur = analyzer_.usingStack_.back();
                        for (const auto &ns : cur.imports) {
                            std::string q = ns + "." + ident;
                            if (existsQ(q))
                                hits.push_back(q);
                        }
                    }
                    if (hits.size() == 1)
                        dotted = hits[0];
                }
                if (dotted.empty())
                    dotted = ident;
            } else {
                // Qualified: apply alias expansion to the first segment, if present.
                std::vector<std::string> segs = expr.typeName;
                if (!analyzer_.usingStack_.empty() && !segs.empty()) {
                    std::string firstCanon = Canon(segs[0]);
                    const auto &aliases = analyzer_.usingStack_.back().aliases;
                    auto it = aliases.find(firstCanon);
                    if (it != aliases.end()) {
                        std::vector<std::string> expanded = SplitDots(it->second);
                        expanded.insert(expanded.end(), segs.begin() + 1, segs.end());
                        segs = std::move(expanded);
                    }
                }
                for (size_t i = 0; i < segs.size(); ++i) {
                    if (i)
                        dotted.push_back('.');
                    dotted += segs[i];
                }
            }
        }
        bool resolved = false;
        if (analyzer_.oopIndex_.interfacesByQname().contains(dotted))
            resolved = true;
        if (!resolved) {
            for (const auto &entry : analyzer_.oopIndex_.classes()) {
                // Case-insensitive comparison since BASIC is case-insensitive
                if (string_utils::iequals(entry.second.qualifiedName, dotted)) {
                    resolved = true;
                    break;
                }
            }
        }

        if (!resolved) {
            std::string msg = std::string("unknown type '") + dotted + "'";
            analyzer_.de.emit(il::support::Severity::Error,
                              "B2111",
                              expr.loc,
                              static_cast<uint32_t>(dotted.size()),
                              std::move(msg));
        } else if (isPrimitive(lhsType)) {
            // E_CAST_INVALID: cannot cast value of type '{From}' to '{To}'.
            std::string from = std::string(semantic_analyzer_detail::semanticTypeName(lhsType));
            std::string to = dotted;
            std::string msg = "cannot cast value of type '" + from + "' to '" + to + "'.";
            analyzer_.de.emit(
                il::support::Severity::Error, "E_CAST_INVALID", expr.loc, 2, std::move(msg));
        }

        result_ = lhsType;
    }

    /// @brief ADDRESSOF expressions yield a function pointer type (Unknown in BASIC semantics).
    void visit(AddressOfExpr &) override {
        // ADDRESSOF produces a function pointer, which is represented as Unknown
        // in BASIC's type system since it's not a traditional BASIC value type.
        result_ = SemanticAnalyzer::Type::Unknown;
    }

    /// @brief Retrieve the semantic type computed during visitation.
    [[nodiscard]] SemanticAnalyzer::Type result() const noexcept {
        return result_;
    }

  private:
    void validateArrayFieldIndexArgs(MethodCallExpr &expr,
                                     const std::vector<SemanticAnalyzer::Type> &argTypes) {
        for (std::size_t i = 0; i < expr.args.size(); ++i) {
            auto &arg = expr.args[i];
            if (!arg)
                continue;

            SemanticAnalyzer::Type ty =
                i < argTypes.size() ? argTypes[i] : SemanticAnalyzer::Type::Unknown;
            if (ty == SemanticAnalyzer::Type::Float) {
                if (as<FloatExpr>(*arg)) {
                    analyzer_.insertImplicitCast(*arg, SemanticAnalyzer::Type::Int);
                    std::string msg = "narrowing conversion from FLOAT to INT in array index";
                    analyzer_.de.emit(
                        il::support::Severity::Warning, "B2002", expr.loc, 1, std::move(msg));
                } else {
                    std::string msg = "index type mismatch";
                    analyzer_.de.emit(
                        il::support::Severity::Error, "B2001", expr.loc, 1, std::move(msg));
                }
            } else if (ty != SemanticAnalyzer::Type::Unknown && ty != SemanticAnalyzer::Type::Int) {
                std::string msg = "index type mismatch";
                analyzer_.de.emit(
                    il::support::Severity::Error, "B2001", expr.loc, 1, std::move(msg));
            }
        }
    }

    SemanticAnalyzer &analyzer_;
    SemanticAnalyzer::Type result_{SemanticAnalyzer::Type::Unknown};
};

/// @brief Resolve a variable reference and compute its semantic type.
///
/// @details Delegates to @ref sem::analyzeVarExpr which handles symbol tracking,
///          Levenshtein suggestions for typos, and BASIC suffix rules.
///
/// @param v Variable expression under analysis.
/// @return Semantic type inferred for the variable.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeVar(VarExpr &v) {
    return sem::analyzeVarExpr(*this, v);
}

/// @brief Analyse a unary expression using helper utilities.
///
/// @param u Unary expression AST node.
/// @return Semantic type computed by @ref sem::analyzeUnaryExpr.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeUnary(const UnaryExpr &u) {
    return sem::analyzeUnaryExpr(*this, u);
}

/// @brief Analyse a binary expression using helper utilities.
///
/// @param b Binary expression AST node.
/// @return Semantic type computed by @ref sem::analyzeBinaryExpr.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeBinary(const BinaryExpr &b) {
    return sem::analyzeBinaryExpr(*this, b);
}

/// @brief Analyse a constructor invocation for argument compatibility.
///
/// @details Evaluates each argument expression, then compares the resulting
///          semantic types against the constructor signature recorded in the
///          OOP index.  When the class is unknown or a synthetic constructor is
///          used, the analyser treats the expression as producing an unknown
///          type but still walks the argument expressions to preserve nested
///          diagnostics.
///
/// @param expr NEW expression being analysed.
/// @return Semantic type observed for the NEW expression (currently Unknown).
SemanticAnalyzer::Type SemanticAnalyzer::analyzeNew(NewExpr &expr) {
    // Helper: map canonical qualified name to declared-case name in OOP index.
    auto mapCanonicalToDeclared = [&](const std::string &qcanon) -> std::string {
        if (const ClassInfo *ci = oopIndex_.findClass(qcanon))
            return ci->qualifiedName;
        auto toCanonQ = [](const std::string &q) -> std::string {
            std::vector<std::string> segs = SplitDots(q);
            return CanonJoin(segs);
        };
        std::string want = toCanonQ(qcanon);
        for (const auto &kv : oopIndex_.classes()) {
            const std::string &decl = kv.second.qualifiedName;
            if (toCanonQ(decl) == want)
                return decl;
        }
        return qcanon;
    };

    // Treat a single qualified segment as an unqualified type name so USING and
    // parent-walk resolution apply.
    if (expr.qualifiedType.size() == 1) {
        expr.className = expr.qualifiedType.front();
        expr.qualifiedType.clear();
    }

    // Apply alias expansion for qualified type, if present.
    if (!expr.qualifiedType.empty() && !usingStack_.empty()) {
        std::string firstCanon = Canon(expr.qualifiedType[0]);
        const auto &aliases = usingStack_.back().aliases;
        auto it = aliases.find(firstCanon);
        if (it != aliases.end()) {
            std::vector<std::string> expanded = SplitDots(it->second);
            expanded.insert(
                expanded.end(), expr.qualifiedType.begin() + 1, expr.qualifiedType.end());
            // Rebuild className from canonicalized expanded segments.
            expr.className = mapCanonicalToDeclared(CanonJoin(expanded));
        }
    }

    // If unqualified type, resolve using parent-walk then USING imports.
    if (expr.qualifiedType.empty()) {
        std::vector<std::string> attempts;
        auto existsQ = [&](const std::string &q) -> bool {
            std::string decl = mapCanonicalToDeclared(q);
            if (oopIndex_.interfacesByQname().contains(decl))
                return true;
            return oopIndex_.findClass(decl) != nullptr;
        };

        std::string ident = Canon(expr.className);
        // Parent-walk: A.B.Point -> A.Point -> Point
        std::vector<std::string> prefixCanon;
        prefixCanon.reserve(nsStack_.size());
        for (const auto &seg : nsStack_) {
            std::string cseg = Canon(seg);
            prefixCanon.push_back(cseg.empty() ? seg : cseg);
        }
        std::vector<std::string> hits;
        for (std::size_t n = prefixCanon.size(); n > 0; --n) {
            std::vector<std::string> parts(prefixCanon.begin(),
                                           prefixCanon.begin() + static_cast<std::ptrdiff_t>(n));
            parts.push_back(ident);
            std::string q = JoinDots(parts);
            attempts.push_back(q);
            if (existsQ(q))
                hits.push_back(q);
        }
        // Try global
        attempts.push_back(ident);
        if (existsQ(ident))
            hits.push_back(ident);

        auto reportAmbiguous = [&](const std::vector<std::string> &cands) {
            std::vector<std::string> sorted = cands;
            std::sort(sorted.begin(), sorted.end());
            std::string msg = std::string("ambiguous type '") + expr.className + "' (candidates: ";
            for (size_t i = 0; i < sorted.size(); ++i) {
                if (i)
                    msg += ", ";
                msg += sorted[i];
            }
            msg += ")";
            de.emit(il::support::Severity::Error,
                    "B2110",
                    expr.loc,
                    static_cast<uint32_t>(expr.className.size()),
                    std::move(msg));
        };

        if (hits.size() == 1) {
            expr.className = mapCanonicalToDeclared(hits[0]);
        } else if (hits.size() > 1) {
            reportAmbiguous(hits);
            return Type::Unknown;
        } else {
            // Try USING imports
            std::vector<std::string> importHits;
            if (!usingStack_.empty()) {
                const UsingScope &cur = usingStack_.back();
                for (const auto &ns : cur.imports) {
                    std::string q = ns;
                    if (!q.empty())
                        q.push_back('.');
                    q += ident;
                    attempts.push_back(q);
                    if (existsQ(q))
                        importHits.push_back(q);
                }
            }
            if (importHits.size() == 1) {
                expr.className = mapCanonicalToDeclared(importHits[0]);
            } else if (importHits.size() > 1) {
                reportAmbiguous(importHits);
                return Type::Unknown;
            } else {
                // Unknown type with tried list
                std::string msg = std::string("unknown type '") + expr.className + "'";
                if (!attempts.empty()) {
                    msg += " (tried: ";
                    size_t limit = std::min<std::size_t>(attempts.size(), 8);
                    for (size_t i = 0; i < limit; ++i) {
                        if (i)
                            msg += ", ";
                        msg += attempts[i];
                    }
                    if (attempts.size() > limit) {
                        msg += ", +" + std::to_string(attempts.size() - limit) + " more";
                    }
                    msg += ")";
                }
                de.emit(il::support::Severity::Error,
                        "B2111",
                        expr.loc,
                        static_cast<uint32_t>(expr.className.size()),
                        std::move(msg));
                return Type::Unknown;
            }
        }
    }

    std::vector<Type> argTypes;
    argTypes.reserve(expr.args.size());
    for (auto &arg : expr.args)
        argTypes.push_back(arg ? visitExpr(*arg) : Type::Unknown);

    // Allow NEW for runtime classes defined in the catalog when a ctor helper exists.
    {
        auto &tyreg = runtimeTypeRegistry();
        if (tyreg.kindOf(expr.className) == TypeKind::BuiltinExternalType) {
            if (const auto *found = il::runtime::findRuntimeClassByQName(expr.className)) {
                if (!found->ctor || std::string(found->ctor).empty()) {
                    std::string msg = "runtime class '" + expr.className + "' has no constructor";
                    de.emit(il::support::Severity::Error,
                            "E_RUNTIME_CLASS_NO_CTOR",
                            expr.loc,
                            static_cast<uint32_t>(expr.className.size()),
                            std::move(msg));
                    return Type::Unknown;
                }
                // Accept; arguments already visited above; return Unknown semantic type.
                return Type::Unknown;
            }
        }
    }

    const ClassInfo *klass = oopIndex_.findClass(expr.className);
    if (!klass)
        return Type::Unknown;

    // Instantiation of abstract classes is not allowed.
    if (klass->isAbstract) {
        std::string msg = "cannot instantiate abstract class '" + expr.className + "'";
        de.emit(il::support::Severity::Error,
                "B2106",
                expr.loc,
                static_cast<uint32_t>(expr.className.size()),
                std::move(msg));
        // Still analyze arguments for nested diagnostics, but return Unknown.
        return Type::Unknown;
    }

    const std::size_t expectedCount = klass->ctorParams.size();
    if (expr.args.size() != expectedCount) {
        std::string msg = "constructor for '" + expr.className + "' expects " +
                          std::to_string(expectedCount) + " argument" +
                          (expectedCount == 1 ? "" : "s") + ", got " +
                          std::to_string(expr.args.size());
        de.emit(il::support::Severity::Error,
                "B2008",
                expr.loc,
                static_cast<uint32_t>(expr.className.size()),
                std::move(msg));
        return Type::Unknown;
    }

    for (std::size_t i = 0; i < expectedCount; ++i) {
        const auto &param = klass->ctorParams[i];
        const Expr *argExpr = expr.args[i].get();
        Type argTy = argTypes[i];

        if (param.isArray) {
            const auto *var = as<const VarExpr>(*argExpr);
            if (!var || !arrays_.contains(var->name)) {
                il::support::SourceLoc loc = argExpr ? argExpr->loc : expr.loc;
                std::string msg = "constructor argument " + std::to_string(i + 1) + " for '" +
                                  expr.className + "' must be an array variable (ByRef)";
                de.emit(il::support::Severity::Error, "B2006", loc, 1, std::move(msg));
            }
            continue;
        }

        auto expectTy = param.type;
        if (expectTy == ::il::frontends::basic::Type::F64 && argTy == Type::Int)
            continue;

        Type want = astToSemanticType(expectTy);
        if (argTy != Type::Unknown && argTy != want) {
            il::support::SourceLoc loc = argExpr ? argExpr->loc : expr.loc;
            std::string msg = "constructor argument type mismatch for '" + expr.className + "'";
            de.emit(il::support::Severity::Error, "B2001", loc, 1, std::move(msg));
        }
    }

    return Type::Unknown;
}

/// @brief Record that @p expr should be implicitly converted to @p targetType.
///
/// @details Stores the target type in an auxiliary map consulted during
///          lowering so conversions can be inserted exactly where the analyser
///          determined they are needed.
///
/// @param expr Expression requiring a conversion.
/// @param targetType Type to which the expression should be coerced.
void SemanticAnalyzer::markImplicitConversion(const Expr &expr, Type targetType) {
    implicitConversions_[&expr] = targetType;
}

/// @brief Request that @p expr be wrapped in an implicit cast to @p target.
///
/// @details The current BASIC AST lacks a dedicated cast node, so the semantic
///          analyser records the intent using the same implicit-conversion map
///          consulted during lowering. Once cast nodes exist this helper can
///          be updated to rewrite the AST directly.
///
/// @param expr Expression slated for conversion.
/// @param target Semantic type to coerce the expression to.
void SemanticAnalyzer::insertImplicitCast(Expr &expr, Type target) {
    auto it = implicitConversions_.find(&expr);
    if (it != implicitConversions_.end() && it->second == target)
        return;
    markImplicitConversion(expr, target);
}

/// @brief Analyse an array element access.
///
/// @details Delegates to @ref sem::analyzeArrayExpr which validates that the
///          referenced symbol is an array, ensures index expressions resolve to
///          integers, and emits warnings for constant indices that fall outside
///          known bounds.
///
/// @param a Array expression under analysis.
/// @return Semantic type of the accessed element or Unknown on error.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeArray(ArrayExpr &a) {
    return sem::analyzeArrayExpr(*this, a);
}

/// @brief Analyse an `LBOUND` expression returning the lower index bound.
///
/// @details Delegates to @ref sem::analyzeLBoundExpr which confirms the
///          referenced symbol is a known array and emits diagnostics otherwise.
///
/// @param expr LBOUND expression node.
/// @return Integer type on success or Unknown when diagnostics were emitted.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeLBound(LBoundExpr &expr) {
    return sem::analyzeLBoundExpr(*this, expr);
}

/// @brief Analyse a `UBOUND` expression returning the upper index bound.
///
/// @details Delegates to @ref sem::analyzeUBoundExpr which shares the same
///          validation steps as LBOUND analysis.
///
/// @param expr UBOUND expression node.
/// @return Integer type on success or Unknown when diagnostics were emitted.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeUBound(UBoundExpr &expr) {
    return sem::analyzeUBoundExpr(*this, expr);
}

/// @brief Visit an expression tree and compute its semantic type.
///
/// @param e Expression to analyse.
/// @return Semantic type determined by the visitor.
SemanticAnalyzer::Type SemanticAnalyzer::visitExpr(Expr &e) {
    SemanticAnalyzerExprVisitor visitor(*this);
    e.accept(visitor);
    return visitor.result();
}

} // namespace il::frontends::basic
