---
status: active
audience: contributors
last-verified: 2026-07-18
---

# ADR 0111: Use PowerShell for Windows Automation Entry Points

## Status

Accepted.

## Context

Zanna's Windows build, demo, IDE, installer, and game-probe entry points were
implemented as batch files. The project reorganization increased their use of
quoted paths, forwarded CMake arguments, structured cleanup, and native package
validation, all of which are difficult to express safely in `cmd.exe`. The
Windows release workflow is an ADR-triggered surface and invoked the batch build
entry point established by ADR 0025.

## Decision

Replace the six repository-owned `.cmd` entry points with PowerShell scripts:

- `scripts/build_zanna_win.ps1`;
- `scripts/build_demos_win.ps1`;
- `scripts/build_ide_win.ps1`;
- `scripts/build_installer.ps1`;
- `examples/games/3dbowling/run_probes.ps1`; and
- `examples/games/ridgebound/run_probes.ps1`.

The scripts remain compatible with Windows PowerShell 5.1 and PowerShell 7.
Existing environment-variable contracts and GNU-style arguments such as
`--clean`, `--arch`, `--stage-dir`, and `--verify-only` remain supported. The
Windows release workflow uses `shell: pwsh` and invokes
`scripts/build_zanna_win.ps1` as its canonical build entry point.

Generated `.cmd` launchers inside an installer payload are outside this
decision. They are product artifacts authored by the native packager and remain
PowerShell-independent, as required by ADR 0103.

## Consequences

- Windows automation gets native arrays, exception handling, structured path
  validation, and deterministic cleanup without command-shell quoting layers.
- Contributor documentation and CI must reference the `.ps1` paths.
- Callers running from `cmd.exe` must use `powershell -NoProfile
  -ExecutionPolicy Bypass -File <script>`; PowerShell callers can invoke the
  scripts directly.
- IL, verifier, runtime C ABI, package format, and installed launcher contracts
  do not change.

## Alternatives Considered

- **Keep batch wrappers around PowerShell implementations.** Rejected because it
  preserves duplicate public entry points and leaves repository-owned `.cmd`
  files behind.
- **Open-code Windows commands in CI.** Rejected because local and release builds
  must share the same canonical validation script.
- **Require PowerShell 7 only.** Rejected because supported Windows developer
  environments include the in-box Windows PowerShell 5.1 host.
