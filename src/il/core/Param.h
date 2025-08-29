#pragma once
#include "il/core/Type.h"
#include <string>

namespace il::core {

/// @brief Function parameter.
struct Param {
  std::string name;
  Type type;
};

} // namespace il::core
