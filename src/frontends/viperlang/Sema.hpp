//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Sema.hpp
// Purpose: Semantic analyzer for ViperLang.
// Key invariants: Type checking and name resolution occur before IL lowering.
// Ownership/Lifetime: Sema borrows AST nodes; owns resolved type information.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/viperlang/AST.hpp"
#include "frontends/viperlang/Types.hpp"
#include "support/diagnostics.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace il::frontends::viperlang
{

/// @brief Information about a declared symbol.
struct Symbol
{
    enum class Kind
    {
        Variable,
        Parameter,
        Function,
        Method,
        Field,
        Type,
    };

    Kind kind;
    std::string name;
    TypeRef type;
    bool isFinal{false};
    Decl *decl{nullptr}; // Owning declaration (if any)
};

/// @brief Scope for symbol lookup.
class Scope
{
  public:
    explicit Scope(Scope *parent = nullptr) : parent_(parent) {}

    void define(const std::string &name, Symbol symbol);
    Symbol *lookup(const std::string &name);
    Symbol *lookupLocal(const std::string &name);

    Scope *parent() const
    {
        return parent_;
    }

  private:
    Scope *parent_{nullptr};
    std::unordered_map<std::string, Symbol> symbols_;
};

/// @brief Semantic analyzer for ViperLang.
/// @details Performs type checking, name resolution, and semantic validation.
class Sema
{
  public:
    /// @brief Create a semantic analyzer with the given diagnostic engine.
    explicit Sema(il::support::DiagnosticEngine &diag);

    /// @brief Analyze a module declaration.
    /// @return true if analysis succeeded without errors.
    bool analyze(ModuleDecl &module);

    /// @brief Get the resolved type for an expression.
    TypeRef typeOf(const Expr *expr) const;

    /// @brief Get the resolved type for a type node.
    TypeRef resolveType(const TypeNode *node) const;

    /// @brief Check if analysis produced errors.
    bool hasError() const
    {
        return hasError_;
    }

    /// @brief Get the current module being analyzed.
    ModuleDecl *currentModule() const
    {
        return currentModule_;
    }

    /// @brief Get the runtime function name for a call expression.
    /// @return The dotted name (e.g., "Viper.Terminal.Say") or empty if not a runtime call.
    std::string runtimeCallee(const CallExpr *expr) const
    {
        auto it = runtimeCallees_.find(expr);
        return it != runtimeCallees_.end() ? it->second : "";
    }

  private:
    //=========================================================================
    // Declaration Analysis
    //=========================================================================

    void analyzeImport(ImportDecl &decl);
    void analyzeGlobalVarDecl(GlobalVarDecl &decl);
    void analyzeValueDecl(ValueDecl &decl);
    void analyzeEntityDecl(EntityDecl &decl);
    void analyzeInterfaceDecl(InterfaceDecl &decl);
    void analyzeFunctionDecl(FunctionDecl &decl);
    void analyzeFieldDecl(FieldDecl &decl, TypeRef ownerType);
    void analyzeMethodDecl(MethodDecl &decl, TypeRef ownerType);

    //=========================================================================
    // Statement Analysis
    //=========================================================================

    void analyzeStmt(Stmt *stmt);
    void analyzeBlockStmt(BlockStmt *stmt);
    void analyzeVarStmt(VarStmt *stmt);
    void analyzeIfStmt(IfStmt *stmt);
    void analyzeWhileStmt(WhileStmt *stmt);
    void analyzeForStmt(ForStmt *stmt);
    void analyzeForInStmt(ForInStmt *stmt);
    void analyzeReturnStmt(ReturnStmt *stmt);
    void analyzeGuardStmt(GuardStmt *stmt);
    void analyzeMatchStmt(MatchStmt *stmt);

    //=========================================================================
    // Expression Analysis
    //=========================================================================

    TypeRef analyzeExpr(Expr *expr);
    TypeRef analyzeIntLiteral(IntLiteralExpr *expr);
    TypeRef analyzeNumberLiteral(NumberLiteralExpr *expr);
    TypeRef analyzeStringLiteral(StringLiteralExpr *expr);
    TypeRef analyzeBoolLiteral(BoolLiteralExpr *expr);
    TypeRef analyzeNullLiteral(NullLiteralExpr *expr);
    TypeRef analyzeUnitLiteral(UnitLiteralExpr *expr);
    TypeRef analyzeIdent(IdentExpr *expr);
    TypeRef analyzeSelf(SelfExpr *expr);
    TypeRef analyzeBinary(BinaryExpr *expr);
    TypeRef analyzeUnary(UnaryExpr *expr);
    TypeRef analyzeTernary(TernaryExpr *expr);
    TypeRef analyzeCall(CallExpr *expr);
    TypeRef analyzeIndex(IndexExpr *expr);
    TypeRef analyzeField(FieldExpr *expr);
    TypeRef analyzeOptionalChain(OptionalChainExpr *expr);
    TypeRef analyzeCoalesce(CoalesceExpr *expr);
    TypeRef analyzeIs(IsExpr *expr);
    TypeRef analyzeAs(AsExpr *expr);
    TypeRef analyzeRange(RangeExpr *expr);
    TypeRef analyzeNew(NewExpr *expr);
    TypeRef analyzeLambda(LambdaExpr *expr);
    TypeRef analyzeListLiteral(ListLiteralExpr *expr);
    TypeRef analyzeMapLiteral(MapLiteralExpr *expr);
    TypeRef analyzeSetLiteral(SetLiteralExpr *expr);

    //=========================================================================
    // Type Resolution
    //=========================================================================

    TypeRef resolveNamedType(const std::string &name) const;
    TypeRef resolveTypeNode(const TypeNode *node);

    //=========================================================================
    // Scope Management
    //=========================================================================

    void pushScope();
    void popScope();
    void defineSymbol(const std::string &name, Symbol symbol);
    Symbol *lookupSymbol(const std::string &name);

    //=========================================================================
    // Error Reporting
    //=========================================================================

    void error(SourceLoc loc, const std::string &message);
    void errorUndefined(SourceLoc loc, const std::string &name);
    void errorTypeMismatch(SourceLoc loc, TypeRef expected, TypeRef actual);

    //=========================================================================
    // Built-in Functions
    //=========================================================================

    void registerBuiltins();

    //=========================================================================
    // Member Variables
    //=========================================================================

    il::support::DiagnosticEngine &diag_;
    bool hasError_{false};

    ModuleDecl *currentModule_{nullptr};
    FunctionDecl *currentFunction_{nullptr};
    TypeRef currentSelfType_{nullptr};
    TypeRef expectedReturnType_{nullptr};

    std::unique_ptr<Scope> globalScope_;
    Scope *currentScope_{nullptr};

    // Type cache for expressions
    std::unordered_map<const Expr *, TypeRef> exprTypes_;

    // Registered types
    std::unordered_map<std::string, TypeRef> typeRegistry_;

    // Method return types: "TypeName.methodName" -> function type (for method calls)
    std::unordered_map<std::string, TypeRef> methodTypes_;

    // Runtime functions: dotted name -> return type
    std::unordered_map<std::string, TypeRef> runtimeFunctions_;

    // Resolved runtime callees: call expression -> dotted name
    std::unordered_map<const CallExpr *, std::string> runtimeCallees_;
};

} // namespace il::frontends::viperlang
