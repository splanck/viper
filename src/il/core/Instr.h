#pragma once
#include "il/core/Opcode.h"
#include "il/core/Type.h"
#include "il/core/Value.h"
#include <optional>
#include <string>
#include <vector>

namespace il::core {

/// @brief Instruction within a basic block.
struct Instr {
  std::optional<unsigned> result; ///< destination temp id
  Opcode op;
  Type type; ///< result type (or void)
  std::vector<Value> operands;
  std::string callee;              ///< for call
  std::vector<std::string> labels; ///< for branch targets
};

} // namespace il::core
