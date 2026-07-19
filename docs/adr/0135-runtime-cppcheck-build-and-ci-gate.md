---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0135: Gate Runtime Portability and Correctness Diagnostics with Cppcheck

## Status

Accepted; amended 2026-07-18. Gating an ordinary build on an external tool
conflicts with the zero-dependency build policy and added significant wall
time, so the canonical Unix and Windows build scripts no longer invoke
`cppcheck-runtime` and the `ZANNA_SKIP_CPPCHECK` opt-out is removed. The
target and its reviewed `cppcheck-runtime.supp` remain available as a manual,
optional check. The Decision bullets below describing build-script invocation
are retained as the historical record of the original gate.

## Context

The repository declared a `cppcheck-runtime` CMake target, but the target named
a suppression file that did not exist and left cppcheck's default success exit
code in place. Depending on cppcheck behavior, the target either failed before
analysis or printed findings while still succeeding. It was not invoked by the
canonical build scripts or any workflow, so runtime changes could not rely on
it as a maintained gate.

Enabling every cppcheck style and whole-program check over the runtime produces
thousands of ABI-oriented suggestions such as renaming declaration parameters,
adding `const` to opaque C entry points, and reporting registered native
functions as unused. Those messages are not correctness failures and obscure
the warning, performance, and portability classes that this gate is intended
to enforce. The runtime also uses generated compile definitions and platform
adapters, so analysis must consume the configured compilation database rather
than inventing a standalone command line.

Adding a workflow is a repository policy change and therefore requires this
decision record.

## Decision

- `cppcheck-runtime` reads the configured `compile_commands.json`, filters to
  `src/runtime`, enables warning, performance, and portability diagnostics,
  permits explicitly marked inconclusive diagnostics, and returns a non-zero
  status for every unsuppressed finding.
- The checked-in `cppcheck-runtime.supp` is required even when it contains only
  a small number of tool-behavior suppressions. Suppressions name checker IDs
  or exact sites; they must not blanket-disable a runtime directory or a
  correctness class.
- The branch-limit advisory is suppressed because it describes cppcheck's
  chosen analysis budget rather than a source defect. A contributor can still
  run an exhaustive one-off analysis directly.
- Canonical Unix and Windows build scripts run the target after compilation
  when cppcheck and a compilation database are available. Local builds without
  cppcheck report a skip. `ZANNA_SKIP_CPPCHECK=1` provides the same explicit
  opt-out convention as other build stages.
- A dedicated Ubuntu workflow installs cppcheck, configures the project with
  Ninja to guarantee a compilation database, and runs the CMake target for pull
  requests and pushes to the primary branch.

## Consequences

- A printed runtime correctness, performance, or portability finding now fails
  both its direct CMake target and the CI job.
- Full canonical developer builds perform the same analysis when the tool is
  installed, while environments that do not package cppcheck can still build
  and test the product.
- Style-only ABI churn remains outside this gate and continues to be handled by
  clang-format, compiler warnings, review, and optional broader analysis.
- The CI job configures but does not build the entire product; cppcheck consumes
  the generated command database and remains independent of product runtime
  dependencies.

## Alternatives Considered

- Keep `--enable=all` and suppress every existing style diagnostic: rejected
  because broad suppressions would hide future errors and make the list
  unauditable.
- Leave cppcheck informational with exit code zero: rejected because CI could
  be green while emitting a known correctness warning.
- Run cppcheck without the compile database: rejected because platform macros,
  generated include paths, and feature definitions would diverge from actual
  builds and create false positives.
- Require cppcheck on every local machine: rejected because the product has no
  external dependencies and not every supported developer environment packages
  the same analysis tool.
