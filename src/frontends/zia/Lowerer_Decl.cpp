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

namespace il::frontends::zia
{

using namespace runtime;

//=============================================================================
// Declaration Lowering
//=============================================================================

void Lowerer::lowerDecl(Decl *decl)
{
    if (!decl)
        return;

    switch (decl->kind)
    {
        case DeclKind::Function:
            lowerFunctionDecl(*static_cast<FunctionDecl *>(decl));
            break;
        case DeclKind::Value:
            lowerValueDecl(*static_cast<ValueDecl *>(decl));
            break;
        case DeclKind::Entity:
            lowerEntityDecl(*static_cast<EntityDecl *>(decl));
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
        default:
            break;
    }
}

std::string Lowerer::qualifyName(const std::string &name) const
{
    if (namespacePrefix_.empty())
        return name;
    return namespacePrefix_ + "." + name;
}

//=============================================================================
// Compile-Time Constant Folding Helper
//=============================================================================

/// @brief Try to evaluate an initializer expression to a compile-time constant.
/// @details Handles integer/float/bool literals, unary negation and bitwise NOT,
///          and binary arithmetic on integer or float literals. String literals
///          require the string-intern table and are handled at call sites.
///          Returns nullopt for any expression that cannot be evaluated at
///          compile time (e.g., function calls, identifier references).
/// @note Fixes BUG-FE-011: non-literal final constant initializers (such as
///       `final X = 0 - 2147483647`) were previously silently dropped, causing
///       all references to resolve to constInt(0).
std::optional<il::core::Value> Lowerer::tryFoldNumericConstant(Expr *init)
{
    if (!init)
        return std::nullopt;

    if (auto *intLit = dynamic_cast<IntLiteralExpr *>(init))
        return Value::constInt(intLit->value);
    if (auto *numLit = dynamic_cast<NumberLiteralExpr *>(init))
        return Value::constFloat(numLit->value);
    if (auto *boolLit = dynamic_cast<BoolLiteralExpr *>(init))
        return Value::constBool(boolLit->value);

    if (auto *unary = dynamic_cast<UnaryExpr *>(init))
    {
        auto inner = tryFoldNumericConstant(unary->operand.get());
        if (inner)
        {
            if (unary->op == UnaryOp::Neg)
            {
                if (inner->kind == Value::Kind::ConstInt)
                    return Value::constInt(-inner->i64);
                if (inner->kind == Value::Kind::ConstFloat)
                    return Value::constFloat(-inner->f64);
            }
            if (unary->op == UnaryOp::BitNot && inner->kind == Value::Kind::ConstInt)
                return Value::constInt(~inner->i64);
        }
    }

    if (auto *binary = dynamic_cast<BinaryExpr *>(init))
    {
        auto lv = tryFoldNumericConstant(binary->left.get());
        auto rv = tryFoldNumericConstant(binary->right.get());

        // Integer x integer
        if (lv && rv && lv->kind == Value::Kind::ConstInt && rv->kind == Value::Kind::ConstInt)
        {
            long long l = lv->i64, r = rv->i64;
            switch (binary->op)
            {
                case BinaryOp::Add:
                    return Value::constInt(l + r);
                case BinaryOp::Sub:
                    return Value::constInt(l - r);
                case BinaryOp::Mul:
                    return Value::constInt(l * r);
                case BinaryOp::BitAnd:
                    return Value::constInt(l & r);
                case BinaryOp::BitOr:
                    return Value::constInt(l | r);
                case BinaryOp::BitXor:
                    return Value::constInt(l ^ r);
                default:
                    break;
            }
        }

        // Float x float
        if (lv && rv && lv->kind == Value::Kind::ConstFloat && rv->kind == Value::Kind::ConstFloat)
        {
            double l = lv->f64, r = rv->f64;
            switch (binary->op)
            {
                case BinaryOp::Add:
                    return Value::constFloat(l + r);
                case BinaryOp::Sub:
                    return Value::constFloat(l - r);
                case BinaryOp::Mul:
                    return Value::constFloat(l * r);
                default:
                    break;
            }
        }
    }

    return std::nullopt;
}

//=============================================================================
// Final Constant Pre-Registration
//=============================================================================

void Lowerer::registerAllFinalConstants(std::vector<DeclPtr> &declarations)
{
    for (auto &decl : declarations)
    {
        if (decl->kind == DeclKind::GlobalVar)
        {
            auto *gvar = static_cast<GlobalVarDecl *>(decl.get());
            if (gvar->isFinal && gvar->initializer)
            {
                std::string qualifiedName = qualifyName(gvar->name);
                // Skip if already registered
                if (globalConstants_.find(qualifiedName) != globalConstants_.end())
                    continue;

                Expr *init = gvar->initializer.get();

                if (auto *intLit = dynamic_cast<IntLiteralExpr *>(init))
                    globalConstants_[qualifiedName] = Value::constInt(intLit->value);
                else if (auto *numLit = dynamic_cast<NumberLiteralExpr *>(init))
                    globalConstants_[qualifiedName] = Value::constFloat(numLit->value);
                else if (auto *boolLit = dynamic_cast<BoolLiteralExpr *>(init))
                    globalConstants_[qualifiedName] = Value::constBool(boolLit->value);
                else if (auto *strLit = dynamic_cast<StringLiteralExpr *>(init))
                {
                    std::string label = stringTable_.intern(strLit->value);
                    globalConstants_[qualifiedName] = Value::constStr(label);
                }
                // Fold constant-expression initializers (e.g. `0 - 2147483647`,
                // `-1`, `2 * 1024`) that are not direct literals (BUG-FE-011).
                else if (auto folded = tryFoldNumericConstant(init))
                    globalConstants_[qualifiedName] = *folded;
            }
        }
        else if (decl->kind == DeclKind::Namespace)
        {
            auto *ns = static_cast<NamespaceDecl *>(decl.get());
            std::string savedPrefix = namespacePrefix_;
            if (namespacePrefix_.empty())
                namespacePrefix_ = ns->name;
            else
                namespacePrefix_ = namespacePrefix_ + "." + ns->name;

            registerAllFinalConstants(ns->declarations);

            namespacePrefix_ = savedPrefix;
        }
    }
}

//=============================================================================
// Namespace and Enum Declaration Lowering
//=============================================================================

void Lowerer::lowerNamespaceDecl(NamespaceDecl &decl)
{
    ZiaLocationScope locScope(*this, decl.loc);

    // Save current namespace prefix
    std::string savedPrefix = namespacePrefix_;

    // Compute new prefix
    if (namespacePrefix_.empty())
        namespacePrefix_ = decl.name;
    else
        namespacePrefix_ = namespacePrefix_ + "." + decl.name;

    // Lower all declarations inside the namespace
    for (auto &innerDecl : decl.declarations)
    {
        lowerDecl(innerDecl.get());
    }

    // Restore previous prefix
    namespacePrefix_ = savedPrefix;
}

void Lowerer::lowerEnumDecl(EnumDecl &decl)
{
    // Enums don't produce IL structures -- each variant is an I64 constant.
    // Just register variant values for later lookup during expression lowering.
    for (const auto &variant : decl.variants)
    {
        std::string key = qualifyName(decl.name) + "." + variant.name;
        enumVariantValues_[key] = variant.explicitValue.value_or(0);
    }
}

} // namespace il::frontends::zia
