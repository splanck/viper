# Recommendation 8: Generated Runtime And Archive Manifest

## Problem

Some cross-platform metadata is still hand-maintained in places where drift is expensive.

The clearest example is `src/codegen/common/RuntimeComponents.hpp`, which explicitly says its symbol-prefix mapping must stay in sync with runtime library organization. That is a long-term maintenance trap, especially when platform-specific runtime composition evolves.

## Solution

Generate runtime-component metadata from one source of truth and use that generated manifest in codegen, audits, and documentation.

## Implementation Outline

### 1. Define a machine-readable manifest

Possible contents:

- runtime component name
- archive target name
- symbol prefixes or exported handlers
- optional feature bucket
- owning headers/modules

The manifest can be generated from existing runtime metadata rather than hand-authored from scratch.

### 2. Generate consumers from the manifest

Initial targets:

- `RuntimeComponents.hpp`
- audit tooling inputs
- optional docs summaries

### 3. Fail when the manifest and build graph drift

Add one machine check that validates:

- each declared runtime component resolves to a real archive target
- each archive target expected by codegen appears in the manifest

## Files To Modify

- `src/codegen/common/RuntimeComponents.hpp`
- runtime build metadata in `src/runtime/CMakeLists.txt`
- possibly `rtgen` or a small generator tool

## Effort

Medium to large.

## How It Prevents Breakage

**Before:** runtime component routing can drift silently until native linking breaks on a specific host.

**After:** the mapping is generated or machine-checked from one source of truth, making cross-platform runtime-link drift much harder to introduce.
