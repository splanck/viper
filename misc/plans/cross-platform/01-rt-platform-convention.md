# Recommendation 1: Shared Platform And Capability Convention

## Problem

Viper already has a good C runtime platform header in `src/runtime/rt_platform.h`, but the rest of the tree still uses raw `_WIN32`, `__APPLE__`, and `__linux__` checks directly in codegen, tools, REPL, common utilities, and tests.

That creates three maintenance problems:

1. The repo uses different names for the same decision in different layers.
2. `#else` branches often collapse macOS and Linux together even when they are not truly equivalent.
3. Platform policy lives in random files instead of in a small number of approved abstraction layers.

Important correction: replacing `#ifdef _WIN32` with `#if RT_PLATFORM_WINDOWS` does **not** make inactive branches get parsed by the compiler. The benefit is consistency and explicitness, not magical cross-platform syntax checking.

## Solution

Adopt one shared platform-capability vocabulary across the whole repo.

### 1. Keep `rt_platform.h` as the runtime C layer

`src/runtime/rt_platform.h` remains the source of truth for C runtime code.

### 2. Add a C++ companion header

Create `src/common/PlatformCapabilities.hpp` for codegen, tools, REPL, and tests.

Suggested contents:

- Host OS macros:
  - `VIPER_HOST_WINDOWS`
  - `VIPER_HOST_MACOS`
  - `VIPER_HOST_LINUX`
- Host compiler macros:
  - `VIPER_COMPILER_MSVC`
  - `VIPER_COMPILER_CLANG`
  - `VIPER_COMPILER_GCC`
- Capability macros:
  - `VIPER_CAN_FORK`
  - `VIPER_CAN_LOCAL_BIND`
  - `VIPER_CAN_IPV6_LOOPBACK`
  - `VIPER_HAS_X11`
  - `VIPER_HAS_ALSA`
  - `VIPER_NATIVE_LINK_X86_64`
  - `VIPER_NATIVE_LINK_AARCH64`

The exact set can start small. The key is to name decisions by capability, not by ad hoc host checks.

### 3. Define approved raw-macro zones

Raw OS/compiler macros should remain allowed only in files that truly have to talk to the platform toolchain directly, for example:

- `src/runtime/rt_platform.h`
- platform backend files in `src/lib/graphics/src/` and `src/lib/audio/src/`
- low-level OS bridge files
- top-level CMake platform probes

Everywhere else should use the shared capability headers.

### 4. Add a repo-wide coding rule

Update `CLAUDE.md` and `docs/cross-platform/platform-checklist.md` with a short rule:

- use shared platform/capability macros in normal code
- use raw macros only inside designated adapter layers
- prefer explicit `elif` branches when behavior really differs
- avoid hiding macOS/Linux differences behind a bare `#else`

### 5. Add a short file-header reminder only in chokepoints

Do this only for high-risk files, not everywhere. A good header block should say:

- which platforms this file directly affects
- which sibling files/tests must be checked when editing it

Good initial candidates:

- `src/codegen/common/linker/NativeLinker.cpp`
- `src/codegen/common/RuntimeComponents.hpp`
- `src/common/RunProcess.cpp`
- `src/codegen/common/LinkerSupport.cpp`
- `src/codegen/x86_64/CodegenPipeline.cpp`
- `src/codegen/aarch64/CodegenPipeline.cpp`

## Files To Modify

- `src/runtime/rt_platform.h`
- new `src/common/PlatformCapabilities.hpp`
- `CLAUDE.md`
- `docs/cross-platform/platform-checklist.md`
- selected high-risk files for header reminders

## Effort

Medium.

The convention change itself is cheap. The real work is creating the shared C++ header and then moving new code to it instead of continuing macro sprawl.

## How It Prevents Breakage

**Before:** platform decisions are encoded differently in runtime, codegen, tests, and tools.

**After:** the repo has one shared vocabulary, one approved set of adapter boundaries, and a much clearer answer to "where should this platform-specific decision live?"
