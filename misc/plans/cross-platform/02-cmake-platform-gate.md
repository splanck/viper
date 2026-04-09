# Recommendation 2: CMake Capability Gate And Build Summary

## Problem

On Linux, graphics and audio currently can silently disappear when dependencies are missing:

- `src/lib/graphics/CMakeLists.txt` returns early if X11 is not found
- `src/lib/audio/CMakeLists.txt` returns early if ALSA is not found
- `src/runtime/CMakeLists.txt` then falls back to disabled surfaces if the library target does not exist

That means two developers can run "the same build" and produce different binaries depending on what happens to be installed locally.

## Solution

Move from silent target omission to explicit feature modes with a single configure-time summary.

## Proposed Policy

Replace boolean "enable if available" behavior with tri-state modes:

- `AUTO`: use the feature if the dependency exists, otherwise disable with an explicit summary line
- `REQUIRE`: fail configure if the feature cannot be built
- `OFF`: do not build the feature

Suggested cache variables:

- `VIPER_GRAPHICS_MODE=AUTO|REQUIRE|OFF`
- `VIPER_AUDIO_MODE=AUTO|REQUIRE|OFF`
- optionally later:
  - `VIPER_GUI_MODE=AUTO|REQUIRE|OFF`
  - `VIPER_NETWORK_MODE=AUTO|REQUIRE|OFF`

## Implementation Outline

### 1. Stop using `return()` as the policy decision

Leaf `CMakeLists.txt` files should probe and report availability, but they should not make the final policy choice by disappearing from the build graph.

Instead, have each library set two values upward:

- availability boolean, for example `VIPERGFX_AVAILABLE`
- human-readable reason, for example `VIPERGFX_UNAVAILABLE_REASON`

### 2. Make the top-level configure step the single decision point

Top-level `CMakeLists.txt` should:

- read the requested mode
- inspect availability and reason strings
- decide whether to build, disable, or fail
- print one summary block for the entire build

### 3. Print the actual product shape

The summary should include at least:

- host OS
- compiler/toolchain
- graphics availability and backend
- audio availability and backend
- GUI availability
- native linker support for the current host

Example:

```text
==========================================
 Viper Capability Summary
==========================================
 Host OS:               Linux
 Compiler:              clang++
 Graphics:              ENABLED (X11)
 Audio:                 DISABLED (ALSA not found, AUTO mode)
 GUI:                   ENABLED
 Native x86_64 link:    ENABLED
 Native AArch64 link:   DISABLED
==========================================
```

### 4. Make `REQUIRE` fail loudly

If the requested mode is `REQUIRE`, then configuration should fail with:

- what is missing
- why it is unavailable
- how to install or disable it intentionally

## Files To Modify

- `CMakeLists.txt`
- `src/lib/graphics/CMakeLists.txt`
- `src/lib/audio/CMakeLists.txt`
- `src/runtime/CMakeLists.txt`

## Effort

Medium.

The change is mechanically small, but it touches the top-level configure policy and how subprojects report capability availability.

## How It Prevents Breakage

**Before:** a missing Linux dependency silently changes the product, and the failure is discovered later in demos or runtime tests.

**After:** the build either fails immediately or prints an explicit capability summary, so nobody accidentally ships or tests the wrong product shape.
