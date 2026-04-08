# Phase 0+1: Runtime Library Discovery and Install Manifest

## Phase 0: Runtime Library Discovery (Critical Prerequisite)

### Problem

`findBuildDir()` in `src/codegen/common/LinkerSupport.cpp:57` walks up directories looking for `CMakeCache.txt`. After installation, there is no CMakeCache.txt. `viper build` will fail to find runtime `.a` archives and cannot produce native executables.

### Solution

Modify `findBuildDir()` → `findLibDir()` with a layered search strategy:

```cpp
std::optional<std::filesystem::path> findLibDir() {
    // 1. Explicit override via environment variable
    if (const char* env = std::getenv("VIPER_LIB_PATH"))
        if (fileExists(std::filesystem::path(env) / "libviper_rt_base.a"))
            return std::filesystem::path(env);

    // 2. Relative to executable: <exe_dir>/../lib/ (installed layout)
    auto exeDir = getExecutableDir(); // use existing rt_path_exe logic
    if (exeDir) {
        auto libDir = *exeDir / ".." / "lib";
        if (fileExists(libDir / "libviper_rt_base.a"))
            return std::filesystem::canonical(libDir);
    }

    // 3. Platform standard install paths
    static const char* kSearchPaths[] = {
#if defined(__APPLE__)
        "/usr/local/viper/lib",
#elif defined(__linux__)
        "/usr/lib/viper",
        "/usr/local/lib/viper",
#elif defined(_WIN32)
        // Windows uses registry or relative-to-exe
#endif
        nullptr
    };
    for (const char** p = kSearchPaths; *p; ++p)
        if (fileExists(std::filesystem::path(*p) / "libviper_rt_base.a"))
            return std::filesystem::path(*p);

    // 4. Fallback: existing build directory discovery (development mode)
    return findBuildDir();
}
```

Also update `runtimeArchivePath()` and `appendSystemLinkInputs()` to use the new lib dir instead of build dir layout when no CMakeCache.txt is present. The key difference: installed layout is flat (`lib/libviper_rt_base.a`) while build layout is nested (`build/src/runtime/libviper_rt_base.a`).

Similarly update `appendSystemLinkInputs()` for graphics/audio/GUI library discovery:
- Installed: `lib/libvipergfx.a`, `lib/libviperaud.a`, `lib/libvipergui.a`
- Build: `build/lib/libvipergfx.a`, `build/src/lib/gui/libvipergui.a`

### Modified Files

- `src/codegen/common/LinkerSupport.cpp` — rewrite `findBuildDir()` to `findLibDir()` with installed-path awareness
- `src/codegen/common/LinkerSupport.hpp` — update declaration

### Testing

- Unit test: Create mock installed layout in temp dir, set `VIPER_LIB_PATH`, verify discovery
- Unit test: Create mock exe-relative layout, verify `../lib/` search
- Integration test: After `cmake --install`, verify `viper build` finds libraries

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
    Extra,          // VS Code extension, examples
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

    std::string licenseText;  // full LICENSE file content (for embedding in installers)

    // Flat list of all entries for iteration
    std::vector<InstallFileEntry> allFiles() const;

    // Total install size in bytes
    uint64_t totalSizeBytes() const;

    // Total install size in KiB (for .pkg installKBytes, .deb Installed-Size)
    uint64_t totalSizeKiB() const;

    // File count
    size_t totalFileCount() const;

    // File associations to register
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
/// These return paths relative to the install root (no leading slash).
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
1. Read version from `sourceRoot/src/buildmeta/VERSION`
2. Walk `installPrefix/bin/` — collect viper, zia, vbasic, ilrun, il-verify, il-dis, zia-server, vbasic-server (with .exe suffix on Windows). Record file sizes.
3. Walk `installPrefix/lib/` — collect `libviper_*.a` (or `.lib` on Windows), `libvipergfx.a`, `libviperaud.a`, `libvipergui.a`
4. Walk `installPrefix/include/viper/` — collect all .hpp headers recursively
5. Walk `sourceRoot/docs/man/` — collect man pages (man1/*.1, man7/*.7)
6. Walk `installPrefix/lib/cmake/Viper/` — collect CMake config files
7. Collect `sourceRoot/LICENSE` — read full text into `licenseText`
8. Optionally collect `sourceRoot/misc/editors/vscode/zia/zia-language-*.vsix`
9. Populate `fileAssociations` with .zia, .bas, .il entries

Path mapping functions apply the platform table from the overview. Key rules:
- Windows: backslash separators, `.exe` suffix on binaries, no man pages
- macOS: everything under `lib/viper/` (installed to `/usr/local/viper/`)
- Linux: FHS standard (`usr/bin/`, `usr/lib/viper/`, `usr/share/man/`, etc.)

### Modified Files

- `src/CMakeLists.txt` — add PlatformInstallConfig.cpp to `viper_packaging` target (after line ~291)

### Testing

Add to `src/tests/unit/test_packaging.cpp`:
- `TEST(PlatformInstallConfig, GatherMockTree)` — create temp install tree, verify manifest completeness
- `TEST(PlatformInstallConfig, WindowsPathMapping)` — verify bin/viper → bin\viper.exe
- `TEST(PlatformInstallConfig, MacOSPathMapping)` — verify bin/viper → lib/viper/bin/viper
- `TEST(PlatformInstallConfig, LinuxPathMapping)` — verify bin/viper → usr/bin/viper
- `TEST(PlatformInstallConfig, TotalSizeBytes)` — verify sum across categories
- `TEST(PlatformInstallConfig, FileAssociations)` — verify .zia/.bas/.il entries populated
- `TEST(PlatformInstallConfig, VersionParsing)` — verify version extraction from buildmeta/VERSION
