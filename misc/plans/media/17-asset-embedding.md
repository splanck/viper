# Plan 17: Asset Embedding System

## Overview

A build-time asset embedding system that gives developers two options for distributing
external files (images, audio, 3D models, video, fonts, tilemaps, data files):

1. **Embed into executable** — assets baked into `.rodata` section, zero disk I/O at runtime
2. **Pack into asset files** — assets bundled into `.vpa` (Viper Pack Archive) files distributed alongside the executable

Both use the same binary container format (VPA). A runtime asset manager transparently
loads from embedded data, mounted packs, or the filesystem (for dev workflow).

---

## 1. VPA Binary Format Specification

### Layout

```
Offset  Size     Field
──────  ────     ─────
0x00    4        Magic: "VPA1" (0x56 0x50 0x41 0x31)
0x04    2        Version: uint16_le (1)
0x06    2        Flags: uint16_le
                   bit 0: has compression
0x08    4        Entry count: uint32_le
0x0C    8        TOC offset: uint64_le (byte offset to TOC start)
0x14    8        TOC size: uint64_le (byte count of TOC region)
0x1C    4        Reserved: zero
────── 32 bytes total header ──────

0x20    ...      Data region (entry payloads, each 8-byte aligned)

TOC     ...      Table of contents (array of variable-size entries):
  Per entry:
    2            name_len: uint16_le
    name_len     name: UTF-8 bytes (relative path, forward slashes, no null term)
    8            data_offset: uint64_le (from file/blob start)
    8            data_size: uint64_le (original uncompressed size)
    8            stored_size: uint64_le (size in data region; == data_size if uncompressed)
    2            flags: uint16_le
                   bit 0: entry is DEFLATE-compressed
    2            reserved: zero
```

### Design Rationale

- TOC at end: writer can stream data without seeking; only header's `toc_offset` patched at close
- 8-byte alignment per entry: efficient memory access when the blob lives in `.rodata`
- Per-entry compression flag: pre-compressed formats (PNG, JPEG, OGG, MP3) skip compression; raw formats (BMP, WAV, JSON, CSV) benefit
- Same format for embedded blob and standalone `.vpa` files — one reader implementation

---

## 2. Project Manifest Directives

### New directives in `viper.project`

```
# Embed single file into executable .rodata
embed sprites/hero.png

# Embed entire directory (recursive) into executable .rodata
embed sprites

# Create named pack file — files added to <name>.vpa
pack textures sprites/hero.png
pack textures sprites/enemy.png

# Add entire directory to named pack
pack textures sprites/

# Pack with DEFLATE compression (skips pre-compressed formats automatically)
pack-compressed music audio/tracks/
```

### Auto-compression skip list

When `pack-compressed` is used, these extensions are stored uncompressed (already compressed):
`.png`, `.jpg`, `.jpeg`, `.gif`, `.ogg`, `.mp3`, `.vaf`, `.glb`, `.gz`, `.zip`, `.vpa`

All other extensions get DEFLATE level 6.

### Example project

```
name my-game
entry main.zia

# Small critical assets embedded in binary
embed sprites/player.png
embed sprites/ui_cursor.png
embed audio/ui_click.wav

# Level data as separate packs
pack level1 levels/level1/
pack level2 levels/level2/

# Large media compressed
pack-compressed cinematics video/
```

Build output:
```
build/my-game                  (executable — player.png, ui_cursor.png, ui_click.wav in .rodata)
build/my-game-level1.vpa       (level 1 assets)
build/my-game-level2.vpa       (level 2 assets)
build/my-game-cinematics.vpa   (compressed video)
```

---

## 3. Build Pipeline Changes

### 3.1 Project Loader — parse new directives

**File:** `src/tools/common/project_loader.hpp`

Add to `ProjectConfig`:
```cpp
struct EmbedEntry {
    std::string sourcePath;  // Relative to project root (file or dir)
};

struct PackGroup {
    std::string name;                    // Pack name → produces <name>.vpa
    std::vector<std::string> sources;    // Files/dirs to include
    bool compressed{false};              // DEFLATE compression
};

struct ProjectConfig {
    // ... existing fields ...
    std::vector<EmbedEntry> embedAssets;   // NEW
    std::vector<PackGroup> packGroups;     // NEW
};
```

**File:** `src/tools/common/project_loader.cpp`

Add parsing in `parseManifest()`, after existing `asset` directive (around line 419):

```cpp
} else if (directive == "embed") {
    // Format: embed <source-path>
    if (value.empty())
        return makeManifestErr(manifestPath, lineNum, "embed requires <source-path>");
    config.embedAssets.push_back({value});

} else if (directive == "pack" || directive == "pack-compressed") {
    // Format: pack <name> <source-path>
    auto sp = value.find_first_of(" \t");
    if (sp == std::string::npos)
        return makeManifestErr(manifestPath, lineNum,
            directive + " requires <name> <source-path>; got '" + value + "'");
    std::string name = value.substr(0, sp);
    std::string src = value.substr(value.find_first_not_of(" \t", sp));
    bool compressed = (directive == "pack-compressed");

    // Find or create pack group
    auto it = std::find_if(config.packGroups.begin(), config.packGroups.end(),
        [&](const PackGroup &g) { return g.name == name; });
    if (it == config.packGroups.end()) {
        config.packGroups.push_back({name, {src}, compressed});
    } else {
        it->sources.push_back(src);
        if (compressed) it->compressed = true;
    }
}
```

### 3.2 Asset Compiler — new module

**New files:**
- `src/tools/common/asset/AssetCompiler.hpp`
- `src/tools/common/asset/AssetCompiler.cpp`
- `src/tools/common/asset/VpaWriter.hpp`
- `src/tools/common/asset/VpaWriter.cpp`

#### VpaWriter (src/tools/common/asset/VpaWriter.hpp)

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace viper::asset {

class VpaWriter {
  public:
    /// Add an entry. If compress is true AND extension is not in skip-list,
    /// the data is DEFLATE-compressed.
    void addEntry(const std::string &name, const uint8_t *data, size_t size, bool compress);

    /// Write VPA to file. Returns false on I/O error, sets err.
    bool writeToFile(const std::string &path, std::string &err);

    /// Write VPA to memory buffer. Returns the complete VPA blob.
    std::vector<uint8_t> writeToMemory();

    /// Number of entries added.
    size_t entryCount() const;

  private:
    struct Entry {
        std::string name;
        std::vector<uint8_t> data;   // stored data (possibly compressed)
        uint64_t originalSize;
        bool compressed;
    };
    std::vector<Entry> entries_;
};

} // namespace viper::asset
```

#### VpaWriter implementation (src/tools/common/asset/VpaWriter.cpp)

- Uses `viper::pkg::deflate()` from `PkgDeflate.hpp` for compression
- Auto-skip list: check file extension before compressing
- Writes header (32 bytes), then data entries (each 8-byte aligned), then TOC
- Patches `toc_offset` in header after writing TOC

#### AssetCompiler (src/tools/common/asset/AssetCompiler.hpp)

```cpp
#pragma once
#include "tools/common/project_loader.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace viper::asset {

struct AssetBundle {
    std::vector<uint8_t> embeddedBlob;       // VPA blob for .rodata (empty if no embeds)
    std::vector<std::string> packFilePaths;  // Paths to generated .vpa files
};

/// Compile assets declared in project config.
///
/// 1. Resolves all `embed` entries → builds VPA blob for .rodata
/// 2. Resolves all `pack` groups → writes .vpa files to outputDir
///
/// @param config Project configuration with embedAssets and packGroups
/// @param outputDir Directory for .vpa output files
/// @param err Error message on failure
/// @return AssetBundle on success, nullopt on failure
std::optional<AssetBundle> compileAssets(
    const il::tools::common::ProjectConfig &config,
    const std::string &outputDir,
    std::string &err);

} // namespace viper::asset
```

#### AssetCompiler implementation (src/tools/common/asset/AssetCompiler.cpp)

Steps:
1. For each `embed` entry:
   - If path is a file: read it, add to VpaWriter
   - If path is a directory: recursively enumerate all files, add each with relative path
   - Validation: file exists, readable, not a symlink outside project root
2. Call `VpaWriter::writeToMemory()` → `bundle.embeddedBlob`
3. For each `pack` group:
   - Create separate VpaWriter
   - Enumerate all source paths (files/dirs)
   - Add entries with compression flag from group
   - Write to `<outputDir>/<projectName>-<packName>.vpa`
   - Store path in `bundle.packFilePaths`
4. Return bundle

### 3.3 Build Command Integration

**File:** `src/tools/viper/cmd_run.cpp`

In `runOrBuild()`, after compilation succeeds and before `compileToNative()` (around line 450):

```cpp
// --- NEW: Asset compilation ---
std::vector<uint8_t> assetBlob;
if (!proj.embedAssets.empty() || !proj.packGroups.empty()) {
    std::string outputDir = std::filesystem::path(config.outputPath).parent_path().string();
    if (outputDir.empty()) outputDir = ".";

    std::string assetErr;
    auto bundle = viper::asset::compileAssets(proj, outputDir, assetErr);
    if (!bundle) {
        std::cerr << "error: asset compilation failed: " << assetErr << "\n";
        return 1;
    }
    assetBlob = std::move(bundle->embeddedBlob);
}

// Write asset blob to temp file for codegen
std::string assetBlobPath;
if (!assetBlob.empty()) {
    assetBlobPath = viper::tools::generateTempAssetPath();
    std::ofstream af(assetBlobPath, std::ios::binary);
    af.write(reinterpret_cast<const char*>(assetBlob.data()), assetBlob.size());
}

int rc = viper::tools::compileToNative(tempIlPath, config.outputPath, arch, assetBlobPath);

// Cleanup temp files
std::filesystem::remove(tempIlPath, ec);
if (!assetBlobPath.empty()) std::filesystem::remove(assetBlobPath, ec);
```

### 3.4 Native Compiler — pass asset blob path

**File:** `src/tools/common/native_compiler.hpp`

```cpp
int compileToNative(const std::string &ilPath,
                    const std::string &outputPath,
                    TargetArch arch = detectHostArch(),
                    const std::string &assetBlobPath = "");

std::string generateTempAssetPath();  // NEW
```

**File:** `src/tools/common/native_compiler.cpp`

```cpp
int compileToNative(const std::string &ilPath,
                    const std::string &outputPath,
                    TargetArch arch,
                    const std::string &assetBlobPath) {
    if (arch == TargetArch::ARM64) {
        std::vector<std::string> storage = {ilPath, "-o", outputPath, "-O0"};
        if (!assetBlobPath.empty()) {
            storage.push_back("--asset-blob");
            storage.push_back(assetBlobPath);
        }
        std::vector<char *> argv;
        argv.reserve(storage.size());
        for (auto &s : storage)
            argv.push_back(s.data());
        return viper::tools::ilc::cmd_codegen_arm64(
            static_cast<int>(argv.size()), argv.data());
    }

    // X64
    viper::codegen::x64::CodegenPipeline::Options opts;
    opts.input_il_path = ilPath;
    opts.output_obj_path = outputPath;
    opts.optimize = 0;
    opts.asset_blob_path = assetBlobPath;  // NEW field

    viper::codegen::x64::CodegenPipeline pipeline(opts);
    PipelineResult result;
    try {
        result = pipeline.run();
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
    if (!result.stdout_text.empty()) std::cout << result.stdout_text;
    if (!result.stderr_text.empty()) std::cerr << result.stderr_text;
    return result.exit_code;
}

std::string generateTempAssetPath() {
    auto dir = std::filesystem::temp_directory_path();
#ifdef _WIN32
    auto pid = _getpid();
#else
    auto pid = getpid();
#endif
    return (dir / ("viper_assets_" + std::to_string(pid) + ".vpa")).string();
}
```

---

## 4. Codegen Changes — Emit Asset Blob into .rodata

### 4.1 X86_64 Pipeline

**File:** `src/codegen/x86_64/CodegenPipeline.hpp`

Add field to `Options`:
```cpp
struct Options {
    // ... existing fields ...
    std::string asset_blob_path;  // NEW: path to VPA blob for embedding (optional)
};
```

**File:** `src/codegen/x86_64/CodegenPipeline.cpp`

In `run()`, after IL loading and before backend invocation, read the asset blob:
```cpp
std::vector<uint8_t> assetBlob;
if (!opts_.asset_blob_path.empty()) {
    std::ifstream af(opts_.asset_blob_path, std::ios::binary | std::ios::ate);
    if (!af.is_open()) {
        // error
    }
    auto sz = af.tellg();
    af.seekg(0);
    assetBlob.resize(static_cast<size_t>(sz));
    af.read(reinterpret_cast<char*>(assetBlob.data()), sz);
}
```

Pass `assetBlob` to the backend emission function.

**File:** `src/codegen/x86_64/Backend.cpp`

After the existing rodata emission loop (after line 331 — f64 literals), add:

```cpp
// --- Asset blob emission ---
if (!assetBlob.empty()) {
    result.rodata.alignTo(16);  // 16-byte align for efficient access

    // Blob data
    std::string blobLabel = "viper_asset_blob";
    if (isDarwin) blobLabel = "_" + blobLabel;
    result.rodata.defineSymbol(
        blobLabel, objfile::SymbolBinding::Global, objfile::SymbolSection::Rodata);
    result.rodata.emitBytes(assetBlob.data(), assetBlob.size());

    // Blob size (uint64)
    result.rodata.alignTo(8);
    std::string sizeLabel = "viper_asset_blob_size";
    if (isDarwin) sizeLabel = "_" + sizeLabel;
    result.rodata.defineSymbol(
        sizeLabel, objfile::SymbolBinding::Global, objfile::SymbolSection::Rodata);
    result.rodata.emit64LE(assetBlob.size());
}
```

Update `emitModuleToBinary()` signature to accept `const std::vector<uint8_t> &assetBlob`.

### 4.2 AArch64 Pipeline

**File:** `src/tools/viper/cmd_codegen_arm64.cpp`

Add `--asset-blob <path>` flag parsing (in the argument parser, around line 80):
```cpp
if (tok == "--asset-blob" && i + 1 < argc) {
    opts.asset_blob_path = argv[++i];
    continue;
}
```

**File:** `src/codegen/aarch64/passes/BinaryEmitPass.cpp`

After rodata pool entry emission (after line 74), add the same blob emission pattern:

```cpp
if (!module.assetBlob.empty()) {
    rodata.alignTo(16);
    rodata.defineSymbol(
        "viper_asset_blob", objfile::SymbolBinding::Global, objfile::SymbolSection::Rodata);
    rodata.emitBytes(module.assetBlob.data(), module.assetBlob.size());
    rodata.alignTo(8);
    rodata.defineSymbol(
        "viper_asset_blob_size", objfile::SymbolBinding::Global, objfile::SymbolSection::Rodata);
    rodata.emit64LE(module.assetBlob.size());
}
```

Note: On macOS AArch64, MachO writer already adds `_` prefix to all global symbols.

### 4.3 Symbol Visibility

The symbols `viper_asset_blob` and `viper_asset_blob_size` are emitted as `Global` symbols.
When no asset blob is provided, these symbols simply don't exist in the object file.

The runtime uses a **null check** at init time rather than weak symbols:

**File:** `src/runtime/io/rt_asset.c` (new)

The runtime's `rt_asset_init()` is called from the app's generated `main` trampoline.
The codegen emits a call to `rt_asset_init(blob_ptr, blob_size)` only when an asset blob
is present. When there's no asset blob, no call is emitted, and the runtime's asset
registry remains empty (searching falls through to disk I/O).

This avoids the need for weak symbol support in the object file writers entirely.

---

## 5. Runtime Asset Manager

### 5.1 New files

```
src/runtime/io/rt_asset.h       (~80 LOC)
src/runtime/io/rt_asset.c       (~500 LOC)
src/runtime/io/rt_vpa_reader.h  (~50 LOC)
src/runtime/io/rt_vpa_reader.c  (~300 LOC)
src/runtime/io/rt_path_exe.c    (~120 LOC)  — platform-specific exe directory detection
```

### 5.2 VPA Reader (rt_vpa_reader.h/c)

Parses VPA format from either memory buffer or file handle.

```c
typedef struct {
    const char *name;       // Asset name (relative path)
    uint64_t data_offset;   // Offset in blob/file
    uint64_t data_size;     // Original size
    uint64_t stored_size;   // Stored size (compressed or not)
    int compressed;         // 1 if DEFLATE compressed
} vpa_entry_t;

typedef struct {
    vpa_entry_t *entries;
    uint32_t count;
    const uint8_t *blob;    // Non-NULL for memory-mapped VPA (embedded)
    FILE *file;             // Non-NULL for file-based VPA (mounted pack)
} vpa_archive_t;

// Parse VPA from memory (for embedded blob)
vpa_archive_t *vpa_open_memory(const uint8_t *data, size_t size);

// Parse VPA from file (for mounted packs)
vpa_archive_t *vpa_open_file(const char *path);

// Find entry by name (binary search on sorted TOC)
const vpa_entry_t *vpa_find(const vpa_archive_t *archive, const char *name);

// Read entry data (decompresses if needed). Caller must free returned buffer.
uint8_t *vpa_read_entry(const vpa_archive_t *archive, const vpa_entry_t *entry, size_t *out_size);

// Close archive (frees TOC, closes file handle if file-based)
void vpa_close(vpa_archive_t *archive);
```

Decompression uses `rt_compress_inflate()` from `rt_compress.h` (already in runtime).

### 5.3 Executable Directory Detection (rt_path_exe.c)

```c
// Returns malloc'd string with the directory containing the running executable.
// Returns NULL on failure.
char *rt_path_exe_dir(void);
```

Platform implementations:
- **macOS:** `_NSGetExecutablePath()` → `realpath()` → `dirname()`
  - Header: `#include <mach-o/dyld.h>`
- **Windows:** `GetModuleFileNameA(NULL, buf, MAX_PATH)` → strip filename
  - Header: `#include <windows.h>`
- **Linux:** `readlink("/proc/self/exe", buf, PATH_MAX)` → `dirname()`
  - Header: `#include <unistd.h>`
- **ViperDOS:** returns `"."` (no meaningful exe path)

Also exposed to Zia/BASIC as `Path.ExeDir()`:
```
RT_FUNC("rt_path_exe_dir_str", "Viper.IO.Path.ExeDir", "str", "")
```

### 5.4 Asset Manager (rt_asset.h/c)

#### Internal State

```c
#define RT_ASSET_MAX_PACKS 32

static struct {
    vpa_archive_t *embedded;                    // From .rodata blob (NULL if none)
    vpa_archive_t *packs[RT_ASSET_MAX_PACKS];   // Mounted pack files
    int pack_count;
    int initialized;
} g_asset_mgr;
```

#### Initialization

Called from codegen-generated main trampoline (only when assets are embedded):

```c
void rt_asset_init(const uint8_t *blob, uint64_t size) {
    if (g_asset_mgr.initialized) return;
    g_asset_mgr.initialized = 1;

    // Parse embedded blob
    if (blob && size >= 32) {
        g_asset_mgr.embedded = vpa_open_memory(blob, size);
    }

    // Auto-discover .vpa packs next to executable
    char *exe_dir = rt_path_exe_dir();
    if (exe_dir) {
        rt_asset_discover_packs(exe_dir);
        free(exe_dir);
    }

#ifdef __APPLE__
    // Also check bundle Resources directory
    char *res_dir = rt_path_macos_resources();
    if (res_dir) {
        rt_asset_discover_packs(res_dir);
        free(res_dir);
    }
#endif
}
```

`rt_asset_discover_packs()` scans a directory for `*.vpa` files and auto-mounts them.

#### Lazy Initialization for Non-Embedded Builds

For projects that don't use `embed` but do distribute `.vpa` packs, the asset manager
auto-initializes on first `Assets.Load()` or `Assets.Mount()` call:

```c
static void ensure_init(void) {
    if (!g_asset_mgr.initialized) {
        rt_asset_init(NULL, 0);  // No embedded blob, but still discover packs
    }
}
```

#### Core Load Function

```c
// Search order: embedded → mounted packs (reverse order) → filesystem
static uint8_t *asset_find_data(const char *name, size_t *out_size) {
    ensure_init();

    // 1. Embedded registry
    if (g_asset_mgr.embedded) {
        const vpa_entry_t *e = vpa_find(g_asset_mgr.embedded, name);
        if (e) return vpa_read_entry(g_asset_mgr.embedded, e, out_size);
    }

    // 2. Mounted packs (reverse order — last mounted wins)
    for (int i = g_asset_mgr.pack_count - 1; i >= 0; --i) {
        const vpa_entry_t *e = vpa_find(g_asset_mgr.packs[i], name);
        if (e) return vpa_read_entry(g_asset_mgr.packs[i], e, out_size);
    }

    // 3. Filesystem fallback (CWD-relative)
    FILE *f = fopen(name, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        *out_size = (size_t)ftell(f);
        rewind(f);
        uint8_t *buf = (uint8_t *)malloc(*out_size);
        if (buf) fread(buf, 1, *out_size, f);
        fclose(f);
        return buf;
    }

    return NULL;  // Not found
}
```

#### Type-Dispatched Loading

```c
void *rt_asset_load(rt_string name) {
    const char *cname = rt_string_cstr(name);
    size_t size = 0;
    uint8_t *data = asset_find_data(cname, &size);
    if (!data) return NULL;

    // Dispatch by extension
    const char *ext = strrchr(cname, '.');
    if (!ext) { /* return as Bytes */ }

    void *result = NULL;
    if (strcasecmp(ext, ".png") == 0)
        result = rt_pixels_load_png_buffer(data, size);
    else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
        result = rt_pixels_load_jpeg_buffer(data, size);
    else if (strcasecmp(ext, ".bmp") == 0)
        result = rt_pixels_load_bmp_buffer(data, size);
    else if (strcasecmp(ext, ".gif") == 0)
        result = rt_pixels_load_gif_buffer(data, size);
    else if (strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".ogg") == 0 ||
             strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".vaf") == 0)
        result = rt_sound_load_mem(data, (int64_t)size);
    else if (strcasecmp(ext, ".obj") == 0)
        result = rt_mesh3d_from_obj_buffer(data, size);
    else if (strcasecmp(ext, ".stl") == 0)
        result = rt_mesh3d_from_stl_buffer(data, size);
    else if (strcasecmp(ext, ".gltf") == 0 || strcasecmp(ext, ".glb") == 0)
        result = rt_gltf_load_buffer(data, size);
    else if (strcasecmp(ext, ".fbx") == 0)
        result = rt_fbx_load_buffer(data, size);
    else if (strcasecmp(ext, ".bdf") == 0)
        result = rt_bitmapfont_load_bdf_buffer(data, size);
    else if (strcasecmp(ext, ".psf") == 0)
        result = rt_bitmapfont_load_psf_buffer(data, size);
    else {
        // Return as Bytes object
        result = rt_bytes_from_data(data, size);
    }

    free(data);
    return result;
}
```

Note: video (`.avi`, `.ogv`) and music (streaming) need MemStream adapters — see Section 6.3.

### 5.5 Runtime API Registration

**File:** `src/il/runtime/runtime.def`

```
// ═══ Asset Management ═══
RT_CLASS("Viper.IO.Assets", "Assets", "none", "none")
RT_FUNC("rt_asset_load",         "Viper.IO.Assets.Load",        "obj", "str")
RT_FUNC("rt_asset_load_bytes",   "Viper.IO.Assets.LoadBytes",   "obj", "str")
RT_FUNC("rt_asset_exists",       "Viper.IO.Assets.Exists",      "i64", "str")
RT_FUNC("rt_asset_size",         "Viper.IO.Assets.Size",        "i64", "str")
RT_FUNC("rt_asset_list",         "Viper.IO.Assets.List",        "obj", "")
RT_FUNC("rt_asset_mount",        "Viper.IO.Assets.Mount",       "i64", "str")
RT_FUNC("rt_asset_unmount",      "Viper.IO.Assets.Unmount",     "i64", "str")

// ═══ Exe Directory ═══
RT_FUNC("rt_path_exe_dir_str",   "Viper.IO.Path.ExeDir",        "str", "")
```

**File:** `src/il/runtime/RuntimeSignatures.cpp`

Add signature entries for each new function (matching the pattern of existing entries).

**File:** `src/il/runtime/RuntimeClasses.hpp`

Add `Assets` class entry in the class registry.

---

## 6. From-Buffer Decoder Refactoring

Every asset loader that currently uses `fopen()` needs an internal `_buffer()` variant.
The existing file-based functions become thin wrappers around the buffer variant.

### 6.1 Image Decoders

**File:** `src/runtime/graphics/rt_pixels_io.c`

#### PNG — extract buffer parser

Current flow: `fopen()` → read all → parse buffer → Pixels
Refactoring: split out `rt_pixels_load_png_buffer(const uint8_t *data, size_t len)`.

The PNG parser already works on an `uint8_t *file_data` buffer internally (reads entire
file into memory first). Extract the parsing code after the `fread()` call into a
separate function.

```c
// NEW: parse PNG from memory buffer
void *rt_pixels_load_png_buffer(const uint8_t *data, size_t len);

// MODIFIED: thin wrapper
void *rt_pixels_load_png(rt_string path) {
    // read file to buffer
    uint8_t *buf = read_file_to_buffer(cpath, &len);
    if (!buf) return NULL;
    void *result = rt_pixels_load_png_buffer(buf, len);
    free(buf);
    return result;
}
```

#### JPEG — expose existing buffer decoder

`rt_jpeg_decode_buffer(const uint8_t *data, size_t len)` already exists internally.
Just add a public header declaration:

```c
void *rt_pixels_load_jpeg_buffer(const uint8_t *data, size_t len);
```

#### BMP — extract buffer parser

Same pattern as PNG. The BMP parser reads the file header and pixel rows from a buffer.

```c
void *rt_pixels_load_bmp_buffer(const uint8_t *data, size_t len);
```

#### GIF — extract buffer parser

**File:** `src/runtime/graphics/rt_gif.c`

Same pattern. GIF decoder reads entire file, then parses LZW blocks from buffer.

```c
void *rt_pixels_load_gif_buffer(const uint8_t *data, size_t len);
// Also for animated GIF (returns frame array):
void *rt_gif_decode_buffer(const uint8_t *data, size_t len);
```

### 6.2 Audio Decoders

**File:** `src/runtime/audio/rt_audio.c`

#### Sound — already has memory loader

`rt_sound_load_mem(const void *data, int64_t size)` exists at line 567 but is NOT
exposed in `runtime.def`. Add:

```
RT_FUNC("rt_sound_load_mem_ext", "Viper.Sound.Sound.LoadBytes", "obj", "obj")
```

The wrapper converts a Bytes object to raw pointer + size, then calls `rt_sound_load_mem()`.

#### Music — needs MemStream adapter

Music uses streaming I/O (`fread()` in a loop). For embedded/packed assets, we need
a memory-backed stream. Add `rt_memstream` support:

```c
// Internal: create a music object from memory buffer
void *rt_music_load_mem(const uint8_t *data, size_t len);
```

This wraps the data in an internal struct that the OGG/MP3 streamers can read from
instead of `FILE *`. The OGG reader (`ogg_reader_open_file`) and MP3 streamer
(`mp3_stream_open`) each need a `_from_buffer` variant that reads from `(uint8_t*, size, pos)`.

### 6.3 3D Model Decoders

**File:** `src/runtime/graphics/rt_obj_loader.c`

OBJ parser uses `fgets()` line-by-line. Refactor to `_buffer` variant that scans
through a byte buffer looking for newlines:

```c
void *rt_mesh3d_from_obj_buffer(const uint8_t *data, size_t len);
```

MTL file references within OBJ: when loading from a buffer, MTL textures are resolved
from the asset system (call `asset_find_data()` for texture paths referenced in the MTL).

**File:** `src/runtime/graphics/rt_stl_loader.c`

STL parser reads entire file into memory. Straightforward extraction:

```c
void *rt_mesh3d_from_stl_buffer(const uint8_t *data, size_t len);
```

**File:** `src/runtime/graphics/rt_fbx_loader.c`

FBX loader reads entire file into buffer (line 1309: `fread()`). Extract:

```c
void *rt_fbx_load_buffer(const uint8_t *data, size_t len);
```

**File:** `src/runtime/graphics/rt_gltf_loader.c`

glTF/GLB loader reads entire file. Extract:

```c
void *rt_gltf_load_buffer(const uint8_t *data, size_t len);
```

For textures referenced by glTF materials: the buffer variant should accept a callback
or use the asset system to resolve texture paths.

### 6.4 Font Decoders

**File:** `src/runtime/graphics/rt_bitmapfont.c`

BDF is line-based text (like OBJ). PSF is binary header + glyph bitmaps.

```c
void *rt_bitmapfont_load_bdf_buffer(const uint8_t *data, size_t len);
void *rt_bitmapfont_load_psf_buffer(const uint8_t *data, size_t len);
```

### 6.5 Video/Tilemap

**File:** `src/runtime/graphics/rt_videoplayer.c`

Video uses `fseek()`/`fread()` for random access (AVI index, frame seeking).
Needs a virtual I/O layer:

```c
// Internal abstraction: reads from file OR memory buffer
typedef struct {
    const uint8_t *mem;   // Non-NULL for memory source
    size_t mem_size;
    size_t mem_pos;
    FILE *file;           // Non-NULL for file source
} vpa_io_t;

size_t vpa_io_read(vpa_io_t *io, void *buf, size_t n);
int vpa_io_seek(vpa_io_t *io, int64_t offset, int whence);
int64_t vpa_io_tell(vpa_io_t *io);
```

The VideoPlayer and Music streamers use this abstraction instead of raw `FILE *`.

**File:** `src/runtime/graphics/rt_tilemap_io.c`

Tilemap loads JSON/CSV from file. The JSON parser already works on strings.
Convert: read buffer → create string → call existing JSON parser.

```c
void *rt_tilemap_load_from_buffer(const uint8_t *data, size_t len);
```

### 6.6 Summary Table

| Decoder | Current API | New Buffer API | Effort |
|---------|------------|----------------|--------|
| PNG | `rt_pixels_load_png(path)` | `rt_pixels_load_png_buffer(data, len)` | ~2h |
| JPEG | `rt_pixels_load_jpeg(path)` | `rt_pixels_load_jpeg_buffer(data, len)` | ~30min (exists internally) |
| BMP | `rt_pixels_load_bmp(path)` | `rt_pixels_load_bmp_buffer(data, len)` | ~2h |
| GIF | `rt_pixels_load_gif(path)` | `rt_pixels_load_gif_buffer(data, len)` | ~3h |
| Sound | `rt_sound_load(path)` | `rt_sound_load_mem(data, size)` | ~30min (exists internally) |
| Music | `rt_music_load(path)` | `rt_music_load_mem(data, len)` | ~4h (needs MemStream) |
| OBJ | `rt_mesh3d_from_obj(path)` | `rt_mesh3d_from_obj_buffer(data, len)` | ~4h |
| STL | `rt_mesh3d_from_stl(path)` | `rt_mesh3d_from_stl_buffer(data, len)` | ~3h |
| FBX | `rt_fbx_load(path)` | `rt_fbx_load_buffer(data, len)` | ~3h |
| glTF | `rt_gltf_load(path)` | `rt_gltf_load_buffer(data, len)` | ~3h |
| BDF font | `rt_bitmapfont_load_bdf(path)` | `rt_bitmapfont_load_bdf_buffer(data, len)` | ~3h |
| PSF font | `rt_bitmapfont_load_psf(path)` | `rt_bitmapfont_load_psf_buffer(data, len)` | ~2h |
| Video | `rt_videoplayer_open(path)` | `rt_videoplayer_open_mem(data, len)` | ~6h (needs vpa_io_t) |
| Tilemap | `rt_tilemap_load_from_file(path)` | `rt_tilemap_load_from_buffer(data, len)` | ~3h |

---

## 7. Packaging Integration

### 7.1 VAPS Packager Changes

**File:** `src/tools/viper/cmd_package.cpp`

After compilation, the packager copies `.vpa` pack files into the installer alongside the exe:

```cpp
// Copy .vpa files into package
for (const auto &vpaPath : assetBundle.packFilePaths) {
    std::string filename = std::filesystem::path(vpaPath).filename().string();
    // Platform builders will place this next to the executable
    packageConfig.assets.push_back({vpaPath, "."});
}
```

### 7.2 Platform-Specific Pack Locations

The asset manager auto-discovers packs in platform-specific locations:

| Platform | Primary search dir | Secondary search dir |
|----------|-------------------|---------------------|
| Windows | Exe directory (from `GetModuleFileNameA`) | — |
| macOS | Exe directory (from `_NSGetExecutablePath`) | `../Resources/` (bundle) |
| Linux | Exe directory (from `/proc/self/exe`) | `/usr/share/<app>/` |

### 7.3 Linux .desktop Fix

**File:** `src/tools/common/packaging/DesktopEntryGenerator.cpp`

Add `Path=` directive to generated `.desktop` files so CWD is set correctly when
launched from the application menu (this helps the filesystem fallback path):

```cpp
// Add working directory for proper asset resolution
desktop << "Path=/usr/share/" << packageName << "\n";
```

---

## 8. Codegen Main Trampoline — rt_asset_init Call

### How the runtime init call gets wired

The codegen backends generate a `main` function that calls the runtime init, then
the user's `@main`. The asset init call is added when an asset blob is present.

**File:** `src/codegen/x86_64/Backend.cpp` (main trampoline generation)

When `assetBlob` is non-empty, the generated main trampoline emits:

```asm
; Load address of viper_asset_blob and its size
lea  rdi, [rip + viper_asset_blob]
mov  rsi, [rip + viper_asset_blob_size]
call rt_asset_init
; ... then call @main as before
```

In the binary encoder, this translates to:
1. Emit `LEA RDI, [RIP + blob_reloc]` — RIP-relative reference to blob symbol
2. Emit `MOV RSI, [RIP + size_reloc]` — load size
3. Emit `CALL rt_asset_init` — external function call

The linker resolves the relocations to the .rodata addresses where the blob was emitted.

**For AArch64**, the equivalent uses ADRP+ADD for blob address and LDR for size.

**When no assets are embedded**, these instructions are simply not emitted. The runtime's
`ensure_init()` handles lazy initialization for pack-only or dev-mode scenarios.

---

## 9. CMake Build System Changes

### 9.1 Asset Compiler Library

**File:** `src/tools/common/CMakeLists.txt`

```cmake
# Asset compiler sources
set(ASSET_SOURCES
    asset/AssetCompiler.cpp
    asset/VpaWriter.cpp
)

# Add to viper_common library (or create separate viper_asset lib)
target_sources(viper_common PRIVATE ${ASSET_SOURCES})
```

### 9.2 Runtime Sources

**File:** `src/runtime/CMakeLists.txt`

Add new source files to the runtime library:

```cmake
# In the IO sources section:
io/rt_asset.c
io/rt_vpa_reader.c
io/rt_path_exe.c
```

Platform-specific link libraries for `rt_path_exe.c`:
- macOS: needs `-framework Foundation` (or just `mach-o/dyld.h` which is in libSystem)
- Windows: already links `kernel32.lib`
- Linux: no extra libraries needed

### 9.3 Stub Functions

**File:** `src/runtime/graphics/rt_graphics_stubs.c`

Add stubs for all new `rt_asset_*` functions (used when graphics is disabled):

```c
void *rt_asset_load(void *name) { return NULL; }
void *rt_asset_load_bytes(void *name) { return NULL; }
int64_t rt_asset_exists(void *name) { return 0; }
int64_t rt_asset_size(void *name) { return 0; }
void *rt_asset_list(void) { return NULL; }
int64_t rt_asset_mount(void *path) { return 0; }
int64_t rt_asset_unmount(void *path) { return 0; }
void *rt_path_exe_dir_str(void) { return rt_const_cstr("."); }
```

---

## 10. Test Plan

### 10.1 Unit Tests — VPA Format

**File:** `src/tests/unit/runtime/TestVpaFormat.cpp`

```
TEST(VpaFormat, WriteAndReadRoundTrip)
  — Write 3 entries via VpaWriter, read back via vpa_open_memory, verify all data matches

TEST(VpaFormat, WriteCompressedRoundTrip)
  — Write entries with compression, read back, verify decompressed data matches original

TEST(VpaFormat, EmptyArchive)
  — Write VPA with 0 entries, verify header is valid, entry count is 0

TEST(VpaFormat, LargeEntry)
  — Write a 1MB entry, verify round-trip

TEST(VpaFormat, FindByName)
  — Write 10 entries, verify vpa_find returns correct entry for each name

TEST(VpaFormat, FindMissing)
  — Verify vpa_find returns NULL for non-existent name

TEST(VpaFormat, InvalidMagic)
  — Pass garbage data to vpa_open_memory, verify returns NULL

TEST(VpaFormat, TruncatedHeader)
  — Pass <32 bytes to vpa_open_memory, verify returns NULL

TEST(VpaFormat, FileWriteAndRead)
  — Write VPA to temp file, read back via vpa_open_file, verify

TEST(VpaFormat, SkipCompressionForPng)
  — Add .png entry with compress=true, verify it's stored uncompressed

TEST(VpaFormat, Alignment)
  — Verify all entry data_offsets are 8-byte aligned
```

CMake registration:
```cmake
viper_add_test(test_vpa_format
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime/TestVpaFormat.cpp
    LIBS viper_runtime viper_test_common)
```

### 10.2 Unit Tests — Asset Manager

**File:** `src/tests/unit/runtime/TestAssetManager.cpp`

```
TEST(AssetManager, LoadFromEmbedded)
  — Create VPA blob in memory, init asset mgr, verify Assets.Load finds entry

TEST(AssetManager, LoadFromMountedPack)
  — Write VPA to temp file, mount, verify Assets.Load finds entry

TEST(AssetManager, ResolutionOrder)
  — Embed asset "a.txt" with content "embedded", mount pack with "a.txt" = "packed",
    verify embedded wins (embedded searched first)

TEST(AssetManager, MountedPackOverride)
  — Mount pack1 with "a.txt" = "first", mount pack2 with "a.txt" = "second",
    verify pack2 wins (last mounted searched first)

TEST(AssetManager, FallbackToFilesystem)
  — No embedded or mounted data, write file to temp dir, chdir there,
    verify Assets.Load reads from disk

TEST(AssetManager, Unmount)
  — Mount pack, verify asset found, unmount, verify asset not found (falls through)

TEST(AssetManager, ExistsAndSize)
  — Verify Assets.Exists returns 1 for present, 0 for missing
  — Verify Assets.Size returns correct byte count

TEST(AssetManager, List)
  — Embed 3 assets, verify Assets.List returns all 3 names

TEST(AssetManager, LoadReturnsNull)
  — Verify Assets.Load returns NULL for non-existent asset

TEST(AssetManager, MountNonExistent)
  — Verify Assets.Mount returns 0 for non-existent .vpa file

TEST(AssetManager, MaxPacks)
  — Mount RT_ASSET_MAX_PACKS packs, verify 33rd mount fails gracefully
```

CMake registration:
```cmake
viper_add_test(test_asset_manager
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime/TestAssetManager.cpp
    LIBS viper_runtime viper_test_common)
```

### 10.3 Unit Tests — From-Buffer Decoders

**File:** `src/tests/unit/runtime/TestAssetDecoders.cpp`

```
TEST(AssetDecoders, PngFromBuffer)
  — Load a known PNG file into memory, call rt_pixels_load_png_buffer, verify dimensions + pixel sample

TEST(AssetDecoders, JpegFromBuffer)
  — Same with JPEG

TEST(AssetDecoders, BmpFromBuffer)
  — Same with BMP

TEST(AssetDecoders, GifFromBuffer)
  — Same with GIF (single frame)

TEST(AssetDecoders, SoundFromBuffer)
  — Load WAV into memory, call rt_sound_load_mem, verify non-NULL

TEST(AssetDecoders, ObjFromBuffer)
  — Load OBJ text into memory, call rt_mesh3d_from_obj_buffer, verify vertex count

TEST(AssetDecoders, StlFromBuffer)
  — Load binary STL into memory, call rt_mesh3d_from_stl_buffer, verify triangle count

TEST(AssetDecoders, NullBuffer)
  — Verify all _buffer functions return NULL for NULL data or 0 length

TEST(AssetDecoders, GarbageBuffer)
  — Verify all _buffer functions return NULL for random bytes
```

CMake registration:
```cmake
viper_add_test(test_asset_decoders
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime/TestAssetDecoders.cpp
    LIBS viper_runtime viper_test_common)
```

### 10.4 Unit Tests — Exe Directory

**File:** `src/tests/unit/runtime/TestPathExeDir.cpp`

```
TEST(PathExeDir, ReturnsNonNull)
  — Verify rt_path_exe_dir() returns non-NULL

TEST(PathExeDir, ReturnsDirectory)
  — Verify returned path is a valid directory (stat + S_ISDIR)

TEST(PathExeDir, ContainsTestBinary)
  — Verify returned directory contains the test executable itself

TEST(PathExeDir, ExeDirString)
  — Verify rt_path_exe_dir_str() returns a valid runtime string
```

CMake registration:
```cmake
viper_add_test(test_path_exe_dir
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime/TestPathExeDir.cpp
    LIBS viper_runtime viper_test_common)
```

### 10.5 Unit Tests — Asset Compiler

**File:** `src/tests/unit/tools/TestAssetCompiler.cpp`

```
TEST(AssetCompiler, EmptyProject)
  — Project with no embed/pack directives → empty blob, no .vpa files

TEST(AssetCompiler, EmbedSingleFile)
  — Create temp PNG file, set embed directive, compile → verify blob contains entry

TEST(AssetCompiler, EmbedDirectory)
  — Create temp dir with 3 files, embed dir → verify blob contains all 3

TEST(AssetCompiler, PackGroup)
  — Create temp files, set pack directive, compile → verify .vpa file created on disk

TEST(AssetCompiler, PackCompressed)
  — Same with pack-compressed → verify .vpa entries are compressed (stored_size < data_size for compressible data)

TEST(AssetCompiler, MultiplePacks)
  — Two pack groups → verify two .vpa files

TEST(AssetCompiler, MissingFile)
  — Embed non-existent file → verify error returned

TEST(AssetCompiler, MixedEmbedAndPack)
  — Both embed and pack directives → verify blob + .vpa file
```

CMake registration:
```cmake
viper_add_test(test_asset_compiler
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/tools/TestAssetCompiler.cpp
    LIBS viper_common viper_packaging viper_test_common)
```

### 10.6 Unit Tests — Project Loader Directives

**File:** `src/tests/unit/tools/TestProjectLoaderAssets.cpp`

```
TEST(ProjectLoader, ParseEmbed)
  — Manifest with "embed sprites/hero.png" → verify embedAssets[0].sourcePath

TEST(ProjectLoader, ParsePack)
  — Manifest with "pack level1 levels/" → verify packGroups[0].name and sources

TEST(ProjectLoader, ParsePackCompressed)
  — Manifest with "pack-compressed music audio/" → verify compressed flag

TEST(ProjectLoader, ParseMultiplePackSameName)
  — Two "pack textures ..." lines → verify merged into single PackGroup

TEST(ProjectLoader, ParseMixed)
  — Manifest with embed + pack + asset → verify all three populated

TEST(ProjectLoader, EmbedMissingPath)
  — "embed" with no argument → verify error diagnostic

TEST(ProjectLoader, PackMissingName)
  — "pack" with one argument → verify error diagnostic
```

CMake registration:
```cmake
viper_add_test(test_project_loader_assets
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/tools/TestProjectLoaderAssets.cpp
    LIBS viper_common viper_test_common)
```

### 10.7 Integration Tests — End-to-End

**File:** `src/tests/integration/TestAssetEmbedE2E.cpp`

```
TEST(AssetEmbedE2E, BuildWithEmbed)
  — Create project with viper.project + embed directive + test .zia that calls Assets.Load
  — Run viper build → verify binary produced
  — Run binary → verify asset loaded correctly (print dimensions or "ok")

TEST(AssetEmbedE2E, BuildWithPack)
  — Create project with pack directive
  — Run viper build → verify binary + .vpa produced
  — Run binary → verify asset loaded from .vpa

TEST(AssetEmbedE2E, BuildNoAssets)
  — Create project without embed/pack
  — Run viper build → verify binary works (no asset symbols in .rodata)
```

CMake registration:
```cmake
viper_add_test(test_asset_embed_e2e
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/TestAssetEmbedE2E.cpp
    LIBS viper_common viper_runtime viper_test_common il_build)
```

### 10.8 Zia Runtime Tests

**File:** `tests/runtime/test_asset_load.zia`

```zia
bind Viper.IO;
bind Viper.Graphics;

// Test loading from filesystem (dev mode fallback)
var pixels = Assets.Load("test_image.png");
if pixels == null {
    IO.PrintLine("RESULT: fail - could not load test_image.png");
} else {
    IO.PrintLine("RESULT: ok");
}
```

**File:** `tests/runtime/test_asset_mount.zia`

```zia
bind Viper.IO;

// Test mounting and loading from .vpa
var ok = Assets.Mount("test_pack.vpa");
if ok == 0 {
    IO.PrintLine("RESULT: fail - mount failed");
} else {
    var data = Assets.LoadBytes("test_entry.txt");
    if data == null {
        IO.PrintLine("RESULT: fail - entry not found in pack");
    } else {
        IO.PrintLine("RESULT: ok");
    }
    Assets.Unmount("test_pack.vpa");
}
```

**File:** `tests/runtime/test_asset_exists.zia`

```zia
bind Viper.IO;

// Test Assets.Exists
if Assets.Exists("nonexistent_file.xyz") != 0 {
    IO.PrintLine("RESULT: fail - false positive");
} else {
    IO.PrintLine("RESULT: ok");
}
```

### 10.9 Test Assets

**Directory:** `tests/runtime/assets/`

Create small test files:
- `test_image.png` — 2x2 RGBA PNG (~100 bytes)
- `test_sound.wav` — 1 sample mono WAV (~50 bytes)
- `test_model.stl` — 1 triangle binary STL (~134 bytes)
- `test_entry.txt` — "hello from vpa" (14 bytes)

These are used by both C++ unit tests and Zia runtime tests.

A `test_pack.vpa` is generated at test build time by a small CMake custom command
that invokes the VpaWriter via a test utility.

---

## 11. Documentation Updates

### 11.1 New documentation file

**File:** `docs/viperlib/io/assets.md`

```markdown
# Viper.IO.Assets

Asset management system for loading embedded and packed resources.

## Overview

Assets can be:
- **Embedded** in the executable (zero disk I/O, declared with `embed` in viper.project)
- **Packed** in .vpa files distributed alongside the executable (`pack` in viper.project)
- **Loose** on the filesystem (development workflow, no declaration needed)

## API

### Assets.Load(name: String) -> Object?
Load an asset by name. Returns typed object based on extension:
- .png/.jpg/.bmp/.gif → Pixels
- .wav/.ogg/.mp3 → Sound
- .obj/.stl → Mesh3D
- .gltf/.glb → GltfAsset
- .fbx → FbxAsset
- Other → Bytes

Returns null if not found.

### Assets.LoadBytes(name: String) -> Bytes?
Load raw bytes regardless of extension. Returns null if not found.

### Assets.Exists(name: String) -> Integer
Returns 1 if asset exists (embedded, in pack, or on disk), 0 otherwise.

### Assets.Size(name: String) -> Integer
Returns asset size in bytes, or 0 if not found.

### Assets.List() -> seq<String>
Returns names of all available assets (embedded + all mounted packs).

### Assets.Mount(path: String) -> Integer
Mount a .vpa pack file. Returns 1 on success, 0 on failure.
Pack files next to the executable are auto-mounted at startup.

### Assets.Unmount(path: String) -> Integer
Unmount a previously mounted pack. Returns 1 on success, 0 on failure.

### Path.ExeDir() -> String
Returns the directory containing the running executable.

## Resolution Order
1. Embedded registry (in .rodata)
2. Mounted packs (last mounted first)
3. Filesystem (CWD-relative)

## viper.project Directives
- `embed <path>` — embed file or directory into executable
- `pack <name> <path>` — add file/directory to named .vpa pack
- `pack-compressed <name> <path>` — same with DEFLATE compression
```

### 11.2 Existing docs to update

**File:** `docs/architecture.md`

Add a section under the compilation pipeline describing the asset embedding stage:

```markdown
### Asset Embedding

When a project declares `embed` or `pack` directives in `viper.project`, the build
pipeline compiles assets into VPA (Viper Pack Archive) format:

- `embed` assets are baked into the executable's `.rodata` section
- `pack` assets are written to separate `.vpa` files

The runtime asset manager (`Viper.IO.Assets`) transparently loads from embedded data,
mounted packs, or the filesystem.
```

**File:** `docs/viperlib/graphics/pixels.md`

Add note about `Assets.Load()` as the preferred way to load images in packaged apps:

```markdown
### Loading via Asset System
For packaged applications, use `Assets.Load("path.png")` instead of `Pixels.Load("path.png")`.
The asset system resolves embedded and packed assets automatically.
```

**File:** `CLAUDE.md`

Add under "Runtime Completeness" or similar section:

```markdown
## Asset System
- `embed` / `pack` / `pack-compressed` directives in viper.project
- VPA format: header + data + TOC, optional DEFLATE
- Runtime: `Viper.IO.Assets.Load/LoadBytes/Exists/Mount/Unmount`
- Resolution order: embedded → mounted packs → filesystem
- `Path.ExeDir()` for exe directory detection
```

---

## 12. Complete File Inventory

### New Files (16 files, ~2400 LOC)

| File | LOC | Purpose |
|------|-----|---------|
| `src/tools/common/asset/AssetCompiler.hpp` | ~60 | Asset compilation interface |
| `src/tools/common/asset/AssetCompiler.cpp` | ~300 | Build-time asset processing |
| `src/tools/common/asset/VpaWriter.hpp` | ~50 | VPA format writer interface |
| `src/tools/common/asset/VpaWriter.cpp` | ~250 | VPA serialization + optional DEFLATE |
| `src/runtime/io/rt_asset.h` | ~80 | Runtime asset manager header |
| `src/runtime/io/rt_asset.c` | ~500 | Asset manager (registry, mount, dispatch) |
| `src/runtime/io/rt_vpa_reader.h` | ~50 | VPA reader header |
| `src/runtime/io/rt_vpa_reader.c` | ~300 | VPA parser (memory + file) |
| `src/runtime/io/rt_path_exe.c` | ~120 | Platform exe dir detection |
| `docs/viperlib/io/assets.md` | ~80 | API documentation |
| `src/tests/unit/runtime/TestVpaFormat.cpp` | ~200 | VPA format tests |
| `src/tests/unit/runtime/TestAssetManager.cpp` | ~250 | Asset manager tests |
| `src/tests/unit/runtime/TestAssetDecoders.cpp` | ~200 | From-buffer decoder tests |
| `src/tests/unit/runtime/TestPathExeDir.cpp` | ~80 | Exe dir tests |
| `src/tests/unit/tools/TestAssetCompiler.cpp` | ~200 | Asset compiler tests |
| `src/tests/unit/tools/TestProjectLoaderAssets.cpp` | ~150 | Directive parsing tests |

### Modified Files (25+ files)

| File | Change |
|------|--------|
| `src/tools/common/project_loader.hpp` | Add EmbedEntry, PackGroup, fields to ProjectConfig |
| `src/tools/common/project_loader.cpp` | Parse embed/pack/pack-compressed directives |
| `src/tools/viper/cmd_run.cpp` | Call AssetCompiler, pass blob to codegen |
| `src/tools/common/native_compiler.hpp` | Add assetBlobPath param + generateTempAssetPath |
| `src/tools/common/native_compiler.cpp` | Pass blob to both backends |
| `src/codegen/x86_64/CodegenPipeline.hpp` | Add asset_blob_path to Options |
| `src/codegen/x86_64/CodegenPipeline.cpp` | Read blob, pass to backend |
| `src/codegen/x86_64/Backend.cpp` | Emit blob + size symbols into .rodata |
| `src/codegen/x86_64/Backend.hpp` | Update emitModuleToBinary signature |
| `src/tools/viper/cmd_codegen_arm64.cpp` | Parse --asset-blob flag |
| `src/codegen/aarch64/passes/BinaryEmitPass.cpp` | Emit blob + size into rodata |
| `src/il/runtime/runtime.def` | Add Assets class + functions + Path.ExeDir |
| `src/il/runtime/RuntimeSignatures.cpp` | Add signature entries |
| `src/il/runtime/RuntimeClasses.hpp` | Add Assets class |
| `src/runtime/CMakeLists.txt` | Add rt_asset.c, rt_vpa_reader.c, rt_path_exe.c |
| `src/tools/common/CMakeLists.txt` | Add asset/ sources |
| `src/runtime/graphics/rt_graphics_stubs.c` | Add stubs for asset functions |
| `src/runtime/graphics/rt_pixels_io.c` | Extract _buffer variants for PNG/JPEG/BMP |
| `src/runtime/graphics/rt_gif.c` | Extract _buffer variant |
| `src/runtime/audio/rt_audio.c` | Expose rt_sound_load_mem via runtime.def wrapper |
| `src/runtime/graphics/rt_fbx_loader.c` | Extract _buffer variant |
| `src/runtime/graphics/rt_gltf_loader.c` | Extract _buffer variant |
| `src/runtime/graphics/rt_obj_loader.c` | Extract _buffer variant |
| `src/runtime/graphics/rt_stl_loader.c` | Extract _buffer variant |
| `src/runtime/graphics/rt_bitmapfont.c` | Extract _buffer variants for BDF/PSF |
| `src/runtime/graphics/rt_tilemap_io.c` | Extract _buffer variant |
| `src/runtime/graphics/rt_videoplayer.c` | Add vpa_io_t abstraction + mem variant |
| `src/runtime/audio/rt_ogg.c` | Add _from_buffer for OGG reader |
| `src/runtime/audio/rt_mp3.c` | Add _from_buffer for MP3 stream |
| `src/tools/viper/cmd_package.cpp` | Copy .vpa files into installer |
| `src/tools/common/packaging/DesktopEntryGenerator.cpp` | Add Path= directive |
| `src/tests/CMakeLists.txt` | Register all new tests |
| `docs/architecture.md` | Add asset embedding section |
| `docs/viperlib/graphics/pixels.md` | Add Assets.Load note |

---

## 13. Implementation Phases

### Phase 1: Foundation (VPA Format + Project Directives)
- [ ] VpaWriter.hpp/cpp — write VPA format
- [ ] rt_vpa_reader.h/c — read VPA format (memory + file)
- [ ] project_loader.hpp/cpp — parse embed/pack/pack-compressed
- [ ] TestVpaFormat.cpp — round-trip tests
- [ ] TestProjectLoaderAssets.cpp — directive parsing tests

### Phase 2: Asset Compiler + Build Pipeline
- [ ] AssetCompiler.hpp/cpp — resolve paths, build blobs, write packs
- [ ] cmd_run.cpp — integrate asset compilation into build
- [ ] native_compiler.hpp/cpp — pass blob path to codegen
- [ ] generateTempAssetPath utility
- [ ] TestAssetCompiler.cpp — compiler tests

### Phase 3: Codegen Emission
- [ ] CodegenPipeline.hpp — add asset_blob_path option
- [ ] CodegenPipeline.cpp — read blob file
- [ ] Backend.cpp (x86_64) — emit blob + size symbols into .rodata
- [ ] cmd_codegen_arm64.cpp — parse --asset-blob flag
- [ ] BinaryEmitPass.cpp (aarch64) — emit blob + size into rodata

### Phase 4: Runtime Asset Manager
- [ ] rt_path_exe.c — platform exe dir detection (macOS/Windows/Linux)
- [ ] rt_asset.h/c — asset manager (init, find, mount, unmount, dispatch)
- [ ] runtime.def + RuntimeSignatures + RuntimeClasses — register API
- [ ] rt_graphics_stubs.c — stub functions
- [ ] CMakeLists.txt updates (runtime + tools)
- [ ] TestAssetManager.cpp — manager tests
- [ ] TestPathExeDir.cpp — exe dir tests

### Phase 5: From-Buffer Decoders
- [ ] rt_pixels_io.c — PNG/JPEG/BMP _buffer variants
- [ ] rt_gif.c — GIF _buffer variant
- [ ] rt_audio.c — expose rt_sound_load_mem, add music mem variant
- [ ] rt_obj_loader.c — OBJ _buffer variant
- [ ] rt_stl_loader.c — STL _buffer variant
- [ ] rt_fbx_loader.c — FBX _buffer variant
- [ ] rt_gltf_loader.c — glTF _buffer variant
- [ ] rt_bitmapfont.c — BDF/PSF _buffer variants
- [ ] rt_tilemap_io.c — tilemap _buffer variant
- [ ] rt_videoplayer.c — vpa_io_t abstraction + mem variant
- [ ] rt_ogg.c / rt_mp3.c — streaming from buffer
- [ ] TestAssetDecoders.cpp — decoder tests

### Phase 6: Packaging + Integration
- [ ] cmd_package.cpp — copy .vpa files into installer
- [ ] DesktopEntryGenerator.cpp — add Path= to .desktop
- [ ] TestAssetEmbedE2E.cpp — end-to-end tests
- [ ] Zia runtime tests (test_asset_load.zia, test_asset_mount.zia, etc.)
- [ ] Test assets (tiny PNG, WAV, STL, TXT)

### Phase 7: Documentation
- [ ] docs/viperlib/io/assets.md — new API docs
- [ ] docs/architecture.md — add asset section
- [ ] docs/viperlib/graphics/pixels.md — add Assets.Load note

---

## 14. Verification

### Build verification
```bash
./scripts/build_viper.sh
```
All existing tests must continue to pass (no regressions).

### Asset-specific test verification
```bash
ctest --test-dir build --output-on-failure -R "test_vpa|test_asset|test_path_exe"
```

### Manual end-to-end verification
1. Create a test project with `embed` and `pack` directives
2. Run `viper build` → verify binary and .vpa files produced
3. Run the binary from a different directory → verify assets load
4. Package with `viper package` → verify installer contains .vpa files
5. Install and run → verify all assets accessible
