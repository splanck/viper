// File: src/support/options.h
// Purpose: Declares command-line option parsing helpers.
// Key invariants: None.
// Ownership/Lifetime: Caller owns option values.
// Links: docs/class-catalog.md
#pragma once
#include <string>
/// @brief Global options controlling compiler behavior.
/// @invariant Flags are independent booleans.
/// @ownership Value type.
namespace il::support {
struct Options {
  bool trace = false;
  bool verify = true;
  std::string target = "x86_64";
};
} // namespace il::support
