---
status: active
audience: contributors
last-verified: 2026-07-22
---

# ADR 0149: Preserve Zanna Studio Identity in the macOS Application Menu

Date: 2026-07-22

Status: Accepted

## Context

Zanna Studio is built as a native Zia executable rather than a standalone
application bundle. For an unbundled GUI process, macOS renders the application
menu label from the executable leaf name. Giving the first `NSMenuItem` an
authored title and changing `NSProcessInfo.processName` do not change the label
exposed by the system menu bar. Consequently, launching the historical
`zannastudio` binary displayed **zannastudio** beside the Apple menu even though
the window and application submenu used **Zanna Studio**.

The lowercase `zannastudio` command remains part of the cross-platform CLI,
installer, file-association, and automation contract established by ADR 0118.
Changing that public entry point on macOS would introduce unnecessary platform
drift.

## Decision

On macOS, stage Zanna Studio as two sibling executable files:

- `Zanna Studio` is the native Mach-O payload produced by the Zanna compiler.
  Its authored leaf name supplies the Cocoa and Accessibility application
  identity.
- `zannastudio` is a small POSIX launcher that resolves its own directory and
  replaces itself with `Zanna Studio`, forwarding every argument and the native
  process's exit status.

Both the ordinary CMake target and `scripts/build_ide.sh` stage this pair in the
primary and compatibility output directories. Installation includes both files,
so the stable `bin/zannastudio` command continues to work. Windows keeps
`zannastudio.exe`; Linux keeps its native `zannastudio` executable.

The GUI runtime also stores the initial `App.New` title separately from the
mutable window/document title. Native About, Hide, and Quit labels use that
stable application name when a document title changes or the menu tree is
rebuilt.

The Apple-only regression executable is itself emitted as `Zanna Studio` and
asserts the executable leaf, `NSRunningApplication.localizedName`, process name,
and stable application-menu commands. Manual release verification queries the
running product with macOS Accessibility/System Events.

## Consequences

- The visible macOS application menu consistently says **Zanna Studio** when
  users launch the documented `zannastudio` command.
- Existing scripts, installer checks, and command-line invocations retain their
  lowercase entry point and argument behavior.
- macOS output and install directories contain one additional native payload
  file whose name includes a space. Packaging must preserve both sibling files.
- Launching a copied or renamed Mach-O payload directly can still change the
  system identity; the staged launcher/payload pair is the supported layout.
