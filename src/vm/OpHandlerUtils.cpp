// File: src/vm/OpHandlerUtils.cpp
// License: MIT License. See LICENSE in the project root for full details.
// Purpose: Implement shared helper routines for VM opcode handlers.
// Key invariants: Helpers operate on VM frames without leaking references.
// Ownership/Lifetime: Functions mutate frame state in-place without storing globals.
// Links: docs/il-guide.md#reference

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

