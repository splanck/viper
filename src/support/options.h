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
