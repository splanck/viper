//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file ZiaAstPrinter.hpp
/// @brief Human-readable dump of a Zia AST module.
///
/// @details Produces an indentation-based tree dump of all declaration,
/// statement, expression, and type nodes in a Zia AST. Each node is printed
/// with its kind, key identifying attributes (names, operators, literal
/// values), and source location. Children are recursively printed with
/// increased indentation.
///
/// Example output:
/// @code
///   ModuleDecl "MyModule" (1:1)
///     FunctionDecl "main" (3:1)
///       Params:
///         Param "x" (3:10)
///           Type: NamedType "Integer" (3:13)
///       ReturnType: NamedType "Integer" (3:25)
///       Body:
///         ReturnStmt (4:5)
///           BinaryExpr (+) (4:12)
///             IdentExpr "x" (4:12)
///             IntLiteral 1 (4:16)
/// @endcode
///
/// @invariant Printing never mutates the AST; only traverses for reading.
/// @invariant Output is deterministic for reproducible test results.
///
/// @see AST.hpp -- Zia AST node definitions.
/// @see AstPrinter.hpp -- analogous printer for the BASIC frontend.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/AST.hpp"
#include <string>

namespace il::frontends::zia
{

/// @brief Produces a human-readable dump of a Zia AST module.
class ZiaAstPrinter
{
  public:
    /// @brief Dump the entire module declaration tree.
    /// @param module The root module declaration to print.
    /// @return String containing the formatted tree dump.
    std::string dump(const ModuleDecl &module);
};

} // namespace il::frontends::zia
