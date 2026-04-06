//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/zia/Lowerer_Decl.cpp
// Purpose: Declaration lowering dispatch and compile-time constant folding
//          for the Zia IL lowerer.
// Key invariants:
//   - lowerDecl dispatches to type-specific lowering methods
//   - tryFoldNumericConstant is a pure function (no side effects)
//   - registerAllFinalConstants must run before any expression lowering
// Ownership/Lifetime:
//   - Lowerer owns globalConstants_ map for compile-time constant storage
// Links: src/frontends/zia/Lowerer.hpp,
//        src/frontends/zia/Lowerer_Decl_Types.cpp,
//        src/frontends/zia/Lowerer_Decl_Functions.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "frontends/zia/ZiaLocationScope.hpp"

#include "il/core/Linkage.hpp"
#include <functional>

namespace il::frontends::zia {

using namespace runtime;

//=============================================================================
// Declaration Lowering
//=============================================================================

void Lowerer::lowerDecl(Decl *decl) {
    if (!decl)
        return;

    switch (decl->kind) {
        case DeclKind::Function:
            lowerFunctionDecl(*static_cast<FunctionDecl *>(decl));
            break;
        case DeclKind::Struct:
            lowerStructDecl(*static_cast<StructDecl *>(decl));
            break;
        case DeclKind::Class:
            lowerClassDecl(*static_cast<ClassDecl *>(decl));
            break;
        case DeclKind::Interface:
            lowerInterfaceDecl(*static_cast<InterfaceDecl *>(decl));
            break;
        case DeclKind::GlobalVar:
            lowerGlobalVarDecl(*static_cast<GlobalVarDecl *>(decl));
            break;
        case DeclKind::Namespace:
            lowerNamespaceDecl(*static_cast<NamespaceDecl *>(decl));
            break;
        case DeclKind::Enum:
            lowerEnumDecl(*static_cast<EnumDecl *>(decl));
            break;
        case DeclKind::TypeAlias:
            break; // Type aliases are resolved at sema time, no IL needed
        default:
            break;
    }
}

std::string Lowerer::qualifyName(const std::string &name) const {
    if (namespacePrefix_.empty())
        return name;
    return namespacePrefix_ + "." + name;
}

//=============================================================================
// Compile-Time Constant Folding Helper
//=============================================================================

/// @brief Try to evaluate an initializer expression to a compile-time constant.
/// @details Handles literals, constant references, enum variants, unary operators,
///          and pure arithmetic/logical expressions. Returns nullopt for any
///          expression that cannot be evaluated at compile time.
/// @note Fixes BUG-FE-011: non-literal final constant initializers (such as
///       `final X = 0 - 2147483647`) were previously silently dropped, causing
///       all references to resolve to constInt(0).
std::optional<il::core::Value> Lowerer::tryFoldNumericConstant(Expr *init) {
    if (!init)
        return std::nullopt;

    auto lookupConstant = [&](const std::string &name) -> std::optional<Value> {
        if (auto it = globalConstants_.find(name); it != globalConstants_.end())
            return it->second;

        std::string qualified = qualifyName(name);
        if (qualified != name) {
            if (auto it = globalConstants_.find(qualified); it != globalConstants_.end())
                return it->second;
        }
        return std::nullopt;
    };

    std::function<std::optional<Value>(Expr *)> fold = [&](Expr *expr) -> std::optional<Value> {
        if (!expr)
            return std::nullopt;

        if (auto *intLit = dynamic_cast<IntLiteralExpr *>(expr))
            return Value::constInt(intLit->value);
        if (auto *numLit = dynamic_cast<NumberLiteralExpr *>(expr))
            return Value::constFloat(numLit->value);
        if (auto *boolLit = dynamic_cast<BoolLiteralExpr *>(expr))
            return Value::constBool(boolLit->value);
        if (auto *strLit = dynamic_cast<StringLiteralExpr *>(expr))
            return Value::constStr(stringTable_.intern(strLit->value));
        if (auto *ident = dynamic_cast<IdentExpr *>(expr))
            return lookupConstant(ident->name);

        if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr)) {
            std::function<std::optional<std::string>(Expr *)> buildQualifiedName =
                [&](Expr *node) -> std::optional<std::string> {
                if (auto *ident = dynamic_cast<IdentExpr *>(node))
                    return ident->name;
                if (auto *field = dynamic_cast<FieldExpr *>(node)) {
                    auto base = buildQualifiedName(field->base.get());
                    if (base)
                        return *base + "." + field->field;
                }
                return std::nullopt;
            };

            if (auto qualified = buildQualifiedName(fieldExpr)) {
                if (auto enumIt = enumVariantValues_.find(*qualified);
                    enumIt != enumVariantValues_.end()) {
                    return Value::constInt(enumIt->second);
                }
                if (auto constIt = globalConstants_.find(*qualified);
                    constIt != globalConstants_.end()) {
                    return constIt->second;
                }
            }

            return lookupConstant(fieldExpr->field);
        }

        if (auto *unary = dynamic_cast<UnaryExpr *>(expr)) {
            auto inner = fold(unary->operand.get());
            if (!inner)
                return std::nullopt;

            switch (unary->op) {
                case UnaryOp::Neg:
                    if (inner->kind == Value::Kind::ConstInt)
                        return Value::constInt(-inner->i64);
                    if (inner->kind == Value::Kind::ConstFloat)
                        return Value::constFloat(-inner->f64);
                    break;
                case UnaryOp::Not:
                    if (inner->kind == Value::Kind::ConstInt && inner->isBool)
                        return Value::constBool(inner->i64 == 0);
                    break;
                case UnaryOp::BitNot:
                    if (inner->kind == Value::Kind::ConstInt && !inner->isBool)
                        return Value::constInt(~inner->i64);
                    break;
                default:
                    break;
            }
            return std::nullopt;
        }

        if (auto *binary = dynamic_cast<BinaryExpr *>(expr)) {
            auto lhs = fold(binary->left.get());
            auto rhs = fold(binary->right.get());
            if (!lhs || !rhs)
                return std::nullopt;

            const bool lhsInt = lhs->kind == Value::Kind::ConstInt && !lhs->isBool;
            const bool rhsInt = rhs->kind == Value::Kind::ConstInt && !rhs->isBool;
            const bool lhsBool = lhs->kind == Value::Kind::ConstInt && lhs->isBool;
            const bool rhsBool = rhs->kind == Value::Kind::ConstInt && rhs->isBool;
            const bool lhsFloat = lhs->kind == Value::Kind::ConstFloat;
            const bool rhsFloat = rhs->kind == Value::Kind::ConstFloat;

            if (lhsInt && rhsInt) {
                const int64_t l = lhs->i64;
                const int64_t r = rhs->i64;
                switch (binary->op) {
                    case BinaryOp::Add:
                        return Value::constInt(l + r);
                    case BinaryOp::Sub:
                        return Value::constInt(l - r);
                    case BinaryOp::Mul:
                        return Value::constInt(l * r);
                    case BinaryOp::Div:
                        return r == 0 ? std::nullopt : std::optional<Value>(Value::constInt(l / r));
                    case BinaryOp::Mod:
                        return r == 0 ? std::nullopt : std::optional<Value>(Value::constInt(l % r));
                    case BinaryOp::Eq:
                        return Value::constBool(l == r);
                    case BinaryOp::Ne:
                        return Value::constBool(l != r);
                    case BinaryOp::Lt:
                        return Value::constBool(l < r);
                    case BinaryOp::Le:
                        return Value::constBool(l <= r);
                    case BinaryOp::Gt:
                        return Value::constBool(l > r);
                    case BinaryOp::Ge:
                        return Value::constBool(l >= r);
                    case BinaryOp::BitAnd:
                        return Value::constInt(l & r);
                    case BinaryOp::BitOr:
                        return Value::constInt(l | r);
                    case BinaryOp::BitXor:
                        return Value::constInt(l ^ r);
                    case BinaryOp::Shl:
                        return Value::constInt(l << r);
                    case BinaryOp::Shr:
                        return Value::constInt(l >> r);
                    default:
                        break;
                }
            }

            if ((lhsInt || lhsFloat) && (rhsInt || rhsFloat)) {
                const double l = lhsFloat ? lhs->f64 : static_cast<double>(lhs->i64);
                const double r = rhsFloat ? rhs->f64 : static_cast<double>(rhs->i64);
                switch (binary->op) {
                    case BinaryOp::Add:
                        return Value::constFloat(l + r);
                    case BinaryOp::Sub:
                        return Value::constFloat(l - r);
                    case BinaryOp::Mul:
                        return Value::constFloat(l * r);
                    case BinaryOp::Div:
                        return r == 0.0 ? std::nullopt
                                        : std::optional<Value>(Value::constFloat(l / r));
                    case BinaryOp::Eq:
                        return Value::constBool(l == r);
                    case BinaryOp::Ne:
                        return Value::constBool(l != r);
                    case BinaryOp::Lt:
                        return Value::constBool(l < r);
                    case BinaryOp::Le:
                        return Value::constBool(l <= r);
                    case BinaryOp::Gt:
                        return Value::constBool(l > r);
                    case BinaryOp::Ge:
                        return Value::constBool(l >= r);
                    default:
                        break;
                }
            }

            if (lhsBool && rhsBool) {
                const bool l = lhs->i64 != 0;
                const bool r = rhs->i64 != 0;
                switch (binary->op) {
                    case BinaryOp::And:
                        return Value::constBool(l && r);
                    case BinaryOp::Or:
                        return Value::constBool(l || r);
                    case BinaryOp::Eq:
                        return Value::constBool(l == r);
                    case BinaryOp::Ne:
                        return Value::constBool(l != r);
                    default:
                        break;
                }
            }
        }

        return std::nullopt;
    };

    return fold(init);
}

//=============================================================================
// Final Constant Pre-Registration
//=============================================================================

void Lowerer::registerAllEnumValues(std::vector<DeclPtr> &declarations) {
    for (auto &decl : declarations) {
        if (decl->kind == DeclKind::Enum) {
            auto *enumDecl = static_cast<EnumDecl *>(decl.get());
            int64_t nextValue = 0;
            for (const auto &variant : enumDecl->variants) {
                std::string key = enumDecl->name + "." + variant.name;
                if (variant.explicitValue.has_value())
                    nextValue = *variant.explicitValue;
                enumVariantValues_[key] = nextValue;
                ++nextValue;
            }
        } else if (decl->kind == DeclKind::Namespace) {
            auto *ns = static_cast<NamespaceDecl *>(decl.get());
            registerAllEnumValues(ns->declarations);
        }
    }
}

void Lowerer::registerAllFinalConstants(std::vector<DeclPtr> &declarations) {
    struct PendingFinal {
        GlobalVarDecl *decl;
        std::string qualifiedName;
    };

    std::vector<PendingFinal> pending;
    std::function<void(std::vector<DeclPtr> &)> collectPending =
        [&](std::vector<DeclPtr> &decls) {
            for (auto &decl : decls) {
                if (decl->kind == DeclKind::GlobalVar) {
                    auto *gvar = static_cast<GlobalVarDecl *>(decl.get());
                    if (gvar->isFinal && gvar->initializer) {
                        pending.push_back({gvar, qualifyName(gvar->name)});
                    }
                    continue;
                }

                if (decl->kind != DeclKind::Namespace)
                    continue;

                auto *ns = static_cast<NamespaceDecl *>(decl.get());
                std::string savedPrefix = namespacePrefix_;
                if (namespacePrefix_.empty())
                    namespacePrefix_ = ns->name;
                else
                    namespacePrefix_ = namespacePrefix_ + "." + ns->name;

                collectPending(ns->declarations);
                namespacePrefix_ = savedPrefix;
            }
        };

    collectPending(declarations);

    bool madeProgress = true;
    while (madeProgress) {
        madeProgress = false;

        for (const auto &entry : pending) {
            if (globalConstants_.find(entry.qualifiedName) != globalConstants_.end())
                continue;

            if (auto folded = tryFoldNumericConstant(entry.decl->initializer.get())) {
                globalConstants_[entry.qualifiedName] = *folded;
                madeProgress = true;
            }
        }
    }

    for (const auto &entry : pending) {
        if (globalConstants_.find(entry.qualifiedName) != globalConstants_.end())
            continue;

        diag_.report({il::support::Severity::Error,
                      "'final' requires a compile-time constant initializer "
                      "(literal, arithmetic expression, or string). "
                      "Use a 'var' field initialized in init() for "
                      "runtime-computed values.",
                      entry.decl->loc,
                      "V3202"});
    }
}

//=============================================================================
// Namespace and Enum Declaration Lowering
//=============================================================================

void Lowerer::lowerNamespaceDecl(NamespaceDecl &decl) {
    ZiaLocationScope locScope(*this, decl.loc);

    // Save current namespace prefix
    std::string savedPrefix = namespacePrefix_;

    // Compute new prefix
    if (namespacePrefix_.empty())
        namespacePrefix_ = decl.name;
    else
        namespacePrefix_ = namespacePrefix_ + "." + decl.name;

    // Lower all declarations inside the namespace
    for (auto &innerDecl : decl.declarations) {
        lowerDecl(innerDecl.get());
    }

    // Restore previous prefix
    namespacePrefix_ = savedPrefix;
}

void Lowerer::lowerEnumDecl(EnumDecl &decl) {
    // Enums don't produce IL structures -- each variant is an I64 constant.
    // Just register variant values for later lookup during expression lowering.
    int64_t nextValue = 0;
    for (const auto &variant : decl.variants) {
        std::string key = qualifyName(decl.name) + "." + variant.name;
        if (variant.explicitValue.has_value())
            nextValue = *variant.explicitValue;
        enumVariantValues_[key] = nextValue;
        ++nextValue;
    }
}

} // namespace il::frontends::zia
