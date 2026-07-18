---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0115: Retain Runtime Resources Across Unlocked Work

## Status

Accepted

## Context

The asset manager resolves embedded and mounted ZPAK archives through a global
source registry. Historically it kept that registry mutex locked while reading
and decompressing entry data. Decompression may raise a recoverable runtime
trap, file I/O may block, and independent asset loads should be able to proceed
concurrently. Simply releasing the registry mutex before reading would allow an
unmount operation to close the archive and invalidate its table-of-contents
entry or `FILE *` while a load still uses it.

This is a cross-layer lifetime dependency between `rt_asset.c` and the internal
ZPAK reader, so the ownership contract must not drift silently.

## Decision

- `zpak_archive_t` has an internal atomic ownership count. Open functions return
  the initial ownership reference.
- `zpak_retain(archive)` acquires a live reference without trapping and returns
  non-zero on success.
- `zpak_close(archive)` releases one reference and destroys the archive only
  after the final release. Existing single-owner callers keep their behavior.
- File-backed archives serialize only their shared seek/read section with a
  per-archive native mutex. Decompression and result construction occur without
  that mutex.
- The asset manager retains the selected archive while holding its registry
  mutex, releases the mutex, performs I/O and decompression, and then releases
  the archive reference through a trap-safe cleanup boundary.
- Asset enumeration snapshots retained archive references and constructs the
  returned sequence after releasing the registry mutex.
- Lazy manager initialization uses uninitialized, staging, and published
  states. One caller discovers and fully validates embedded/adjacent packs in a
  private registry without the live mutex; concurrent first-use callers wait
  on a native condition variable, and the complete registry is published in
  one short locked transaction. Corrupt candidates are skipped without
  stranding initialization or its waiters.

The `zpak_*` functions remain an internal runtime-reader interface; no language
runtime registry name, IL opcode, or generated-code signature changes.

## Consequences

- A mounted pack may be unmounted concurrently with an already-started load;
  the load completes against its retained archive snapshot.
- A corrupt compressed asset cannot strand the global asset registry mutex.
- Independent archives can be read concurrently, while reads sharing one
  `FILE *` remain ordered and position-safe.
- Pack discovery and validation no longer serialize ordinary lookups, and no
  caller can observe a partially discovered first-use registry.
- Each archive gains one ownership counter and, for file-backed archives, one
  native mutex allocation.

## Alternatives Considered

- Keep the global mutex and add trap recovery around every call: rejected
  because it preserves global I/O serialization and is fragile as new trap
  sites are added.
- Reopen and reparse a mounted pack for every load: rejected because it adds
  substantial I/O and parsing overhead and changes behavior if the path is
  replaced after mounting.
- Copy complete packed entries under the global lock: rejected because blocking
  file reads would still serialize all asset operations.
