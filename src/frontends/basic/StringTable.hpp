//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/StringTable.hpp
// Purpose: String literal interning and deduplication for BASIC frontend.
//
// This module provides efficient string literal management during lowering:
//   - Interning: Each unique string content gets exactly one IL global
//   - Label generation: Deterministic ".L<id>" labels for reproducible output
//   - Caching: Fast lookup for previously-seen strings
//
// Key Invariants:
//   - Each distinct string content produces exactly one global string
//   - Labels are monotonically increasing (.L0, .L1, .L2, ...)
//   - The table persists across procedure boundaries for deduplication
//
// Ownership/Lifetime:
//   - Owned by Lowerer (or LoweringContext)
//   - Lives for the duration of module lowering
//   - Reset between modules (not between procedures)
//
// Links: docs/architecture.md, docs/codemap.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::frontends::basic
{

/// @brief String literal interning table for IL lowering.
///
/// Manages string literal deduplication and global label generation.
/// Each unique string content is assigned a deterministic label and
/// registered as an IL global exactly once.
class StringTable
{
  public:
    /// @brief Callback type for registering string globals in IL.
    /// @param label The generated label (e.g., ".L0")
    /// @param content The string literal content
    using GlobalEmitter = std::function<void(const std::string &label, const std::string &content)>;

    /// @brief Default constructor creates an empty table.
    StringTable() = default;

    /// @brief Construct with a global emitter callback.
    /// @param emitter Callback invoked when a new string global is needed.
    explicit StringTable(GlobalEmitter emitter);

    /// @brief Set the global emitter callback.
    void setEmitter(GlobalEmitter emitter);

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// @brief Get or create a label for a string literal.
    /// @param content The string literal content.
    /// @return The IL global label for this string (e.g., ".L0").
    /// @details If this is the first time seeing this content, a new
    ///          global is registered via the emitter callback.
    [[nodiscard]] std::string intern(const std::string &content);

    /// @brief Check if a string has already been interned.
    /// @param content The string literal content.
    /// @return True if the string is already in the table.
    [[nodiscard]] bool contains(const std::string &content) const;

    /// @brief Look up a label without interning.
    /// @param content The string literal content.
    /// @return The label if found, empty string otherwise.
    [[nodiscard]] std::string lookup(const std::string &content) const;

    // =========================================================================
    // Statistics and Debugging
    // =========================================================================

    /// @brief Get the number of unique strings interned.
    [[nodiscard]] std::size_t size() const noexcept;

    /// @brief Check if the table is empty.
    [[nodiscard]] bool empty() const noexcept;

    /// @brief Get the next label ID that would be assigned.
    [[nodiscard]] std::size_t nextId() const noexcept;

    // =========================================================================
    // Lifecycle Management
    // =========================================================================

    /// @brief Clear all interned strings and reset the label counter.
    /// @details Call this between modules (not between procedures).
    void clear();

    /// @brief Reset the label counter without clearing cached strings.
    /// @note Typically not needed; use clear() for full reset.
    void resetCounter();

    // =========================================================================
    // Iteration
    // =========================================================================

    /// @brief Iterate over all interned strings.
    /// @param fn Callback receiving (label, content) pairs.
    template <typename Func>
    void forEach(Func &&fn) const
    {
        for (const auto &[content, label] : stringToLabel_)
            fn(label, content);
    }

  private:
    /// @brief Generate the next label.
    [[nodiscard]] std::string generateLabel();

    /// @brief Map from string content to assigned label.
    std::unordered_map<std::string, std::string> stringToLabel_;

    /// @brief Counter for deterministic label generation.
    std::size_t nextId_{0};

    /// @brief Callback for emitting global string definitions.
    GlobalEmitter emitter_;
};

} // namespace il::frontends::basic
