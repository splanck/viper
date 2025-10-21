//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Bridges the lowering driver with the reusable emitter utilities that
// manipulate BASIC array temporaries.  The helpers provided here forward the
// procedural interface exposed on `Lowerer` to the stateful emitter instance so
// that ownership bookkeeping remains encapsulated in one component.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements runtime helper emission forwarding for BASIC built-ins.
/// @details The functions in this translation unit act as the out-of-line glue
///          between `Lowerer` and the reusable emitter implementation.  Keeping
///          the forwarding logic here avoids including emitter internals in
///          every translation unit that instantiates `Lowerer`, while also
///          documenting the ownership conventions used for array-valued
///          temporaries.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Record a lowered array value into a stack slot owned by the procedure.
///
/// @details Lowered BASIC operations often yield temporary arrays that must be
///          retained so subsequent statements can access them.  The emitter is
///          responsible for pairing the store with the matching runtime retain
///          call; this forwarding helper simply hands the store request to that
///          component so all ownership tracking remains centralised.
///
/// @param slot Address where the array handle should be written.
/// @param value Array value produced by the lowering routine.
void Lowerer::storeArray(Value slot, Value value)
{
    emitter().storeArray(slot, value);
}

/// @brief Release any array locals that were materialised within the current procedure.
///
/// @details Array temporaries lowered from BASIC constructs require paired
///          release calls so the runtime can drop reference counts.  This
///          helper simply forwards to the shared emitter instance, which tracks
///          which local slots own arrays and emits the finaliser calls in a
///          deterministic order.
///
/// @param paramNames Name set describing parameters that must be preserved.
void Lowerer::releaseArrayLocals(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseArrayLocals(paramNames);
}

/// @brief Request runtime releases for array parameters once a procedure exits.
///
/// @details Procedures that accept array arguments borrow ownership from the
///          caller.  Before returning, the lowering pipeline has to synthesise
///          release helpers so reference counts remain balanced.  Delegating the
///          actual emission to the central emitter guarantees that the
///          canonical release order is respected across all lowering sites.
///
/// @param paramNames Identifier set describing the formal parameters that
///        should be handed back to the runtime.
void Lowerer::releaseArrayParams(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseArrayParams(paramNames);
}

} // namespace il::frontends::basic
