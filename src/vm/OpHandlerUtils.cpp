//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Collects helper routines shared across the opcode handler implementations.
// The functions encapsulate the common bookkeeping required when writing back
// to frame registers so individual handlers remain focused on opcode semantics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements shared utilities for opcode handlers in the VM.
/// @details Most opcode handlers delegate register writes to these helpers so
///          string reference counting and register resizing logic live in one
///          well-documented location.

#include "vm/OpHandlerUtils.hpp"

#include "il/core/Instr.hpp"

namespace il::vm::detail
{
namespace ops
{
/// @brief Write an opcode result into the destination register while honouring
///        ownership semantics.
///
/// @details The helper resizes the register file on demand, retains/releases
///          runtime strings when the destination type is
///          @ref il::core::Type::Kind::Str, and then stores the slot payload.
///          Handlers delegate here to avoid duplicating register management
///          logic or forgetting to balance string reference counts.  When the
///          instruction lacks a result operand the function simply returns,
///          allowing opcode implementations to call it unconditionally.
///
/// @param fr Frame whose register file receives the result.
/// @param in Instruction describing the destination register and result type.
/// @param val Evaluated result slot to copy into the register file.
void storeResult(Frame &fr, const il::core::Instr &in, const Slot &val)
{
    if (!in.result)
        return;
    const size_t destIndex = *in.result;
    const bool hadRegister = destIndex < fr.regs.size();
    if (!hadRegister)
        fr.regs.resize(destIndex + 1);

    if (in.type.kind == il::core::Type::Kind::Str)
    {
        Slot stored = val;
        rt_str_retain_maybe(stored.str);

        Slot &dest = fr.regs[destIndex];
        if (hadRegister)
            rt_str_release_maybe(dest.str);

        dest = stored;
        return;
    }

    fr.regs[destIndex] = val;
}
} // namespace ops
} // namespace il::vm::detail

