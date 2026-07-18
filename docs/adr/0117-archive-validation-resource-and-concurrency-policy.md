---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0117: Validate Archive Structure and Bound Serialized Writer Transactions

## Status

Accepted

## Context

`Zanna.IO.Archive` crosses four trust and ownership boundaries: it parses
untrusted ZIP metadata, inflates attacker-controlled compressed data, stages a
complete writer image in memory, and atomically replaces files through the
platform adapter. The previous reader trusted most central-directory metadata
until an entry was selected, searched duplicate names linearly, and did not
compare every central entry with its local header during open. A crafted archive
could therefore defer structural failures, alias local byte ranges, or drive
large allocations. The writer had the same complete-image memory shape but no
matching resource policy.

The write-mode object also exposed mutable entry arrays and a growable byte
buffer to `Add`, `AddDir`, `Count`, `Names`, and `Finish` without per-instance
synchronization. Atomic reference counts protect object lifetime only; they do
not make those compound archive transactions coherent. Finally, a failed
atomic replacement left central-directory bytes appended to the in-memory
writer, preventing a safe retry.

This is a cross-layer contract among the ZIP parser, runtime object lifetime,
compression code, platform filesystem adapters, and public Archive behavior.
It does not add an IL opcode or change the language-visible runtime ABI.

## Decision

- Opening an archive validates the complete central directory and every
  referenced local record before publishing the entry table. Central and local
  names, versions, flags, methods, CRCs, sizes, and supported extra fields must
  agree. Local record byte ranges must be disjoint and end before the central
  directory.
- Read and write entry names use open-addressed FNV-1a indexes with full-name
  collision checks. Duplicate detection and ordinary lookup are expected O(1),
  while ZIP order remains represented by the entry arrays.
- Each Archive samples encoded-file, per-entry uncompressed, and aggregate
  uncompressed byte ceilings at construction. Environment overrides are strict
  positive decimal values, clamped to audited hard ceilings. Readers enforce
  limits before complete-buffer copies, name allocation, or inflation; writers
  enforce the same limits before compression and every assembled-buffer growth.
- A write-mode Archive owns a platform-native reader-writer lock. `Add`,
  `AddDir`, and `Finish` hold it exclusively across their transaction and
  rollback boundaries. `Count` and `Names` hold it in shared mode while taking
  snapshots. Read-mode archives remain immutable after parsing.
- `Names` copies entry names into independently owned runtime strings; returned
  snapshots do not borrow storage from the Archive.
- `Finish` appends the central directory and EOCD transactionally. Filesystem
  adapters close descriptors and remove sidecars before trapping. On any
  failure the writer restores its pre-finish length and remains retryable.
- Atomic replacement preserves an existing regular destination's POSIX mode.
  Windows uses its metadata-preserving replacement operation for an existing
  regular destination and rejects directory/reparse-point leaves. Extraction
  rejects symlink/reparse traversal; POSIX extraction resolves and replaces
  files relative to already-open directory descriptors.
- Native lock helpers and parser/index structures remain internal. Callers that
  invoke Archive methods concurrently must keep an owning Archive reference for
  the complete call, as with every other runtime object.

## Consequences

- Malformed local/central disagreements and overlapping payload layouts fail at
  open time rather than during a later read or extraction.
- Resource use is bounded consistently for file-backed, byte-backed, and newly
  written archives. ZIP32 format limits remain an additional ceiling.
- Concurrent writer operations observe complete entry transactions. One
  Archive serializes compression while an Add holds the writer lock; independent
  Archive objects still compress and write concurrently.
- `Names` pays for string copies, trading small snapshot allocation cost for a
  clear lifetime contract and freedom from dangling views.
- A transient permission, destination-type, or replacement error can be fixed
  and `Finish` retried without reconstructing the writer.

## Alternatives Considered

- Validate local headers only when reading an entry: rejected because malformed
  archives would be partially observable and aggregate range aliases would be
  harder to reason about safely.
- Keep linear name searches: rejected because duplicate validation and repeated
  `Has` calls become quadratic on large, otherwise valid ZIP32 archives.
- Apply limits only to inflation: rejected because complete encoded copies,
  central metadata, stored entries, and writer buffers can exhaust memory before
  inflation begins.
- Use one process-wide Archive mutex: rejected because independent archives do
  not share state and should not block one another.
- Hold the writer lock only around the final array append: rejected because the
  byte buffer, metadata table, name index, and rollback length form one atomic
  transaction.
- Make failed `Finish` terminal: rejected because the failure is commonly an
  external filesystem condition and the staged entry data is still valid.
