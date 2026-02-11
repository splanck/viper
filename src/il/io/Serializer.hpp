//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/io/Serializer.hpp
// Purpose: Declares the Serializer class -- converts IL modules to their
//          textual representation in Pretty (human-readable) or Canonical
//          (minimal, deterministic) mode. Inverse of Parser; output conforms
//          to the IL grammar and round-trips through parse(serialize(m)).
// Key invariants:
//   - Serializer is stateless; all methods are static and thread-safe.
//   - Output conforms to docs/il-guide.md grammar.
// Ownership/Lifetime: Stateless facade. The caller owns the ostream and
//          Module passed to write()/toString().
// Links: docs/il-guide.md, il/io/Parser.hpp, il/core/fwd.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"
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
