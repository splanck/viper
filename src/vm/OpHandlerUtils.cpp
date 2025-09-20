// File: src/vm/OpHandlerUtils.cpp
// License: MIT License. See LICENSE in the project root for full details.
// Purpose: Implement shared helper routines for VM opcode handlers.
// Key invariants: Helpers operate on VM frames without leaking references.
// Ownership/Lifetime: Functions mutate frame state in-place without storing globals.
// Links: docs/il-spec.md

#include "vm/OpHandlerUtils.hpp"

#include "il/core/Instr.hpp"

namespace il::vm::detail
{
namespace ops
{
void storeResult(Frame &fr, const il::core::Instr &in, const Slot &val)
{
    if (!in.result)
        return;
    if (fr.regs.size() <= *in.result)
        fr.regs.resize(*in.result + 1);
    fr.regs[*in.result] = val;
}
} // namespace ops
} // namespace il::vm::detail

