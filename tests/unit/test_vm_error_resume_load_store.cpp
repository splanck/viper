// File: tests/unit/test_vm_error_resume_load_store.cpp
// Purpose: Ensure Error and ResumeTok memory loads preserve pointer values.
// Key invariants: Stored pointers for Error/ResumeTok types must round-trip through memory helpers.
// Ownership: Standalone unit test executable.
// Links: docs/codemap.md

#include "vm/OpHandlers_Memory.hpp"
#include "vm/Trap.hpp"
#include "vm/VM.hpp"

#include <cassert>

int main()
{
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

    return 0;
}
