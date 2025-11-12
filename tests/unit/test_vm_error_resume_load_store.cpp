// File: tests/unit/test_vm_error_resume_load_store.cpp
// Purpose: Ensure Error and ResumeTok memory loads preserve pointer values.
// Key invariants: Stored pointers for Error/ResumeTok types must round-trip through memory helpers.
// Ownership: Standalone unit test executable.
// Links: docs/codemap.md

#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "vm/OpHandlers_Memory.hpp"
#include "vm/Trap.hpp"
#include "vm/VM.hpp"

#include <cassert>

int main()
{
    using il::core::Opcode;
    using il::core::Type;
    using il::vm::Frame;
    using il::vm::Slot;
    using il::vm::VmError;
    using il::vm::detail::memory::inline_impl::loadSlotFromPtr;
    using il::vm::detail::memory::inline_impl::storeSlotToPtr;

    VmError errorPayload{};
    Slot errorSlot{};
    errorSlot.ptr = &errorPayload;
    void *errorCell = nullptr;
    storeSlotToPtr(Type::Kind::Error, &errorCell, errorSlot);
    Slot loadedError = loadSlotFromPtr(Type::Kind::Error, &errorCell);
    assert(loadedError.ptr == errorSlot.ptr);

    Frame::ResumeState resumeState{};
    Slot resumeSlot{};
    resumeSlot.ptr = &resumeState;
    void *resumeCell = nullptr;
    storeSlotToPtr(Type::Kind::ResumeTok, &resumeCell, resumeSlot);
    Slot loadedResume = loadSlotFromPtr(Type::Kind::ResumeTok, &resumeCell);
    assert(loadedResume.ptr == resumeSlot.ptr);

    il::core::Module module;
    il::vm::VM vm(module);

    {
        il::vm::Frame frame{};
        frame.regs.resize(1);

        il::core::Instr constNull;
        constNull.result = 0U;
        constNull.op = Opcode::ConstNull;
        constNull.type = Type(Type::Kind::Error);

        il::vm::VM::BlockMap blocks;
        const il::core::BasicBlock *bbCtx = nullptr;
        size_t ipCtx = 0;

        il::vm::detail::memory::handleConstNull(vm, frame, constNull, blocks, bbCtx, ipCtx);
        assert(frame.regs[0].ptr == nullptr);
    }

    {
        il::vm::Frame frame{};
        frame.regs.resize(1);
        frame.regs[0].ptr = reinterpret_cast<void *>(0x1);

        il::core::Instr constNull;
        constNull.result = 0U;
        constNull.op = Opcode::ConstNull;
        constNull.type = Type(Type::Kind::ResumeTok);

        il::vm::VM::BlockMap blocks;
        const il::core::BasicBlock *bbCtx = nullptr;
        size_t ipCtx = 0;

        il::vm::detail::memory::handleConstNull(vm, frame, constNull, blocks, bbCtx, ipCtx);
        assert(frame.regs[0].ptr == nullptr);
    }

    return 0;
}
