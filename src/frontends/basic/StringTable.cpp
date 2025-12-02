//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/StringTable.cpp
// Purpose: Implementation of string literal interning table.
//
// Key invariants:
//   - Labels are generated as ".L<n>" where n is monotonically increasing
//   - The emitter callback is invoked exactly once per unique string content
//   - Interning the same content returns the same label every time
//
// Ownership/Lifetime: See StringTable.hpp
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/StringTable.hpp"

namespace il::frontends::basic
{

StringTable::StringTable(GlobalEmitter emitter) : emitter_(std::move(emitter)) {}

void StringTable::setEmitter(GlobalEmitter emitter)
{
    emitter_ = std::move(emitter);
}

// =============================================================================
// Core Operations
// =============================================================================

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

bool StringTable::contains(const std::string &content) const
{
    return stringToLabel_.find(content) != stringToLabel_.end();
}

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

std::size_t StringTable::size() const noexcept
{
    return stringToLabel_.size();
}

bool StringTable::empty() const noexcept
{
    return stringToLabel_.empty();
}

std::size_t StringTable::nextId() const noexcept
{
    return nextId_;
}

// =============================================================================
// Lifecycle Management
// =============================================================================

void StringTable::clear()
{
    stringToLabel_.clear();
    nextId_ = 0;
}

void StringTable::resetCounter()
{
    nextId_ = 0;
}

// =============================================================================
// Internal Helpers
// =============================================================================

std::string StringTable::generateLabel()
{
    return ".L" + std::to_string(nextId_++);
}

} // namespace il::frontends::basic
