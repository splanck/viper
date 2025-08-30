#include "frontends/basic/LoweringContext.h"
#include "il/build/IRBuilder.h"
#include "il/core/BasicBlock.h"
#include "il/core/Function.h"

namespace il::frontends::basic {

LoweringContext::LoweringContext(build::IRBuilder &builder, core::Function &func)
    : builder(builder), function(func) {}

std::string LoweringContext::getOrCreateSlot(const std::string &name) {
  auto it = varSlots.find(name);
  if (it != varSlots.end())
    return it->second;
  std::string slot = "%" + name + "_slot";
  varSlots[name] = slot;
  return slot;
}

core::BasicBlock *LoweringContext::getOrCreateBlock(int line) {
  auto it = blocks.find(line);
  if (it != blocks.end())
    return it->second;
  std::string label = mangler.block("L" + std::to_string(line));
  core::BasicBlock &bb = builder.addBlock(function, label);
  blocks[line] = &bb;
  return &bb;
}

std::string LoweringContext::getOrAddString(const std::string &value) {
  auto it = strings.find(value);
  if (it != strings.end())
    return it->second;
  std::string name = ".L" + std::to_string(nextStringId++);
  strings[value] = name;
  return name;
}

} // namespace il::frontends::basic
