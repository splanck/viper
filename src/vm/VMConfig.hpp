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
