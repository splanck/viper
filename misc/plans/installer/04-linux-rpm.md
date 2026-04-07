# Phase 4: Linux RPM Package

## Goal

Generate .rpm packages for RHEL/CentOS/Fedora/openSUSE. Combined with the existing .deb support, this covers the two major Linux package families.

The existing `LinuxPackageBuilder` already generates valid .deb packages. This phase adds RPM and creates a `ViperLinuxPackageBuilder` that produces both formats from the same `ViperInstallManifest`.

## Architecture

RPM v4 binary format:
```
[Lead - 96 bytes]          ← magic 0xEDABEEDB, package name, arch
[Signature header]         ← RPM header containing size/digest tags
[Main header]              ← RPM header containing all package metadata
[Payload]                  ← gzip(cpio) — reuses CpioWriter from Phase 3
```

## New Files

### `src/tools/common/packaging/PkgSHA256.hpp/cpp` (~200 LOC)

Self-contained SHA-256. Port from `src/runtime/text/rt_hash.c` (line 368+), same approach as PkgMD5 and PkgSHA1.

```cpp
void sha256(const uint8_t* data, size_t len, uint8_t digest[32]);
std::string sha256Hex(const uint8_t* data, size_t len);
```

RPM signatures use both MD5 (existing) and SHA256 (new).

### `src/tools/common/packaging/RpmWriter.hpp/cpp` (~500 LOC)

RPM v4 binary format writer.

```cpp
struct RpmFileEntry {
    std::string installPath;        // absolute, e.g. "/usr/bin/viper"
    std::vector<uint8_t> data;      // file content bytes
    uint32_t mode;                  // Unix mode (0100755 for executables)
    uint32_t flags;                 // RPMFILE_DOC, RPMFILE_CONFIG, etc.
};

struct RpmBuildParams {
    std::string name;               // "viper"
    std::string version;            // "0.2.4"
    std::string release;            // "1"
    std::string arch;               // "x86_64" or "aarch64"
    std::string summary;            // one-line description
    std::string description;        // multi-line description
    std::string license;            // "GPL-3.0-only"
    std::string url;                // project URL
    std::string group;              // "Development/Tools"
    std::vector<RpmFileEntry> files;
};

bool buildRpm(const RpmBuildParams& params, const std::string& outputPath, std::ostream& err);
```

**RPM Lead** (96 bytes):
```
bytes 0-3:   0xED 0xAB 0xEE 0xDB    (magic)
bytes 4:     3                        (major version)
bytes 5:     0                        (minor version)
bytes 6-7:   0x0000                   (type: binary)
bytes 8-9:   archnum                  (1=x86_64, 12=aarch64)
bytes 10-75: name (NUL-padded)
bytes 76-77: osnum (1=Linux)
bytes 78-79: signature_type (5=header)
bytes 80-95: reserved (zeros)
```

**RPM Header** structure (used for both signature and main headers):
```
bytes 0-2:   0x8E 0xAD 0xE8          (magic)
byte 3:      0x01                     (version)
bytes 4-7:   reserved (zeros)
bytes 8-11:  nindex (big-endian)      (number of index entries)
bytes 12-15: hsize (big-endian)       (data store size in bytes)

[Index entries — 16 bytes each]
  bytes 0-3: tag (big-endian)
  bytes 4-7: type (big-endian)        (6=STRING, 4=INT32, 7=BIN, 8=STRING_ARRAY)
  bytes 8-11: offset (big-endian)     (offset into data store)
  bytes 12-15: count (big-endian)

[Data store — hsize bytes]
  Concatenated tag values, aligned per type rules:
    INT16: 2-byte aligned
    INT32: 4-byte aligned
    INT64: 8-byte aligned
    STRING: NUL-terminated, no alignment
    STRING_ARRAY: consecutive NUL-terminated strings
    BIN: raw bytes, no alignment
```

**Signature header tags**:
| Tag | Value | Type | Content |
|-----|-------|------|---------|
| RPMSIGTAG_SIZE | 1000 | INT32 | header+payload size |
| RPMSIGTAG_MD5 | 1004 | BIN(16) | MD5 of header+payload |
| RPMSIGTAG_PAYLOADSIZE | 1007 | INT32 | uncompressed payload size |
| RPMSIGTAG_SHA256 | 277 | STRING | hex SHA-256 of main header |

**Main header tags** (essential subset):
| Tag | Value | Type |
|-----|-------|------|
| RPMTAG_NAME | 1000 | STRING |
| RPMTAG_VERSION | 1001 | STRING |
| RPMTAG_RELEASE | 1002 | STRING |
| RPMTAG_SUMMARY | 1004 | I18NSTRING |
| RPMTAG_DESCRIPTION | 1005 | I18NSTRING |
| RPMTAG_BUILDTIME | 1006 | INT32 |
| RPMTAG_SIZE | 1009 | INT32 |
| RPMTAG_LICENSE | 1014 | STRING |
| RPMTAG_GROUP | 1016 | STRING |
| RPMTAG_OS | 1021 | STRING |
| RPMTAG_ARCH | 1022 | STRING |
| RPMTAG_PAYLOADFORMAT | 1124 | STRING ("cpio") |
| RPMTAG_PAYLOADCOMPRESSOR | 1125 | STRING ("gzip") |
| RPMTAG_PAYLOADFLAGS | 1126 | STRING ("9") |
| RPMTAG_DIRNAMES | 1118 | STRING_ARRAY |
| RPMTAG_BASENAMES | 1117 | STRING_ARRAY |
| RPMTAG_DIRINDEXES | 1116 | INT32_ARRAY |
| RPMTAG_FILESIZES | 1028 | INT32_ARRAY |
| RPMTAG_FILEMODES | 1030 | INT16_ARRAY |
| RPMTAG_FILEMTIMES | 1034 | INT32_ARRAY |
| RPMTAG_FILEFLAGS | 1037 | INT32_ARRAY |
| RPMTAG_FILEUSERNAME | 1039 | STRING_ARRAY |
| RPMTAG_FILEGROUPNAME | 1040 | STRING_ARRAY |
| RPMTAG_FILEMD5S | 1035 | STRING_ARRAY (hex digests) |
| RPMTAG_FILELINKTOS | 1036 | STRING_ARRAY |
| RPMTAG_FILERDEVS | 1033 | INT16_ARRAY |
| RPMTAG_FILEINODES | 1096 | INT32_ARRAY |

**Payload**: `PkgGzip::gzip(cpioWriter.finish())` — reuses CpioWriter from Phase 3.

Build flow:
1. Split file paths into DIRNAMES + BASENAMES + DIRINDEXES (RPM's compressed path representation)
2. Compute per-file MD5 hex digests
3. Build cpio payload from files using `CpioWriter`, gzip it
4. Build main header with all metadata tags
5. Compute SHA-256 of main header bytes
6. Build signature header with size, MD5, SHA-256, payload size
7. Write: lead + signature header + main header + gzipped payload
8. Pad signature header to 8-byte alignment (RPM requirement)

### `src/tools/common/packaging/ViperLinuxPackageBuilder.hpp/cpp` (~250 LOC)

Wrapper that builds both .deb and .rpm from a `ViperInstallManifest`.

```cpp
struct ViperLinuxBuildParams {
    ViperInstallManifest manifest;
    std::string outputDir;        // directory for output files
};

/// Builds viper-VERSION-ARCH.deb
void buildViperDeb(const ViperLinuxBuildParams& params);

/// Builds viper-VERSION-RELEASE.ARCH.rpm
void buildViperRpm(const ViperLinuxBuildParams& params);
```

For .deb: Adapts existing `LinuxPackageBuilder::buildDebPackage()` pattern — maps manifest to FHS paths, generates control file, md5sums, postinst (runs `mandb`), data.tar.gz, control.tar.gz, wraps in ar archive.

For .rpm: Maps manifest to FHS paths, calls `RpmWriter::buildRpm()`.

Both share the same FHS path mapping from Phase 1's `linuxInstallPath()`.

## Modified Files

- `src/CMakeLists.txt` — add PkgSHA256.cpp, RpmWriter.cpp, ViperLinuxPackageBuilder.cpp to `viper_packaging`

## Testing

- `PkgSHA256`: NIST known-answer vectors
- `RpmWriter`: Build minimal RPM, verify lead magic (0xEDABEEDB), verify header magic (0x8EADE801), verify tag presence
- Linux CI: `rpm -qip output.rpm` to verify metadata; `rpm -qlp output.rpm` to verify file list; `rpm -K output.rpm` to verify signatures
- Linux CI: `dpkg-deb -I output.deb` and `dpkg-deb -c output.deb` for .deb validation

## Risks

- **RPM header alignment**: INT16/INT32/INT64 values in the data store must be naturally aligned. Incorrect padding produces corrupt RPMs that `rpm -K` rejects. Mitigation: implement `alignStore(typeAlignment)` helper called before writing each typed value.
- **Signature header padding**: The region after the signature header must be padded to an 8-byte boundary before the main header starts. This is an RPM v4 requirement that's easy to miss.
