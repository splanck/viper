---
status: active
audience: contributors
last-verified: 2026-07-22
---

# ADR 0148: Bounded Immediate-Directory Paging

Date: 2026-07-22

Status: Accepted

## Context

Zanna Studio's Explorer historically called `Zanna.IO.Dir.Dirs` and
`Zanna.IO.Dir.Files` whenever a folder opened. Both methods enumerate the whole
directory before returning, so one flat directory with many entries can block
the GUI frame loop. `Zanna.Workspace.FileIndex.Page` is recursive and applies
workspace filters; using it for one Explorer node would walk unrelated
descendants and cannot provide an immediate-child snapshot efficiently.

Adding a method to `Zanna.IO.Dir` changes the runtime C ABI and public registry
surface, so the contract is recorded here.

## Decision

Add `Zanna.IO.Dir.Page(path, offset, limit)` backed by
`rt_dir_page(rt_string, int64_t, int64_t)`.

The method enumerates only the immediate children of `path`. It returns a map
with these fields:

- `valid`: whether `path` named a readable directory when traversal began.
- `path`: the normalized absolute directory path when valid.
- `entries`: a sequence of maps containing `name`, `path`, `kind`, and
  `isDirectory`. `kind` is `directory`, `file`, or `other`.
- `offset`, `limit`, `emitted`, and `nextOffset`: logical page metadata.
- `done`: whether traversal reached the end of the directory.
- `diagnostics`: non-fatal traversal diagnostics.

`.` and `..` are never emitted. Enumeration order remains filesystem-dependent;
callers that need presentation ordering collect the bounded pages and sort after
enumeration. `limit` defaults to 128 and is clamped to `1..4096`.

The API is stateless from the caller's perspective. The runtime may retain a
small process-local cursor cache so sequential calls using `nextOffset` resume a
native directory iterator. A missing cursor restarts traversal and skips to the
requested logical offset. Completed cursors are released immediately and the
cache is bounded by least-recently-used eviction.

Directory mutation during paging follows the host filesystem iterator's normal
weak-consistency rules. Callers that require a coherent refreshed view restart
at offset zero. A traversal error ends the page sequence and is reported in
`diagnostics`; it does not trap.

## Consequences

GUI and tooling callers can budget immediate-directory enumeration across frame
or work-loop boundaries without allocating a complete listing in one call.
Existing `List`, `Entries`, `Files`, and `Dirs` behavior is unchanged. Random
access after cursor eviction can repeat prefix traversal, while the sequential
path used by Explorer remains incremental.
