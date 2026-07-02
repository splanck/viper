# ADR 0055: Exec CommandResult API

Date: 2026-07-02
Status: Accepted

## Context

`Viper.System.Exec.ShellFull(command)` captures stdout but stores the matching
exit status in thread-local state read later through
`Viper.System.Exec.LastExitCode()`. The two-call pattern is easy to misuse: any
intervening exec call can overwrite the status, and the output/status
relationship is implicit.

Non-zero command exits are normal process outcomes, not runtime failures, so a
`Result.Err` shape would make ordinary shell control flow look exceptional.

## Decision

Add `Viper.System.Exec.ShellResult(command) -> Viper.System.CommandResult`.

`CommandResult` is an immutable snapshot containing:

- `Output: String`
- `ExitCode: Integer`
- `Succeeded: Boolean`

`Succeeded` is true when `ExitCode == 0`. The output and exit code come from the
same shell invocation, so callers no longer need `LastExitCode()` for normal
code paths.

`ShellFull(command)` and `LastExitCode()` remain available for compatibility.
Runtime API metadata marks them as legacy and points callers to `ShellResult`.

## Consequences

- New code can reason about command output and status from a single returned
  object.
- Non-zero exits remain data, preserving shell scripting semantics.
- Existing callers using `ShellFull` and `LastExitCode` continue to work.
