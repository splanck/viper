# Recommendation 4: Disabled-Surface Completeness Audit

## Problem

The current risk is real, but the original draft solution was too brittle.

A shell regex over C headers and `rt_graphics_stubs.c` will not age well in this repo:

- public runtime functions use many return types and signatures
- some surfaces are declared through generated/runtime metadata rather than one simple header pattern
- the failure mode that matters is not "missing text in a file", it is "declared surface does not link when the feature is disabled"

Viper already has stronger building blocks than grep:

- `scripts/audit_runtime_surface.sh`
- `rtgen --audit`
- `RTGraphicsSurfaceLinkTests.cpp`

## Solution

Turn this into a generated disabled-surface audit that checks **linkability**, not just text presence.

## Implementation Outline

### 1. Define the surfaces that must exist in disabled builds

Start with:

- graphics-disabled runtime surface
- audio-disabled runtime surface

Optionally later:

- graphics-disabled 3D surface
- no-network/no-TLS surfaces where applicable

### 2. Generate the expected symbol list from real runtime metadata

Use `rtgen --audit` or a small companion mode to emit the expected C runtime entrypoints for each optional feature bucket.

That keeps the expected set tied to `runtime.def` and the generated class catalog instead of hand-maintained regexes.

### 3. Build dedicated disabled-surface link tests

For each optional surface:

- configure a build where the feature is disabled
- compile a generated translation unit that references every expected runtime symbol
- link it against the disabled/stubbed runtime

If any symbol is missing, the test fails immediately.

This is stronger than checking whether a stub function textually exists in a source file.

### 4. Keep a small symbol-audit script only as a helper

If a shell script is still useful, make it inspect built objects or libraries with `nm`/`llvm-nm`, not raw C syntax. Use it as a fast audit helper, not as the source of truth.

## Files To Modify

- `scripts/audit_runtime_surface.sh`
- `rtgen` audit path
- `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp`
- add matching audio-disabled link-smoke coverage
- runtime CMake files for feature-disabled test variants

## Effort

Medium.

The mechanism is more involved than a shell grep, but it is far more reliable and aligned with how Viper already audits runtime surface drift.

## How It Prevents Breakage

**Before:** a feature can work on a fully enabled developer machine and fail only when another machine links the disabled surface.

**After:** disabled-build linkability is exercised directly and automatically, using the real declared runtime surface rather than a hand-maintained approximation.
