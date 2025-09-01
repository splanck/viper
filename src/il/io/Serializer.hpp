// File: src/il/io/Serializer.hpp
// Purpose: Declares text serializer for IL modules.
// Key invariants: None.
// Ownership/Lifetime: Serializer operates on provided modules.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Module.hpp"
#include <ostream>
#include <string>

namespace il::io
{

/// @brief Serializes IL modules to their textual form.
class Serializer
{
  public:
    /// @brief Controls output formatting style.
    enum class Mode
    {
        Pretty,
        Canonical
    };

    /// @brief Write module @p m to output stream @p os.
    /// @param m Module to serialize.
    /// @param os Destination stream.
    /// @param mode Formatting mode (pretty by default).
    static void write(const il::core::Module &m, std::ostream &os, Mode mode = Mode::Pretty);

    /// @brief Serialize module @p m to a string.
    /// @param m Module to serialize.
    /// @param mode Formatting mode (pretty by default).
    /// @return Textual IL representation.
    static std::string toString(const il::core::Module &m, Mode mode = Mode::Pretty);
};

} // namespace il::io
