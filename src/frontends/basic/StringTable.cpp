//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements the BASIC frontend string interning table.
/// @details Provides the out-of-line definitions for the shared string table
///          used by BASIC lowering. The table assigns deterministic labels to
///          unique string literals, caches content-to-label mappings, and
///          optionally notifies the emitter callback exactly once per unique
///          literal.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/StringTable.hpp"

namespace il::frontends::basic
{

/// @brief Construct a string table with an emitter callback.
/// @details Stores the provided callback and leaves the table empty; no globals
///          are emitted until @ref intern is invoked.
/// @param emitter Callback invoked when a new string literal must be emitted.
StringTable::StringTable(GlobalEmitter emitter) : emitter_(std::move(emitter)) {}

/// @brief Replace the global-emission callback.
/// @details Updates the callback used for subsequent @ref intern calls. Existing
///          cached strings are not re-emitted when the emitter changes.
/// @param emitter New callback for emitting string globals.
void StringTable::setEmitter(GlobalEmitter emitter)
{
    emitter_ = std::move(emitter);
}

// =============================================================================
// Core Operations
// =============================================================================

/// @brief Return the label for a string literal, creating it if necessary.
/// @details If the literal was already interned, returns the cached label. If
///          not, generates the next deterministic label (".L<id>"), invokes the
///          emitter callback if present, records the mapping, and returns it.
/// @param content String literal content to intern.
/// @return Deterministic label for the content.
std::string StringTable::intern(const std::string &content)
{
    // Check if already interned
    auto it = stringToLabel_.find(content);
    if (it != stringToLabel_.end())
        return it->second;

    // Generate new label
    std::string label = generateLabel();

    // Register the global via callback
    if (emitter_)
        emitter_(label, content);

    // Cache and return
    stringToLabel_.emplace(content, label);
    return label;
}

/// @brief Check whether a string literal has been interned.
/// @param content String literal content to query.
/// @return True if the table already contains a label for @p content.
bool StringTable::contains(const std::string &content) const
{
    return stringToLabel_.find(content) != stringToLabel_.end();
}

/// @brief Look up a label without inserting a new entry.
/// @details This does not call the emitter or allocate new labels.
/// @param content String literal content to look up.
/// @return Cached label, or an empty string if not interned.
std::string StringTable::lookup(const std::string &content) const
{
    auto it = stringToLabel_.find(content);
    if (it != stringToLabel_.end())
        return it->second;
    return {};
}

// =============================================================================
// Statistics and Debugging
// =============================================================================

/// @brief Return the number of unique strings in the table.
std::size_t StringTable::size() const noexcept
{
    return stringToLabel_.size();
}

/// @brief Check whether the table is empty.
bool StringTable::empty() const noexcept
{
    return stringToLabel_.empty();
}

/// @brief Report the next label id that would be assigned.
/// @details Useful for debugging and deterministic output checks.
std::size_t StringTable::nextId() const noexcept
{
    return nextId_;
}

// =============================================================================
// Lifecycle Management
// =============================================================================

/// @brief Remove all cached literals and reset the label counter.
/// @details This does not emit any diagnostics or callbacks; it simply clears
///          the local cache and returns the table to its initial state.
void StringTable::clear()
{
    stringToLabel_.clear();
    nextId_ = 0;
}

/// @brief Reset the label counter without clearing cached literals.
/// @details Intended for specialized tooling; if existing entries remain, the
///          next generated label may collide with earlier labels.
void StringTable::resetCounter()
{
    nextId_ = 0;
}

// =============================================================================
// Internal Helpers
// =============================================================================

/// @brief Generate the next deterministic string label.
/// @details Labels are emitted as ".L<id>" where @c id is a monotonically
///          increasing counter.
/// @return Newly generated label string.
std::string StringTable::generateLabel()
{
    return ".L" + std::to_string(nextId_++);
}

} // namespace il::frontends::basic
