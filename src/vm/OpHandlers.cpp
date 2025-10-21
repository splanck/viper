//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/OpHandlers.cpp
// Purpose: Materialise opcode-dispatch tables used by the VM execution loop.
// Key invariants: Dispatch entries mirror il::core::Opcode enumerators and
//                 handler pointers respect the metadata in il/core/Opcode.def.
// Ownership/Lifetime: Tables are static and shared across VM instances; no
//                     dynamic allocation beyond compile-time arrays.
// Links: docs/runtime-vm.md#vm-dispatch
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers.hpp"

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"

#include <array>

using namespace il::core;

namespace il::vm
{
/// @brief Expose the lazily materialised opcode → handler mapping shared across
///        all VM instances.
/// @details Delegates to @ref detail::getOpcodeHandlers(), which consults the
///          declarative metadata emitted from Opcode.def so each
///          @ref il::core::Opcode enumerator reuses the dispatch handler recorded
///          alongside its definition.  The function returns a reference to a
///          process-wide table initialised on first use so subsequent calls incur
///          no rebuild cost.
/// @return Reference to the canonical opcode handler table.
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

/// @brief Populate an array that maps @ref VMDispatch categories to handler
///        function pointers.
/// @details The constexpr builder iterates the IL opcode table and records the
///          first handler pointer associated with each dispatch category.
///          Categories marked as @c VMDispatch::None remain @c nullptr so
///          opcodes can opt out of the VM dispatch entirely.  Because the logic
///          runs during compilation, the dispatch metadata stays aligned with
///          Opcode.def whenever the table is regenerated.
///
/// @return Array of handler pointers indexed by @ref VMDispatch enumerators.
constexpr std::array<VM::OpcodeHandler, kNumDispatchKinds> buildDispatchHandlers()
{
    std::array<VM::OpcodeHandler, kNumDispatchKinds> handlers{};

#define VM_DISPATCH_IMPL(DISPATCH, HANDLER_EXPR) (VMDispatch::DISPATCH, HANDLER_EXPR)
#define VM_DISPATCH(NAME) VM_DISPATCH_IMPL(NAME, &handle##NAME)
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

/// @brief Translate a @ref VMDispatch enumerator into an opcode handler.
/// @details Guarded against out-of-range enumerators to prevent accidental
///          access past the precomputed table.  Returning @c nullptr on invalid
///          categories allows the caller to flag metadata mismatches without
///          dereferencing garbage.  Categories explicitly mapped to
///          @ref VMDispatch::None also yield @c nullptr to signal that the opcode
///          bypasses the main dispatcher.
/// @param dispatch Dispatch category recorded in the opcode metadata.
/// @return Handler pointer associated with the category, or @c nullptr when the
///         category encodes @ref VMDispatch::None or lies out of range.
constexpr VM::OpcodeHandler handlerForDispatch(VMDispatch dispatch)
{
    const size_t index = static_cast<size_t>(dispatch);
    if (index >= kDispatchHandlers.size())
        return nullptr;
    return kDispatchHandlers[index];
}
} // namespace

namespace memory
{
/// @brief Handle load opcodes by delegating to the shared implementation.
/// @details Obtains the current execution state via
///          @ref VMAccess::currentExecState and forwards the VM, frame, and
///          instruction context to @ref handleLoadImpl.  Keeping the state lookup
///          here allows the shared implementation to remain agnostic about how
///          the dispatcher tracks execution frames while guaranteeing that loads
///          always see the most recent execution context.
/// @param vm Virtual machine coordinating execution.
/// @param fr Active frame whose registers the opcode manipulates.
/// @param in IL instruction being executed.
/// @param blocks Mapping from block labels to block definitions.
/// @param bb Reference to the pointer identifying the current block.
/// @param ip Instruction pointer index within @p bb.
/// @return Execution status from the shared load handler.
VM::ExecResult handleLoad(VM &vm,
                          Frame &fr,
                          const Instr &in,
                          const VM::BlockMap &blocks,
                          const BasicBlock *&bb,
                          size_t &ip)
{
    VMAccess::ExecState *state = VMAccess::currentExecState(vm);
    return handleLoadImpl(vm, state, fr, in, blocks, bb, ip);
}

/// @brief Handle store opcodes by delegating to the shared implementation.
/// @details Mirrors @ref handleLoad but forwards to @ref handleStoreImpl after
///          resolving the current execution state.  Abstracting the state lookup
///          avoids duplicating boilerplate across opcode definitions and keeps
///          the actual handler implementations focused on memory semantics.
/// @param vm Virtual machine coordinating execution.
/// @param fr Active frame whose registers the opcode manipulates.
/// @param in IL instruction being executed.
/// @param blocks Mapping from block labels to block definitions.
/// @param bb Reference to the pointer identifying the current block.
/// @param ip Instruction pointer index within @p bb.
/// @return Execution status from the shared store handler.
VM::ExecResult handleStore(VM &vm,
                           Frame &fr,
                           const Instr &in,
                           const VM::BlockMap &blocks,
                           const BasicBlock *&bb,
                           size_t &ip)
{
    VMAccess::ExecState *state = VMAccess::currentExecState(vm);
    return handleStoreImpl(vm, state, fr, in, blocks, bb, ip);
}
} // namespace memory

namespace integer
{
/// @brief Dispatch integer addition by binding the current execution state.
/// @details Fetches the thread-local execution state and passes it to
///          @ref handleAddImpl so arithmetic semantics remain centralised.
///          Separating the trampoline from the implementation keeps the
///          metadata-driven dispatch table simple while ensuring that integer
///          instructions always observe up-to-date VM context.
/// @param vm Virtual machine orchestrating the program.
/// @param fr Frame executing the opcode.
/// @param in IL instruction carrying operands and result.
/// @param blocks Mapping from block labels to block definitions.
/// @param bb Reference to the pointer identifying the current block.
/// @param ip Instruction pointer index within @p bb.
/// @return Execution status from the shared addition handler.
VM::ExecResult handleAdd(VM &vm,
                         Frame &fr,
                         const Instr &in,
                         const VM::BlockMap &blocks,
                         const BasicBlock *&bb,
                         size_t &ip)
{
    VMAccess::ExecState *state = VMAccess::currentExecState(vm);
    return handleAddImpl(vm, state, fr, in, blocks, bb, ip);
}

/// @brief Dispatch integer subtraction by binding the current execution state.
/// @details Resolves the thread-local execution state and forwards execution to
///          @ref handleSubImpl.  Implementing the trampoline in one place keeps
///          the main opcode table free from state-management boilerplate while
///          guaranteeing consistent state hand-off.
/// @param vm Virtual machine orchestrating the program.
/// @param fr Frame executing the opcode.
/// @param in IL instruction carrying operands and result.
/// @param blocks Mapping from block labels to block definitions.
/// @param bb Reference to the pointer identifying the current block.
/// @param ip Instruction pointer index within @p bb.
/// @return Execution status from the shared subtraction handler.
VM::ExecResult handleSub(VM &vm,
                         Frame &fr,
                         const Instr &in,
                         const VM::BlockMap &blocks,
                         const BasicBlock *&bb,
                         size_t &ip)
{
    VMAccess::ExecState *state = VMAccess::currentExecState(vm);
    return handleSubImpl(vm, state, fr, in, blocks, bb, ip);
}

/// @brief Dispatch integer multiplication by binding the current execution state.
/// @details Works identically to @ref handleAdd and @ref handleSub but forwards
///          to @ref handleMulImpl, ensuring multiplication benefits from the same
///          execution-context plumbing without duplicating code.
/// @param vm Virtual machine orchestrating the program.
/// @param fr Frame executing the opcode.
/// @param in IL instruction carrying operands and result.
/// @param blocks Mapping from block labels to block definitions.
/// @param bb Reference to the pointer identifying the current block.
/// @param ip Instruction pointer index within @p bb.
/// @return Execution status from the shared multiplication handler.
VM::ExecResult handleMul(VM &vm,
                         Frame &fr,
                         const Instr &in,
                         const VM::BlockMap &blocks,
                         const BasicBlock *&bb,
                         size_t &ip)
{
    VMAccess::ExecState *state = VMAccess::currentExecState(vm);
    return handleMulImpl(vm, state, fr, in, blocks, bb, ip);
}
} // namespace integer

/// @brief Build and cache the opcode dispatch table from the declarative IL
///        opcode list.
/// @details A function-local static initialises the table exactly once by
///          iterating the IL_OPCODE definitions in Opcode.def, which must stay
///          aligned with the @ref il::core::Opcode enumerators. Each metadata
///          entry contributes its recorded dispatch category, and
///          @ref handlerForDispatch translates that category to the handler
///          pointer stored in the lazily initialised table.
/// @return Reference to the cached opcode handler table.
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
