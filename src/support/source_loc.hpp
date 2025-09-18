// File: src/support/source_loc.hpp
// Purpose: Declares lightweight source location POD used across IL components.
// Key invariants: File id zero denotes an unknown location.
// Ownership/Lifetime: Simple value type stored by clients; no ownership semantics.
// Links: docs/class-catalog.md
#pragma once

#include <cstdint>

namespace il::support
{
/// @brief Absolute position within a source file.
struct SourceLoc
{
    /// Identifier assigned by SourceManager; 0 indicates an invalid location.
    uint32_t file_id = 0;

    /// 1-based line number within the file; 0 if unknown.
    uint32_t line = 0;

    /// 1-based column number within the line; 0 if unknown.
    uint32_t column = 0;

    /// @brief Whether this location refers to a tracked file.
    [[nodiscard]] bool isValid() const
    {
        return file_id != 0;
    }
};

} // namespace il::support
