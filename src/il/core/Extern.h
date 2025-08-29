#pragma once
#include "il/core/Type.h"
#include <string>
#include <vector>

namespace il::core {

/// @brief External function declaration.
struct Extern {
  std::string name;
  Type retType;
  std::vector<Type> params;
};

} // namespace il::core
