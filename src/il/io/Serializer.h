#pragma once
#include "il/core/Module.h"
#include <ostream>
#include <string>

namespace il::io {

class Serializer {
public:
  static void write(const il::core::Module &m, std::ostream &os);
  static std::string toString(const il::core::Module &m);
};

} // namespace il::io
