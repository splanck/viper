---
status: active
audience: public
last-verified: 2026-05-07
---

# Viper.IO.Assets

Asset management system for loading embedded and packed resources.

## Overview

Assets can be:
- **Embedded** in the executable (zero disk I/O, declared with `embed` in `viper.project`)
- **Packed** in `.vpa` files distributed alongside the executable (`pack` in `viper.project`)
- **Loose** on the filesystem (development workflow, no declaration needed)

## viper.project Directives

```
# Embed file or directory into executable .rodata
embed sprites/player.png
embed sprites

# Create named .vpa pack file
pack level1 levels/level1/

# Pack with DEFLATE compression
pack-compressed music audio/tracks/
```

Pre-compressed formats (`.png`, `.jpg`, `.ogg`, `.mp3`, `.glb`, etc.) automatically skip compression even with `pack-compressed`.

## API Reference

### Assets.Load(name: String) -> Object?

Load an asset by name. Returns a typed object based on file extension:

| Extension | Return Type |
|-----------|-------------|
| `.png`, `.jpg`, `.jpeg`, `.bmp`, `.gif` | Pixels |
| `.wav`, `.ogg`, `.mp3` | Sound |
| Other | Bytes |

Returns null if not found.

### Assets.LoadBytes(name: String) -> Bytes?

Load raw bytes regardless of extension. Returns null if not found. A zero-byte loose filesystem asset returns a `Bytes` object with length 0.

### Assets.Exists(name: String) -> Integer

Returns 1 if asset exists (embedded, in pack, or as a regular file on disk), 0 otherwise. Directories and special filesystem nodes are not assets.

### Assets.Size(name: String) -> Integer

Returns asset size in bytes, or -1 if the asset is missing or resolves to a non-regular filesystem path such as a directory. A found zero-byte asset reports 0, so zero-byte files are distinguishable from missing assets without a separate `Exists()` call.

### Assets.List() -> seq\<String\>

Returns names of all available assets (embedded + all mounted packs).

### Assets.Mount(path: String) -> Integer

Mount a `.vpa` pack file for asset resolution. Returns 1 on success, 0 on failure.
Pack files next to the executable are auto-mounted at startup.
If the pack opens but its path cannot be recorded, the mount fails and the pack handle is closed.
Mounting the same canonical pack path more than once is idempotent and returns 1 without adding a duplicate mount.

### Assets.Unmount(path: String) -> Integer

Unmount a previously mounted pack. Returns 1 on success, 0 on failure.
Unmount first matches the canonical full path used at mount time. Passing only a basename such as `"level2.vpa"` is allowed only when exactly one mounted pack has that basename; if two mounted packs share the same basename, basename-only unmount returns 0 rather than removing an arbitrary pack.

### Path.ExeDir() -> String

Returns the directory containing the running executable. Uses platform-specific APIs:
- macOS: `_NSGetExecutablePath()`
- Windows: `GetModuleFileNameA()`
- Linux: `readlink("/proc/self/exe")`

## Resolution Order

When `Assets.Load("sprites/hero.png")` is called:

1. **Embedded registry** (in `.rodata`) — zero disk I/O
2. **Mounted packs** (last mounted first) — single file read
3. **Filesystem** (CWD-relative) — development fallback

This means existing code keeps working during development (step 3), and packaged apps find their assets automatically (steps 1-2).
Loose filesystem fallback is constrained to relative, CWD-based asset names. Absolute names, drive-qualified names, colon-containing names, empty or `.`/`..` path segments, traversal syntax, and embedded NUL bytes are rejected. Loose fallback only opens regular files and refuses symlink targets.
Mount paths containing embedded NUL bytes are rejected.
Asset lookup, mounting, unmounting, listing, and lazy initialization are synchronized internally so concurrent readers cannot race with pack mount changes.

## VPA Format

VPA (Viper Pack Archive) is a simple binary container: 32-byte header + data blob + table of contents. The same format is used for both embedded blobs and standalone `.vpa` files.
The runtime validates TOC bounds, complete TOC parsing, duplicate names, entry data ranges, and compressed/uncompressed size agreement before returning asset bytes.

## Example

```zia
bind Viper.IO;
bind Viper.Graphics;

// Load from embedded or mounted packs — transparent
var hero = Assets.Load("sprites/hero.png");
var sound = Assets.Load("audio/click.wav");
var data = Assets.LoadBytes("config.json");

// Mount additional pack files
Assets.Mount("level2.vpa");
var bg = Assets.Load("backgrounds/sky.png");
Assets.Unmount("level2.vpa");
```

## See Also

- [Files & Directories](files.md) — Lower-level file I/O
- [Streams](streams.md) — Stream-based reading and writing
- [Viper Runtime Library](../README.md)
