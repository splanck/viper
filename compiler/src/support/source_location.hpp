//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/source_location.hpp
// Purpose: Declares lightweight source location POD for diagnostics and IL metadata.
// Key invariants: file_id == 0 denotes an invalid location; line/column are 1-based when valid.
// Ownership/Lifetime: Value type with no dynamic ownership.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

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

    /// @brief Determine whether a concrete file identifier is attached.
    [[nodiscard]] bool hasFile() const
    {
        return file_id != 0;
    }

    /// @brief Determine whether a 1-based line number is available.
    [[nodiscard]] bool hasLine() const
    {
        return line != 0;
    }

    /// @brief Determine whether a 1-based column number is available.
    [[nodiscard]] bool hasColumn() const
    {
        return column != 0;
    }
};

/// @brief Represents a half-open range within a source file.
/// @invariant When valid, both @ref begin and @ref end originate from the same
///            file and @ref begin precedes @ref end.
/// @ownership Value type with no owned resources.
struct SourceRange
{
    /// @brief Starting position of the range; invalid when @ref isValid
    ///        returns false.
    SourceLoc begin{};

    /// @brief One-past-the-end location of the range; invalid when
    ///        @ref isValid returns false.
    SourceLoc end{};

    /// @brief Check whether both endpoints reference tracked source
    ///        locations.
    [[nodiscard]] bool isValid() const;
};

} // namespace il::support
