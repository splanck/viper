// File: src/vm/OpHandlerUtils.hpp
// Purpose: Shared helper routines for VM opcode handlers.
// Key invariants: Helpers operate on VM frames without leaking references.
// Ownership/Lifetime: Functions mutate frame state in-place without storing globals.
// Links: docs/il-spec.md
#pragma once

#include "vm/VM.hpp"

namespace il::vm::detail
{
namespace ops
{
/// @brief Store the result of an instruction if it produces one.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param val Slot to write into the destination register.
inline void storeResult(Frame &fr, const il::core::Instr &in, const Slot &val)
{
    if (!in.result)
        return;
    if (fr.regs.size() <= *in.result)
        fr.regs.resize(*in.result + 1);
    fr.regs[*in.result] = val;
}
} // namespace ops
} // namespace il::vm::detail

