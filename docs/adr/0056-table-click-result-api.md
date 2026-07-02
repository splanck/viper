# ADR 0056: Table ClickResult API

Date: 2026-07-02
Status: Accepted

## Context

`Viper.Game.UI.Table.HandleClick(x, y)` returns three different meanings through
one integer channel:

- a row index for body-row hits
- `-2` for column-header hits
- `-1` for misses

Header clicks also require a second call to `LastHeaderClick()` to recover the
clicked column. That pairing is compact, but it is easy to misuse and does not
scale well as table interaction grows beyond row selection and sorting.

## Decision

Add `Viper.Game.UI.Table.HandleClickResult(x, y) ->
Viper.Game.UI.TableClickResult`.

`TableClickResult` is an immutable click-outcome snapshot with:

- `Kind: Integer`
- `IsNone: Boolean`
- `IsRow: Boolean`
- `IsHeader: Boolean`
- `RowOption() -> Option[Integer]`
- `ColumnOption() -> Option[Integer]`

`Kind` uses `0` for no hit, `1` for row hit, and `2` for header hit. The boolean
properties are the preferred high-level checks for application code, while
`Kind` keeps low-level switch-style code compact.

`HandleClickResult` performs the same table state updates as `HandleClick`: row
clicks select rows, and sortable header clicks toggle sort order. It returns the
row or header column in the same object, so callers do not need the
`LastHeaderClick()` side channel.

`HandleClick()` and `LastHeaderClick()` remain available for compatibility.
Runtime API metadata marks both as legacy and points callers to
`HandleClickResult`.

## Consequences

- New code can distinguish miss, row, and header clicks without sentinel values.
- Header-click handling no longer depends on mutable last-click state.
- Existing game UI code keeps working while API inventory tools guide migration.
