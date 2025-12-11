//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/SemanticAnalyzer.hpp
// Purpose: Declares the semantic analyzer for Viper Pascal.
// Key invariants: Two-pass analysis (declarations then bodies); symbol tables
//                 track types and scopes.
// Ownership/Lifetime: Borrows DiagnosticEngine; AST not owned.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/sem/Types.hpp"
#include "frontends/pascal/sem/OOPTypes.hpp"
#include "support/diagnostics.hpp"
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace il::frontends::pascal
{

//===----------------------------------------------------------------------===//
// Semantic Analyzer
//===----------------------------------------------------------------------===//

/// @brief Performs semantic analysis on Pascal AST.
/// @details Implements two-pass analysis:
///   Pass 1: Collect all type and function declarations
///   Pass 2: Analyze function/procedure bodies, check types
///
/// @invariant Symbol tables reflect all declarations after pass 1.
/// @ownership Borrows DiagnosticEngine; does not own AST nodes.
class SemanticAnalyzer
{
  public:
    /// @brief Create analyzer reporting to @p diag.
    explicit SemanticAnalyzer(il::support::DiagnosticEngine &diag);

    /// @brief Analyze a complete Pascal program.
    /// @param prog Program AST to analyze.
    /// @return True if no errors occurred.
    bool analyze(Program &prog);

    /// @brief Analyze a complete Pascal unit.
    /// @param unit Unit AST to analyze.
    /// @return True if no errors occurred.
    bool analyze(Unit &unit);

    /// @brief Check if any errors occurred during analysis.
    bool hasError() const
    {
        return hasError_;
    }

    /// @brief Get the resolved type of an expression.
    /// @param expr Expression to type-check.
    /// @return The resolved type, or Unknown on error.
    PasType typeOf(Expr &expr);

    /// @brief Lookup a type by name.
    /// @param name Type name to look up.
    /// @return The type if found, or nullopt.
    std::optional<PasType> lookupType(const std::string &name) const;

    /// @brief Lookup a variable by name.
    /// @param name Variable name to look up.
    /// @return The variable's type if found, or nullopt.
    std::optional<PasType> lookupVariable(const std::string &name) const;

    /// @brief Lookup a constant by name.
    /// @param name Constant name to look up.
    /// @return The constant's type if found, or nullopt.
    std::optional<PasType> lookupConstant(const std::string &name) const;

    /// @brief Lookup an integer constant value by name.
    /// @param name Constant name to look up.
    /// @return The integer value if found, or nullopt.
    std::optional<int64_t> lookupConstantInt(const std::string &name) const;

    /// @brief Lookup a real constant value by name.
    /// @param name Constant name to look up.
    /// @return The real value if found, or nullopt.
    std::optional<double> lookupConstantReal(const std::string &name) const;

    /// @brief Lookup a string constant value by name.
    /// @param name Constant name to look up.
    /// @return The string value if found, or nullopt.
    std::optional<std::string> lookupConstantStr(const std::string &name) const;

    /// @brief Lookup a function/procedure by name.
    /// @param name Function name to look up.
    /// @return The signature if found, or nullptr.
    const FuncSignature *lookupFunction(const std::string &name) const;

    /// @brief Lookup a class by name.
    /// @param name Class name to look up.
    /// @return The class info if found, or nullptr.
    const ClassInfo *lookupClass(const std::string &name) const;

    /// @brief Lookup an interface by name.
    /// @param name Interface name to look up.
    /// @return The interface info if found, or nullptr.
    const InterfaceInfo *lookupInterface(const std::string &name) const;

    /// @brief Register a unit's exports for use by other units/programs.
    /// @param unitInfo The unit information to register.
    void registerUnit(const UnitInfo &unitInfo);

    /// @brief Get a registered unit by name.
    /// @param name Unit name (case-insensitive).
    /// @return Pointer to unit info if found, nullptr otherwise.
    const UnitInfo *getUnit(const std::string &name) const;

    /// @brief Import symbols from a list of units into the current scope.
    /// @param unitNames List of unit names to import.
    /// @return True if all units were found and imported.
    bool importUnits(const std::vector<std::string> &unitNames);

    /// @brief Extract exported symbols from a unit's interface declarations.
    /// @param unit The unit to extract exports from.
    /// @return UnitInfo containing the exported symbols.
    UnitInfo extractUnitExports(const Unit &unit);

    /// @brief Resolve a TypeNode to a PasType (public for Lowerer access).
    /// @param typeNode The AST type node to resolve.
    /// @return The resolved type, or Unknown on error.
    PasType resolveType(TypeNode &typeNode);

    /// @brief Collect all methods from interface and its bases (public for Lowerer).
    /// @param ifaceName The interface name (lowercase).
    /// @param methods Output map to collect methods into (first overload per name).
    void collectInterfaceMethods(const std::string &ifaceName,
                                 std::map<std::string, MethodInfo> &methods) const;

    /// @brief Collect all method overloads from interface and its bases.
    /// @param ifaceName The interface name (lowercase).
    /// @param methods Output vector to collect all method overloads into.
    void collectInterfaceMethods(const std::string &ifaceName,
                                 std::vector<MethodInfo> &methods) const;

  private:
    //=========================================================================
    // Pass 1: Declaration Collection
    //=========================================================================

    /// @brief Collect all declarations from a program.
    void collectDeclarations(Program &prog);

    /// @brief Collect all declarations from a unit.
    void collectDeclarations(Unit &unit);

    /// @brief Process a single declaration.
    void collectDecl(Decl &decl);

    /// @brief Register a type declaration.
    void registerType(const std::string &name, TypeNode &typeNode);

    /// @brief Register a variable declaration.
    void registerVariable(const std::string &name, TypeNode &typeNode);

    /// @brief Register a constant declaration.
    void registerConstant(const std::string &name, Expr &value, TypeNode *typeNode);

    /// @brief Register a procedure declaration.
    void registerProcedure(ProcedureDecl &decl);

    /// @brief Register a function declaration.
    void registerFunction(FunctionDecl &decl);

    /// @brief Register a class declaration.
    void registerClass(ClassDecl &decl);

    /// @brief Register an interface declaration.
    void registerInterface(InterfaceDecl &decl);

    //=========================================================================
    // Class/Interface Semantic Checks
    //=========================================================================

    /// @brief Check all class inheritance and method overrides.
    void checkClassSemantics();

    /// @brief Check a single class for semantic errors.
    void checkClassInfo(const ClassInfo &classInfo);

    /// @brief Check method overrides for a class.
    void checkOverrides(const ClassInfo &classInfo);

    /// @brief Check method overrides with an effective base class.
    void checkOverridesWithBase(const ClassInfo &classInfo, const std::string &effectiveBaseClass);

    /// @brief Check interface implementations for a class.
    void checkInterfaceImplementation(const ClassInfo &classInfo);

    /// @brief Check interface implementations with effective interface list.
    void checkInterfaceImplementationWith(const ClassInfo &classInfo,
                                          const std::vector<std::string> &effectiveInterfaces);

    /// @brief Check weak field validity.
    void checkWeakFields(const ClassInfo &classInfo);

    /// @brief Check constructor/destructor validity.
    void checkConstructorDestructor(ClassDecl &decl);

    /// @brief Find a virtual method in the base class hierarchy.
    /// @param className Starting class name.
    /// @param methodName Method name to find.
    /// @return The method info if found virtual in a base class, nullopt otherwise.
    std::optional<MethodInfo> findVirtualInBase(const std::string &className,
                                                const std::string &methodName) const;

    /// @brief Check if a method signature matches another.
    bool signaturesMatch(const MethodInfo &m1, const MethodInfo &m2) const;

    /// @brief Check if two method parameter type lists are identical (for duplicate detection).
    bool parameterTypesMatch(const MethodInfo &m1, const MethodInfo &m2) const;

    /// @brief Find a virtual method in base with matching signature (for override with overloads).
    /// @param className Starting class name.
    /// @param method The override method with signature to match.
    /// @return The method info if found virtual in a base class with matching signature, nullopt
    /// otherwise.
    std::optional<MethodInfo> findVirtualInBaseWithSignature(const std::string &className,
                                                             const MethodInfo &method) const;

    /// @brief Resolve overloaded method call.
    /// @param overloads Vector of candidate method overloads.
    /// @param argTypes Argument types from the call site.
    /// @param loc Source location for error reporting.
    /// @return Pointer to best matching method, or nullptr if no match/ambiguous.
    const MethodInfo *resolveOverload(const std::vector<MethodInfo> &overloads,
                                      const std::vector<PasType> &argTypes,
                                      il::support::SourceLoc loc);

    /// @brief Check if arguments are compatible with a method's parameters.
    /// @param method The method to check against.
    /// @param argTypes Argument types from the call site.
    /// @return True if arguments can be passed to this method.
    bool argumentsCompatible(const MethodInfo &method, const std::vector<PasType> &argTypes);

    /// @brief Score how well arguments match a method's parameters (higher is better).
    /// @param method The method to score against.
    /// @param argTypes Argument types from the call site.
    /// @return Match score (higher = better match, -1 = incompatible).
    int overloadMatchScore(const MethodInfo &method, const std::vector<PasType> &argTypes);

    /// @brief Check if a class implements an interface (directly or via base class).
    /// @param className Name of the class.
    /// @param interfaceName Name of the interface.
    /// @return True if the class implements the interface.
    bool classImplementsInterface(const std::string &className,
                                  const std::string &interfaceName) const;

    /// @brief Check if a class inherits from another class.
    /// @param derivedName Name of the derived class.
    /// @param baseName Name of the potential base class.
    /// @return True if derived inherits from base (or they are the same).
    bool classInheritsFrom(const std::string &derivedName, const std::string &baseName) const;

    /// @brief Determine if a class is abstract (declares or inherits abstract methods not
    /// implemented).
    /// @param className Name of the class.
    /// @return True if abstract, false otherwise.
    bool isAbstractClass(const std::string &className) const;

    /// @brief Check if a member with given visibility in declaringClass is visible from
    /// accessingClass.
    /// @param visibility The member's visibility (Public or Private).
    /// @param declaringClass Name of the class that declares the member.
    /// @param accessingClass Name of the class trying to access the member (empty if outside any
    /// class).
    /// @return True if the member is visible.
    bool isMemberVisible(Visibility visibility,
                         const std::string &declaringClass,
                         const std::string &accessingClass) const;

    /// @brief Check if an interface extends another interface.
    /// @param derivedName Name of the derived interface.
    /// @param baseName Name of the potential base interface.
    /// @return True if derived extends base (or they are the same).
    bool interfaceExtendsInterface(const std::string &derivedName,
                                   const std::string &baseName) const;

    //=========================================================================
    // Pass 2: Body Analysis
    //=========================================================================

    /// @brief Analyze all function/procedure bodies.
    void analyzeBodies(Program &prog);

    /// @brief Analyze all function/procedure bodies in a unit.
    void analyzeBodies(Unit &unit);

    /// @brief Analyze a procedure body.
    void analyzeProcedureBody(ProcedureDecl &decl);

    /// @brief Analyze a function body.
    void analyzeFunctionBody(FunctionDecl &decl);

    /// @brief Analyze a constructor body.
    void analyzeConstructorBody(ConstructorDecl &decl);

    /// @brief Analyze a destructor body.
    void analyzeDestructorBody(DestructorDecl &decl);

    //=========================================================================
    // Statement Analysis
    //=========================================================================

    /// @brief Analyze a statement.
    void analyzeStmt(Stmt &stmt);

    /// @brief Analyze a block statement.
    void analyzeBlock(BlockStmt &block);

    /// @brief Analyze an assignment statement.
    void analyzeAssign(AssignStmt &stmt);

    /// @brief Analyze a call statement.
    void analyzeCall(CallStmt &stmt);

    /// @brief Analyze an if statement.
    void analyzeIf(IfStmt &stmt);

    /// @brief Analyze a while statement.
    void analyzeWhile(WhileStmt &stmt);

    /// @brief Analyze a repeat statement.
    void analyzeRepeat(RepeatStmt &stmt);

    /// @brief Analyze a for statement.
    void analyzeFor(ForStmt &stmt);

    /// @brief Analyze a for-in statement.
    void analyzeForIn(ForInStmt &stmt);

    /// @brief Analyze a case statement.
    void analyzeCase(CaseStmt &stmt);

    /// @brief Analyze a raise statement.
    void analyzeRaise(RaiseStmt &stmt);

    /// @brief Analyze an exit statement.
    void analyzeExit(ExitStmt &stmt);

    /// @brief Analyze a try-except statement.
    void analyzeTryExcept(TryExceptStmt &stmt);

    /// @brief Analyze a try-finally statement.
    void analyzeTryFinally(TryFinallyStmt &stmt);

    /// @brief Analyze a with statement.
    void analyzeWith(WithStmt &stmt);

    /// @brief Analyze an inherited statement.
    void analyzeInherited(InheritedStmt &stmt);

    //=========================================================================
    // Expression Type Checking
    //=========================================================================

    /// @brief Get type of an integer literal.
    PasType typeOfIntLiteral(IntLiteralExpr &expr);

    /// @brief Get type of a real literal.
    PasType typeOfRealLiteral(RealLiteralExpr &expr);

    /// @brief Get type of a string literal.
    PasType typeOfStringLiteral(StringLiteralExpr &expr);

    /// @brief Get type of a boolean literal.
    PasType typeOfBoolLiteral(BoolLiteralExpr &expr);

    /// @brief Get type of nil.
    PasType typeOfNil(NilLiteralExpr &expr);

    /// @brief Get type of a name expression (variable/constant reference).
    PasType typeOfName(NameExpr &expr);

    /// @brief Get type of a unary expression.
    PasType typeOfUnary(UnaryExpr &expr);

    /// @brief Get type of a binary expression.
    PasType typeOfBinary(BinaryExpr &expr);

    /// @brief Get type of a function/procedure call.
    PasType typeOfCall(CallExpr &expr);

    /// @brief Get type of an array index expression.
    PasType typeOfIndex(IndexExpr &expr);

    /// @brief Get type of a field access expression.
    PasType typeOfField(FieldExpr &expr);

    /// @brief Get type of a type cast expression.
    PasType typeOfTypeCast(TypeCastExpr &expr);

    /// @brief Get type of an 'is' type-check expression.
    PasType typeOfIs(IsExpr &expr);

    /// @brief Get type of an 'as' safe-cast expression.
    PasType typeOfAs(AsExpr &expr);

    /// @brief Get type of a set constructor.
    PasType typeOfSetConstructor(SetConstructorExpr &expr);

    /// @brief Get type of an address-of expression.
    PasType typeOfAddressOf(AddressOfExpr &expr);

    /// @brief Get type of a dereference expression.
    PasType typeOfDereference(DereferenceExpr &expr);

    //=========================================================================
    // Type Resolution
    //=========================================================================

    /// @brief Check if @p source can be assigned to @p target.
    bool isAssignableFrom(const PasType &target, const PasType &source);

    /// @brief Get the result type of a binary operation.
    PasType binaryResultType(BinaryExpr::Op op, const PasType &left, const PasType &right);

    /// @brief Get the result type of a unary operation.
    PasType unaryResultType(UnaryExpr::Op op, const PasType &operand);

    /// @brief Check if an expression is a compile-time constant.
    /// @param expr The expression to check.
    /// @return True if the expression is a compile-time constant.
    bool isConstantExpr(const Expr &expr) const;

    /// @brief Evaluate a constant integer expression.
    /// @param expr The expression to evaluate (must be a constant integer expression).
    /// @return The integer value, or 0 if evaluation fails.
    int64_t evaluateConstantInt(const Expr &expr) const;

    /// @brief Evaluate a constant real expression.
    /// @param expr The expression to evaluate (must be a constant real expression).
    /// @return The real value, or 0.0 if evaluation fails.
    double evaluateConstantReal(const Expr &expr) const;

    /// @brief Evaluate a constant string expression.
    /// @param expr The expression to evaluate (must be a constant string expression).
    /// @return The string value, or empty string if evaluation fails.
    std::string evaluateConstantString(const Expr &expr) const;

    /// @brief Evaluate a constant boolean expression.
    /// @param expr The expression to evaluate (must be a constant boolean expression).
    /// @return The boolean value, or false if evaluation fails.
    bool evaluateConstantBool(const Expr &expr) const;

    /// @brief Fold a constant expression to a ConstantValue.
    /// @param expr The expression to evaluate (must be a constant expression).
    /// @return ConstantValue with hasValue=true if successful, hasValue=false otherwise.
    ConstantValue foldConstant(const Expr &expr);

    /// @brief Check for division by zero in a constant expression.
    /// @param expr The expression to check.
    /// @return True if the expression contains a division by zero.
    bool checkConstantDivZero(const Expr &expr);

    /// @brief Validate default parameters in a parameter list.
    /// @param params The parameters to validate.
    /// @param loc Location for error reporting.
    /// @return Number of required parameters (before first default).
    size_t validateDefaultParams(const std::vector<ParamDecl> &params, il::support::SourceLoc loc);

    //=========================================================================
    // Scope Management
    //=========================================================================

    /// @brief Push a new variable scope.
    void pushScope();

    /// @brief Pop the current variable scope.
    void popScope();

    /// @brief Add a variable to the current scope.
    void addVariable(const std::string &name, const PasType &type);

    /// @brief Add a local variable and track if it requires definite assignment.
    /// @details Non-nullable class/interface locals are tracked for definite assignment.
    void addLocalVariable(const std::string &name, const PasType &type);

    /// @brief Mark a non-nullable variable as definitely assigned.
    void markDefinitelyAssigned(const std::string &name);

    /// @brief Check if a non-nullable variable has been definitely assigned.
    bool isDefinitelyAssigned(const std::string &name) const;

    //=========================================================================
    // Error Reporting
    //=========================================================================

    /// @brief Report an error at a source location.
    void error(il::support::SourceLoc loc, const std::string &message);

    /// @brief Report an error at an expression's location.
    void error(const Expr &expr, const std::string &message);

    /// @brief Report an error at a statement's location.
    void error(const Stmt &stmt, const std::string &message);

    //=========================================================================
    // Built-in Registration
    //=========================================================================

    /// @brief Register primitive types (Integer, Real, Boolean, String).
    void registerPrimitives();

    /// @brief Register built-in functions (WriteLn, ReadLn, etc.).
    void registerBuiltins();

    /// @brief Register built-in units (Viper.Strings, Viper.Math).
    void registerBuiltinUnits();

    //=========================================================================
    // Flow-Sensitive Narrowing
    //=========================================================================

    /// @brief Check if a condition is a nil check (x <> nil or x = nil).
    /// @param expr The condition expression.
    /// @param varName Output: the variable name being checked.
    /// @param isNotNil Output: true if "x <> nil", false if "x = nil".
    /// @return True if this is a nil check on a simple variable.
    bool isNilCheck(const Expr &expr, std::string &varName, bool &isNotNil) const;

    /// @brief Push a narrowing scope with narrowed variable types.
    /// @param narrowed Map of variable names to their narrowed types.
    void pushNarrowing(const std::unordered_map<std::string, PasType> &narrowed);

    /// @brief Pop the current narrowing scope.
    void popNarrowing();

    /// @brief Invalidate narrowing for a variable (after assignment).
    /// @param varName The variable name to invalidate.
    void invalidateNarrowing(const std::string &varName);

    /// @brief Look up the effective type of a variable (checking narrowing first).
    /// @param name The variable name to look up.
    /// @return The narrowed type if available, otherwise the declared type.
    std::optional<PasType> lookupEffectiveType(const std::string &name) const;

    //=========================================================================
    // Member Variables
    //=========================================================================

    il::support::DiagnosticEngine &diag_; ///< Diagnostic engine
    bool hasError_{false};                ///< Error flag

    /// @brief Registered type names -> types
    std::unordered_map<std::string, PasType> types_;

    /// @brief Registered constant names -> types
    std::unordered_map<std::string, PasType> constants_;

    /// @brief Registered integer constant values (for compile-time evaluation)
    std::unordered_map<std::string, int64_t> constantValues_;

    /// @brief Registered real constant values (for compile-time evaluation)
    std::unordered_map<std::string, double> constantRealValues_;

    /// @brief Registered string constant values (for compile-time evaluation)
    std::unordered_map<std::string, std::string> constantStrValues_;

    /// @brief Stack of variable scopes (each is name -> type)
    std::vector<std::unordered_map<std::string, PasType>> varScopes_;

    /// @brief Registered function/procedure signatures
    std::unordered_map<std::string, FuncSignature> functions_;

    /// @brief Registered class information
    std::unordered_map<std::string, ClassInfo> classes_;

    /// @brief Registered interface information
    std::unordered_map<std::string, InterfaceInfo> interfaces_;

    /// @brief Registry of compiled units (lowercase name -> info)
    std::unordered_map<std::string, UnitInfo> units_;

    /// @brief Current loop depth (for break/continue validation)
    int loopDepth_{0};

    /// @brief Current except handler depth (for raise; validation)
    int exceptHandlerDepth_{0};

    /// @brief Current function being analyzed (for return type checking)
    const FuncSignature *currentFunction_{nullptr};

    /// @brief Current class being analyzed (for Self resolution)
    std::string currentClassName_;

    /// @brief Current method name when analyzing a method body (for inherited)
    std::string currentMethodName_;

    /// @brief Stack of narrowing scopes (for flow-sensitive type narrowing)
    /// Each scope maps variable names to their narrowed types.
    std::vector<std::unordered_map<std::string, PasType>> narrowingScopes_;

    /// @brief Set of read-only loop variables (lowercase names).
    /// @details Variables added here cannot be assigned during loop body analysis.
    std::set<std::string> readOnlyLoopVars_;

    /// @brief Set of undefined variables (lowercase names).
    /// @details Variables added here cannot be read; they become undefined after for loop exits.
    std::set<std::string> undefinedVars_;

    /// @brief Depth of nested procedure/function (0 = top-level)
    /// @details Used to reject nested procedures/functions in v0.1.
    int routineDepth_{0};

    /// @brief Set of non-nullable reference variables that require definite assignment (lowercase).
    /// @details Non-optional class/interface locals must be assigned before they are read.
    std::set<std::string> uninitializedNonNullableVars_;

    /// @brief Set of definitely-assigned non-nullable variables in current scope (lowercase).
    /// @details Variables are added here when assigned; removed from uninitializedNonNullableVars_.
    std::set<std::string> definitelyAssignedVars_;

    /// @brief Info about a 'with' context for name resolution.
    struct WithContext
    {
        PasType type;            ///< Type of the with expression (class or record)
        std::string tempVarName; ///< Generated temp variable name for lowering
    };

    /// @brief Stack of 'with' contexts (innermost first for lookup priority).
    std::vector<WithContext> withContexts_;

    /// @brief Cache for resolved types keyed by TypeNode address to avoid recomputation.
    std::unordered_map<const TypeNode *, PasType> typeCache_;
};

} // namespace il::frontends::pascal
