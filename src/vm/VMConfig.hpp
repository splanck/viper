//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/VMConfig.hpp
// Purpose: Defines compile-time configuration flags for the VM subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: Shared header; no owning object or runtime state.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

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
#define VIPER_VM_DISPATCH_BEFORE(ST, OPCODE)                                                       \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#endif

#ifndef VIPER_VM_DISPATCH_AFTER
#define VIPER_VM_DISPATCH_AFTER(ST, OPCODE)                                                        \
    do                                                                                             \
    {                                                                                              \
        auto &cfg = (ST).config;                                                                   \
        if (cfg.interruptEveryN && ((++(ST).pollTick % cfg.interruptEveryN) == 0))                 \
        {                                                                                          \
            if (cfg.pollCallback && !(cfg.pollCallback(*((ST).vm()))))                             \
            {                                                                                      \
                (ST).requestPause();                                                               \
            }                                                                                      \
        }                                                                                          \
    } while (0)
#endif

// -----------------------------------------------------------------------------
// Tail-call optimisation toggle
// -----------------------------------------------------------------------------
// Tail-call is enabled by default; can be overridden via -D or env handled at runtime.
#ifndef VIPER_VM_TAILCALL
#define VIPER_VM_TAILCALL 1
#endif

// -----------------------------------------------------------------------------
// Opcode execution counters (compile-time + runtime toggle)
// -----------------------------------------------------------------------------
#ifndef VIPER_VM_OPCOUNTS
#define VIPER_VM_OPCOUNTS 1
#endif

#if VIPER_VM_OPCOUNTS
#undef VIPER_VM_DISPATCH_BEFORE
#define VIPER_VM_DISPATCH_BEFORE(ST, OPCODE)                                                       \
    do                                                                                             \
    {                                                                                              \
        if ((ST).config.enableOpcodeCounts)                                                        \
            ++((ST).vm()->opCounts_[static_cast<size_t>(OPCODE)]);                                 \
    } while (0)
#endif
