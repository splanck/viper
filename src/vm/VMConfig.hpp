// src/vm/VMConfig.hpp
// Purpose: Defines compile-time configuration flags for the VM subsystem.
// Invariants: All macros mirror the active build configuration options.
// Ownership: Shared header; no owning object or runtime state.
// Notes: Controlled by CMake options declared in the project root.

#pragma once

#if defined(VIPER_VM_THREADED) && (defined(__GNUC__) || defined(__clang__))
#define VIPER_THREADING_SUPPORTED 1
#else
#define VIPER_THREADING_SUPPORTED 0
#endif

// -----------------------------------------------------------------------------
// Dispatch hook macros (compiled away by default)
//
// These macros are invoked immediately before and after executing each
// instruction in the VM regardless of dispatch strategy (switch/labels/table).
// They default to empty do-while blocks so the optimizer removes them entirely
// when not overridden by the build or embedding application.
// -----------------------------------------------------------------------------
#ifndef VIPER_VM_DISPATCH_BEFORE
#define VIPER_VM_DISPATCH_BEFORE(ST, OPCODE) do { } while (0)
#endif

#ifndef VIPER_VM_DISPATCH_AFTER
#define VIPER_VM_DISPATCH_AFTER(ST, OPCODE)  do { } while (0)
#endif

// -----------------------------------------------------------------------------
// Tail-call optimisation toggle
// -----------------------------------------------------------------------------
#ifndef VIPER_VM_TAILCALL
#define VIPER_VM_TAILCALL 0
#endif

// -----------------------------------------------------------------------------
// Opcode execution counters (compile-time + runtime toggle)
// -----------------------------------------------------------------------------
#ifndef VIPER_VM_OPCOUNTS
#define VIPER_VM_OPCOUNTS 1
#endif

#if VIPER_VM_OPCOUNTS
#undef VIPER_VM_DISPATCH_BEFORE
#define VIPER_VM_DISPATCH_BEFORE(ST, OPCODE)                                                     \
    do                                                                                           \
    {                                                                                            \
        if ((ST).config.enableOpcodeCounts)                                                      \
            ++((ST).vm()->opCounts_[static_cast<size_t>(OPCODE)]);                               \
    } while (0)
#endif
