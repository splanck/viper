// File: src/vm/OpHandlers.cpp
// Purpose: Build the VM opcode dispatch table from declarative opcode metadata.
// Key invariants: Table entries align with il/core/Opcode.def definitions.
// Ownership/Lifetime: Dispatch table is lazily initialised and shared across VMs.
// License: MIT (see LICENSE file in the project root for terms).
// Links: docs/il-guide.md#reference

#include "vm/OpHandlers.hpp"

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"

#include <array>

using namespace il::core;

namespace il::vm
{
/// @brief Exposes the lazily materialised opcode → handler mapping shared across
/// all VM instances.
/// @details Delegates to detail::getOpcodeHandlers(), which consults the
/// declarative metadata emitted from Opcode.def so each il::core::Opcode
/// enumerator reuses the dispatch handler recorded alongside its definition.
/// Entries are cached after first construction to avoid recomputing the table.
const VM::OpcodeHandlerTable &VM::getOpcodeHandlers()
{
    return detail::getOpcodeHandlers();
}
} // namespace il::vm

namespace il::vm::detail
{
namespace
{
constexpr size_t kNumDispatchKinds = static_cast<size_t>(VMDispatch::Count);

constexpr std::array<VM::OpcodeHandler, kNumDispatchKinds> buildDispatchHandlers()
{
    std::array<VM::OpcodeHandler, kNumDispatchKinds> handlers{};

#define VM_DISPATCH_IMPL(DISPATCH, HANDLER_EXPR) (VMDispatch::DISPATCH, HANDLER_EXPR)
#define VM_DISPATCH(NAME) VM_DISPATCH_IMPL(NAME, &OpHandlers::handle##NAME)
#define VM_DISPATCH_ALT(DISPATCH, HANDLER_EXPR) VM_DISPATCH_IMPL(DISPATCH, HANDLER_EXPR)
#define IL_OPCODE(NAME,                                                                            \
                  MNEMONIC,                                                                        \
                  RES_ARITY,                                                                       \
                  RES_TYPE,                                                                        \
                  MIN_OPS,                                                                         \
                  MAX_OPS,                                                                         \
                  OP0,                                                                             \
                  OP1,                                                                             \
                  OP2,                                                                             \
                  SIDE_EFFECTS,                                                                    \
                  SUCCESSORS,                                                                      \
                  TERMINATOR,                                                                      \
                  DISPATCH,                                                                        \
                  PARSE0,                                                                          \
                  PARSE1,                                                                          \
                  PARSE2,                                                                          \
                  PARSE3)                                                                          \
    IL_VM_REGISTER_DISPATCH DISPATCH

#define IL_VM_REGISTER_DISPATCH(DISPATCH_ENUM, HANDLER_EXPR)                                       \
    if constexpr ((DISPATCH_ENUM) != VMDispatch::None)                                             \
    {                                                                                              \
        auto &slot = handlers[static_cast<size_t>(DISPATCH_ENUM)];                                 \
        if (slot == nullptr)                                                                       \
        {                                                                                          \
            slot = HANDLER_EXPR;                                                                   \
        }                                                                                          \
    }

#include "il/core/Opcode.def"

#undef IL_VM_REGISTER_DISPATCH
#undef IL_OPCODE
#undef VM_DISPATCH_ALT
#undef VM_DISPATCH
#undef VM_DISPATCH_IMPL

    return handlers;
}

constexpr auto kDispatchHandlers = buildDispatchHandlers();

constexpr VM::OpcodeHandler handlerForDispatch(VMDispatch dispatch)
{
    const size_t index = static_cast<size_t>(dispatch);
    if (index >= kDispatchHandlers.size())
        return nullptr;
    return kDispatchHandlers[index];
}
} // namespace

/// @brief Builds and caches the opcode dispatch table from the declarative IL
/// opcode list.
/// @details A function-local static initialises the table exactly once by
/// iterating the IL_OPCODE definitions in Opcode.def, which must stay aligned
/// with the il::core::Opcode enumerators. Each metadata entry contributes its
/// recorded dispatch category, and handlerForDispatch translates that category
/// to the handler pointer stored in the lazily initialised table.
const VM::OpcodeHandlerTable &getOpcodeHandlers()
{
    static const VM::OpcodeHandlerTable table = []
    {
        VM::OpcodeHandlerTable handlers{};
#define IL_OPCODE(NAME,                                                                            \
                  MNEMONIC,                                                                        \
                  RES_ARITY,                                                                       \
                  RES_TYPE,                                                                        \
                  MIN_OPS,                                                                         \
                  MAX_OPS,                                                                         \
                  OP0,                                                                             \
                  OP1,                                                                             \
                  OP2,                                                                             \
                  SIDE_EFFECTS,                                                                    \
                  SUCCESSORS,                                                                      \
                  TERMINATOR,                                                                      \
                  DISPATCH,                                                                        \
                  PARSE0,                                                                          \
                  PARSE1,                                                                          \
                  PARSE2,                                                                          \
                  PARSE3)                                                                          \
    handlers[static_cast<size_t>(Opcode::NAME)] = handlerForDispatch(DISPATCH);
#include "il/core/Opcode.def"
#undef IL_OPCODE
        return handlers;
    }();
    return table;
}

} // namespace il::vm::detail
