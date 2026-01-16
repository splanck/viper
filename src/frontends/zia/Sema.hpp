//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema.hpp
/// @brief Semantic analyzer for the Zia programming language.
///
/// @details The semantic analyzer performs type checking and name resolution
/// on the AST produced by the parser. It transforms raw AST nodes into a
/// semantically valid representation with resolved types and symbols.
///
/// ## Semantic Analysis Phases
///
/// The analyzer performs several passes over the AST:
///
/// **Phase 1: Type Registration**
/// - Registers all type declarations (value, entity, interface)
/// - Builds the type hierarchy (inheritance, interface implementation)
/// - Creates entries in the type registry
///
/// **Phase 2: Declaration Analysis**
/// - Analyzes global variable declarations
/// - Analyzes function declarations (signatures)
/// - Analyzes type members (fields and methods)
///
/// **Phase 3: Body Analysis**
/// - Type-checks function and method bodies
/// - Validates statements and expressions
/// - Ensures return types match declarations
///
/// ## Type System Features
///
/// The analyzer handles:
/// - Primitive types: Integer, Number, Boolean, String, Byte
/// - User-defined types: value types, entity types, interfaces
/// - Generic types: List[T], Map[K,V], Result[T]
/// - Optional types: T? with null safety checks
/// - Function types: (A, B) -> C for closures and references
///
/// ## Symbol Resolution
///
/// Symbols are resolved in nested scopes:
/// 1. Local variables in current block
/// 2. Parameters of enclosing function
/// 3. Fields/methods of enclosing type (via self)
/// 4. Module-level functions and global variables
/// 5. Built-in runtime functions
///
/// ## Error Reporting
///
/// The analyzer reports errors for:
/// - Undefined names and types
/// - Type mismatches in expressions and assignments
/// - Invalid operations (wrong types for operators)
/// - Missing or type-mismatched return statements
/// - Invalid assignments (to immutable variables)
///
/// ## Usage Example
///
/// ```cpp
/// DiagnosticEngine diag;
/// Lexer lexer(source, fileId, diag);
/// Parser parser(lexer, diag);
/// auto module = parser.parseModule();
///
/// Sema sema(diag);
/// bool success = sema.analyze(*module);
///
/// if (success) {
///     // Use sema.typeOf() to get expression types
///     // Use sema.runtimeCallee() for runtime function resolution
/// }
/// ```
///
/// @invariant Type information is immutable after analysis.
/// @invariant All expressions have associated type information after analysis.
/// @invariant Symbol table correctly reflects scope nesting.
///
/// @see AST.hpp - AST node types
/// @see Types.hpp - Semantic type representation
/// @see Lowerer.hpp - Consumes analyzed AST for code generation
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/AST.hpp"
#include "frontends/zia/Types.hpp"
#include "support/diagnostics.hpp"
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::zia
{

//===----------------------------------------------------------------------===//
/// @name Symbol Information
/// @brief Structure for tracking declared symbols.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Information about a declared symbol (variable, function, type, etc.).
/// @details Represents any named entity that can be looked up in a scope.
/// Used during semantic analysis to track declarations and their types.
///
/// ## Symbol Categories
///
/// - **Variable**: Local or global variable, can be read/written based on isFinal
/// - **Parameter**: Read-only function/method parameter
/// - **Function**: Global function that can be called
/// - **Method**: Method on a type that can be called on an object
/// - **Field**: Field in a type that can be accessed on an object
/// - **Type**: Type declaration (value, entity, interface)
struct Symbol
{
    /// @brief The kind of symbol.
    /// @details Determines how the symbol can be used in expressions.
    enum class Kind
    {
        Variable,  ///< Local or global variable
        Parameter, ///< Function/method parameter
        Function,  ///< Global function declaration
        Method,    ///< Method in a type declaration
        Field,     ///< Field in a type declaration
        Type,      ///< Type declaration (value, entity, interface)
        Module,    ///< Imported module namespace
    };

    /// @brief The symbol kind.
    Kind kind;

    /// @brief The symbol name as declared.
    std::string name;

    /// @brief The resolved semantic type of this symbol.
    /// @details For functions/methods, this is the function type.
    /// For types, this is the type itself (e.g., entity("MyClass")).
    TypeRef type;

    /// @brief True if this symbol is immutable (declared with `final`).
    /// @details Only meaningful for Variable and Field kinds.
    bool isFinal{false};

    /// @brief True if this is an external/runtime function.
    /// @details For functions in the Viper.* namespace, this is true.
    /// The lowerer uses this to emit extern calls instead of direct calls.
    bool isExtern{false};

    /// @brief Pointer to the AST declaration node.
    /// @details May be nullptr for built-in symbols or extern functions.
    Decl *decl{nullptr};
};

/// @}

//===----------------------------------------------------------------------===//
/// @name Scope Management
/// @brief Class for managing symbol scopes.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Scope for symbol lookup.
/// @details Represents a lexical scope containing symbol definitions.
/// Scopes are linked to parent scopes to enable nested lookup.
///
/// ## Scope Hierarchy
///
/// Scopes form a tree structure:
/// - Global scope (module-level)
///   - Function scope
///     - Block scope (if, while, for bodies)
///       - Nested block scope
///
/// Symbol lookup proceeds from innermost to outermost scope.
///
/// @invariant A scope's parent pointer is set at construction and never changes.
/// @invariant Symbol names are unique within a single scope.
class Scope
{
  public:
    /// @brief Create a scope with an optional parent.
    /// @param parent The enclosing scope, or nullptr for global scope.
    explicit Scope(Scope *parent = nullptr) : parent_(parent) {}

    /// @brief Define a symbol in this scope.
    /// @param name The symbol name.
    /// @param symbol The symbol information.
    ///
    /// @details If a symbol with the same name already exists in this scope,
    /// it is replaced (shadowing). Parent scope symbols are not affected.
    void define(const std::string &name, Symbol symbol);

    /// @brief Look up a symbol by name in this scope and ancestors.
    /// @param name The symbol name to find.
    /// @return Pointer to the symbol if found, nullptr otherwise.
    ///
    /// @details Searches this scope first, then parent scopes recursively.
    /// Returns the first match found (innermost scope wins for shadowing).
    Symbol *lookup(const std::string &name);

    /// @brief Look up a symbol only in this scope (not ancestors).
    /// @param name The symbol name to find.
    /// @return Pointer to the symbol if found in this scope, nullptr otherwise.
    ///
    /// @details Used to check for redefinition in the current scope.
    Symbol *lookupLocal(const std::string &name);

    /// @brief Get the parent scope.
    /// @return The parent scope, or nullptr for the global scope.
    Scope *parent() const
    {
        return parent_;
    }

  private:
    /// @brief The enclosing scope.
    Scope *parent_{nullptr};

    /// @brief Symbols defined in this scope.
    std::unordered_map<std::string, Symbol> symbols_;
};

/// @}

//===----------------------------------------------------------------------===//
/// @name Semantic Analyzer
/// @{
//===----------------------------------------------------------------------===//

/// @brief Semantic analyzer for Zia programs.
/// @details Performs type checking, name resolution, and semantic validation
/// on parsed AST nodes. After successful analysis, provides access to:
/// - Expression types via typeOf()
/// - Type resolution via resolveType()
/// - Runtime function resolution via runtimeCallee()
///
/// ## Analysis Process
///
/// The analyze() method performs multi-pass analysis:
/// 1. Register built-in types and functions
/// 2. Process imports (bring runtime functions into scope)
/// 3. Register all type declarations
/// 4. Analyze global variables
/// 5. Analyze type members (fields, methods)
/// 6. Analyze function declarations
/// 7. Type-check all function/method bodies
///
/// ## Scope Management
///
/// Scopes are managed via pushScope()/popScope(). Each scope can contain:
/// - Local variables
/// - Parameters
/// - Nested scopes inherit access to parent scopes
///
/// ## Self and Return Type Context
///
/// The analyzer tracks:
/// - currentSelfType_: The type of `self` in methods
/// - expectedReturnType_: The declared return type for return validation
///
/// @invariant Scope stack is balanced (pushScope/popScope pairs).
/// @invariant Expression type map is populated after analyze().
class Sema
{
  public:
    /// @brief Create a semantic analyzer with the given diagnostic engine.
    /// @param diag Diagnostic engine for error reporting.
    ///
    /// @details Initializes the analyzer and registers built-in types and
    /// functions. The diagnostic engine is borrowed and must outlive the
    /// analyzer.
    explicit Sema(il::support::DiagnosticEngine &diag);

    /// @brief Analyze a module declaration.
    /// @param module The parsed module to analyze.
    /// @return True if analysis succeeded without errors.
    ///
    /// @details Performs complete semantic analysis on the module:
    /// 1. Registers built-in symbols
    /// 2. Processes imports
    /// 3. Analyzes all declarations
    /// 4. Type-checks all bodies
    ///
    /// Even on errors, populates as much type information as possible.
    bool analyze(ModuleDecl &module);

    /// @brief Get the resolved type for an expression.
    /// @param expr The expression to look up.
    /// @return The semantic type, or nullptr if not found/analyzed.
    ///
    /// @details Call after analyze() to get expression types.
    /// Returns nullptr for expressions that couldn't be typed.
    TypeRef typeOf(const Expr *expr) const;

    /// @brief Resolve an AST type node to a semantic type.
    /// @param node The AST type node.
    /// @return The resolved semantic type.
    ///
    /// @details Handles named types, generic types, optionals, and functions.
    /// May return unknown type for unresolved types.
    TypeRef resolveType(const TypeNode *node) const;

    /// @brief Check if analysis produced errors.
    /// @return True if at least one error was reported.
    bool hasError() const
    {
        return hasError_;
    }

    /// @brief Get the current module being analyzed.
    /// @return The module, or nullptr if not in analyze().
    ModuleDecl *currentModule() const
    {
        return currentModule_;
    }

    /// @brief Get the runtime function name for a call expression.
    /// @param expr The call expression to look up.
    /// @return The dotted name (e.g., "Viper.Terminal.Say") or empty string.
    ///
    /// @details After analysis, call expressions that invoke runtime library
    /// functions have their resolved names stored. This is used during
    /// lowering to generate the correct runtime calls.
    std::string runtimeCallee(const CallExpr *expr) const
    {
        auto it = runtimeCallees_.find(expr);
        return it != runtimeCallees_.end() ? it->second : "";
    }

    /// @brief Get the runtime getter function name for a field expression.
    /// @param expr The field expression to look up.
    /// @return The getter name (e.g., "Viper.Math.get_Pi") or empty string.
    ///
    /// @details For field expressions that resolve to runtime class property getters
    /// (like Viper.Math.Pi), this returns the resolved getter function name.
    std::string runtimeFieldGetter(const FieldExpr *expr) const
    {
        auto it = runtimeFieldGetters_.find(expr);
        return it != runtimeFieldGetters_.end() ? it->second : "";
    }

    /// @brief Look up the return type of a function by name.
    /// @param name The function name (e.g., "Viper.Random.NextInt" or "MyLib.helper").
    /// @return The return type, or nullptr if not found.
    ///
    /// @details Works for both runtime (extern) functions and user-defined functions.
    TypeRef functionReturnType(const std::string &name)
    {
        Symbol *sym = lookupSymbol(name);
        return sym && sym->kind == Symbol::Kind::Function ? sym->type : nullptr;
    }

    /// @brief Find an extern (runtime) function by name.
    /// @param name The function name (e.g., "Viper.GUI.App.get_ShouldClose").
    /// @return The symbol if found and is extern, nullptr otherwise.
    ///
    /// @details Used by the lowerer to resolve runtime property getters.
    Symbol *findExternFunction(const std::string &name)
    {
        Symbol *sym = lookupSymbol(name);
        return (sym && sym->isExtern) ? sym : nullptr;
    }

    /// @brief Look up the type of a variable by name.
    /// @param name The variable name.
    /// @return The variable's type, or nullptr if not found.
    TypeRef lookupVarType(const std::string &name);

  private:
    //=========================================================================
    /// @name Declaration Analysis
    /// @brief Methods for analyzing declarations.
    /// @{
    //=========================================================================

    /// @brief Analyze a bind declaration.
    /// @param decl The bind declaration.
    ///
    /// @details Brings runtime functions into scope based on the bind path.
    /// For example, `bind Viper.Terminal as Term;` makes Term.Say, Term.Ask, etc. available.
    void analyzeBind(BindDecl &decl);

    /// @brief Analyze a global variable declaration.
    /// @param decl The global variable declaration.
    ///
    /// @details Type-checks the initializer and registers the variable
    /// in the global scope.
    void analyzeGlobalVarDecl(GlobalVarDecl &decl);

    /// @brief Analyze a value type declaration.
    /// @param decl The value type declaration.
    ///
    /// @details Registers the type and analyzes all members.
    void analyzeValueDecl(ValueDecl &decl);

    /// @brief Register type member signatures for cross-module resolution.
    /// @tparam T Decl type (EntityDecl, ValueDecl, or InterfaceDecl)
    /// @param decl The type declaration.
    /// @param includeFields Whether to register field types (false for interfaces).
    template <typename T>
    void registerTypeMembers(T &decl, bool includeFields = true);

    /// @brief Register entity member signatures for cross-module resolution.
    void registerEntityMembers(EntityDecl &decl);

    /// @brief Register value type member signatures for cross-module resolution.
    void registerValueMembers(ValueDecl &decl);

    /// @brief Register interface member signatures for cross-module resolution.
    void registerInterfaceMembers(InterfaceDecl &decl);

    /// @brief Analyze an entity type declaration.
    /// @param decl The entity type declaration.
    ///
    /// @details Registers the type, resolves inheritance, and analyzes members.
    void analyzeEntityDecl(EntityDecl &decl);

    /// @brief Analyze an interface declaration.
    /// @param decl The interface declaration.
    ///
    /// @details Registers the interface type and its method signatures.
    void analyzeInterfaceDecl(InterfaceDecl &decl);

    /// @brief Analyze a namespace declaration.
    /// @param decl The namespace declaration.
    ///
    /// @details Processes all declarations within the namespace, prefixing
    /// their names with the namespace path. Supports nested namespaces.
    void analyzeNamespaceDecl(NamespaceDecl &decl);

    /// @brief Compute the qualified name for a declaration.
    /// @param name The unqualified name.
    /// @return The fully qualified name including namespace prefix.
    ///
    /// @details If currently inside a namespace, prepends the namespace path.
    /// Example: inside "MyLib", name "Parser" becomes "MyLib.Parser".
    std::string qualifyName(const std::string &name) const;

    /// @brief Analyze a function declaration.
    /// @param decl The function declaration.
    ///
    /// @details Analyzes the signature and body, validating return types.
    void analyzeFunctionDecl(FunctionDecl &decl);

    /// @brief Analyze a field declaration within a type.
    /// @param decl The field declaration.
    /// @param ownerType The type containing this field.
    void analyzeFieldDecl(FieldDecl &decl, TypeRef ownerType);

    /// @brief Analyze a method declaration within a type.
    /// @param decl The method declaration.
    /// @param ownerType The type containing this method.
    void analyzeMethodDecl(MethodDecl &decl, TypeRef ownerType);

    /// @brief Initialize all runtime function type mappings.
    /// @details Registers all Viper.* namespace functions as extern symbols
    /// in the global scope. Called once during Sema construction.
    void initRuntimeFunctions();

    /// @brief Register an external (runtime) function.
    /// @param name The fully qualified function name (e.g., "Viper.Terminal.Say").
    /// @param returnType The function's return type.
    ///
    /// @details Creates a Symbol with isExtern=true and registers it in scope.
    /// Used for runtime library functions that have no AST declaration.
    void defineExternFunction(const std::string &name, TypeRef returnType);

    /// @}
    //=========================================================================
    /// @name Statement Analysis
    /// @brief Methods for analyzing statements.
    /// @{
    //=========================================================================

    /// @brief Analyze any statement (dispatches to specific methods).
    /// @param stmt The statement to analyze.
    void analyzeStmt(Stmt *stmt);

    /// @brief Analyze a block statement.
    /// @param stmt The block statement.
    ///
    /// @details Creates a new scope and analyzes all statements within.
    void analyzeBlockStmt(BlockStmt *stmt);

    /// @brief Analyze a variable declaration statement.
    /// @param stmt The variable statement.
    void analyzeVarStmt(VarStmt *stmt);

    /// @brief Analyze an if statement.
    /// @param stmt The if statement.
    void analyzeIfStmt(IfStmt *stmt);

    /// @brief Analyze a while statement.
    /// @param stmt The while statement.
    void analyzeWhileStmt(WhileStmt *stmt);

    /// @brief Analyze a C-style for statement.
    /// @param stmt The for statement.
    void analyzeForStmt(ForStmt *stmt);

    /// @brief Analyze a for-in statement.
    /// @param stmt The for-in statement.
    void analyzeForInStmt(ForInStmt *stmt);

    /// @brief Analyze a return statement.
    /// @param stmt The return statement.
    ///
    /// @details Validates that the return value type matches the expected
    /// return type of the enclosing function.
    void analyzeReturnStmt(ReturnStmt *stmt);

    /// @brief Analyze a guard statement.
    /// @param stmt The guard statement.
    void analyzeGuardStmt(GuardStmt *stmt);

    /// @brief Analyze a match statement.
    /// @param stmt The match statement.
    void analyzeMatchStmt(MatchStmt *stmt);

    /// @brief Track coverage details for match exhaustiveness checks.
    struct MatchCoverage
    {
        bool hasIrrefutable = false;
        bool coversNull = false;
        bool coversSome = false;
        std::set<int64_t> coveredIntegers;
        std::set<bool> coveredBooleans;
    };

    /// @brief Analyze a match pattern and collect bindings/coverage.
    bool analyzeMatchPattern(const MatchArm::Pattern &pattern,
                             TypeRef scrutineeType,
                             MatchCoverage &coverage,
                             std::unordered_map<std::string, TypeRef> &bindings);

    /// @brief Compute a common type for two branches.
    TypeRef commonType(TypeRef lhs, TypeRef rhs);

    /// @brief Determine whether a statement always exits the current scope.
    bool stmtAlwaysExits(Stmt *stmt);

    /// @}
    //=========================================================================
    /// @name Expression Analysis
    /// @brief Methods for analyzing expressions.
    /// @details Each method analyzes a specific expression type and returns
    /// the inferred type. The type is also stored in exprTypes_.
    /// @{
    //=========================================================================

    /// @brief Analyze any expression (dispatches to specific methods).
    /// @param expr The expression to analyze.
    /// @return The inferred type, or unknown type on error.
    TypeRef analyzeExpr(Expr *expr);

    /// @brief Analyze an integer literal.
    /// @return types::integer()
    TypeRef analyzeIntLiteral(IntLiteralExpr *expr);

    /// @brief Analyze a floating-point literal.
    /// @return types::number()
    TypeRef analyzeNumberLiteral(NumberLiteralExpr *expr);

    /// @brief Analyze a string literal.
    /// @return types::string()
    TypeRef analyzeStringLiteral(StringLiteralExpr *expr);

    /// @brief Analyze a boolean literal.
    /// @return types::boolean()
    TypeRef analyzeBoolLiteral(BoolLiteralExpr *expr);

    /// @brief Analyze a null literal.
    /// @return types::optional(types::unknown()) - inferred from context
    TypeRef analyzeNullLiteral(NullLiteralExpr *expr);

    /// @brief Analyze a unit literal.
    /// @return types::unit()
    TypeRef analyzeUnitLiteral(UnitLiteralExpr *expr);

    /// @brief Analyze an identifier expression.
    /// @return The type of the referenced symbol.
    TypeRef analyzeIdent(IdentExpr *expr);

    /// @brief Analyze a self expression.
    /// @return The current self type.
    TypeRef analyzeSelf(SelfExpr *expr);

    /// @brief Analyze a binary expression.
    /// @return The result type based on operator and operands.
    TypeRef analyzeBinary(BinaryExpr *expr);

    /// @brief Analyze a unary expression.
    /// @return The result type based on operator and operand.
    TypeRef analyzeUnary(UnaryExpr *expr);

    /// @brief Analyze a ternary conditional expression.
    /// @return The common type of the branches.
    TypeRef analyzeTernary(TernaryExpr *expr);

    /// @brief Analyze a function/method call expression.
    /// @return The return type of the called function.
    TypeRef analyzeCall(CallExpr *expr);

    /// @brief Analyze an index expression.
    /// @return The element type of the collection.
    TypeRef analyzeIndex(IndexExpr *expr);

    /// @brief Analyze a field access expression.
    /// @return The type of the accessed field.
    TypeRef analyzeField(FieldExpr *expr);

    /// @brief Analyze an optional chain expression.
    /// @return An optional type wrapping the field type.
    TypeRef analyzeOptionalChain(OptionalChainExpr *expr);

    /// @brief Analyze a null coalesce expression.
    /// @return The non-optional type of the result.
    TypeRef analyzeCoalesce(CoalesceExpr *expr);

    /// @brief Analyze an is-expression (type check).
    /// @return types::boolean()
    TypeRef analyzeIs(IsExpr *expr);

    /// @brief Analyze an as-expression (type cast).
    /// @return The target type.
    TypeRef analyzeAs(AsExpr *expr);

    /// @brief Analyze a range expression.
    /// @return types::list(types::integer()) for iteration.
    TypeRef analyzeRange(RangeExpr *expr);

    /// @brief Analyze a match expression.
    /// @return The common type of all arm bodies.
    TypeRef analyzeMatchExpr(MatchExpr *expr);

    /// @brief Analyze a new expression (object creation).
    /// @return The entity type being constructed.
    TypeRef analyzeNew(NewExpr *expr);

    /// @brief Analyze a lambda expression.
    /// @return A function type matching the lambda's signature.
    TypeRef analyzeLambda(LambdaExpr *expr);

    /// @brief Analyze a list literal expression.
    /// @return types::list(elementType)
    TypeRef analyzeListLiteral(ListLiteralExpr *expr);

    /// @brief Analyze a map literal expression.
    /// @return types::map(keyType, valueType)
    TypeRef analyzeMapLiteral(MapLiteralExpr *expr);

    /// @brief Analyze a set literal expression.
    /// @return types::set(elementType)
    TypeRef analyzeSetLiteral(SetLiteralExpr *expr);

    /// @brief Analyze a tuple literal expression.
    /// @return types::tuple(elementTypes)
    TypeRef analyzeTuple(TupleExpr *expr);

    /// @brief Analyze a tuple index access expression.
    /// @return The type of the element at the given index.
    TypeRef analyzeTupleIndex(TupleIndexExpr *expr);

    /// @brief Analyze a block expression (statements followed by optional value).
    /// @return The type of the block's value expression, or Void if none.
    TypeRef analyzeBlockExpr(BlockExpr *expr);

    /// @}
    //=========================================================================
    /// @name Type Resolution
    /// @brief Methods for resolving type annotations.
    /// @{
    //=========================================================================

    /// @brief Resolve a simple type name to a semantic type.
    /// @param name The type name (e.g., "Integer", "MyClass").
    /// @return The semantic type, or unknown if not found.
    TypeRef resolveNamedType(const std::string &name) const;

    /// @brief Resolve a type node to a semantic type.
    /// @param node The AST type node.
    /// @return The resolved semantic type.
    ///
    /// @details Handles named, generic, optional, function, and tuple types.
    TypeRef resolveTypeNode(const TypeNode *node);

    /// @}
    //=========================================================================
    /// @name Scope Management
    /// @brief Methods for managing variable scopes.
    /// @{
    //=========================================================================

    /// @brief Push a new scope onto the scope stack.
    ///
    /// @details Creates a new scope with the current scope as parent.
    /// Must be balanced with popScope().
    void pushScope();

    /// @brief Pop the current scope from the scope stack.
    ///
    /// @details Returns to the parent scope. All symbols in the popped
    /// scope become inaccessible.
    void popScope();

    /// @brief Define a symbol in the current scope.
    /// @param name The symbol name.
    /// @param symbol The symbol information.
    void defineSymbol(const std::string &name, Symbol symbol);

    /// @brief Look up a symbol by name.
    /// @param name The symbol name.
    /// @return Pointer to the symbol, or nullptr if not found.
    Symbol *lookupSymbol(const std::string &name);

    /// @brief Collect captured variables from a lambda body.
    /// @param expr The expression to scan for free variables.
    /// @param lambdaLocals Names local to the lambda (params).
    /// @param captures Output vector of captured variables.
    void collectCaptures(const Expr *expr,
                         const std::set<std::string> &lambdaLocals,
                         std::vector<CapturedVar> &captures);

    /// @}
    //=========================================================================
    /// @name Error Reporting
    /// @brief Methods for reporting semantic errors.
    /// @{
    //=========================================================================

    /// @brief Report a semantic error.
    /// @param loc Source location of the error.
    /// @param message Error message.
    void error(SourceLoc loc, const std::string &message);

    /// @brief Report an undefined name error.
    /// @param loc Source location.
    /// @param name The undefined name.
    void errorUndefined(SourceLoc loc, const std::string &name);

    /// @brief Report a type mismatch error.
    /// @param loc Source location.
    /// @param expected The expected type.
    /// @param actual The actual type.
    void errorTypeMismatch(SourceLoc loc, TypeRef expected, TypeRef actual);

    /// @}
    //=========================================================================
    /// @name Built-in Registration
    /// @{
    //=========================================================================

    /// @brief Register built-in types and functions.
    ///
    /// @details Registers:
    /// - Primitive types (Integer, Number, Boolean, String, etc.)
    /// - Collection constructors (List, Map, Set)
    /// - Runtime library functions based on imports
    void registerBuiltins();

    /// @}
    //=========================================================================
    /// @name Member Variables
    /// @{
    //=========================================================================

    /// @brief Diagnostic engine for error reporting.
    il::support::DiagnosticEngine &diag_;

    /// @brief Whether any errors have occurred.
    bool hasError_{false};

    /// @brief Current module being analyzed.
    ModuleDecl *currentModule_{nullptr};

    /// @brief Current function being analyzed (for return validation).
    FunctionDecl *currentFunction_{nullptr};

    /// @brief Type of `self` in current method context.
    /// @details Set when analyzing methods, cleared afterward.
    TypeRef currentSelfType_{nullptr};

    /// @brief Expected return type of current function/method.
    /// @details Used to validate return statements.
    TypeRef expectedReturnType_{nullptr};

    /// @brief Current loop nesting depth for break/continue validation.
    int loopDepth_{0};

    /// @brief Current namespace prefix for qualified names.
    /// @details When inside a namespace block, this contains the namespace path.
    /// Empty when at module level. Example: "MyLib.Internal"
    std::string namespacePrefix_;

    /// @brief Owned lexical scope stack (scopes_[0] is global).
    std::vector<std::unique_ptr<Scope>> scopes_;

    /// @brief The current scope for symbol lookup.
    Scope *currentScope_{nullptr};

    /// @brief Map from expression pointers to their resolved types.
    /// @details Populated during expression analysis.
    std::unordered_map<const Expr *, TypeRef> exprTypes_;

    /// @brief Map from type names to semantic types.
    /// @details Includes both built-in types and user-defined types.
    std::unordered_map<std::string, TypeRef> typeRegistry_;

    /// @brief Value type declarations for pattern analysis.
    std::unordered_map<std::string, ValueDecl *> valueDecls_;

    /// @brief Entity type declarations for pattern analysis.
    std::unordered_map<std::string, EntityDecl *> entityDecls_;

    /// @brief Interface declarations for implementation checks.
    std::unordered_map<std::string, InterfaceDecl *> interfaceDecls_;

    /// @brief Map from method signatures to function types.
    /// @details Key format: "TypeName.methodName"
    /// Used for method call resolution.
    std::unordered_map<std::string, TypeRef> methodTypes_;

    /// @brief Map from field signatures to field types.
    /// @details Key format: "TypeName.fieldName"
    std::unordered_map<std::string, TypeRef> fieldTypes_;

    /// @brief Map from member signatures to visibility.
    /// @details Key format: "TypeName.memberName"
    std::unordered_map<std::string, Visibility> memberVisibility_;

    /// @brief Map from call expressions to their resolved extern function names.
    /// @details Populated for calls to extern functions (runtime library).
    /// Used during lowering to emit extern calls instead of direct calls.
    std::unordered_map<const CallExpr *, std::string> runtimeCallees_;

    /// @brief Map from field expressions to their resolved runtime getter names.
    /// @details For namespace-style property access like Viper.Math.Pi, this maps
    /// the FieldExpr to "Viper.Math.get_Pi" so the lowerer can emit a getter call.
    std::unordered_map<const FieldExpr *, std::string> runtimeFieldGetters_;

    /// @brief Set of bind paths seen in the current module.
    std::unordered_set<std::string> binds_;

    /// @brief Map from imported module names to their exported symbols.
    /// @details When `import "./colors"` is processed, "colors" maps to
    /// the symbols defined in colors.zia. Used for qualified access.
    std::unordered_map<std::string, std::unordered_map<std::string, Symbol>> moduleExports_;

    /// @}
};

/// @}

} // namespace il::frontends::zia
