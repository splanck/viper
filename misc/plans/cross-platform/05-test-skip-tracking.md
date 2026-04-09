# Recommendation 5: Capability-Aware Skip Tracking

## Problem

Some tests currently short-circuit on a host platform and effectively report "pass" even though they never exercised the intended behavior. The repo already has useful labels and `VIPER_TEST_SKIP()` support, but skip accounting is not yet centralized or consistently applied.

This creates false confidence, especially on Windows-specific gaps and feature-dependent tests.

## Solution

Make skip behavior first-class in the test harness and ctest registration layer.

## Implementation Outline

### 1. Set `SKIP_RETURN_CODE 77` centrally

Do this in the shared CMake helpers instead of repeating it per test:

- `viper_add_test()`
- `viper_add_ctest()`

That change belongs in `src/tests/cmake/TestHelpers.cmake`.

### 2. Add one shared C helper for non-C++ tests

Create a small header, for example `src/tests/common/PlatformSkip.h`, that provides:

- `VIPER_PLATFORM_SKIP(reason)` for C tests
- shared exit code `77`

That removes the need for ad hoc `return 0;` skip patterns in C tests.

### 3. Prefer per-test skips over whole-file exits

When only some test cases are host-limited, use per-test skips instead of abandoning the whole executable.

For C++ tests, prefer `VIPER_TEST_SKIP("reason")`.

For C tests, use the shared platform-skip helper.

### 4. Attach skip reasons to capability labels

When a test skips, it should be obvious whether the cause is:

- `requires_display`
- `requires_posix_shell`
- `requires_ipv6`
- missing host capability
- known broken host behavior

The label system in `TestHelpers.cmake` is already a good foundation. The plan here is to make the skip reporting line up with that labeling.

### 5. Add a simple skip budget report

Do not fail CI immediately on all skips. Instead:

- print skip counts by label or reason
- track them over time
- optionally fail only if a category unexpectedly grows

That keeps the plan realistic while still making gaps visible.

## Files To Modify

- `src/tests/cmake/TestHelpers.cmake`
- new `src/tests/common/PlatformSkip.h`
- selected tests that currently short-circuit silently

## Effort

Medium.

The central helper changes are small. The main work is the one-time cleanup of existing silent skip sites.

## How It Prevents Breakage

**Before:** a host can show a large swath of tests as "passed" even when those tests never actually ran.

**After:** skips are explicit, counted, categorized, and visible enough to drive real follow-up work.
