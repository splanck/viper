#pragma once
#include "il/core/Extern.h"
#include "il/core/Function.h"
#include "il/core/Global.h"
#include <vector>

namespace il::core {

/// @brief IL module aggregating externs, globals, and functions.
struct Module {
  std::vector<Extern> externs;
  std::vector<Global> globals;
  std::vector<Function> functions;
};

} // namespace il::core
