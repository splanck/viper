//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/vm/int_ops_cmp.cpp
//
// Summary:
//   Defines the integer comparison opcode handlers for the Viper virtual
//   machine.  Each handler delegates to the shared comparison helper while
//   supplying the predicate that implements the desired IL semantics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Integer comparison opcode handlers for the VM.
/// @details Provides thin wrappers around @ref ops::applyCompare that evaluate
///          signed and unsigned predicates over @ref Slot values.  The handlers
///          normalise results to the canonical @c i1 representation expected by
///          the interpreter.

#include "vm/OpHandlers_Int.hpp"

namespace il::vm::detail::integer
{

/// Macro to generate a comparison handler that delegates to ops::applyCompare.
/// Each handler has the same signature and simply varies in the comparison predicate.
#define DEFINE_CMP_HANDLER(Name, Pred)                                                             \
    VM::ExecResult Name(VM &vm,                                                                    \
                        Frame &fr,                                                                 \
                        const il::core::Instr &in,                                                 \
                        const VM::BlockMap &,                                                      \
                        const il::core::BasicBlock *&,                                             \
                        size_t &)                                                                  \
    {                                                                                              \
        return ops::applyCompare(vm, fr, in, Pred);                                                \
    }

// Signed / equality comparisons (operate on i64 directly)
DEFINE_CMP_HANDLER(handleICmpEq,
                   [](const Slot &a, const Slot &b) { return a.i64 == b.i64; })
DEFINE_CMP_HANDLER(handleICmpNe,
                   [](const Slot &a, const Slot &b) { return a.i64 != b.i64; })
DEFINE_CMP_HANDLER(handleSCmpGT,
                   [](const Slot &a, const Slot &b) { return a.i64 > b.i64; })
DEFINE_CMP_HANDLER(handleSCmpLT,
                   [](const Slot &a, const Slot &b) { return a.i64 < b.i64; })
DEFINE_CMP_HANDLER(handleSCmpLE,
                   [](const Slot &a, const Slot &b) { return a.i64 <= b.i64; })
DEFINE_CMP_HANDLER(handleSCmpGE,
                   [](const Slot &a, const Slot &b) { return a.i64 >= b.i64; })

// Unsigned comparisons (cast to uint64_t before comparing)
DEFINE_CMP_HANDLER(handleUCmpLT, [](const Slot &a, const Slot &b) {
    return static_cast<uint64_t>(a.i64) < static_cast<uint64_t>(b.i64);
})
DEFINE_CMP_HANDLER(handleUCmpLE, [](const Slot &a, const Slot &b) {
    return static_cast<uint64_t>(a.i64) <= static_cast<uint64_t>(b.i64);
})
DEFINE_CMP_HANDLER(handleUCmpGT, [](const Slot &a, const Slot &b) {
    return static_cast<uint64_t>(a.i64) > static_cast<uint64_t>(b.i64);
})
DEFINE_CMP_HANDLER(handleUCmpGE, [](const Slot &a, const Slot &b) {
    return static_cast<uint64_t>(a.i64) >= static_cast<uint64_t>(b.i64);
})

#undef DEFINE_CMP_HANDLER

} // namespace il::vm::detail::integer
