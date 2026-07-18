---
status: active
audience: contributors
last-verified: 2026-07-08
---

# ADR 0068: GUI App Activation API

## Status

Accepted

## Context

Zanna GUI apps already exposed `App.Focus`, but focus was too weak for macOS
apps launched as bare executables from a terminal. A window could be visible and
frontmost while Terminal remained the active application, leaving Terminal's
native menu bar in place and preventing Zanna Studio's pull-down menus from being
usable.

macOS separates window key/main state from application activation and menu-bar
ownership. Zanna Studio needs an explicit foreground-activation request after its
main menu has been installed and after the first frame is visible.

## Decision

Add `Zanna.GUI.App.Activate()` as an explicit runtime method backed by
`rt_app_activate`. It maps to a new ZannaGFX `vgfx_request_foreground` API.
`App.Focus` now uses the same stronger path so existing callers get the fixed
behavior.

On macOS, foreground activation sets the application policy to regular,
finishes launching when needed, ensures the app menu exists, makes the window
key/main, orders it front, and activates `NSApp`. Windows and Linux map the
request to their existing foreground/focus mechanisms. The mock backend marks
the window focused for deterministic tests.

## Consequences

Zanna Studio can request OS-level app/menu ownership independently from widget
focus. Existing `App.Focus` behavior remains source-compatible, while
`App.Activate` documents the stronger intent at call sites that need app-level
foreground activation.
