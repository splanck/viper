# Recommendation 7: Host Capability Smoke Slices

## Problem

The original draft proposed fake cross-platform header compilation with forced macros and synthetic SDK types. That is unlikely to be robust in this repo and would create a second, fragile approximation of real host behavior.

The better local signal is not "pretend to be Windows on macOS." It is:

- validate the real capability bundle of the current host
- make that bundle explicit
- run a small, stable smoke slice for it after each build

## Solution

Define a handful of host capability smoke slices and make them easy to run from build scripts and CI.

## Implementation Outline

### 1. Define capability bundles

Examples:

- `host_core`
- `host_graphics`
- `host_audio`
- `host_native_link`
- `host_examples_smoke`
- `host_headless_disabled_surfaces`

Each host only runs the bundles that it is expected to support.

### 2. Map existing tests into those bundles

Viper already has useful labels and smoke probes:

- runtime smoke tests
- native-link/codegen smoke
- example `smoke_probe.zia` programs
- graphics/runtime link-surface tests

The work here is to define a curated subset that is:

- fast
- representative
- stable enough to run all the time

### 3. Add one wrapper script

Create something like `scripts/run_cross_platform_smoke.sh` that:

- detects the host
- prints the expected capability bundle
- runs the appropriate ctest labels or named tests
- runs the right example smoke probes
- reports skips explicitly

### 4. Hook it into the canonical build flow

After the normal build:

- run the smoke slice for the host
- keep it short enough to be reasonable for everyday use

This should complement, not replace, full `ctest`.

## Files To Modify

- `src/tests/cmake/TestHelpers.cmake`
- `src/tests/CMakeLists.txt`
- new `scripts/run_cross_platform_smoke.sh`
- build scripts that should invoke it

## Effort

Medium.

Most of the tests already exist. The main work is choosing the right slices and making their host expectations explicit.

## How It Prevents Breakage

**Before:** the fast local signal is "it built here."

**After:** the fast local signal is "it built here and passed the smoke slice that corresponds to this host's supported capabilities."
