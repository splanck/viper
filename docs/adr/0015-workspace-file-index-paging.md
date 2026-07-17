---
status: active
audience: contributors
last-verified: 2026-07-04
---

# ADR 0015: Workspace File Index Paging

Date: 2026-06-26

Status: Accepted

## Context

ZannaIDE uses `Zanna.Workspace.FileIndex.Enumerate` for project trees, Quick
Open, search discovery, and semantic workspace indexing. `Enumerate` returns a
complete sequence of entry maps. That is convenient for small projects but forces
IDE callers to allocate the full result before they can start processing it.

ADR 0010 added `Zanna.Workspace.FileIndex.Status` so callers could query large
workspace metadata without allocating every entry. The next IDE polish pass needs
the same bounded behavior for actual entry consumption, especially for background
indexing where the frame loop should process a small slice at a time.

Adding a runtime method under `Zanna.Workspace.FileIndex` is a runtime C ABI
surface change, so it is recorded here.

## Decision

Add `Zanna.Workspace.FileIndex.Page(root, extensionsCsv, excludesCsv,
includeDirs, offset, limit)`.

The method is stateless from the caller's perspective: callers pass `offset` and
use the returned `nextOffset` for the following call. The runtime may keep a
private process-local cursor for sequential calls with the same root, filters,
offset, and limit so later pages can continue the traversal instead of walking
from the root again. If a call is non-sequential or uses a different key, the
runtime restarts traversal and skips matching entries before `offset`. The
runtime clamps `limit` to `1..4096` so an accidental huge request does not
recreate the full-allocation problem. It applies the same hard excludes,
`.gitignore` subset, extension filters, project exclude patterns, and
`kWorkspaceFileIndexMaxEntries` cap as `Enumerate`.

The returned map contains:

- `valid`: boolean root/traversal validity.
- `root`: normalized root path.
- `entries`: sequence of entry maps with the same shape as `Enumerate`.
- `offset`, `limit`, `emitted`, `nextOffset`, `scanned`.
- `done`: true when no more entries are available under the current cap.
- `truncated`: true when the existing file-index entry cap was reached.
- `maxEntries`: the runtime cap.
- `diagnostics`: sequence of diagnostic maps.

`Enumerate`, `Status`, and `ShouldIgnore` remain unchanged.

## Consequences

ZannaIDE can page workspace indexing and other discovery jobs without allocating
the whole workspace result up front. Because the API is stateless from the
caller side, callers do not need to manage native handles or lifetime, and no new
close/destroy protocol is introduced. The private cursor optimizes the common
sequential paging case without adding ABI surface. Random access or interleaved
callers can still repeat traversal work up to the requested offset; that
trade-off keeps the public API small.
