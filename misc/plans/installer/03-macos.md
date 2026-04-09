# Phase 3: macOS .pkg Installer

## Goal

A standard macOS flat package (.pkg) that opens in Apple's Installer.app when double-clicked. User sees the familiar macOS install experience: introduction → license → install location → install → summary. This is how Go, Node.js, Rust, and Xcode CLT distribute on macOS.

## Architecture

A flat .pkg is a **xar archive** containing:
```
Distribution                  ← XML: installer UI definition
Resources/                    ← directory for license file
  en.lproj/                   ← English localization
    license.txt               ← GPL v3 license text
viper.pkg/                    ← component package
  PackageInfo                 ← XML: package metadata
  Payload                     ← gzip(cpio) of installed files
  Scripts/                    ← shell scripts
    postinstall               ← creates /usr/local/bin symlinks
    preinstall                ← (optional) clean up old installation
```

This requires three new format writers: CpioWriter, XarWriter, PkgZlib. Plus SHA-1 for xar checksums.

## Code Signing Status

Unsigned .pkg files on macOS 13+ trigger Gatekeeper warnings. Users must right-click → Open → confirm to install. This is acceptable for development/alpha distribution. For production:
- Apple Developer ID Installer certificate required (costs $99/year)
- Sign with `productsign --sign "Developer ID Installer: ..." unsigned.pkg signed.pkg`
- Notarize with `notarytool submit` for full Gatekeeper approval
- All signing is a POST-BUILD step, not part of the .pkg generation code

The builder should produce a structurally valid unsigned .pkg. Signing is out of scope.

## New Files

### `src/tools/common/packaging/CpioWriter.hpp/cpp` (~220 LOC)

SVR4/newc (ASCII) cpio format. Shared by macOS .pkg and Linux .rpm.

```cpp
class CpioWriter {
public:
    /// Add a regular file. Mode includes file type bits (0100644 for regular, 0100755 for executable).
    void addFile(const std::string& path, const uint8_t* data, size_t len,
                 uint32_t mode = 0100644);
    /// Add a directory. Mode includes directory type bit (040755).
    void addDirectory(const std::string& path, uint32_t mode = 040755);
    /// Add a symbolic link. Target is stored as the "file data".
    void addSymlink(const std::string& path, const std::string& target,
                    uint32_t mode = 0120777);
    /// Finalize: appends TRAILER!!! sentinel entry and returns the archive bytes.
    std::vector<uint8_t> finish();

    /// Set the modification time for all subsequent entries (Unix timestamp).
    void setMtime(uint32_t mtime);
private:
    std::vector<uint8_t> buffer_;
    uint32_t nextInode_{1};
    uint32_t mtime_{0};  // 0 = use current time

    void writeEntry(const std::string& name, const uint8_t* data, size_t dataLen,
                    uint32_t mode, uint32_t nlink);
};
```

Entry format (110-byte ASCII header):
```
070701           ← magic (6 chars, NOT NUL-terminated)
%08X             ← inode
%08X             ← mode (file type + permissions, e.g. 0100755)
%08X             ← uid (0 for root)
%08X             ← gid (0 for wheel/root)
%08X             ← nlink (1 for files, 2 for dirs)
%08X             ← mtime (Unix timestamp)
%08X             ← filesize (0 for dirs; symlink target length for symlinks)
%08X             ← devmajor (0)
%08X             ← devminor (0)
%08X             ← rdevmajor (0)
%08X             ← rdevminor (0)
%08X             ← namesize (including NUL terminator)
%08X             ← check (0, unused in newc)
```
Followed by: filename (NUL-terminated, padded to 4-byte boundary), file data (padded to 4-byte boundary). For symlinks, "file data" is the target path (no NUL terminator, padded to 4-byte boundary). Archive ends with entry named `TRAILER!!!` (11 chars + NUL = 12).

**Important**: Directories must appear BEFORE their contents in the archive. The builder must sort entries to ensure parent directories precede children.

### `src/tools/common/packaging/PkgZlib.hpp/cpp` (~100 LOC)

Zlib-wrapped DEFLATE (RFC 1950). xar uses zlib compression, not raw DEFLATE.

```cpp
/// Compress data with zlib framing (2-byte header + DEFLATE + 4-byte Adler-32).
std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t len, int level = 6);

/// Decompress zlib-framed data.
std::vector<uint8_t> zlibDecompress(const uint8_t* data, size_t len);

/// Compute Adler-32 checksum (RFC 1950).
uint32_t adler32(const uint8_t* data, size_t len);
```

Format: 2-byte header (CMF=0x78, FLG=0x9C for default level) + raw DEFLATE data (from existing `PkgDeflate::deflate()`) + 4-byte big-endian Adler-32 checksum.

The CMF/FLG bytes encode:
- CMF: CM=8 (deflate), CINFO=7 (32K window) → 0x78
- FLG: FCHECK computed so (CMF*256+FLG) % 31 == 0, FDICT=0, FLEVEL=2 (default) → 0x9C

Adler-32: `s1 = 1, s2 = 0; for each byte: s1 = (s1 + byte) % 65521; s2 = (s2 + s1) % 65521; return (s2 << 16) | s1`

### `src/tools/common/packaging/PkgSHA1.hpp/cpp` (~160 LOC)

Self-contained SHA-1. Port from `src/runtime/text/rt_hash.c` (starting at the SHA-1 section, around line 266), removing runtime `rt_string` dependencies. Same pattern as `PkgMD5`.

```cpp
void sha1(const uint8_t* data, size_t len, uint8_t digest[20]);
std::string sha1Hex(const uint8_t* data, size_t len);  // lowercase hex
```

Must handle:
- Padding to 512-bit boundary with 0x80 + zeros + 64-bit big-endian length
- 80 rounds with rotl32, Ch/Parity/Maj functions, K constants
- Big-endian digest output (20 bytes)

### `src/tools/common/packaging/XarWriter.hpp/cpp` (~400 LOC)

Apple xar archive format writer.

```cpp
class XarWriter {
public:
    /// Add a file at the given path within the archive.
    /// Path components create implicit directory entries in the TOC.
    void addFile(const std::string& path, const std::vector<uint8_t>& data);

    /// Add an explicit directory entry.
    void addDirectory(const std::string& path);

    /// Write the xar archive to the given output path.
    /// Returns false and writes errors to err on failure.
    bool write(const std::string& outputPath, std::ostream& err);

    /// Write to a byte vector instead of a file.
    bool writeToBuffer(std::vector<uint8_t>& output, std::ostream& err);

private:
    struct Entry {
        std::string path;         // full path within archive
        std::string name;         // leaf name
        std::vector<uint8_t> data;
        bool isDir;
        uint32_t id;              // unique file ID for TOC
    };
    std::vector<Entry> entries_;
    uint32_t nextId_{1};

    // Build the TOC XML and heap, then assemble the final archive.
    bool buildArchive(std::vector<uint8_t>& output, std::ostream& err);
    std::string buildTocXml(const std::vector<uint8_t>& heap);
};
```

Binary format:
```
[Header - 28 bytes, all big-endian]
  uint32_t magic = 0x78617221  ("xar!")
  uint16_t size = 28           (header size)
  uint16_t version = 1
  uint64_t toc_compressed_size
  uint64_t toc_uncompressed_size
  uint32_t cksum_algo = 1      (SHA-1)

[TOC - toc_compressed_size bytes]
  zlib-compressed XML

[Heap]
  byte[20]: SHA-1 checksum of the UNCOMPRESSED TOC XML (at heap offset 0)
  Concatenated zlib-compressed file data blobs (offsets referenced by TOC)
```

The TOC XML must have this structure (Apple's Installer.app validates it):
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
        <length>NNN</length>
        <offset>20</offset>
        <size>NNN</size>
        <encoding style="application/x-gzip"/>
        <archived-checksum style="sha1">HEX</archived-checksum>
        <extracted-checksum style="sha1">HEX</extracted-checksum>
      </data>
    </file>
    <!-- directories use <type>directory</type> and nest child <file> elements -->
  </toc>
</xar>
```

**Key details**:
- All integers in the header are big-endian
- TOC checksum in the heap is the SHA-1 of the UNCOMPRESSED TOC XML bytes
- File data in heap is individually zlib-compressed; offset is relative to heap start
- `archived-checksum` is SHA-1 of the COMPRESSED data blob
- `extracted-checksum` is SHA-1 of the UNCOMPRESSED original data
- Directories have `<type>directory</type>` and contain nested `<file>` children
- File IDs must be unique positive integers

### `src/tools/common/packaging/MacOSPkgBuilder.hpp/cpp` (~400 LOC)

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
2. Build cpio payload using `CpioWriter`:
   - Add all directories first (sorted to ensure parents before children)
   - Add all files with correct modes (0100755 for bins, 0100644 for others)
   - `finish()` to get the cpio bytes
3. Gzip the cpio bytes using `PkgGzip::gzip()`
4. Generate `PackageInfo` XML:
   ```xml
   <pkg-info format-version="2" identifier="com.viper-lang.viper"
             version="0.2.4" install-location="/" auth="root">
       <payload numberOfFiles="N" installKBytes="K"/>
       <scripts>
           <postinstall file="./postinstall"/>
       </scripts>
   </pkg-info>
   ```
5. Generate `preinstall` script (optional — clean stale symlinks from previous installs):
   ```bash
   #!/bin/bash
   # Remove stale symlinks from previous Viper installation
   for tool in viper zia vbasic ilrun il-verify il-dis zia-server vbasic-server; do
       [ -L "/usr/local/bin/$tool" ] && rm -f "/usr/local/bin/$tool"
   done
   exit 0
   ```
6. Generate `postinstall` script (MUST be mode 0755 in the cpio Scripts archive):
   ```bash
   #!/bin/bash
   # Create /usr/local/bin symlinks for Viper CLI tools
   mkdir -p /usr/local/bin
   for tool in viper zia vbasic ilrun il-verify il-dis zia-server vbasic-server; do
       ln -sf /usr/local/viper/bin/$tool /usr/local/bin/$tool
   done
   # Update man page database if available
   # macOS Ventura+ uses /usr/libexec/makewhatis; older uses /usr/bin/makewhatis
   for cmd in /usr/libexec/makewhatis /usr/bin/makewhatis; do
       if [ -x "$cmd" ]; then
           "$cmd" /usr/local/share/man 2>/dev/null || true
           break
       fi
   done
   exit 0
   ```
   **Important**: The Scripts directory in the .pkg is a SEPARATE cpio archive from the Payload. Apple's Installer.app extracts Scripts to a temp directory and executes them. The postinstall script path within PackageInfo is `./postinstall` (relative to the extracted Scripts dir). The script must have mode 0100755 in the Scripts cpio.
7. Generate `Distribution` XML:
   ```xml
   <?xml version="1.0" encoding="utf-8"?>
   <installer-gui-script minSpecVersion="2">
       <title>Viper Compiler Toolchain v0.2.4</title>
       <license file="license.txt"/>
       <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
       <domains enable_localSystem="true"/>
       <choices-outline>
           <line choice="default"/>
       </choices-outline>
       <choice id="default" title="Viper">
           <pkg-ref id="com.viper-lang.viper"/>
       </choice>
       <pkg-ref id="com.viper-lang.viper" version="0.2.4" installKBytes="NNNN">#viper.pkg</pkg-ref>
       <volume-check>
           <allowed-os-versions>
               <os-version min="11.0"/>
           </allowed-os-versions>
       </volume-check>
   </installer-gui-script>
   ```
8. Build the Scripts archive — a SEPARATE gzipped cpio containing preinstall and postinstall:
   ```
   CpioWriter scriptsCpio;
   scriptsCpio.addFile("./preinstall", preinstallBytes, preinstallLen, 0100755);
   scriptsCpio.addFile("./postinstall", postinstallBytes, postinstallLen, 0100755);
   auto scriptsPayload = PkgGzip::gzip(scriptsCpio.finish());
   ```
   Note: paths in the Scripts cpio use `./` prefix (relative to extraction dir).
9. Pack everything into a xar archive using `XarWriter`:
   - `Distribution` (file)
   - `Resources/en.lproj/license.txt` (file — the LICENSE text)
   - `viper.pkg/PackageInfo` (file)
   - `viper.pkg/Payload` (file — the gzipped cpio of installed files)
   - `viper.pkg/Scripts` (file — the gzipped cpio of pre/postinstall scripts)
10. Write to output path

**Critical .pkg structure note**: Both `Payload` and `Scripts` are gzipped cpio archives stored as opaque blobs inside the xar. They are NOT individual files within the xar — the xar sees them as single binary entries. Apple's Installer.app knows to decompress and extract them based on the PackageInfo XML.

**Architecture handling**: The `hostArchitectures` attribute in the Distribution XML controls which Mac architectures can install. For a universal installer containing both arm64 and x86_64 binaries, set `hostArchitectures="arm64,x86_64"`. For single-arch installers, use just the target arch. The builder generates separate .pkg files per architecture.

## Modified Files

- `src/CMakeLists.txt` — add CpioWriter.cpp, XarWriter.cpp, PkgZlib.cpp, PkgSHA1.cpp, MacOSPkgBuilder.cpp to `viper_packaging`

## Testing

- `CpioWriter`:
  - Write a 3-file archive, verify "070701" magic on each entry, verify TRAILER!!! sentinel
  - Verify 4-byte alignment of name and data regions
  - Verify directories come before their children
  - Verify symlink mode bits (0120777) and target stored as data
  - Verify execute bit preservation (0100755)
- `XarWriter`:
  - Write a 2-file archive, verify magic 0x78617221
  - Verify header size field = 28
  - Decompress TOC, verify valid XML with `<xar><toc>` structure
  - Verify heap blob SHA-1 checksums match TOC declarations
  - Verify nested directory structure in TOC
- `PkgZlib`:
  - Round-trip compress/decompress, verify data matches
  - Verify header bytes (0x78 0x9C)
  - Verify Adler-32 known values: adler32("") = 1, adler32("abc") = 0x024d0127
- `PkgSHA1`:
  - NIST known-answer vectors:
    - SHA-1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
    - SHA-1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
    - SHA-1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") = 84983e441c3bd26ebaae4aa1f95129e5e54670f1
- `MacOSPkgBuilder`:
  - Build a minimal .pkg from 3-file manifest, verify xar structure
  - Verify Distribution file in xar TOC
  - Extract and decompress Payload, verify cpio entries
  - Verify Scripts/postinstall in xar
  - macOS CI: `pkgutil --check-signature`, `pkgutil --payload-files`, `sudo installer -pkg ... -target /`

## Risks

- **xar TOC schema**: Apple's Installer.app is picky. The most common failure modes are:
  1. Missing `<checksum>` in TOC → "This package is damaged"
  2. Wrong `archived-checksum`/`extracted-checksum` → silent extraction failure
  3. Wrong `<encoding style>` value → decompression failure
  Mitigation: Test with Apple's open-source xar reference implementation and real macOS CI.
- **BOM**: Omitted. Flat packages don't require it. If Installer.app rejects the package, add a minimal BOM as a fallback (but this is unlikely).
- **Gatekeeper**: Unsigned .pkg will show "unidentified developer" warning. Users must right-click → Open. Document this in README.
