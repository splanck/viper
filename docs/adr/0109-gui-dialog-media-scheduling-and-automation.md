---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0109: Make GUI Dialogs, Media, and Automation Frame-Driven

Date: 2026-07-16

## Status

Accepted. This ADR governs review recommendations 12-14, 24-25, 31, 36-40 and
the automation portions of 17.

## Context

File dialogs and message boxes implement synchronous modal behavior with nested
poll/render loops. This can re-enter event dispatch and reset per-frame edge
state. Static file-dialog helpers return cancellation through empty strings and
multiple paths through an escaped delimiter protocol. Message-box cancel/default
roles are inferred from English labels, making localized dialogs incorrect, and
prompt cancellation is indistinguishable from accepting an empty value.

Image upload converts into a temporary buffer and then copies again. The lower
setter cannot report allocation failure, image scaling performs a floating-point
nearest-neighbor loop even for 1:1 opaque images, and lower file decoding differs
by platform. VideoWidget requires manual ticking and sends every frame through
the copying image path. Minimap binding does not observe editor revisions and
rescans document rows during paint.

The current TestHarness is a synthetic rectangle registry: key events are
journaled, time is a counter, and capture is inferred geometry rather than the
framebuffer. CodeEditor exposes individual implementation-specific performance
counters on its stable surface, and several result maps and integer domains lack
schema or named constants.

These changes affect the public registry, runtime C embedding surface, event
loop, platform adapters, media ownership, and testing contract.

## Decision

### Dialogs

- FileDialog and MessageBox become app-scheduled controllers with `ShowAsync`,
  open/completion/status/error state, and typed result access. Completion edges
  fire once per show generation.
- Existing synchronous methods remain and run a guarded outer modal driver. They
  never invoke nested `App.Poll` from inside active dispatch; when called during
  dispatch they defer opening until the current event returns.
- File dialog options include hidden files, overwrite confirmation, default
  extension, bookmarks, filters, and multiple selection. New static APIs return
  `Option` or `Seq`; the escaped legacy string remains compatible.
- macOS, Windows, and Linux provide equivalent behavior. Native adapters may be
  used where already available; the toolkit dialog is the deterministic common
  fallback and must implement the complete option/result contract everywhere.
- Message-box buttons have unique IDs and explicit normal/default/cancel/
  destructive/accept/reject/help roles. Localized labels never determine
  semantics. `PromptOption` distinguishes cancellation from accepted empty text.

### Images, video, and minimap

- Image mutation has status-returning `TrySetPixels` and `UpdateRegion`. It
  validates and allocates before replacing state, accepts explicit source/dest
  rectangles, and reuses storage when dimensions/format permit.
- The shared built-in decoder path handles the same supported PNG, BMP, JPEG,
  and GIF formats on every platform. Platform-native decoding may be an optional
  optimization but cannot change success or pixel semantics.
- Image paint has a row-copy path for 1:1 opaque rendering, integer nearest
  scaling, optional bilinear filtering, cached scaled results, and dirty region
  invalidation. No external codec or graphics dependency is introduced.
- VideoWidget registers with the app scheduler by default, advances at most once
  per frame generation, reuses conversion/upload buffers, and reports load,
  failure, buffering, end, and seek events. Manual update remains compatible.
- Minimap observes editor text/layout/scroll revisions, invalidates affected
  lines, and caches bounded line rasters. Paint work is proportional to dirty or
  visible cached lines rather than the entire document.

### Automation and public diagnostics

- TestHarness can bind a real App, dispatch input through the normal hit-test and
  event path, run deterministic injected-time frames, capture actual framebuffer
  pixels/hashes, compare regions with tolerance, and snapshot accessibility.
  Existing synthetic registration remains available as a compatibility mode.
- ViperGFX exposes `vgfx_post_event`, a validated value-copy enqueue operation
  for automation and embedders. It uses the synchronized native event queue and
  its overflow/release-state policy; NONE and out-of-range event discriminators
  are rejected. TestHarness therefore does not maintain a second GUI dispatcher.
- A harness retains its bound App and validates a narrow borrowed window/root/
  timestamp snapshot for every operation. Binding marks historical synthetic
  records dispatched; later records advance exactly once even when malformed or
  dropped, preventing an invalid journal entry from blocking subsequent input.
  Key input emits down/text/up where appropriate; mouse supports move, down, up,
  and click with physical coordinates and left/right/middle ordinals 0/1/2.
- `CapturePixels` is a deep canonical `0xRRGGBBAA` copy with transparent
  out-of-window clipping. `CaptureHash` is lowercase FNV-1a 64 over explicit
  dimensions and RGBA byte order. `CompareRegion` clamps tolerance to 0..255
  and returns schema version, match/count/delta, and mean-absolute-error fields.
- Software/mock rendering is the canonical visual-golden source. Goldens cover
  themes, scale, states, shadows, overlays, accessibility focus, and partial/full
  equivalence. GPU/platform smoke uses structural/tolerance checks where exact
  pixels differ legitimately.
- CodeEditor adds one schema-versioned `GetPerfStats` snapshot. Individual
  counters remain for compatibility but generated docs identify the snapshot as
  preferred. Its stable keys expose all nine raw counters: total-height,
  total-visual-row, visual-row, locate-visual-row, highlight-call,
  syntax-state-line, highlight-span, full-text-copy, and copied-byte counts.
  Every new map has `schemaVersion` and documented stable keys.
- Static constant classes name public integer domains while retaining `i64` ABI
  signatures. The classes are Align, Justify, FlexDirection, FlexWrap, Dock,
  ThemeMode, AccessibleRole, LiveRegionMode, DialogButtonRole, DialogStatus,
  ImageFilter, and SortDirection; their getter implementations remain available
  in graphics-disabled builds.

## Consequences

- Dialogs no longer require reentrant event loops and can integrate with normal
  application state machines.
- Cancellation, empty values, localized roles, and multiple paths are unambiguous.
- Images/video avoid redundant allocation and copying in steady state, and
  decode semantics are cross-platform.
- Minimap and overlay-heavy editor windows repaint only relevant content.
- GUI tests exercise the actual runtime event/render/accessibility path and can
  catch visual regressions previously invisible to geometry-only assertions.
- Existing compatibility methods and counters remain, increasing short-term
  documentation surface but avoiding source breaks.

## Alternatives Considered

- Remove synchronous dialog methods. Rejected because the modernization request
  explicitly preserves all existing features.
- Use platform-native dialogs exclusively. Rejected because behavior and test
  determinism would diverge, and Linux environments may lack a suitable portal.
- Adopt third-party image, Unicode, automation, or GUI libraries. Rejected by
  the zero-dependency policy.
- Keep TestHarness synthetic and add a second harness. Rejected because one
  harness can preserve synthetic mode while real binding prevents duplicated
  automation APIs.
- Remove individual CodeEditor counters. Rejected for compatibility; the
  snapshot provides the stable migration target.
