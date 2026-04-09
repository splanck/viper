# Phase 0+1: Runtime Library Discovery and Install Manifest

## Phase 0: Prerequisites

### 0A: Fix Man Page Install Path

The root `CMakeLists.txt` install commands (lines 588-594) reference `${CMAKE_SOURCE_DIR}/man/` but the actual man pages live at `${CMAKE_SOURCE_DIR}/docs/man/`. Fix the install() commands to use the correct path. Without this, `cmake --install` silently skips man pages.

**Files**: `CMakeLists.txt` — change `man/man1` to `docs/man/man1` and `man/man7` to `docs/man/man7` in the install() calls.

**Man pages available** (9 total):
- `docs/man/man1/`: viper.1, zia.1, vbasic.1, ilrun.1, il-verify.1, il-dis.1, basic-ast-dump.1, basic-lex-dump.1
- `docs/man/man7/`: viper.7

### 0B: Runtime Library Discovery (Critical)

#### Problem

`findBuildDir()` in `src/codegen/common/LinkerSupport.cpp:59` walks up directories looking for `CMakeCache.txt`. After installation, there is no CMakeCache.txt. **No installed-path search logic exists.** `viper build` will fail to find runtime `.a`/`.lib` archives and cannot produce native executables.

#### Solution

Add `findInstalledLibDir()` as a new search path tried BEFORE `findBuildDir()`. The existing build-dir search remains as the final fallback for development mode.

```cpp
/// Search for Viper runtime libraries in an installed layout.
/// Returns the lib directory if found, or nullopt.
static std::optional<std::filesystem::path> findInstalledLibDir() {
    // 1. Explicit override via environment variable
    if (const char* env = std::getenv("VIPER_LIB_PATH")) {
        std::filesystem::path p(env);
        if (fileExists(p / kProbeLibName))
            return p;
    }

    // 2. Relative to executable: <exe_dir>/../lib/
    //    This is the standard FHS-like layout for installed tools.
    //    Uses the same exe-dir logic as rt_path_exe.c (Path.ExeDir).
    auto exeDir = getExecutableDir();
    if (exeDir) {
        std::error_code ec;
        auto libDir = std::filesystem::canonical(*exeDir / ".." / "lib", ec);
        if (!ec && fileExists(libDir / kProbeLibName))
            return libDir;
    }

    // 3. Platform standard install paths
#if RT_PLATFORM_MACOS
    static const char* kPaths[] = {"/usr/local/viper/lib", nullptr};
#elif RT_PLATFORM_LINUX
    static const char* kPaths[] = {"/usr/lib/viper", "/usr/local/lib/viper", nullptr};
#elif RT_PLATFORM_WINDOWS
    // Windows relies on exe-relative or VIPER_LIB_PATH; no fixed system path.
    static const char* kPaths[] = {nullptr};
#else
    static const char* kPaths[] = {nullptr};
#endif
    for (const char** p = kPaths; *p; ++p) {
        std::filesystem::path dir(*p);
        if (fileExists(dir / kProbeLibName))
            return dir;
    }

    return std::nullopt;
}
```

Where `kProbeLibName` is:
```cpp
#if RT_PLATFORM_WINDOWS
static constexpr const char* kProbeLibName = "viper_rt_base.lib";
#else
static constexpr const char* kProbeLibName = "libviper_rt_base.a";
#endif
```

The `getExecutableDir()` function already exists conceptually in `rt_path_exe.c` — port the logic to C++ or call through `std::filesystem`.

#### Integration with existing code

`runtimeArchivePath()` (line 148) currently takes a `buildDir` and constructs nested build paths (`buildDir/src/runtime/libfoo.a`). For installed layout, the path is flat: `libDir/libfoo.a`. Modify to:

```cpp
std::filesystem::path runtimeArchivePath(const std::filesystem::path &libOrBuildDir,
                                         std::string_view libBaseName,
                                         bool installedLayout) {
    if (installedLayout) {
        // Flat layout: lib/libviper_rt_base.a
#if RT_PLATFORM_WINDOWS
        return libOrBuildDir / (std::string(libBaseName) + ".lib");
#else
        return libOrBuildDir / ("lib" + std::string(libBaseName) + ".a");
#endif
    }
    // Existing build-dir nested layout...
}
```

Similarly update `appendSystemLinkInputs()` for graphics/audio/GUI libraries:
- Installed: `lib/libvipergfx.a` (flat, same dir)
- Build: `build/lib/libvipergfx.a` or `build/src/lib/gui/libvipergui.a` (nested)

#### Modified Files

- `src/codegen/common/LinkerSupport.cpp` — add `findInstalledLibDir()`, modify `runtimeArchivePath()` and `appendSystemLinkInputs()` for dual layout
- `src/codegen/common/LinkerSupport.hpp` — update declarations
- `CMakeLists.txt` — fix man page install path

#### Testing

- Unit test: Create mock installed layout in temp dir (`lib/libviper_rt_base.a`), set `VIPER_LIB_PATH`, verify `findInstalledLibDir()` returns correct path
- Unit test: Create mock exe-relative layout, verify `../lib/` search works
- Unit test: Verify `runtimeArchivePath()` returns flat path when `installedLayout=true` and nested path when `false`
- Integration test: After `cmake --install`, verify `viper build` finds libraries and produces a working native executable

---

## Phase 1: Install Manifest and Path Mapping

### Goal

Create a data model that represents all files in a Viper platform installation, with platform-specific path mapping. This is the foundation every platform builder consumes.

### New Files

#### `src/tools/common/packaging/PlatformInstallConfig.hpp`

```cpp
namespace viper::pkg {

struct InstallFileEntry {
    std::string sourcePath;   // absolute path on build host
    std::string relativePath; // normalized relative path (forward slashes)
    uint64_t fileSize;        // size in bytes (for progress bars, disk space checks)
    uint32_t unixMode;        // 0755 for executables, 0644 for data
    bool isExecutable;
    bool isSymlink;           // for macOS /usr/local/bin symlinks
    std::string symlinkTarget;
};

enum class InstallCategory {
    Binary,
    Library,
    Header,
    ManPage,
    CMakeConfig,
    Doc,
    Extra,          // VS Code extension
};

struct ViperInstallManifest {
    std::string version;      // from VIPER_VERSION_STR (e.g. "0.2.4")
    std::string versionFull;  // from buildmeta/VERSION (e.g. "0.2.4-snapshot")
    std::string arch;         // "x64" or "arm64"

    std::vector<InstallFileEntry> binaries;   // CLI tools
    std::vector<InstallFileEntry> libraries;  // static .a/.lib archives
    std::vector<InstallFileEntry> headers;    // C++ API headers
    std::vector<InstallFileEntry> manPages;   // man1/*.1, man7/*.7
    std::vector<InstallFileEntry> cmakeFiles; // ViperConfig.cmake etc.
    std::vector<InstallFileEntry> docs;       // LICENSE, README
    std::vector<InstallFileEntry> extras;     // VS Code extension

    std::string licenseText;  // full LICENSE file content (for embedding)

    std::vector<InstallFileEntry> allFiles() const;
    uint64_t totalSizeBytes() const;
    uint64_t totalSizeKiB() const;   // for .pkg installKBytes, .deb Installed-Size
    size_t totalFileCount() const;

    struct FileAssociation {
        std::string extension;    // ".zia"
        std::string description;  // "Zia Source File"
        std::string mimeType;     // "text/x-zia"
        std::string openWith;     // "zia" (binary name)
    };
    std::vector<FileAssociation> fileAssociations;
};

/// Scan a cmake --install prefix and gather all Viper files.
/// @param installPrefix  Path to staged cmake install (e.g. build/install_staging)
/// @param sourceRoot     Path to Viper source tree (for LICENSE, man pages, vsix)
/// @param arch           "x64" or "arm64"
ViperInstallManifest gatherViperInstallTree(
    const std::string& installPrefix,
    const std::string& sourceRoot,
    const std::string& arch);

/// Map a manifest entry to its platform-specific install path.
/// Returns paths relative to the install root (no leading slash).
std::string windowsInstallPath(const InstallFileEntry& entry);
std::string macosInstallPath(const InstallFileEntry& entry);
std::string linuxInstallPath(const InstallFileEntry& entry);

/// Default install root per platform.
std::string windowsDefaultInstallDir();  // "C:\\Program Files\\Viper"
std::string macosInstallRoot();          // "/usr/local/viper"
std::string linuxInstallRoot();          // "" (FHS paths are absolute)

} // namespace viper::pkg
```

#### `src/tools/common/packaging/PlatformInstallConfig.cpp` (~300 LOC)

`gatherViperInstallTree()` implementation:
1. Read version string from `sourceRoot/src/buildmeta/VERSION`, strip trailing whitespace/newlines
2. Walk `installPrefix/bin/` — collect the 8 CLI tools. On Windows, look for `.exe` suffix. Record file sizes via `std::filesystem::file_size()`.
3. Walk `installPrefix/lib/` — collect library archives:
   - macOS/Linux: `libviper_*.a`, `libvipergfx.a`, `libviperaud.a`, `libvipergui.a`
   - Windows: `viper_*.lib`, `vipergfx.lib`, `viperaud.lib`, `vipergui.lib`
4. Walk `installPrefix/include/viper/` — collect all .hpp/.h headers recursively, preserving subdirectory structure
5. Walk `sourceRoot/docs/man/` — collect man pages (man1/*.1, man7/*.7). Note: these come from the SOURCE tree, not the install prefix, because the CMake install path is being fixed in Phase 0A.
6. Walk `installPrefix/lib/cmake/Viper/` — collect CMake config files
7. Collect `sourceRoot/LICENSE` — read full text into `licenseText`
8. Optionally collect `sourceRoot/misc/editors/vscode/zia/zia-language-*.vsix` (currently `zia-language-0.1.0.vsix`)
9. Populate `fileAssociations` with .zia, .bas, .il entries

Path mapping rules:
- `windowsInstallPath()`: backslash separators, `.exe` suffix on binaries, `include\viper\` preserves subdirs, no man pages
- `macosInstallPath()`: everything relative to `/usr/local/viper/` — `bin/`, `lib/`, `include/viper/`, man pages under `share/man/`
- `linuxInstallPath()`: FHS standard — `usr/bin/`, `usr/lib/viper/`, `usr/include/viper/`, `usr/share/man/man1/`, `usr/share/doc/viper/`

### Modified Files

- `src/CMakeLists.txt` — add PlatformInstallConfig.cpp to `viper_packaging` target

### Testing

Add to `src/tests/unit/test_packaging.cpp`:
- `TEST(PlatformInstallConfig, GatherMockTree)` — create temp dir with mock bin/viper, lib/libviper_rt_base.a, include/viper/version.hpp, verify manifest completeness
- `TEST(PlatformInstallConfig, WindowsPathMapping)` — verify bin/viper → `bin\viper.exe`, lib/libviper_rt_base.a → `lib\viper_rt_base.lib`
- `TEST(PlatformInstallConfig, MacOSPathMapping)` — verify bin/viper → `bin/viper`, docs/man/man1/viper.1 → `share/man/man1/viper.1`
- `TEST(PlatformInstallConfig, LinuxPathMapping)` — verify bin/viper → `usr/bin/viper`, lib/libviper_rt_base.a → `usr/lib/viper/libviper_rt_base.a`
- `TEST(PlatformInstallConfig, TotalSizeBytes)` — verify sum across all categories
- `TEST(PlatformInstallConfig, FileAssociations)` — verify .zia/.bas/.il entries populated
- `TEST(PlatformInstallConfig, VersionParsing)` — verify version extraction from mock VERSION file containing "0.2.4-snapshot\n"
- `TEST(PlatformInstallConfig, EmptyPrefixReportsError)` — verify error when install prefix doesn't contain expected binaries
