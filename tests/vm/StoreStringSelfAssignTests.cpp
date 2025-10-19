// File: tests/vm/StoreStringSelfAssignTests.cpp
// Purpose: Regression test ensuring storeResult handles self-assigning strings.
// Key invariants: Retains incoming handles before releasing existing register contents.
// Ownership/Lifetime: Test releases any allocated runtime strings before exit.
// Links: docs/testing.md

#include "vm/OpHandlerUtils.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"

#include "rt_internal.h"
#include "rt_string.h"

int main()
{
    il::vm::Frame fr{};
    fr.regs.resize(1);

    const rt_string original = rt_str_i32_alloc(7);
    if (!original)
        return 1;

    fr.regs[0].str = original;

    il::core::Instr instr;
    instr.result = 0U;
    instr.op = il::core::Opcode::Trap;
    instr.type = il::core::Type{il::core::Type::Kind::Str};

    il::vm::Slot value{};
    value.str = fr.regs[0].str; // Self-assignment scenario.

    auto *impl = reinterpret_cast<rt_string_impl *>(original);
    if (!impl || !impl->heap)
        return 1;

    const size_t refBefore = impl->heap->refcnt;

    il::vm::detail::ops::storeResult(fr, instr, value);

    if (fr.regs[0].str != original)
        return 1;

    if (impl->heap->refcnt != refBefore)
        return 1;

    rt_str_release_maybe(fr.regs[0].str);
    fr.regs[0].str = nullptr;
    return 0;
}
