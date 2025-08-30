// File: src/il/io/Serializer.h
// Purpose: Declares text serializer for IL modules.
// Key invariants: None.
// Ownership/Lifetime: Serializer operates on provided modules.
// Links: docs/il-spec.md
#pragma once
#include "il/core/Module.h"
#include <ostream>
#include <string>

namespace il::io {

/// @brief Serializes IL modules to their textual form.
class Serializer {
public:
  /// @brief Write module @p m to output stream @p os.
  /// @param m Module to serialize.
  /// @param os Destination stream.
  static void write(const il::core::Module &m, std::ostream &os);

  /// @brief Serialize module @p m to a string.
  /// @param m Module to serialize.
  /// @return Textual IL representation.
  static std::string toString(const il::core::Module &m);
};

} // namespace il::io
