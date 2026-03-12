//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/TextUtils.hpp
// Purpose: Text scanning utilities for language server hover and cursor ops.
// Key invariants:
//   - Pure text manipulation — no compiler or AST dependencies
//   - Line/column are 1-based (matching compiler convention)
// Ownership/Lifetime:
//   - All returned data is fully owned
// Links: tools/lsp-common/ICompilerBridge.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace viper::server
{

/// @brief Context extracted from cursor position for hover resolution.
struct HoverContext
{
    std::string identifier; ///< The identifier under the cursor
    std::string dotPrefix;  ///< Dot-chain prefix (e.g., "shell.app" for "shell.app.run")
    bool valid{false};      ///< False if cursor is on whitespace/operator
};

/// @brief Check if a character is part of an identifier.
bool isIdentChar(char c);

/// @brief Extract the identifier and dot-chain prefix at a cursor position.
/// @param source  Full source text.
/// @param line    1-based line number.
/// @param col     1-based column number.
/// @return HoverContext with extracted identifier and optional dot prefix.
HoverContext extractIdentifierAtCursor(const std::string &source, int line, int col);

} // namespace viper::server
