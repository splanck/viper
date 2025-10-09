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

#include "vm/OpHandlerUtils.hpp"

#include "il/core/Instr.hpp"

namespace il::vm::detail
{
namespace ops
{
/// @brief Write an opcode result into the destination register while honouring
///        ownership semantics.
///
/// The helper resizes the register file on demand, retains/release runtime
/// strings when the destination type is @ref il::core::Type::Kind::Str, and
/// then stores the slot payload.  Handlers delegate here to avoid duplicating
/// register management logic.
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
        if (hadRegister)
            rt_str_release_maybe(fr.regs[destIndex].str);

        Slot stored = val;
        rt_str_retain_maybe(stored.str);
        fr.regs[destIndex] = stored;
        return;
    }

    fr.regs[destIndex] = val;
}
} // namespace ops
} // namespace il::vm::detail

