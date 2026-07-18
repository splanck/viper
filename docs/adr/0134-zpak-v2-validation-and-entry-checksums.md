---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0134: Validate ZPAK Metadata and Checksum Every Version 2 Entry

## Status

Accepted

## Context

The ZPAK runtime reader accepted the magic bytes but ignored the header version,
header flags, header reserved field, per-entry unknown flags, and per-entry
reserved field. It also allocated the entry array from the untrusted entry
count before proving that the table of contents contained even the minimum
number of bytes for that count. Duplicate detection compared each new name with
every prior name, making valid large tables quadratic to open.

An entry's stored range and uncompressed-size relationship were not rejected
until the entry was read. More importantly, ZPAK had no end-to-end integrity
field: a modified uncompressed payload could be returned silently, and a
modified compressed payload was detected only when the DEFLATE stream happened
to become invalid. ZPAK is produced by the build-time asset writer, consumed by
the runtime from files and embedded read-only blobs, and documented as a stable
asset container. Changing its binary version is therefore a cross-layer format
decision.

## Decision

- The existing 32-byte header remains unchanged in size and byte order. Version
  1 remains readable for compatibility, but new writers emit version 2.
- Header flag bit 0 continues to mean that at least one entry is compressed.
  Version 2 sets required flag bit 1 to declare a CRC-32 field on every table
  entry. No other header flags are currently valid, and all header reserved
  bytes must be zero.
- A version 1 table entry remains `name_len`, name bytes, data offset,
  uncompressed size, stored size, 16-bit entry flags, and a zero 16-bit reserved
  field. A version 2 entry appends a 32-bit little-endian CRC-32 of the original
  uncompressed bytes. Entry flag bit 0 is the only supported entry flag.
- CRC-32 uses the runtime's existing IEEE 802.3 implementation, including the
  normal pre/post complement convention used by ZIP and PNG. The reader checks
  it after copying an uncompressed entry or after successful inflation.
- The reader rejects unsupported versions, missing required version 2 flags,
  unknown flags, non-zero reserved fields, an inconsistent aggregate
  compression flag, unsafe entry paths, truncated or trailing table bytes, and
  table or payload ranges outside the archive.
- Before allocating entries, the reader proves that the entry count is no
  larger than `toc_size / minimum_record_size`. It also applies the same 64 MiB
  table ceiling to file- and memory-backed archives.
- All non-empty stored ranges must lie after the header, end before the table of
  contents, and not overlap. Uncompressed stored size must equal uncompressed
  size. These properties are validated before the archive is published.
- Entries are temporarily sorted by stored offset to validate disjoint ranges,
  then sorted by name. Adjacent equal names are rejected, removing the previous
  quadratic duplicate scan while retaining binary-search lookup.

## Consequences

- Existing version 1 packs remain loadable and keep their historical lack of a
  checksum. Rebuilding a project naturally upgrades generated packs to version
  2 without changing asset names or runtime APIs.
- Corruption of either raw or compressed version 2 payloads causes the entry
  read to fail instead of returning modified bytes.
- Malformed entry counts cannot request an allocation disproportionate to the
  actual table, and malformed payload metadata is rejected at mount/open time.
- The version 2 table grows by four bytes per entry. No product dependency or
  new compression/checksum implementation is introduced.

## Alternatives Considered

- Reinterpret the version 1 reserved field as a checksum fragment: rejected
  because sixteen bits is too weak and changing the meaning of existing bytes
  would break compatibility.
- Put one checksum over the complete archive: rejected because file-backed
  packs intentionally read entries on demand and should not scan all asset data
  during mount.
- Drop version 1 support: rejected because deployed packs and embedded objects
  may outlive the compiler that produced them.
- Accept unknown flags for forward compatibility: rejected because their
  semantics can affect parsing, integrity, or compression and cannot be safely
  ignored by an older reader.
