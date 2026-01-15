//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/SemanticAnalyzer_Internal.hpp
// Purpose: Declares shared helper utilities for SemanticAnalyzer implementation
// Key invariants: Helpers remain internal to the BASIC front end and avoid
// Ownership/Lifetime: Stateless free functions used by SemanticAnalyzer
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/BasicTypes.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "frontends/basic/SemanticAnalyzer_Stmts_Control.hpp"
#include "frontends/basic/SemanticAnalyzer_Stmts_IO.hpp"
#include "frontends/basic/SemanticAnalyzer_Stmts_Runtime.hpp"
#include "frontends/basic/SemanticAnalyzer_Stmts_Shared.hpp"

namespace il::frontends::basic::sem
{
class ExprCheckContext;

/// @brief Analyzes a unary expression and determines its result type.
///
/// Handles unary operators such as negation (-), logical NOT, and any other
/// prefix operators defined in BASIC. Validates that the operand type is
/// compatible with the operator and emits diagnostics for type mismatches.
///
/// @param analyzer The semantic analyzer instance containing symbol tables and diagnostics.
/// @param expr The unary expression AST node to analyze.
/// @return The computed result type of the unary expression, or Error on failure.
SemanticAnalyzer::Type analyzeUnaryExpr(SemanticAnalyzer &analyzer, const UnaryExpr &expr);

/// @brief Analyzes a binary expression and determines its result type.
///
/// Handles all binary operators including arithmetic (+, -, *, /, MOD, ^),
/// comparison (<, >, <=, >=, =, <>), logical (AND, OR, XOR), and string
/// concatenation (&). Uses the ExprRule table to validate operand types
/// and compute the result type. Emits diagnostics for incompatible operands.
///
/// @param analyzer The semantic analyzer instance containing symbol tables and diagnostics.
/// @param expr The binary expression AST node to analyze.
/// @return The computed result type, or Error if operands are incompatible.
SemanticAnalyzer::Type analyzeBinaryExpr(SemanticAnalyzer &analyzer, const BinaryExpr &expr);

/// @brief Analyzes a function or subroutine call expression.
///
/// Resolves the callee (function/subroutine name) in the symbol table, validates
/// argument count and types against the declaration, and determines the return
/// type. Handles both user-defined procedures and built-in functions. For
/// subroutine calls used in expression context, emits an error since SUBs
/// have no return value.
///
/// @param analyzer The semantic analyzer instance for symbol resolution.
/// @param expr The call expression AST node containing callee and arguments.
/// @return The return type of the called function, or Error on resolution failure.
SemanticAnalyzer::Type analyzeCallExpr(SemanticAnalyzer &analyzer, const CallExpr &expr);

/// @brief Analyzes a variable reference expression.
///
/// Looks up the variable in the current scope chain (local variables, parameters,
/// module-level variables). Validates that the variable has been declared and
/// determines its type. May modify the expression to attach resolved symbol info.
///
/// @param analyzer The semantic analyzer for scope and symbol lookup.
/// @param expr The variable expression (modified to store resolution result).
/// @return The declared type of the variable, or Error if undefined.
SemanticAnalyzer::Type analyzeVarExpr(SemanticAnalyzer &analyzer, VarExpr &expr);

/// @brief Analyzes an array element access expression.
///
/// Validates that the base expression refers to an array, checks that the
/// index expressions are numeric (integer or coercible), and verifies the
/// correct number of dimensions. Returns the element type of the array.
///
/// @param analyzer The semantic analyzer for type checking.
/// @param expr The array access expression (base + indices).
/// @return The element type of the array, or Error on dimension/type mismatch.
SemanticAnalyzer::Type analyzeArrayExpr(SemanticAnalyzer &analyzer, ArrayExpr &expr);

/// @brief Analyzes an LBOUND expression that returns an array's lower bound.
///
/// LBOUND(array [, dimension]) returns the lower bound index of an array
/// dimension. In BASIC, arrays typically have a lower bound of 0 or 1
/// depending on OPTION BASE. Validates the array argument and optional
/// dimension specifier.
///
/// @param analyzer The semantic analyzer for type checking.
/// @param expr The LBOUND expression with array and optional dimension.
/// @return Integer type (the bound is always an integer), or Error on failure.
SemanticAnalyzer::Type analyzeLBoundExpr(SemanticAnalyzer &analyzer, LBoundExpr &expr);

/// @brief Analyzes a UBOUND expression that returns an array's upper bound.
///
/// UBOUND(array [, dimension]) returns the upper bound index of an array
/// dimension. Combined with LBOUND, this allows iterating over array elements
/// without hardcoding sizes. Validates the array argument and optional
/// dimension specifier.
///
/// @param analyzer The semantic analyzer for type checking.
/// @param expr The UBOUND expression with array and optional dimension.
/// @return Integer type (the bound is always an integer), or Error on failure.
SemanticAnalyzer::Type analyzeUBoundExpr(SemanticAnalyzer &analyzer, UBoundExpr &expr);

} // namespace il::frontends::basic::sem

/// @brief Internal implementation details for the BASIC semantic analyzer.
///
/// This namespace contains helper functions and data structures used by
/// the semantic analyzer implementation. These are not part of the public
/// API and may change without notice. External code should use the
/// SemanticAnalyzer class directly.
namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Defines validation and type computation rules for binary operators.
///
/// Each binary operator in BASIC has specific rules for what operand types
/// are allowed and how the result type is computed. This structure captures
/// those rules in a table-driven format for efficient lookup during semantic
/// analysis of binary expressions.
///
/// @see exprRule() to look up rules by operator
struct ExprRule
{
    /// @brief Function pointer type for validating operand type combinations.
    ///
    /// Called during binary expression analysis to check if the left and right
    /// operand types are compatible with the operator. Should emit diagnostics
    /// via the ExprCheckContext if validation fails.
    ///
    /// @param ctx The expression checking context for emitting diagnostics.
    /// @param expr The binary expression being validated.
    /// @param lhs The type of the left operand.
    /// @param rhs The type of the right operand.
    /// @param diagMsg Diagnostic message template for type mismatches.
    using OperandValidator = void (*)(sem::ExprCheckContext &,
                                      const BinaryExpr &,
                                      SemanticAnalyzer::Type,
                                      SemanticAnalyzer::Type,
                                      std::string_view);

    /// @brief Function pointer type for computing the result type of a binary operation.
    ///
    /// Given the types of both operands, returns the type of the expression result.
    /// For example, Integer + Double yields Double due to numeric promotion.
    ///
    /// @param lhs The type of the left operand.
    /// @param rhs The type of the right operand.
    /// @return The computed result type of the binary operation.
    using ResultTypeFn = SemanticAnalyzer::Type (*)(SemanticAnalyzer::Type, SemanticAnalyzer::Type);

    BinaryExpr::Op op;           ///< The binary operator this rule applies to.
    OperandValidator validator;  ///< Function to validate operand type compatibility.
    ResultTypeFn result;         ///< Function to compute the result type.
    std::string_view mismatchDiag; ///< Diagnostic message template for type errors.
};

/// @brief Retrieves the expression rule for a given binary operator.
///
/// Looks up the validation and result type computation rules for the specified
/// operator from the internal rule table. Every valid BinaryExpr::Op value
/// must have a corresponding rule entry.
///
/// @param op The binary operator to look up.
/// @return Reference to the ExprRule for this operator.
/// @pre op must be a valid BinaryExpr::Op value.
const ExprRule &exprRule(BinaryExpr::Op op);

/// @brief Formats a human-readable error message for logical operator type mismatches.
///
/// Creates a diagnostic message explaining why a logical operation (AND, OR, XOR)
/// failed type checking, including the actual types of both operands.
///
/// @param op The logical operator that failed (AND, OR, or XOR).
/// @param lhs The type of the left operand.
/// @param rhs The type of the right operand.
/// @return A formatted error message string.
std::string formatLogicalOperandMessage(BinaryExpr::Op op,
                                        SemanticAnalyzer::Type lhs,
                                        SemanticAnalyzer::Type rhs);

/// @brief Determines the common numeric type for binary arithmetic operations.
///
/// Implements numeric type promotion rules for BASIC. When two numeric values
/// are combined in an arithmetic operation, they are promoted to a common type:
/// - Integer op Integer -> Integer
/// - Integer op Single -> Single
/// - Integer op Double -> Double
/// - Single op Double -> Double
/// - etc.
///
/// @param lhs The type of the left operand.
/// @param rhs The type of the right operand.
/// @return The common type after numeric promotion, or Error if not numeric.
SemanticAnalyzer::Type commonNumericType(SemanticAnalyzer::Type lhs,
                                         SemanticAnalyzer::Type rhs) noexcept;

/// @brief Computes the Levenshtein (edit) distance between two strings.
///
/// Used for "did you mean?" suggestions when a variable or function name
/// is not found. The edit distance measures how many single-character edits
/// (insertions, deletions, substitutions) are needed to transform one string
/// into another.
///
/// @param a The first string.
/// @param b The second string.
/// @return The minimum number of edits to transform a into b.
size_t levenshtein(const std::string &a, const std::string &b);

/// @brief Converts an AST type node to a semantic analyzer type enum.
///
/// Maps the Type representation used in the AST (which may include user-defined
/// types) to the SemanticAnalyzer::Type enum used during type checking.
///
/// @param ty The AST type to convert.
/// @return The corresponding semantic type.
SemanticAnalyzer::Type astToSemanticType(::il::frontends::basic::Type ty);

/// @brief Returns the canonical name of a built-in function.
///
/// Maps a BuiltinCallExpr::Builtin enum value to its string representation
/// as it would appear in BASIC source code (e.g., "LEN", "MID$", "VAL").
///
/// @param b The built-in function enum value.
/// @return The function name as a C string.
const char *builtinName(BuiltinCallExpr::Builtin b);

/// @brief Returns a human-readable name for a semantic type.
///
/// Used in diagnostic messages to describe types to the user.
/// Returns strings like "Integer", "String", "Double", etc.
///
/// @param type The semantic type to name.
/// @return The type name as a C string.
const char *semanticTypeName(SemanticAnalyzer::Type type);

/// @brief Returns the BASIC keyword for a logical operator.
///
/// Maps BinaryExpr::Op values for logical operators to their BASIC keywords.
///
/// @param op The logical operator (AND, OR, XOR, NOT).
/// @return The operator keyword as a C string (e.g., "AND", "OR").
const char *logicalOpName(BinaryExpr::Op op);

/// @brief Extracts a textual representation of a condition expression.
///
/// Used in diagnostic messages to show the user what condition expression
/// was being evaluated when an error occurred.
///
/// @param expr The expression to convert to text.
/// @return A string representation of the expression.
std::string conditionExprText(const Expr &expr);

/// @brief Determines the BASIC type from a variable name suffix.
///
/// In BASIC, variable names can have type suffixes: $ for String, % for Integer,
/// ! for Single, # for Double. This function extracts the type from such suffixes.
///
/// @param name The variable name potentially ending with a type suffix.
/// @return The BasicType if a suffix is present, or nullopt if no suffix.
std::optional<BasicType> suffixBasicType(std::string_view name);

/// @brief Converts a BasicType to a SemanticAnalyzer::Type.
///
/// Maps the BasicType enum (String, Integer, Single, Double, etc.) to the
/// corresponding SemanticAnalyzer::Type used during type checking.
///
/// @param type The BasicType to convert.
/// @return The corresponding semantic type, or nullopt if not mappable.
std::optional<SemanticAnalyzer::Type> semanticTypeFromBasic(BasicType type);

/// @brief Returns the uppercase BASIC keyword for a type.
///
/// Returns strings like "INTEGER", "STRING", "DOUBLE" for use in
/// diagnostic messages and AS clauses.
///
/// @param type The BasicType to name.
/// @return The uppercase type name as a string.
std::string uppercaseBasicTypeName(BasicType type);

/// @brief Checks if a semantic type is numeric (can participate in arithmetic).
///
/// Returns true for Integer, Single, Double, and other numeric types.
/// Returns false for String, Object, and error types.
///
/// @param type The type to check.
/// @return True if the type is numeric, false otherwise.
bool isNumericSemanticType(SemanticAnalyzer::Type type) noexcept;

} // namespace il::frontends::basic::semantic_analyzer_detail
