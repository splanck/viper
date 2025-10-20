//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements object ownership emission for BASIC OOP features.
/// @details Helpers release retained object handles at scope exits. They
/// consult the ProcedureContext for active blocks and append control-flow as
/// required, leaving terminator management consistent with control helpers.
/// Temporary values remain owned by the lowerer while runtime helpers manage
/// the underlying reference counts.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

void Lowerer::releaseObjectLocals(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseObjectLocals(paramNames);
}

void Lowerer::releaseObjectParams(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseObjectParams(paramNames);
}

} // namespace il::frontends::basic
