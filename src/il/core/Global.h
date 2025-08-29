#pragma once
#include "il/core/Type.h"
#include <string>

namespace il::core {

/// @brief Global constant or variable.
struct Global {
  std::string name;
  Type type;
  std::string init; // used for const str
};

} // namespace il::core
