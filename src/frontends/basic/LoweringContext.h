#pragma once
#include "frontends/basic/NameMangler.h"
#include <string>
#include <unordered_map>

namespace il {
namespace core {
struct BasicBlock;
class Function;
} // namespace core
namespace build {
class IRBuilder;
} // namespace build
} // namespace il

namespace il::frontends::basic {

/// @brief Tracks mappings needed during BASIC lowering.
/// @invariant Each variable, line, and string literal is unique in its map.
/// @ownership Holds references to IR structures owned elsewhere.
class LoweringContext {
public:
  LoweringContext(build::IRBuilder &builder, core::Function &func);

  /// @brief Get or create stack slot name for BASIC variable @p name.
  std::string getOrCreateSlot(const std::string &name);

  /// @brief Get or create a block for the given BASIC line number.
  core::BasicBlock *getOrCreateBlock(int line);

  /// @brief Get deterministic name for string literal @p value.
  std::string getOrAddString(const std::string &value);

private:
  build::IRBuilder &builder;
  core::Function &function;
  NameMangler mangler;
  std::unordered_map<std::string, std::string> varSlots;
  std::unordered_map<int, core::BasicBlock *> blocks;
  std::unordered_map<std::string, std::string> strings;
  unsigned nextStringId{0};
};

} // namespace il::frontends::basic
