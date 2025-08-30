// File: src/il/io/Parser.h
// Purpose: Declares parser for IL textual representation.
// Key invariants: None.
// Ownership/Lifetime: Parser views input string without owning.
// Links: docs/il-spec.md
#pragma once
#include "il/core/Module.h"
#include <istream>
#include <ostream>

namespace il::io {

/// @brief Hand-rolled parser for textual IL subset.
class Parser {
public:
  /// Parse IL from stream into module. Returns true on success.
  static bool parse(std::istream &is, il::core::Module &m, std::ostream &err);
};

} // namespace il::io
