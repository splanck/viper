# ViperIDE Architecture Notes

This document is the contributor-facing map for ViperIDE's Zia source. Keep it
updated whenever a new subsystem is added or a large module changes ownership.

## Source Layout

```text
viperide/src/
    main.zia        Application bootstrap and frame loop only.
    app/            Frame-loop helpers, settings apply, watcher, explorer runners.
    build/          Build/run/debug process state.
    commands/       User command handlers and declarative command catalog.
    core/           Documents, projects, settings, and session persistence.
    editor/         Editor controllers and language-service integration.
    probes/         CI probe entry points.
    services/       Shared utilities with no UI ownership.
    terminal/       Integrated terminal process/session code.
    ui/             Persistent widgets, overlays, panels, and shell layout.
    zia/            Pure Zia parsing, formatting, bind, and refactor helpers.
```

## Layering Rules

`main.zia` should wire controllers and dispatch events. It should not grow new
parsers, file-system mutation flows, formatting rules, or command metadata.

Prefer this dependency direction:

```text
main -> app -> commands/ui/editor/core/build/services/zia
commands -> core/editor/services/zia/ui
editor -> core/services/zia
ui -> editor/core only when rendering state requires it
zia/services -> leaf helpers; no AppShell ownership
```

Avoid introducing cycles. When two modules need the same rule, move that rule to
`services/` or `zia/` instead of copying it.

## Command Pattern

Command metadata belongs in `commands/command_catalog.zia`. Runtime behavior
belongs in command modules such as `edit_commands.zia`, `file_commands.zia`, and
`search_commands.zia`. The registry should stay focused on shortcut install,
palette population, and capability checks.

## Refactor Helpers

Pure source-to-source behavior belongs in `src/zia/`:

- `identifier_utils.zia`: identifier and keyword rules.
- `source_scan.zia`: trivia-aware source scanning and structural lookup.
- `formatters.zia`: document/range formatting.
- `bind_utils.zia`: bind normalization and rewrite helpers.
- `refactors.zia`: extract/inline transformations.

These modules should remain UI-free and easy to exercise from probes.

## Comments

New Zia modules should start with a short module header that explains purpose,
ownership, and where the module fits. New functions should use `///` comments
with `@brief`, parameters for nontrivial signatures, return values where useful,
and a `@details` note when there is a policy or safety assumption.

Comments should teach intent. Avoid restating the next line of code.

## File Size Budget

Use these as review triggers, not hard compiler limits:

- 300 lines: check whether helpers should move out.
- 500 lines: require a clear reason the module is cohesive.
- 1000 lines: split before adding more behavior unless the file is a generated
  or intentionally exhaustive fixture.

The goal is for a new Zia developer to understand one ownership area at a time.

## Probe Layout

ViperIDE probe entry points live in `src/probes/` and are registered in
`src/tests/CMakeLists.txt`. Keep probe-local binds relative to `src/probes/`,
usually `bind "../editor/..."` or `bind "../commands/..."`.

When adding a new subsystem, add or extend a focused probe rather than relying
only on the full IDE smoke probe.
