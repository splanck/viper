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
| `.wav`, `.ogg`, `.mp3`, `.vaf` | Sound |
| Other | Bytes |

Returns null if not found.

### Assets.LoadBytes(name: String) -> Bytes?

Load raw bytes regardless of extension. Returns null if not found.

### Assets.Exists(name: String) -> Integer

Returns 1 if asset exists (embedded, in pack, or on disk), 0 otherwise.

### Assets.Size(name: String) -> Integer

Returns asset size in bytes, or 0 if not found.

### Assets.List() -> seq\<String\>

Returns names of all available assets (embedded + all mounted packs).

### Assets.Mount(path: String) -> Integer

Mount a `.vpa` pack file for asset resolution. Returns 1 on success, 0 on failure.
Pack files next to the executable are auto-mounted at startup.

### Assets.Unmount(path: String) -> Integer

Unmount a previously mounted pack. Returns 1 on success, 0 on failure.

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

## VPA Format

VPA (Viper Pack Archive) is a simple binary container: 32-byte header + data blob + table of contents. The same format is used for both embedded blobs and standalone `.vpa` files.

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
