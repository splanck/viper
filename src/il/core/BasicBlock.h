#pragma once
#include "il/core/Instr.h"
#include <string>
#include <vector>

namespace il::core {

/// @brief Sequence of instructions terminated by a control-flow instruction.
struct BasicBlock {
  std::string label;
  std::vector<Instr> instructions;
  bool terminated = false;
};

} // namespace il::core
