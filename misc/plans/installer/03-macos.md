# Phase 3: macOS .pkg Installer

## Goal

A standard macOS flat package (.pkg) that opens in Apple's Installer.app when double-clicked. User sees the familiar macOS install experience: introduction → license → install location → install → summary. This is how Go, Node.js, Rust, and Xcode CLT distribute on macOS.

## Architecture

A flat .pkg is a **xar archive** containing:
```
Distribution                  ← XML: installer UI definition
viper.pkg/                    ← component package
  PackageInfo                 ← XML: package metadata
  Payload                     ← gzip(cpio) of installed files
  Scripts/                    ← shell scripts
    postinstall               ← creates /usr/local/bin symlinks
```

This requires three new format writers: CpioWriter, XarWriter, PkgZlib. Plus SHA-1 for xar checksums.

## New Files

### `src/tools/common/packaging/CpioWriter.hpp/cpp` (~200 LOC)

SVR4/newc (ASCII) cpio format. Shared by macOS .pkg and Linux .rpm.

```cpp
class CpioWriter {
public:
    void addFile(const std::string& path, const uint8_t* data, size_t len,
                 uint32_t mode = 0100644);
    void addDirectory(const std::string& path, uint32_t mode = 040755);
    void addSymlink(const std::string& path, const std::string& target);
    std::vector<uint8_t> finish(); // appends TRAILER!!! sentinel
private:
    std::vector<uint8_t> buffer_;
    uint32_t nextInode_{1};
};
```

Entry format (110-byte ASCII header):
```
070701           ← magic (6 chars)
%08X             ← inode
%08X             ← mode (file type + permissions)
%08X             ← uid (0)
%08X             ← gid (0)
%08X             ← nlink (1 for files, 2 for dirs)
%08X             ← mtime
%08X             ← filesize (0 for dirs/symlinks-with-no-data)
%08X             ← devmajor (0)
%08X             ← devminor (0)
%08X             ← rdevmajor (0)
%08X             ← rdevminor (0)
%08X             ← namesize (including NUL)
%08X             ← check (0)
```
Followed by: filename (NUL-terminated, padded to 4-byte boundary), file data (padded to 4-byte boundary). Archive ends with entry named `TRAILER!!!`.

### `src/tools/common/packaging/PkgZlib.hpp/cpp` (~80 LOC)

Zlib-wrapped DEFLATE (RFC 1950). xar uses zlib compression, not raw DEFLATE.

```cpp
std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t len, int level = 6);
std::vector<uint8_t> zlibDecompress(const uint8_t* data, size_t len);
uint32_t adler32(const uint8_t* data, size_t len);
```

Format: 2-byte header (0x78 0x9C for default level) + raw DEFLATE data + 4-byte big-endian Adler-32 checksum. Wraps the existing `PkgDeflate::deflate()`.

Adler-32 is trivial: two 16-bit running sums (s1 starts at 1, s2 starts at 0), updated per byte with modulo 65521.

### `src/tools/common/packaging/PkgSHA1.hpp/cpp` (~150 LOC)

Self-contained SHA-1 for xar checksums. Port from `src/runtime/text/rt_hash.c` (line 266+), removing runtime string dependencies. Same approach as the existing `PkgMD5`.

```cpp
void sha1(const uint8_t* data, size_t len, uint8_t digest[20]);
std::string sha1Hex(const uint8_t* data, size_t len);
```

### `src/tools/common/packaging/XarWriter.hpp/cpp` (~350 LOC)

Apple xar archive format writer.

```cpp
class XarWriter {
public:
    void addFile(const std::string& path, const uint8_t* data, size_t len);
    void addDirectory(const std::string& path);
    bool write(const std::string& outputPath, std::ostream& err);
private:
    struct Entry { std::string path; std::vector<uint8_t> data; bool isDir; };
    std::vector<Entry> entries_;
};
```

Binary format:
```
[Header - 28 bytes]
  magic:     0x78617221  ("xar!")
  size:      28          (header size, always 28)
  version:   1
  toc_compressed_size:   N
  toc_uncompressed_size: M
  cksum_algo: 1          (SHA-1)

[TOC - N bytes]
  zlib-compressed XML describing all entries with heap offsets

[Heap]
  SHA-1 checksum of TOC (20 bytes, at offset 0)
  Concatenated zlib-compressed file data blobs
```

The TOC XML structure:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<xar>
  <toc>
    <creation-time>2026-04-07T00:00:00Z</creation-time>
    <checksum style="sha1">
      <size>20</size>
      <offset>0</offset>
    </checksum>
    <file id="1">
      <name>Distribution</name>
      <type>file</type>
      <data>
        <length>...</length>        <!-- compressed size -->
        <offset>20</offset>         <!-- heap offset after checksum -->
        <size>...</size>            <!-- uncompressed size -->
        <encoding style="application/x-gzip"/>
        <archived-checksum style="sha1">HEX</archived-checksum>
        <extracted-checksum style="sha1">HEX</extracted-checksum>
      </data>
    </file>
    <file id="2">
      <name>viper.pkg</name>
      <type>directory</type>
      <file id="3"><name>PackageInfo</name>...</file>
      <file id="4"><name>Payload</name>...</file>
      <file id="5">
        <name>Scripts</name>
        <type>directory</type>
        <file id="6"><name>postinstall</name>...</file>
      </file>
    </file>
  </toc>
</xar>
```

### `src/tools/common/packaging/MacOSPkgBuilder.hpp/cpp` (~350 LOC)

Builds the complete .pkg.

```cpp
struct MacOSPkgBuildParams {
    ViperInstallManifest manifest;
    std::string outputPath;       // e.g. "viper-0.2.4-macos-arm64.pkg"
    std::string identifier;       // "com.viper-lang.viper"
    std::string title;            // "Viper Compiler Toolchain"
    std::string minOSVersion;     // "11.0"
};

void buildMacOSPkg(const MacOSPkgBuildParams& params);
```

Build flow:
1. Map manifest entries to macOS paths under `/usr/local/viper/`
2. Build cpio payload using `CpioWriter` — all files under their install paths
3. Gzip the cpio bytes using `PkgGzip::gzip()`
4. Generate `PackageInfo` XML:
   ```xml
   <pkg-info format-version="2" identifier="com.viper-lang.viper"
             version="0.2.4" install-location="/" auth="root">
       <payload numberOfFiles="N" installKBytes="K"/>
       <scripts><postinstall file="./postinstall"/></scripts>
   </pkg-info>
   ```
5. Generate `postinstall` script:
   ```bash
   #!/bin/bash
   mkdir -p /usr/local/bin
   for tool in viper zia vbasic ilrun il-verify il-dis zia-server vbasic-server; do
       ln -sf /usr/local/viper/bin/$tool /usr/local/bin/$tool
   done
   ```
6. Generate `Distribution` XML with license, title, volume check (min macOS 11.0), choices, pkg-ref
7. Read LICENSE text, embed in Distribution as `<license>` element
8. Pack everything into a xar archive using `XarWriter`:
   - `Distribution` (file)
   - `viper.pkg/PackageInfo` (file)
   - `viper.pkg/Payload` (file — the gzipped cpio)
   - `viper.pkg/Scripts/postinstall` (file)
9. Write to output path

## Modified Files

- `src/CMakeLists.txt` — add CpioWriter.cpp, XarWriter.cpp, PkgZlib.cpp, PkgSHA1.cpp, MacOSPkgBuilder.cpp to `viper_packaging`

## Testing

- `CpioWriter`: Write a 3-file archive, verify "070701" magic on each entry, verify TRAILER!!! sentinel, verify 4-byte padding
- `XarWriter`: Write a 2-file archive, verify magic 0x78617221, decompress TOC, verify valid XML, verify heap blob matches
- `PkgZlib`: Round-trip compress/decompress, verify header bytes (0x78 0x9C), verify Adler-32 checksum
- `PkgSHA1`: Test against NIST known-answer vectors ("abc" → a9993e36...)
- `MacOSPkgBuilder`: Build a minimal .pkg, verify xar structure, on macOS CI run `pkgutil --check-signature` and `lsbom` to validate

## Risks

- **xar TOC schema**: Apple's Installer.app is picky about the XML structure. If the TOC doesn't match expectations, Installer.app shows "damaged package." Mitigation: test against Apple's open-source xar reference and real macOS CI early.
- **BOM (Bill of Materials)**: Some .pkg tools generate a BOM file. Apple's Installer.app does NOT require it for flat packages — only for bundle-style .pkg. We omit it to avoid implementing Apple's proprietary B-tree format.
