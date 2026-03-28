//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/TextUtils.cpp
// Purpose: Implementation of text scanning utilities for hover/cursor ops.
// Key invariants:
//   - Line/column are 1-based
//   - Dot-chain scanning walks backwards from the cursor
// Ownership/Lifetime:
//   - All returned data is fully owned
// Links: tools/lsp-common/TextUtils.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/TextUtils.hpp"

#include <cctype>

namespace viper::server {

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

HoverContext extractIdentifierAtCursor(const std::string &source, int line, int col) {
    HoverContext ctx;

    // Find the start of the requested line (1-based).
    size_t lineStart = 0;
    int curLine = 1;
    for (size_t i = 0; i < source.size() && curLine < line; ++i) {
        if (source[i] == '\n') {
            ++curLine;
            lineStart = i + 1;
        }
    }

    // Find line end.
    size_t lineEnd = lineStart;
    while (lineEnd < source.size() && source[lineEnd] != '\n')
        ++lineEnd;

    // col is 1-based; convert to 0-based offset within line.
    size_t cursorOff = lineStart + static_cast<size_t>(col - 1);
    if (cursorOff > lineEnd)
        cursorOff = lineEnd;

    // Expand left from cursor to find identifier start.
    size_t idStart = cursorOff;
    while (idStart > lineStart && isIdentChar(source[idStart - 1]))
        --idStart;

    // Expand right from cursor to find identifier end.
    size_t idEnd = cursorOff;
    while (idEnd < lineEnd && isIdentChar(source[idEnd]))
        ++idEnd;

    if (idStart == idEnd)
        return ctx; // cursor on whitespace/operator

    ctx.identifier = source.substr(idStart, idEnd - idStart);
    ctx.valid = true;

    // Scan left past dots to capture dot-chain prefix (e.g., "shell.app").
    if (idStart > lineStart && source[idStart - 1] == '.') {
        size_t prefixEnd = idStart - 1; // position of the '.'
        size_t prefixStart = prefixEnd;
        // Walk backwards through identifiers and dots.
        while (prefixStart > lineStart) {
            size_t p = prefixStart - 1;
            if (isIdentChar(source[p])) {
                while (p > lineStart && isIdentChar(source[p - 1]))
                    --p;
                prefixStart = p;
                if (p > lineStart && source[p - 1] == '.')
                    prefixStart = p - 1;
                else
                    break;
            } else {
                break;
            }
        }
        ctx.dotPrefix = source.substr(prefixStart, prefixEnd - prefixStart);
    }

    return ctx;
}

} // namespace viper::server
