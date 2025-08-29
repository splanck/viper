#pragma once
#include "il/core/BasicBlock.h"
#include "il/core/Function.h"
#include "il/core/Module.h"
#include "il/core/Opcode.h"
#include "il/core/Value.h"
#include <optional>
#include <vector>

namespace il::build {

using namespace il::core;

/// @brief Helper to construct IL modules and enforce block termination.
class IRBuilder {
public:
  explicit IRBuilder(Module &m);

  Extern &addExtern(const std::string &name, Type ret, const std::vector<Type> &params);
  Global &addGlobalStr(const std::string &name, const std::string &value);
  Function &startFunction(const std::string &name, Type ret, const std::vector<Param> &params);
  BasicBlock &addBlock(Function &fn, const std::string &label);
  void setInsertPoint(BasicBlock &bb);

  Value emitConstStr(const std::string &globalName);
  void emitCall(const std::string &callee, const std::vector<Value> &args);
  void emitRet(const std::optional<Value> &v = std::nullopt);

private:
  Module &mod;
  Function *curFunc{nullptr};
  BasicBlock *curBlock{nullptr};
  unsigned nextTemp{0};

  Instr &append(Instr instr);
  bool isTerminator(Opcode op) const;
};

} // namespace il::build
