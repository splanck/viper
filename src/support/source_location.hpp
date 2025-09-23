// File: src/support/source_location.hpp
// Purpose: Declares lightweight source location POD for diagnostics and IL metadata.
// Key invariants: file_id == 0 denotes an invalid location; line/column are 1-based when valid.
// Ownership/Lifetime: Value type with no dynamic ownership.
// Links: docs/codemap.md
#pragma once

#include <cstdint>

namespace il::support
{

/// @brief Represents an absolute position within a source file.
/// @invariant file_id == 0 indicates an unknown location.
/// @ownership Value type with no owned resources.
struct SourceLoc
{
    /// @brief Identifier assigned by SourceManager; 0 denotes invalid location.
    uint32_t file_id = 0;

    /// @brief One-based line number within the file; 0 when unknown.
    uint32_t line = 0;

    /// @brief One-based column number within the line; 0 when unknown.
    uint32_t column = 0;

    /// @brief Check whether the location references a valid file entry.
    [[nodiscard]] bool isValid() const;
};

} // namespace il::support

