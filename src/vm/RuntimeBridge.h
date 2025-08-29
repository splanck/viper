#pragma once
#include "rt.h"
#include <string>
#include <vector>

namespace il::vm {

union Slot; // defined in VM.h

class RuntimeBridge {
public:
  static Slot call(const std::string &name, const std::vector<Slot> &args);
};

} // namespace il::vm
