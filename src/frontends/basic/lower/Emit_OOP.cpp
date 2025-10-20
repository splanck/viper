//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements object ownership emission for BASIC OOP features.
/// @details Helpers release retained object handles at scope exits. They
///          consult the ProcedureContext for active blocks and append
///          control-flow as required, leaving terminator management consistent
///          with control helpers. Temporary values remain owned by the lowerer
///          while runtime helpers manage the underlying reference counts.

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

/// @brief Release retained object locals held by the active procedure.
/// @details Defer to the emitter implementation so that reference counting and
///          runtime helper sequencing remain centralised.  The parameter filter
///          prevents releasing locals that alias by-reference parameters.
/// @param paramNames Parameter names that should be excluded from release.
void Lowerer::releaseObjectLocals(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseObjectLocals(paramNames);
}

/// @brief Release owned object parameters when leaving a procedure.
/// @details Ensures the runtime decrements reference counts for object
///          parameters that were retained upon entry.  Delegating to the shared
///          emitter guarantees that all lowering paths follow the same runtime
///          calling convention.
/// @param paramNames Parameter names whose object handles must be released.
void Lowerer::releaseObjectParams(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseObjectParams(paramNames);
}

} // namespace il::frontends::basic
