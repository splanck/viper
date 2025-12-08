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
#include "support/diagnostics.hpp"
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace il::frontends::pascal
{

//===----------------------------------------------------------------------===//
// Type Representation
//===----------------------------------------------------------------------===//

/// @brief Discriminator for Pascal type kinds.
enum class PasTypeKind
{
    Integer,   ///< 64-bit signed integer
    Real,      ///< Double-precision floating-point
    Boolean,   ///< Boolean (True/False)
    String,    ///< String type
    Enum,      ///< Enumeration type
    Array,     ///< Array type (static or dynamic)
    Record,    ///< Record type
    Class,     ///< Class type
    Interface, ///< Interface type
    Optional,  ///< Optional type (T?)
    Pointer,   ///< Pointer type (^T)
    Procedure, ///< Procedure type
    Function,  ///< Function type
    Set,       ///< Set type
    Range,     ///< Subrange type
    Nil,       ///< Nil literal type (assignable to optionals, pointers, classes)
    Unknown,   ///< Unknown/error type
    Void       ///< No value (procedure return)
};

/// @brief Represents a resolved Pascal type.
/// @details This structure captures the semantic meaning of types after
///          resolution from AST TypeNodes. It supports composite types
///          like arrays, optionals, and records.
struct PasType
{
    PasTypeKind kind{PasTypeKind::Unknown};

    /// For Named types: the fully-qualified type name (e.g., "TMyClass")
    std::string name;

    /// For Array: element type
    std::shared_ptr<PasType> elementType;

    /// For Array: dimension count (0 = dynamic array)
    size_t dimensions{0};

    /// For Optional: wrapped inner type
    std::shared_ptr<PasType> innerType;

    /// For Pointer: pointee type
    std::shared_ptr<PasType> pointeeType;

    /// For Enum: list of enumerator names
    std::vector<std::string> enumValues;

    /// For Enum constants: ordinal value (-1 if not an enum constant)
    int enumOrdinal{-1};

    /// For Record/Class: field name -> type
    std::map<std::string, std::shared_ptr<PasType>> fields;

    /// For Procedure/Function: parameter types
    std::vector<std::shared_ptr<PasType>> paramTypes;

    /// For Function: return type
    std::shared_ptr<PasType> returnType;

    /// @brief Create an unknown type.
    static PasType unknown()
    {
        PasType t;
        t.kind = PasTypeKind::Unknown;
        return t;
    }

    /// @brief Create a void type.
    static PasType voidType()
    {
        PasType t;
        t.kind = PasTypeKind::Void;
        return t;
    }

    /// @brief Create an integer type.
    static PasType integer()
    {
        PasType t;
        t.kind = PasTypeKind::Integer;
        return t;
    }

    /// @brief Create a real type.
    static PasType real()
    {
        PasType t;
        t.kind = PasTypeKind::Real;
        return t;
    }

    /// @brief Create a boolean type.
    static PasType boolean()
    {
        PasType t;
        t.kind = PasTypeKind::Boolean;
        return t;
    }

    /// @brief Create a string type.
    static PasType string()
    {
        PasType t;
        t.kind = PasTypeKind::String;
        return t;
    }

    /// @brief Create a nil type.
    static PasType nil()
    {
        PasType t;
        t.kind = PasTypeKind::Nil;
        return t;
    }

    /// @brief Create an optional type wrapping @p inner.
    static PasType optional(PasType inner)
    {
        PasType t;
        t.kind = PasTypeKind::Optional;
        t.innerType = std::make_shared<PasType>(std::move(inner));
        return t;
    }

    /// @brief Create an array type with @p elem element type.
    static PasType array(PasType elem, size_t dims = 0)
    {
        PasType t;
        t.kind = PasTypeKind::Array;
        t.elementType = std::make_shared<PasType>(std::move(elem));
        t.dimensions = dims;
        return t;
    }

    /// @brief Create a pointer type to @p pointee.
    static PasType pointer(PasType pointee)
    {
        PasType t;
        t.kind = PasTypeKind::Pointer;
        t.pointeeType = std::make_shared<PasType>(std::move(pointee));
        return t;
    }

    /// @brief Create an enum type with given values.
    static PasType enumType(std::vector<std::string> values)
    {
        PasType t;
        t.kind = PasTypeKind::Enum;
        t.enumValues = std::move(values);
        return t;
    }

    /// @brief Create an enum constant with a specific ordinal.
    /// @param typeName Name of the enum type.
    /// @param values All enum member names (for type identity).
    /// @param ordinal The ordinal value of this constant.
    static PasType enumConstant(std::string typeName, std::vector<std::string> values, int ordinal)
    {
        PasType t;
        t.kind = PasTypeKind::Enum;
        t.name = std::move(typeName);
        t.enumValues = std::move(values);
        t.enumOrdinal = ordinal;
        return t;
    }

    /// @brief Create a class type with a given name.
    static PasType classType(std::string className)
    {
        PasType t;
        t.kind = PasTypeKind::Class;
        t.name = std::move(className);
        return t;
    }

    /// @brief Create an interface type with a given name.
    static PasType interfaceType(std::string interfaceName)
    {
        PasType t;
        t.kind = PasTypeKind::Interface;
        t.name = std::move(interfaceName);
        return t;
    }

    /// @brief Check if this is an optional type (T?).
    bool isOptional() const { return kind == PasTypeKind::Optional; }

    /// @brief Unwrap an optional type to get the inner type.
    /// @return The inner type if this is optional, or *this if not optional.
    PasType unwrap() const
    {
        if (kind == PasTypeKind::Optional && innerType)
            return *innerType;
        return *this;
    }

    /// @brief Make this type optional (T -> T?).
    /// @return A new optional type wrapping this type.
    static PasType makeOptional(const PasType &t)
    {
        // Already optional - don't double-wrap
        if (t.kind == PasTypeKind::Optional)
            return t;
        return optional(t);
    }

    /// @brief Check if this is a non-optional reference type (class/interface).
    /// @details Non-optional reference types cannot be assigned nil.
    bool isNonOptionalReference() const
    {
        return (kind == PasTypeKind::Class || kind == PasTypeKind::Interface) &&
               !isOptional();
    }

    /// @brief Check if this type requires definite assignment before use.
    /// @details Non-optional class/interface locals must be definitely assigned before reading.
    bool requiresDefiniteAssignment() const
    {
        return isNonOptionalReference();
    }

    /// @brief Check if this is a numeric type (Integer or Real).
    bool isNumeric() const { return kind == PasTypeKind::Integer || kind == PasTypeKind::Real; }

    /// @brief Check if this is an ordinal type (Integer, Boolean, Enum, Range).
    bool isOrdinal() const
    {
        return kind == PasTypeKind::Integer || kind == PasTypeKind::Boolean ||
               kind == PasTypeKind::Enum || kind == PasTypeKind::Range;
    }

    /// @brief Check if this is a reference type (Class, Interface, dynamic Array, String).
    bool isReference() const
    {
        return kind == PasTypeKind::Class || kind == PasTypeKind::Interface ||
               kind == PasTypeKind::String ||
               (kind == PasTypeKind::Array && dimensions == 0);
    }

    /// @brief Check if this is a value type (Integer, Real, Boolean, Enum, Record, fixed Array).
    /// @details Value types need (hasValue, value) pair representation when optional.
    bool isValueType() const
    {
        return kind == PasTypeKind::Integer || kind == PasTypeKind::Real ||
               kind == PasTypeKind::Boolean || kind == PasTypeKind::Enum ||
               kind == PasTypeKind::Record ||
               (kind == PasTypeKind::Array && dimensions > 0);
    }

    /// @brief For optional types, check if inner type is a value type.
    bool isValueTypeOptional() const
    {
        if (kind != PasTypeKind::Optional || !innerType)
            return false;
        return innerType->isValueType();
    }

    /// @brief Check if nil can be assigned to this type.
    /// @details Per spec: nil can be assigned to T?, pointers, and dynamic arrays.
    ///          Non-optional class/interface types do NOT accept nil assignment.
    bool isNilAssignable() const
    {
        // Optional types always accept nil
        if (kind == PasTypeKind::Optional)
            return true;

        // Pointers accept nil
        if (kind == PasTypeKind::Pointer)
            return true;

        // Dynamic arrays accept nil
        if (kind == PasTypeKind::Array && dimensions == 0)
            return true;

        // Non-optional class/interface do NOT accept nil (per spec)
        // They require definite assignment before use
        return false;
    }

    /// @brief Check if this is an error/unknown type.
    bool isError() const { return kind == PasTypeKind::Unknown; }

    /// @brief Get a string representation of this type for diagnostics.
    std::string toString() const;
};

//===----------------------------------------------------------------------===//
// Function Signature
//===----------------------------------------------------------------------===//

/// @brief Signature for a procedure or function.
struct FuncSignature
{
    std::string name;                               ///< Procedure/function name
    std::vector<std::pair<std::string, PasType>> params; ///< Parameter name-type pairs
    std::vector<bool> isVarParam;                   ///< Whether each param is var/out
    std::vector<bool> hasDefault;                   ///< Whether each param has a default value
    PasType returnType;                             ///< Return type (Void for procedures)
    bool isForward{false};                          ///< Forward declaration?
    size_t requiredParams{0};                       ///< Number of required (non-default) params
};

//===----------------------------------------------------------------------===//
// Method and Field Information
//===----------------------------------------------------------------------===//

/// @brief Information about a class method.
struct MethodInfo
{
    std::string name;                                    ///< Method name
    std::vector<std::pair<std::string, PasType>> params; ///< Parameter name-type pairs
    std::vector<bool> isVarParam;                        ///< Whether each param is var/out
    std::vector<bool> hasDefault;                        ///< Whether each param has a default value
    PasType returnType;                                  ///< Return type (Void for procedures)
    bool isVirtual{false};                               ///< Marked virtual
    bool isOverride{false};                              ///< Marked override
    bool isAbstract{false};                              ///< Marked abstract
    Visibility visibility{Visibility::Public};           ///< Visibility
    il::support::SourceLoc loc;                          ///< Source location
    size_t requiredParams{0};                            ///< Number of required (non-default) params
};

/// @brief Information about a class field.
struct FieldInfo
{
    std::string name;                          ///< Field name
    PasType type;                              ///< Field type
    bool isWeak{false};                        ///< Marked weak
    Visibility visibility{Visibility::Public}; ///< Visibility
    il::support::SourceLoc loc;                ///< Source location
};

/// @brief Information about a class.
struct ClassInfo
{
    std::string name;                              ///< Class name
    std::string baseClass;                         ///< Base class name (empty if none)
    std::vector<std::string> interfaces;           ///< Implemented interface names
    std::map<std::string, MethodInfo> methods;     ///< Method name -> info (lowercase key)
    std::map<std::string, FieldInfo> fields;       ///< Field name -> info (lowercase key)
    bool hasConstructor{false};                    ///< Has at least one constructor
    bool hasDestructor{false};                     ///< Has a destructor
    il::support::SourceLoc loc;                    ///< Source location
};

/// @brief Information about an interface.
struct InterfaceInfo
{
    std::string name;                              ///< Interface name
    std::vector<std::string> baseInterfaces;       ///< Extended interface names
    std::map<std::string, MethodInfo> methods;     ///< Method name -> info (lowercase key)
    il::support::SourceLoc loc;                    ///< Source location
};

//===----------------------------------------------------------------------===//
// Unit Information
//===----------------------------------------------------------------------===//

/// @brief Information about a compiled unit's exports.
struct UnitInfo
{
    std::string name;                              ///< Unit name
    std::map<std::string, PasType> types;          ///< Exported types (lowercase key)
    std::map<std::string, PasType> constants;      ///< Exported constants (lowercase key)
    std::map<std::string, FuncSignature> functions; ///< Exported functions/procedures
    std::map<std::string, ClassInfo> classes;      ///< Exported classes
    std::map<std::string, InterfaceInfo> interfaces; ///< Exported interfaces
};

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
    bool hasError() const { return hasError_; }

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
    void checkOverridesWithBase(const ClassInfo &classInfo,
                                 const std::string &effectiveBaseClass);

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
    bool classInheritsFrom(const std::string &derivedName,
                           const std::string &baseName) const;

    /// @brief Check if an interface extends another interface.
    /// @param derivedName Name of the derived interface.
    /// @param baseName Name of the potential base interface.
    /// @return True if derived extends base (or they are the same).
    bool interfaceExtendsInterface(const std::string &derivedName,
                                    const std::string &baseName) const;

    /// @brief Collect all methods from interface and its bases.
    void collectInterfaceMethods(const std::string &ifaceName,
                                 std::map<std::string, MethodInfo> &methods) const;

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

    /// @brief Validate default parameters in a parameter list.
    /// @param params The parameters to validate.
    /// @param loc Location for error reporting.
    /// @return Number of required parameters (before first default).
    size_t validateDefaultParams(const std::vector<ParamDecl> &params,
                                  il::support::SourceLoc loc);

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
    void pushNarrowing(const std::map<std::string, PasType> &narrowed);

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
    bool hasError_{false};                 ///< Error flag

    /// @brief Registered type names -> types
    std::map<std::string, PasType> types_;

    /// @brief Registered constant names -> types
    std::map<std::string, PasType> constants_;

    /// @brief Registered integer constant values (for compile-time evaluation)
    std::map<std::string, int64_t> constantValues_;

    /// @brief Stack of variable scopes (each is name -> type)
    std::vector<std::map<std::string, PasType>> varScopes_;

    /// @brief Registered function/procedure signatures
    std::map<std::string, FuncSignature> functions_;

    /// @brief Registered class information
    std::map<std::string, ClassInfo> classes_;

    /// @brief Registered interface information
    std::map<std::string, InterfaceInfo> interfaces_;

    /// @brief Registry of compiled units (lowercase name -> info)
    std::map<std::string, UnitInfo> units_;

    /// @brief Current loop depth (for break/continue validation)
    int loopDepth_{0};

    /// @brief Current except handler depth (for raise; validation)
    int exceptHandlerDepth_{0};

    /// @brief Current function being analyzed (for return type checking)
    const FuncSignature *currentFunction_{nullptr};

    /// @brief Current class being analyzed (for Self resolution)
    std::string currentClassName_;

    /// @brief Stack of narrowing scopes (for flow-sensitive type narrowing)
    /// Each scope maps variable names to their narrowed types.
    std::vector<std::map<std::string, PasType>> narrowingScopes_;

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
};

} // namespace il::frontends::pascal
