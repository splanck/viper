# Recommendation 9: Native Subprocess Layer

## Problem

`src/common/RunProcess.cpp` still builds shell command strings and launches them through `popen`, with merged stdout/stderr and historically inconsistent `cwd` behavior.

That is one of the most fragile cross-platform boundaries in the repo because:

- quoting rules differ by shell and host
- cwd/env handling is easy to get subtly wrong
- tools and build flows inherit shell behavior they did not ask for

## Solution

Replace shell-string launching with a structured subprocess API backed by native platform implementations.

## Implementation Outline

### 1. Add a structured process API

Suggested surface:

- `argv` as a vector, not a shell string
- explicit `cwd`
- explicit environment overrides
- separate stdout and stderr capture
- exit status and spawn failure reporting

### 2. Implement platform backends

- POSIX: `posix_spawn` or `fork/exec` where needed
- Windows: `CreateProcessW`

### 3. Keep shell mode opt-in only

If a caller really wants shell semantics, that should be explicit:

- `run_shell_command(...)`

Most existing callers should use the structured API instead.

### 4. Migrate the highest-risk callers first

Start with:

- compiler and assembler invocation
- packaging helpers
- scripts/tool wrappers that currently rely on shell quoting

## Files To Modify

- `src/common/RunProcess.cpp`
- `src/common/RunProcess.hpp`
- possibly new `src/common/process/*`
- tool and codegen callers

## Effort

Medium to large.

## How It Prevents Breakage

**Before:** the same logical command can behave differently on Windows and POSIX because the shell layer differs.

**After:** process launching becomes a normal API with platform-specific implementation hidden underneath it, which is much easier to reason about and test.
