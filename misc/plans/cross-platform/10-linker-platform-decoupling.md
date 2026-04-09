# Recommendation 10: Native Linker Platform Decoupling

## Problem

`src/codegen/common/linker/NativeLinker.cpp` currently mixes the common linker pipeline with large chunks of Windows, macOS, and Linux import-planning policy in the same file.

That makes it a high-risk cross-platform chokepoint:

- changes for one OS land in the same diff as shared logic
- review scope is wider than it needs to be
- regression risk rises because platform policy and common flow are tightly interleaved

## Solution

Split the native linker into a common core plus per-platform planners and data.

## Implementation Outline

### 1. Separate common flow from platform import policy

Keep the shared steps in a common layer:

- object/archive parsing
- symbol resolution
- dead-strip
- ICF
- section merge
- relocation application
- executable writing orchestration

Move platform-specific import planning into separate compilation units.

### 2. Use per-platform planners

Examples:

- `WindowsImportPlanner.cpp`
- `MacImportPlanner.cpp`
- `LinuxImportPlanner.cpp`

### 3. Move large symbol-to-library rules out of the common file

Prefer:

- data tables in dedicated files
- generated metadata where possible

This reduces noise in the common linker path and makes platform ownership clearer.

### 4. Add planner-focused tests

Keep the common linker tests, but add targeted tests per planner so platform policy changes have smaller blast radius and faster feedback.

## Files To Modify

- `src/codegen/common/linker/NativeLinker.cpp`
- new planner/data files under `src/codegen/common/linker/`
- related linker tests

## Effort

Large.

## How It Prevents Breakage

**Before:** fixing one host's import policy means editing one big cross-platform file that also owns shared linker flow.

**After:** platform-specific linker policy is isolated, easier to review, and less likely to destabilize unrelated hosts.
