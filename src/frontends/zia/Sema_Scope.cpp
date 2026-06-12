//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Scope.cpp
/// @brief Lexical scope stack management, symbol lookup, visibility checks, flow-sensitive type
/// narrowing, and initialization-state helpers for Zia Sema.
///
/// @details This file was split out of Sema.cpp to keep semantic analysis
/// responsibilities navigable without changing the Sema public interface or
/// diagnostic behavior. Member functions remain declared in Sema.hpp.
///
/// @see frontends/zia/Sema.hpp
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"

#include <cassert>
#include <utility>

namespace il::frontends::zia {

namespace {

/// @brief Three-way compare two source locations by (file, line, column).
/// @param a Left source location.
/// @param b Right source location.
/// @return -1 if @p a precedes @p b, +1 if it follows, 0 if identical.
int compareLoc(const SourceLoc &a, const SourceLoc &b) {
    if (a.file_id != b.file_id)
        return (a.file_id < b.file_id) ? -1 : 1;
    if (a.line != b.line)
        return (a.line < b.line) ? -1 : 1;
    if (a.column != b.column)
        return (a.column < b.column) ? -1 : 1;
    return 0;
}

} // namespace

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

} // namespace il::frontends::zia
