//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/SemanticAnalyzer.Exprs.cpp
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
#include "frontends/basic/SemanticAnalyzer.Internal.hpp"
#include "frontends/basic/IdentifierUtil.hpp"

#include <limits>

namespace il::frontends::basic::semantic_analyzer_detail
{

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic
{

using semantic_analyzer_detail::astToSemanticType;
using semantic_analyzer_detail::levenshtein;
using semantic_analyzer_detail::semanticTypeName;

/// @brief Visitor that routes AST expression nodes through SemanticAnalyzer helpers.
///
/// @details Each override forwards to the corresponding SemanticAnalyzer method
///          or returns an immediate type for literals.  The visitor stores the
///          resulting semantic type so callers can retrieve it after walking an
///          expression tree.
class SemanticAnalyzerExprVisitor final : public MutExprVisitor
{
  public:
    /// @brief Create a visitor bound to @p analyzer.
    explicit SemanticAnalyzerExprVisitor(SemanticAnalyzer &analyzer) noexcept : analyzer_(analyzer)
    {
    }

    /// @brief Literal integers yield the integer semantic type.
    void visit(IntExpr &) override
    {
        result_ = SemanticAnalyzer::Type::Int;
    }

    /// @brief Literal floats evaluate to floating-point semantic type.
    void visit(FloatExpr &) override
    {
        result_ = SemanticAnalyzer::Type::Float;
    }

    /// @brief Literal strings evaluate to string semantic type.
    void visit(StringExpr &) override
    {
        result_ = SemanticAnalyzer::Type::String;
    }

    /// @brief Boolean literals propagate the boolean semantic type.
    void visit(BoolExpr &) override
    {
        result_ = SemanticAnalyzer::Type::Bool;
    }

    /// @brief Variables defer to SemanticAnalyzer for resolution.
    void visit(VarExpr &expr) override
    {
        result_ = analyzer_.analyzeVar(expr);
    }

    /// @brief Array expressions trigger array-specific analysis.
    void visit(ArrayExpr &expr) override
    {
        result_ = analyzer_.analyzeArray(expr);
    }

    /// @brief Unary expressions are analysed via SemanticAnalyzer helpers.
    void visit(UnaryExpr &expr) override
    {
        result_ = analyzer_.analyzeUnary(expr);
    }

    /// @brief Binary expressions defer to SemanticAnalyzer::analyzeBinary.
    void visit(BinaryExpr &expr) override
    {
        result_ = analyzer_.analyzeBinary(expr);
    }

    /// @brief Builtin calls delegate to dedicated builtin analysis.
    void visit(BuiltinCallExpr &expr) override
    {
        result_ = analyzer_.analyzeBuiltinCall(expr);
    }

    /// @brief LBOUND expressions compute integer results via analyser logic.
    void visit(LBoundExpr &expr) override
    {
        result_ = analyzer_.analyzeLBound(expr);
    }

    /// @brief UBOUND expressions compute integer results via analyser logic.
    void visit(UBoundExpr &expr) override
    {
        result_ = analyzer_.analyzeUBound(expr);
    }

    /// @brief Procedure calls re-use general call analysis.
    void visit(CallExpr &expr) override
    {
        result_ = analyzer_.analyzeCall(expr);
    }

    /// @brief NEW expressions analyse constructor signatures before returning Unknown.
    void visit(NewExpr &expr) override
    {
        result_ = analyzer_.analyzeNew(expr);
    }

    /// @brief ME references are currently untyped placeholders.
    void visit(MeExpr &) override
    {
        result_ = SemanticAnalyzer::Type::Unknown;
    }

    /// @brief Member access expressions remain Unknown until OOP analysis matures.
    void visit(MemberAccessExpr &) override
    {
        result_ = SemanticAnalyzer::Type::Unknown;
    }

    /// @brief Method calls are treated as Unknown until OOP semantics are added.
    void visit(MethodCallExpr &) override
    {
        result_ = SemanticAnalyzer::Type::Unknown;
    }

    /// @brief IS expressions evaluate to boolean; validate operands and type name.
    void visit(IsExpr &expr) override
    {
        // Check left operand type; reject obvious primitives.
        SemanticAnalyzer::Type lhsType = analyzer_.visitExpr(*expr.value);
        auto isPrimitive = [&](SemanticAnalyzer::Type t)
        {
            using T = SemanticAnalyzer::Type;
            return t == T::Int || t == T::Float || t == T::Bool || t == T::String ||
                   t == T::ArrayInt;
        };

        // Resolve right-hand dotted type to class or interface.
        auto existsQ = [&](const std::string &q) -> bool
        {
            if (analyzer_.oopIndex_.interfacesByQname().count(q))
                return true;
            return analyzer_.oopIndex_.findClass(q) != nullptr;
        };

        std::string dotted;
        if (!expr.typeName.empty())
        {
            if (expr.typeName.size() == 1)
            {
                // Unqualified: resolve with parent-walk then imports.
                std::string ident = Canon(expr.typeName[0]);
                std::vector<std::string> prefix;
                for (const auto &seg : analyzer_.nsStack_)
                    prefix.push_back(Canon(seg));
                std::vector<std::string> hits;
                for (std::size_t n = prefix.size(); n > 0; --n)
                {
                    std::vector<std::string> parts(prefix.begin(), prefix.begin() + static_cast<std::ptrdiff_t>(n));
                    parts.push_back(ident);
                    std::string q = JoinDots(parts);
                    if (existsQ(q))
                        hits.push_back(q);
                }
                if (existsQ(ident))
                    hits.push_back(ident);
                if (hits.size() == 1)
                    dotted = hits[0];
                else if (hits.empty())
                {
                    if (!analyzer_.usingStack_.empty())
                    {
                        const auto &cur = analyzer_.usingStack_.back();
                        for (const auto &ns : cur.imports)
                        {
                            std::string q = ns + "." + ident;
                            if (existsQ(q))
                                hits.push_back(q);
                        }
                    }
                    if (hits.size() == 1)
                        dotted = hits[0];
                }
                if (dotted.empty())
                {
                    // Default to single ident (unknown handling occurs below)
                    dotted = ident;
                }
            }
            else
            {
                // Qualified: join as-is
                for (size_t i = 0; i < expr.typeName.size(); ++i)
                {
                    if (i)
                        dotted.push_back('.');
                    dotted += expr.typeName[i];
                }
            }
        }
        bool resolved = existsQ(dotted);

        if (!resolved)
        {
            std::string msg = std::string("unknown type '") + dotted + "'";
            analyzer_.de.emit(il::support::Severity::Error,
                              "B2111",
                              expr.loc,
                              static_cast<uint32_t>(dotted.size()),
                              std::move(msg));
        }
        else if (isPrimitive(lhsType))
        {
            analyzer_.de.emit(il::support::Severity::Error,
                              "B2121",
                              expr.loc,
                              2,
                              "'IS' requires an object/interface value on the left");
        }

        result_ = SemanticAnalyzer::Type::Bool;
    }

    /// @brief AS expressions yield the inner value's type; validate operands and type name.
    void visit(AsExpr &expr) override
    {
        // Preserve operand type; runtime returns NULL on failure.
        SemanticAnalyzer::Type lhsType = analyzer_.visitExpr(*expr.value);
        auto isPrimitive = [&](SemanticAnalyzer::Type t)
        {
            using T = SemanticAnalyzer::Type;
            return t == T::Int || t == T::Float || t == T::Bool || t == T::String ||
                   t == T::ArrayInt;
        };

        std::string dotted;
        if (!expr.typeName.empty())
        {
            if (expr.typeName.size() == 1)
            {
                std::string ident = Canon(expr.typeName[0]);
                std::vector<std::string> prefix;
                for (const auto &seg : analyzer_.nsStack_)
                    prefix.push_back(Canon(seg));
                std::vector<std::string> hits;
                auto existsQ = [&](const std::string &q) -> bool
                {
                    if (analyzer_.oopIndex_.interfacesByQname().count(q))
                        return true;
                    return analyzer_.oopIndex_.findClass(q) != nullptr;
                };
                for (std::size_t n = prefix.size(); n > 0; --n)
                {
                    std::vector<std::string> parts(prefix.begin(), prefix.begin() + static_cast<std::ptrdiff_t>(n));
                    parts.push_back(ident);
                    std::string q = JoinDots(parts);
                    if (existsQ(q))
                        hits.push_back(q);
                }
                if (existsQ(ident))
                    hits.push_back(ident);
                if (hits.size() == 1)
                    dotted = hits[0];
                else if (hits.empty())
                {
                    if (!analyzer_.usingStack_.empty())
                    {
                        const auto &cur = analyzer_.usingStack_.back();
                        for (const auto &ns : cur.imports)
                        {
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
            }
            else
            {
                // Qualified: apply alias expansion to the first segment, if present.
                std::vector<std::string> segs = expr.typeName;
                if (!analyzer_.usingStack_.empty() && !segs.empty())
                {
                    std::string firstCanon = Canon(segs[0]);
                    const auto &aliases = analyzer_.usingStack_.back().aliases;
                    auto it = aliases.find(firstCanon);
                    if (it != aliases.end())
                    {
                        std::vector<std::string> expanded = SplitDots(it->second);
                        expanded.insert(expanded.end(), segs.begin() + 1, segs.end());
                        segs = std::move(expanded);
                    }
                }
                for (size_t i = 0; i < segs.size(); ++i)
                {
                    if (i)
                        dotted.push_back('.');
                    dotted += segs[i];
                }
            }
        }
        bool resolved = false;
        if (analyzer_.oopIndex_.interfacesByQname().count(dotted))
            resolved = true;
        if (!resolved)
        {
            for (const auto &entry : analyzer_.oopIndex_.classes())
            {
                if (entry.second.qualifiedName == dotted)
                {
                    resolved = true;
                    break;
                }
            }
        }

        if (!resolved)
        {
            std::string msg = std::string("unknown type '") + dotted + "'";
            analyzer_.de.emit(il::support::Severity::Error,
                              "B2111",
                              expr.loc,
                              static_cast<uint32_t>(dotted.size()),
                              std::move(msg));
        }
        else if (isPrimitive(lhsType))
        {
            // E_CAST_INVALID: cannot cast value of type '{From}' to '{To}'.
            std::string from = std::string(semantic_analyzer_detail::semanticTypeName(lhsType));
            std::string to = dotted;
            std::string msg = "cannot cast value of type '" + from + "' to '" + to + "'.";
            analyzer_.de.emit(
                il::support::Severity::Error, "E_CAST_INVALID", expr.loc, 2, std::move(msg));
        }

        result_ = lhsType;
    }

    /// @brief Retrieve the semantic type computed during visitation.
    [[nodiscard]] SemanticAnalyzer::Type result() const noexcept
    {
        return result_;
    }

  private:
    SemanticAnalyzer &analyzer_;
    SemanticAnalyzer::Type result_{SemanticAnalyzer::Type::Unknown};
};

/// @brief Resolve a variable reference and compute its semantic type.
///
/// @details Tracks the symbol for later use, suggests corrections via
///          Levenshtein distance when unresolved, and applies BASIC suffix rules
///          when no explicit declaration is available.  Diagnostics are emitted
///          for unknown variables.
///
/// @param v Variable expression under analysis.
/// @return Semantic type inferred for the variable.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeVar(VarExpr &v)
{
    resolveAndTrackSymbol(v.name, SymbolKind::Reference);
    if (!symbols_.count(v.name))
    {
        std::string best;
        size_t bestDist = std::numeric_limits<size_t>::max();
        for (const auto &s : symbols_)
        {
            size_t d = levenshtein(v.name, s);
            if (d < bestDist)
            {
                bestDist = d;
                best = s;
            }
        }
        std::string suggestion;
        if (!best.empty())
        {
            suggestion = "; did you mean '" + best + "'?";
        }
        de.emit(
            diag::BasicDiag::UnknownVariable,
            v.loc,
            static_cast<uint32_t>(v.name.size()),
            std::initializer_list<diag::Replacement>{diag::Replacement{"name", v.name},
                                                     diag::Replacement{"suggestion", suggestion}});
        return Type::Unknown;
    }
    auto it = varTypes_.find(v.name);
    if (it != varTypes_.end())
        return it->second;
    if (!v.name.empty())
    {
        // BASIC suffix rules provide implicit types for undeclared variables:
        // '$' for STRING and '#'/'!' for floating point. Tracking this here keeps
        // later passes aligned with language defaults even when declarations are
        // omitted in source.
        if (v.name.back() == '$')
            return Type::String;
        if (v.name.back() == '#' || v.name.back() == '!')
            return Type::Float;
    }
    return Type::Int;
}

/// @brief Analyse a unary expression using helper utilities.
///
/// @param u Unary expression AST node.
/// @return Semantic type computed by @ref sem::analyzeUnaryExpr.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeUnary(const UnaryExpr &u)
{
    return sem::analyzeUnaryExpr(*this, u);
}

/// @brief Analyse a binary expression using helper utilities.
///
/// @param b Binary expression AST node.
/// @return Semantic type computed by @ref sem::analyzeBinaryExpr.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeBinary(const BinaryExpr &b)
{
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
SemanticAnalyzer::Type SemanticAnalyzer::analyzeNew(NewExpr &expr)
{
    // Apply alias expansion for qualified type, if present.
    if (!expr.qualifiedType.empty() && !usingStack_.empty())
    {
        std::string firstCanon = Canon(expr.qualifiedType[0]);
        const auto &aliases = usingStack_.back().aliases;
        auto it = aliases.find(firstCanon);
        if (it != aliases.end())
        {
            std::vector<std::string> expanded = SplitDots(it->second);
            expanded.insert(expanded.end(), expr.qualifiedType.begin() + 1, expr.qualifiedType.end());
            // Rebuild className from canonicalized expanded segments.
            expr.className = CanonJoin(expanded);
        }
    }

    // If unqualified type, resolve using parent-walk then USING imports.
    if (expr.qualifiedType.empty())
    {
        std::vector<std::string> attempts;
        auto existsQ = [&](const std::string &q) -> bool
        {
            if (oopIndex_.interfacesByQname().count(q))
                return true;
            return oopIndex_.findClass(q) != nullptr;
        };

        std::string ident = Canon(expr.className);
        // Parent-walk: A.B.Point -> A.Point -> Point
        std::vector<std::string> prefixCanon;
        prefixCanon.reserve(nsStack_.size());
        for (const auto &seg : nsStack_)
        {
            std::string cseg = Canon(seg);
            prefixCanon.push_back(cseg.empty() ? seg : cseg);
        }
        std::vector<std::string> hits;
        for (std::size_t n = prefixCanon.size(); n > 0; --n)
        {
            std::vector<std::string> parts(prefixCanon.begin(), prefixCanon.begin() + static_cast<std::ptrdiff_t>(n));
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

        auto reportAmbiguous = [&](const std::vector<std::string> &cands)
        {
            std::vector<std::string> sorted = cands;
            std::sort(sorted.begin(), sorted.end());
            std::string msg = std::string("ambiguous type '") + expr.className + "' (candidates: ";
            for (size_t i = 0; i < sorted.size(); ++i)
            {
                if (i)
                    msg += ", ";
                msg += sorted[i];
            }
            msg += ")";
            de.emit(il::support::Severity::Error, "B2110", expr.loc, static_cast<uint32_t>(expr.className.size()), std::move(msg));
        };

        if (hits.size() == 1)
        {
            expr.className = hits[0];
        }
        else if (hits.size() > 1)
        {
            reportAmbiguous(hits);
            return Type::Unknown;
        }
        else
        {
            // Try USING imports
            std::vector<std::string> importHits;
            if (!usingStack_.empty())
            {
                const UsingScope &cur = usingStack_.back();
                for (const auto &ns : cur.imports)
                {
                    std::string q = ns;
                    if (!q.empty())
                        q.push_back('.');
                    q += ident;
                    attempts.push_back(q);
                    if (existsQ(q))
                        importHits.push_back(q);
                }
            }
            if (importHits.size() == 1)
            {
                expr.className = importHits[0];
            }
            else if (importHits.size() > 1)
            {
                reportAmbiguous(importHits);
                return Type::Unknown;
            }
            else
            {
                // Unknown type with tried list
                std::string msg = std::string("unknown type '") + expr.className + "'";
                if (!attempts.empty())
                {
                    msg += " (tried: ";
                    size_t limit = std::min<std::size_t>(attempts.size(), 8);
                    for (size_t i = 0; i < limit; ++i)
                    {
                        if (i)
                            msg += ", ";
                        msg += attempts[i];
                    }
                    if (attempts.size() > limit)
                    {
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

    const ClassInfo *klass = oopIndex_.findClass(expr.className);
    if (!klass)
        return Type::Unknown;

    // Instantiation of abstract classes is not allowed.
    if (klass->isAbstract)
    {
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
    if (expr.args.size() != expectedCount)
    {
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

    for (std::size_t i = 0; i < expectedCount; ++i)
    {
        const auto &param = klass->ctorParams[i];
        const Expr *argExpr = expr.args[i].get();
        Type argTy = argTypes[i];

        if (param.isArray)
        {
            const auto *var = as<const VarExpr>(*argExpr);
            if (!var || !arrays_.count(var->name))
            {
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
        if (argTy != Type::Unknown && argTy != want)
        {
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
void SemanticAnalyzer::markImplicitConversion(const Expr &expr, Type targetType)
{
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
void SemanticAnalyzer::insertImplicitCast(Expr &expr, Type target)
{
    auto it = implicitConversions_.find(&expr);
    if (it != implicitConversions_.end() && it->second == target)
        return;
    markImplicitConversion(expr, target);
}

/// @brief Analyse an array element access.
///
/// @details Validates that the referenced symbol is an array, ensures the index
///          expression resolves to an integer, and emits warnings for constant
///          indices that fall outside known bounds.
///
/// @param a Array expression under analysis.
/// @return Semantic type of the accessed element or Unknown on error.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeArray(ArrayExpr &a)
{
    resolveAndTrackSymbol(a.name, SymbolKind::Reference);
    if (!arrays_.count(a.name))
    {
        de.emit(diag::BasicDiag::UnknownArray,
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", a.name}});
        // Visit all indices for type checking even on error path
        // For backward compatibility, check deprecated 'index' field first
        if (a.index)
            visitExpr(*a.index);
        for (auto &indexPtr : a.indices)
        {
            if (indexPtr)
                visitExpr(*indexPtr);
        }
        return Type::Unknown;
    }
    if (auto itType = varTypes_.find(a.name); itType != varTypes_.end() &&
                                              itType->second != Type::ArrayInt &&
                                              itType->second != Type::ArrayString)
    {
        de.emit(diag::BasicDiag::NotAnArray,
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", a.name}});
        // Visit all indices for type checking even on error path
        // For backward compatibility, check deprecated 'index' field first
        if (a.index)
            visitExpr(*a.index);
        for (auto &indexPtr : a.indices)
        {
            if (indexPtr)
                visitExpr(*indexPtr);
        }
        return Type::Unknown;
    }

    // Validate each index expression (supports multi-dimensional arrays)
    // For backward compatibility: use 'index' for single-dim, 'indices' for multi-dim
    if (a.index)
    {
        // Single-dimensional array (backward compatible path)
        Type ty = visitExpr(*a.index);
        if (ty == Type::Float)
        {
            if (auto *floatLiteral = as<FloatExpr>(*a.index))
            {
                insertImplicitCast(*a.index, Type::Int);
                std::string msg = "narrowing conversion from FLOAT to INT in array index";
                de.emit(il::support::Severity::Warning, "B2002", a.loc, 1, std::move(msg));
            }
            else
            {
                std::string msg = "index type mismatch";
                de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
            }
        }
        else if (ty != Type::Unknown && ty != Type::Int)
        {
            std::string msg = "index type mismatch";
            de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
        }

        // Bounds check for single-dimensional arrays
        auto it = arrays_.find(a.name);
        if (it != arrays_.end() && !it->second.extents.empty() && it->second.extents.size() == 1)
        {
            long long arraySize = it->second.extents[0];
            if (arraySize >= 0)
            {
                if (auto *ci = as<const IntExpr>(*a.index))
                {
                    if (ci->value < 0 || ci->value >= arraySize)
                    {
                        std::string msg = "index out of bounds";
                        de.emit(il::support::Severity::Warning, "B3001", a.loc, 1, std::move(msg));
                    }
                }
            }
        }
    }
    else
    {
        // Multi-dimensional array (new path)
        for (auto &indexPtr : a.indices)
        {
            if (!indexPtr)
                continue;
            Type ty = visitExpr(*indexPtr);
            if (ty == Type::Float)
            {
                if (auto *floatLiteral = as<FloatExpr>(*indexPtr))
                {
                    insertImplicitCast(*indexPtr, Type::Int);
                    std::string msg = "narrowing conversion from FLOAT to INT in array index";
                    de.emit(il::support::Severity::Warning, "B2002", a.loc, 1, std::move(msg));
                }
                else
                {
                    std::string msg = "index type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
                }
            }
            else if (ty != Type::Unknown && ty != Type::Int)
            {
                std::string msg = "index type mismatch";
                de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
            }
        }
        // TODO: Bounds check for multi-dimensional arrays
    }

    // Return element type based on array type
    auto itType = varTypes_.find(a.name);
    if (itType != varTypes_.end() && itType->second == Type::ArrayString)
        return Type::String;

    return Type::Int;
}

/// @brief Analyse an `LBOUND` expression returning the lower index bound.
///
/// @details Confirms the referenced symbol is a known array and emits
///          diagnostics otherwise.
///
/// @param expr LBOUND expression node.
/// @return Integer type on success or Unknown when diagnostics were emitted.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeLBound(LBoundExpr &expr)
{
    resolveAndTrackSymbol(expr.name, SymbolKind::Reference);
    if (!arrays_.count(expr.name))
    {
        de.emit(diag::BasicDiag::UnknownArray,
                expr.loc,
                static_cast<uint32_t>(expr.name.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name}});
        return Type::Unknown;
    }
    if (auto itType = varTypes_.find(expr.name);
        itType != varTypes_.end() && itType->second != Type::ArrayInt)
    {
        de.emit(diag::BasicDiag::NotAnArray,
                expr.loc,
                static_cast<uint32_t>(expr.name.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name}});
        return Type::Unknown;
    }
    return Type::Int;
}

/// @brief Analyse a `UBOUND` expression returning the upper index bound.
///
/// @details Shares the same validation steps as @ref analyzeLBound.
///
/// @param expr UBOUND expression node.
/// @return Integer type on success or Unknown when diagnostics were emitted.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeUBound(UBoundExpr &expr)
{
    resolveAndTrackSymbol(expr.name, SymbolKind::Reference);
    if (!arrays_.count(expr.name))
    {
        de.emit(diag::BasicDiag::UnknownArray,
                expr.loc,
                static_cast<uint32_t>(expr.name.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name}});
        return Type::Unknown;
    }
    if (auto itType = varTypes_.find(expr.name);
        itType != varTypes_.end() && itType->second != Type::ArrayInt)
    {
        de.emit(diag::BasicDiag::NotAnArray,
                expr.loc,
                static_cast<uint32_t>(expr.name.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name}});
        return Type::Unknown;
    }
    return Type::Int;
}

/// @brief Visit an expression tree and compute its semantic type.
///
/// @param e Expression to analyse.
/// @return Semantic type determined by the visitor.
SemanticAnalyzer::Type SemanticAnalyzer::visitExpr(Expr &e)
{
    SemanticAnalyzerExprVisitor visitor(*this);
    e.accept(visitor);
    return visitor.result();
}

} // namespace il::frontends::basic
