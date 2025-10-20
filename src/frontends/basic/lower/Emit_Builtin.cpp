//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements runtime helper emission for BASIC built-ins.
/// @details These utilities manage array ownership by retaining and releasing
/// handles while appending the required calls to the active block. They do not
/// produce terminators and therefore rely on control helpers to manage block
/// lifetimes; temporaries remain owned by the lowerer and follow the standard
/// ProcedureContext tracking.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"

using namespace il::core;

namespace il::frontends::basic
{

void Lowerer::storeArray(Value slot, Value value)
{
    emitter().storeArray(slot, value);
}

void Lowerer::releaseArrayLocals(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseArrayLocals(paramNames);
}

void Lowerer::releaseArrayParams(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseArrayParams(paramNames);
}

} // namespace il::frontends::basic
