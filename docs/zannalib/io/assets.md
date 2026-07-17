---
status: active
audience: public
last-verified: 2026-07-15
---

# Zanna.IO.Assets

Asset management system for loading embedded and packed resources.

## Overview

Assets can be:

- **Embedded** in the executable's read-only data (lookup does not read a pack or loose file;
  declared with `embed` in `zanna.project`). Some typed decoders still spill bytes to a private
  temporary path because their underlying loader requires a filename.
- **Packed** in `.zpak` files distributed alongside the executable (`pack` in `zanna.project`)
- **Loose** on the filesystem (development workflow, no declaration needed)

## zanna.project Directives

```text
# Embed file or directory into the executable's read-only data
embed sprites/player.png
embed sprites

# Create named .zpak pack file
pack level1 levels/level1/

# Pack with DEFLATE compression
pack-compressed music audio/tracks/
```

Pre-compressed formats (`.png`, `.jpg`, `.ogg`, `.mp3`, `.glb`, etc.) automatically skip compression even with `pack-compressed`.
Generated pack filenames are `<project-name>-<pack-name>.zpak`; the logical asset
names inside a pack come from the source paths, not from the pack name.

## API Reference

Asset names may be written either as plain package paths such as
`"assets/models/tree.glb"` or with the explicit `asset://` scheme, such as
`"asset://models/tree.glb"`. The scheme is stripped before lookup; it is useful
when code needs to make it clear that the value is a packaged asset path.

### Assets.Load(name: String) -> Object?

Load an asset by name. Returns a typed object based on file extension:

| Extension | Return Type |
|-----------|-------------|
| `.png`, `.jpg`, `.jpeg`, `.bmp`, `.gif` | Pixels (`.gif` loads the first frame) |
| `.wav`, `.ogg`, `.mp3` | Sound |
| Other | Bytes |

Returns null if not found.

Each recognized extension has one stable result type: a recognized image/audio extension returns
its typed object (`Pixels` or `Sound`), or `null` when the bytes are malformed — it never
silently downgrades a corrupt known-format asset to `Bytes`. Only unrecognized extensions return
raw `Bytes`. Use `LoadBytes` to read any asset (recognized or not) as raw bytes unconditionally.

### Assets.LoadBytes(name: String) -> Bytes?

Load raw bytes regardless of extension. Returns null if not found. A zero-byte loose filesystem asset returns a `Bytes` object with length 0.

### Assets.Exists(name: String) -> Integer

Returns 1 if asset exists (embedded, in pack, or as a regular file on disk), 0 otherwise. Directories and special filesystem nodes are not assets.

### Assets.SizeBytes(name: String) -> Integer

Returns asset size in bytes, or -1 if the asset is missing or resolves to a non-regular filesystem path such as a directory. A found zero-byte asset reports 0, so zero-byte files are distinguishable from missing assets without a separate `Exists()` call.

### Assets.List() -> Object (runtime Seq\<String\>)

Returns names from the embedded archive followed by every mounted pack in mount
order. Loose filesystem assets are not enumerated, and duplicate logical names
from different sources are retained. The registry exposes the result as opaque
`Object`; use `Zanna.Collections.Seq` operations to inspect it from a frontend.

### Assets.Mount(path: String) -> Integer

Mount a `.zpak` pack file for asset resolution. Returns 1 on success, 0 on failure.
Pack files next to the executable are auto-mounted at startup. macOS application
bundles also scan their `Resources` directory.
If the pack opens but its path cannot be recorded, the mount fails and the pack handle is closed.
Mounting the same canonical pack path more than once is idempotent and returns 1 without adding a duplicate mount.

### Assets.Unmount(path: String) -> Integer

Unmount a previously mounted pack. Returns 1 on success, 0 on failure.
Unmount first matches the canonical full path used at mount time. Passing only a basename such as `"level2.zpak"` is allowed only when exactly one mounted pack has that basename; if two mounted packs share the same basename, basename-only unmount returns 0 rather than removing an arbitrary pack.

### Path.ExeDir() -> String

Returns the directory containing the running executable. Uses platform-specific APIs with
dynamically sized buffers and truncation detection:
- macOS: `_NSGetExecutablePath()` (queries the required size), then `realpath`
- Windows: `GetModuleFileNameW()` in a growing buffer (Unicode; truncation-detecting), then UTF-8
- Linux: `readlink("/proc/self/exe")` in a growing buffer (truncation-detecting)

Long and non-ASCII executable paths resolve to the real directory; `"."` is returned only when
the OS lookup genuinely fails, so a non-`"."` result is a reliable executable location.

## Resolution Order

When `Assets.Load("sprites/hero.png")` or `Assets.Load("asset://sprites/hero.png")` is called:

1. **Embedded registry** (in `.rodata`) — zero disk I/O
2. **Mounted packs** (last mounted first) — entry bytes read on demand from one pack file
3. **Filesystem** (CWD-relative) — development fallback

This means existing code keeps working during development (step 3), and packaged apps find their assets automatically (steps 1-2).
Loose filesystem fallback is constrained to relative, CWD-based asset names. Absolute names, drive-qualified names, colon-containing names, empty or `.`/`..` path segments, traversal syntax, and embedded NUL bytes are rejected. Loose fallback only opens regular files and refuses symlink targets. Pack auto-discovery also skips symlinks and Windows reparse points rather than following them.
Mount paths containing embedded NUL bytes are rejected.
Asset lookup, mounting, unmounting, listing, and lazy initialization are synchronized internally so concurrent readers cannot race with pack mount changes.

## ZPAK Format

ZPAK (Zanna Pack Archive) is a simple binary container: 32-byte header + data blob + table of contents. The same format is used for both embedded blobs and standalone `.zpak` files.
The runtime validates TOC bounds, complete TOC parsing, duplicate names, entry data ranges, and compressed/uncompressed size agreement before returning asset bytes.
Typed asset decoders that need a file path spill bytes through an exclusive private temporary file or directory and clean it up after decoding; temporary handles are non-inheritable/close-on-exec where supported.

## Example

```rust
bind Zanna.IO;
bind Zanna.Graphics;

// Load from embedded or mounted packs — transparent
var hero = Assets.Load("sprites/hero.png");
var hero2 = Assets.Load("asset://sprites/hero.png");
var sound = Assets.Load("audio/click.wav");
var data = Assets.LoadBytes("config.json");

// Mount additional pack files
Assets.Mount("level2.zpak");
var bg = Assets.Load("backgrounds/sky.png");
Assets.Unmount("level2.zpak");
```

## Editor Resolver

`Zanna.Assets.Resolver.Resolve(scenePath, projectRoot, assetRootsCsv, assetPath)` returns a structured lookup record for editor and scene workflows that need to explain where an asset was found.

The returned `Map` includes:

| Field | Description |
|-------|-------------|
| `found` / `exists` | True when the asset resolved to an existing filesystem path or mounted asset |
| `path` | Absolute, lexically normalized filesystem path, or the mounted asset name |
| `displayPath` | Project-relative display path when available |
| `source` | `absolute`, `scene`, `project`, `assetRoot`, `mounted`, or `missing` |
| `diagnostic` | Human-readable missing-asset message when unresolved |

Resolution checks absolute paths first, then the scene's directory, then `projectRoot`, then each comma-separated asset root. If no filesystem candidate exists, the resolver checks mounted assets through `Zanna.IO.Assets`.
Filesystem resolution uses existence checks rather than regular-file checks, so
editor callers that require a file should validate the returned path's kind.
An empty `assetPath` is rejected as not found with an `empty asset name` diagnostic (it does not
resolve to the project directory). A relative `scenePath` is interpreted relative to `projectRoot`,
so scene-relative resolution is independent of the editor's current working directory.

## See Also

- [Files & Directories](files.md) — Lower-level file I/O
- [Streams](streams.md) — Stream-based reading and writing
- [Zanna Runtime Library](../README.md)
