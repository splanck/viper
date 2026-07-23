---
status: active
audience: contributors
last-verified: 2026-07-22
---

# ADR 0152: Use Stable File Identity for Editor Document De-duplication

Date: 2026-07-22

Status: Accepted

## Context

Zanna Studio historically de-duplicated open documents with absolute lexical
path strings. That handles `.` and `..`, but not hard links, symlinks, or
case-equivalent spellings on a case-insensitive filesystem. Opening the same
underlying file through two such paths created independent editable buffers.
Either tab could then save over the other tab's assumptions and external-change
metadata.

Unconditionally lowercasing paths is not a valid replacement: Linux and
case-sensitive macOS filesystems may contain distinct `Foo.zia` and `foo.zia`
files. Platform-name checks are also insufficient because macOS volumes can use
either case policy.

The runtime already compares stable file identity internally to prevent
self-copy and self-move operations, but that predicate was not public.

## Decision

Expose `Zanna.IO.File.SameFile(left, right)` through the runtime entry point
`rt_file_same`. The predicate compares volume and file-index values on Windows,
and device and inode values on POSIX. It follows normal filesystem resolution,
so links and case-equivalent paths compare correctly according to the mounted
filesystem rather than an OS-wide text rule.

`SameFile` is non-trapping. It returns false if either operand is empty,
malformed, missing, inaccessible, or not an existing regular file.

Zanna Studio's `DocumentManager` first compares normalized lexical paths, then
uses `SameFile` for existing files. Missing files continue to use lexical
identity so an externally deleted document remains addressable. Recently closed
file de-duplication uses the same policy while the file still exists.

## Consequences

- One underlying existing file owns at most one editable Studio document.
- Hard links, symlinks, and case aliases no longer create conflicting tabs.
- Case-distinct files remain independent on case-sensitive filesystems.
- The runtime C header, registry, class catalog, authored/generated docs, and
  runtime/Studio regressions must evolve together.
