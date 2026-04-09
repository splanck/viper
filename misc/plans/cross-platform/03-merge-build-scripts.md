# Recommendation 3: Consolidate Build Entry Points

## Problem

The current build scripts are close enough to drift but far enough apart to keep drifting:

- `scripts/build_viper_mac.sh`
- `scripts/build_viper_linux.sh`
- `scripts/build_viper_win.cmd`
- `scripts/build_viper.sh`

The macOS and Linux scripts are almost identical. The wrapper script also currently references `build_viper.cmd`, while the actual Windows script in the tree is `build_viper_win.cmd`.

That is exactly the kind of low-grade divergence that causes platform-only breakage later.

## Solution

Create one canonical POSIX build driver, keep compatibility shims, and standardize the environment variables and flow across all entry points.

## Implementation Outline

### 1. Add one canonical POSIX driver

Create `scripts/build_viper_unix.sh` and move the shared logic there:

- compiler detection
- configure/build/test/install flow
- `VIPER_SKIP_INSTALL`
- job count selection
- build dir and build type handling

### 2. Keep the old entry points as thin wrappers

- `scripts/build_viper_mac.sh` -> validate host, then exec `build_viper_unix.sh`
- `scripts/build_viper_linux.sh` -> validate host, then exec `build_viper_unix.sh`
- `scripts/build_viper.sh` -> dispatch to the correct wrapper

That preserves existing commands while removing duplicated logic.

### 3. Standardize inputs

All build scripts should support the same environment variables:

- `VIPER_BUILD_DIR`
- `VIPER_BUILD_TYPE`
- `VIPER_SKIP_INSTALL`
- `VIPER_CMAKE_GENERATOR`
- `VIPER_EXTRA_CMAKE_ARGS`

Windows can keep a batch implementation, but it should honor the same logical inputs.

### 4. Fix naming and compatibility bugs while doing the consolidation

At minimum:

- fix `build_viper.sh` to dispatch to the actual Windows script name
- use consistent install-prefix behavior and output messages
- keep test invocation semantics aligned

### 5. Plan the same consolidation for demo scripts next

The same pattern exists in:

- `scripts/build_demos_mac.sh`
- `scripts/build_demos_linux.sh`
- `scripts/build_demos_win.cmd`

That should be a follow-up once the main build scripts are unified.

## Files To Modify

- new `scripts/build_viper_unix.sh`
- `scripts/build_viper.sh`
- `scripts/build_viper_mac.sh`
- `scripts/build_viper_linux.sh`
- `scripts/build_viper_win.cmd`

## Effort

Small to medium.

The logic is already almost shared; the work is mainly to centralize it without breaking compatibility.

## How It Prevents Breakage

**Before:** a flow or flag change lands in one host script and is forgotten in another.

**After:** there is one canonical POSIX flow, thin compatibility wrappers, and far fewer places for build behavior to drift.
