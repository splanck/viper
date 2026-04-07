# Phase 1: Install Manifest and Path Mapping

## Goal

Create a data model that represents all files in a Viper platform installation, with platform-specific path mapping. This is the foundation every platform builder consumes.

## New Files

### `src/tools/common/packaging/PlatformInstallConfig.hpp`

```cpp
namespace viper::pkg {

struct InstallFileEntry {
    std::string sourcePath;   // absolute path on build host
    std::string relativePath; // normalized relative path (forward slashes)
    uint32_t unixMode;        // 0755 for executables, 0644 for data
    bool isExecutable;
};

struct ViperInstallManifest {
    std::string version;      // from VIPER_VERSION_STR
    std::string arch;         // "x64" or "arm64"

    std::vector<InstallFileEntry> binaries;   // CLI tools
    std::vector<InstallFileEntry> libraries;  // static .a/.lib archives
    std::vector<InstallFileEntry> headers;    // C++ API headers
    std::vector<InstallFileEntry> manPages;   // man1/*.1, man7/*.7
    std::vector<InstallFileEntry> cmakeFiles; // ViperConfig.cmake etc.
    std::vector<InstallFileEntry> docs;       // LICENSE, README
    std::vector<InstallFileEntry> extras;     // VS Code extension, examples

    std::string licensePath;  // absolute path to LICENSE file

    // Flat list of all entries for iteration
    std::vector<InstallFileEntry> allFiles() const;

    // Total install size in bytes
    uint64_t totalSizeBytes() const;
};

/// Scan a cmake --install prefix and gather all Viper files.
ViperInstallManifest gatherViperInstallTree(
    const std::string& installPrefix,
    const std::string& sourceRoot);

/// Map a manifest entry to its platform-specific install path.
std::string windowsInstallPath(const InstallFileEntry& entry);
std::string macosInstallPath(const InstallFileEntry& entry);
std::string linuxInstallPath(const InstallFileEntry& entry);

} // namespace viper::pkg
```

### `src/tools/common/packaging/PlatformInstallConfig.cpp` (~250 LOC)

`gatherViperInstallTree()` implementation:
1. Walk `installPrefix/bin/` — collect viper, zia, vbasic, ilrun, il-verify, il-dis, zia-server, vbasic-server (with .exe suffix on Windows)
2. Walk `installPrefix/lib/` — collect libviper_*.a, libvipergfx.a, libviperaud.a
3. Walk `installPrefix/include/viper/` — collect all .hpp headers
4. Walk `sourceRoot/docs/man/` — collect man pages
5. Walk `installPrefix/lib/cmake/Viper/` — collect CMake config
6. Collect `sourceRoot/LICENSE`
7. Optionally collect `sourceRoot/misc/editors/vscode/zia/zia-language-*.vsix`

Path mapping functions apply the platform table from the overview.

## Modified Files

- `src/CMakeLists.txt` — add PlatformInstallConfig.cpp to `viper_packaging` target

## Testing

Unit test `test_platform_install_config.cpp`:
- Create a mock install tree in a temp directory
- Verify `gatherViperInstallTree` finds all categories
- Verify path mapping for all three platforms
- Verify `allFiles()` includes everything
- Verify `totalSizeBytes()` sums correctly
