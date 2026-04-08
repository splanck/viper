# Phase 4: Linux RPM Package + .deb Enhancements

## Goal

Generate .rpm packages for RHEL/CentOS/Fedora/openSUSE. Enhance the existing .deb builder with missing features (mandb trigger, file associations, dependencies). Create a unified `ViperLinuxPackageBuilder` that produces both formats from the same `ViperInstallManifest`.

## Architecture

RPM v4 binary format:
```
[Lead - 96 bytes]          ← magic 0xEDABEEDB, package name, arch
[Signature header]         ← RPM header with size/digest tags
  [8-byte alignment pad]   ← critical: sig header padded to 8-byte boundary
[Main header]              ← RPM header with all package metadata
[Payload]                  ← gzip(cpio) — reuses CpioWriter from Phase 3
```

## New Files

### `src/tools/common/packaging/PkgSHA256.hpp/cpp` (~200 LOC)

Self-contained SHA-256. Port from `src/runtime/text/rt_hash.c` (the SHA-256 section around line 368), same approach as PkgMD5 and PkgSHA1.

```cpp
void sha256(const uint8_t* data, size_t len, uint8_t digest[32]);
std::string sha256Hex(const uint8_t* data, size_t len);
```

RPM signatures use both MD5 (existing `PkgMD5`) and SHA-256 (new).

### `src/tools/common/packaging/RpmWriter.hpp/cpp` (~550 LOC)

RPM v4 binary format writer.

```cpp
struct RpmFileEntry {
    std::string installPath;        // absolute, e.g. "/usr/bin/viper"
    std::vector<uint8_t> data;      // file content bytes
    uint32_t mode;                  // Unix mode (0100755 for executables)
    uint32_t flags;                 // RPMFILE_DOC, RPMFILE_CONFIG, etc.
    std::string linkTarget;         // for symlinks only
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
    std::string vendor;             // "Viper Project"
    std::vector<RpmFileEntry> files;
};

bool buildRpm(const RpmBuildParams& params, const std::string& outputPath, std::ostream& err);
```

**RPM Lead** (96 bytes, all big-endian except name):
| Offset | Size | Content |
|--------|------|---------|
| 0 | 4 | Magic: `0xED 0xAB 0xEE 0xDB` |
| 4 | 1 | Major version: 3 |
| 5 | 1 | Minor version: 0 |
| 6 | 2 | Type: 0x0000 (binary) |
| 8 | 2 | Archnum: 1=x86_64, 12=aarch64 |
| 10 | 66 | Name (NUL-padded ASCII) |
| 76 | 2 | Osnum: 1 (Linux) |
| 78 | 2 | Signature type: 5 (header-style) |
| 80 | 16 | Reserved (zeros) |

**RPM Header structure** (used for both signature and main headers):
| Offset | Size | Content |
|--------|------|---------|
| 0 | 3 | Magic: `0x8E 0xAD 0xE8` |
| 3 | 1 | Version: `0x01` |
| 4 | 4 | Reserved (zeros) |
| 8 | 4 | nindex (big-endian) — number of index entries |
| 12 | 4 | hsize (big-endian) — data store size in bytes |

Index entries (16 bytes each, big-endian):
| Offset | Size | Content |
|--------|------|---------|
| 0 | 4 | Tag number |
| 4 | 4 | Type (6=STRING, 4=INT32, 7=BIN, 8=STRING_ARRAY, 3=INT16) |
| 8 | 4 | Offset into data store |
| 12 | 4 | Count |

Data store alignment rules (**critical for validity**):
- INT16: must start on 2-byte boundary (pad with zeros)
- INT32: must start on 4-byte boundary
- INT64: must start on 8-byte boundary
- STRING/STRING_ARRAY/BIN: no alignment requirement
- Implement `alignStore(size_t alignment)` helper that inserts zero padding

**Signature header tags**:
| Tag | Value | Type | Content |
|-----|-------|------|---------|
| RPMSIGTAG_SIZE | 1000 | INT32 | main header + payload size |
| RPMSIGTAG_MD5 | 1004 | BIN(16) | MD5 of main header + payload |
| RPMSIGTAG_PAYLOADSIZE | 1007 | INT32 | uncompressed cpio payload size |
| RPMSIGTAG_SHA256 | 277 | STRING | hex SHA-256 of main header bytes only |

**After the signature header**: Pad to the next 8-byte boundary with zero bytes. This is a hard RPM v4 requirement — without this padding, `rpm -K` rejects the package.

**Main header tags** (essential subset):
| Tag | Value | Type |
|-----|-------|------|
| RPMTAG_NAME | 1000 | STRING |
| RPMTAG_VERSION | 1001 | STRING |
| RPMTAG_RELEASE | 1002 | STRING |
| RPMTAG_SUMMARY | 1004 | I18NSTRING |
| RPMTAG_DESCRIPTION | 1005 | I18NSTRING |
| RPMTAG_BUILDTIME | 1006 | INT32 |
| RPMTAG_BUILDHOST | 1007 | STRING |
| RPMTAG_SIZE | 1009 | INT32 (total installed size in bytes) |
| RPMTAG_LICENSE | 1014 | STRING |
| RPMTAG_VENDOR | 1011 | STRING |
| RPMTAG_GROUP | 1016 | STRING |
| RPMTAG_URL | 1020 | STRING |
| RPMTAG_OS | 1021 | STRING ("linux") |
| RPMTAG_ARCH | 1022 | STRING ("x86_64" or "aarch64") |
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
| RPMTAG_FILEUSERNAME | 1039 | STRING_ARRAY (all "root") |
| RPMTAG_FILEGROUPNAME | 1040 | STRING_ARRAY (all "root") |
| RPMTAG_FILEMD5S | 1035 | STRING_ARRAY (hex MD5 digests, empty for dirs) |
| RPMTAG_FILELINKTOS | 1036 | STRING_ARRAY (symlink targets, empty for non-links) |
| RPMTAG_FILERDEVS | 1033 | INT16_ARRAY (all 0) |
| RPMTAG_FILEINODES | 1096 | INT32_ARRAY (unique per file) |
| RPMTAG_FILEVERIFYFLAGS | 1045 | INT32_ARRAY |
| RPMTAG_FILEDEVICES | 1095 | INT32_ARRAY (all 1) |
| RPMTAG_FILELANGS | 1097 | STRING_ARRAY (all empty) |

**File path representation**: RPM uses BASENAMES + DIRNAMES + DIRINDEXES instead of full paths.
- DIRNAMES: unique directory prefixes with trailing `/` (e.g. `"/usr/bin/"`, `"/usr/lib/viper/"`)
- BASENAMES: leaf filenames (e.g. `"viper"`, `"libviper_rt_base.a"`)
- DIRINDEXES: index into DIRNAMES for each file
- Algorithm: for each file path, split at last `/`, store prefix in DIRNAMES (deduped), store leaf in BASENAMES.

**Payload**: `PkgGzip::gzip(cpioWriter.finish(), 9)` — reuses CpioWriter from Phase 3 with max compression.

Build flow:
1. Sort files by install path
2. Split paths into DIRNAMES + BASENAMES + DIRINDEXES
3. Compute per-file MD5 hex digests
4. Build cpio payload from files using `CpioWriter` (must include directory entries), gzip it
5. Build main header with all metadata tags, respecting alignment rules
6. Compute SHA-256 hex of main header bytes (just the header, not the payload)
7. Compute MD5 of main header + payload (both together)
8. Build signature header with SIZE, MD5, SHA256, PAYLOADSIZE
9. Pad signature header to 8-byte alignment
10. Write: lead + padded signature header + main header + gzipped payload

### `src/tools/common/packaging/ViperLinuxPackageBuilder.hpp/cpp` (~300 LOC)

Wrapper that builds both .deb and .rpm from a `ViperInstallManifest`.

```cpp
struct ViperLinuxBuildParams {
    ViperInstallManifest manifest;
    std::string outputDir;        // directory for output files
};

/// Builds viper_VERSION_ARCH.deb
void buildViperDeb(const ViperLinuxBuildParams& params);

/// Builds viper-VERSION-1.ARCH.rpm
void buildViperRpm(const ViperLinuxBuildParams& params);
```

**For .deb** — adapts existing `LinuxPackageBuilder::buildDebPackage()` pattern with enhancements:
- Control file fields:
  ```
  Package: viper
  Version: 0.2.4
  Architecture: amd64
  Maintainer: Viper Project <noreply@viper-lang.dev>
  Section: devel
  Priority: optional
  Installed-Size: NNNN
  Depends: libc6 (>= 2.17), libx11-6
  Suggests: libx11-dev, libasound2-dev
  Homepage: https://viper-lang.dev
  Description: Viper IL compiler toolchain and VM
   A complete compiler platform with Zia and BASIC frontends,
   bytecode VM, native code generation for x86-64 and AArch64,
   2D/3D graphics engine, and networking stack.
  ```
- **postinst** script:
  ```bash
  #!/bin/sh
  set -e
  # Update man page database
  if command -v mandb >/dev/null 2>&1; then
      mandb -q 2>/dev/null || true
  fi
  # Update MIME database for file associations
  if command -v update-mime-database >/dev/null 2>&1; then
      update-mime-database /usr/share/mime 2>/dev/null || true
  fi
  # Update desktop database
  if command -v update-desktop-database >/dev/null 2>&1; then
      update-desktop-database /usr/share/applications 2>/dev/null || true
  fi
  exit 0
  ```
- **postrm** script (cleanup on removal):
  ```bash
  #!/bin/sh
  set -e
  if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
      if command -v mandb >/dev/null 2>&1; then
          mandb -q 2>/dev/null || true
      fi
  fi
  exit 0
  ```
- **File associations**: Generate MIME XML for .zia/.bas/.il under `/usr/share/mime/packages/viper.xml`
- **Desktop file**: Generate `viper.desktop` under `/usr/share/applications/`
- **Icons**: Generate multi-size PNG icons under `/usr/share/icons/hicolor/NxN/apps/` (if icon provided)

**For .rpm** — maps manifest to FHS paths, calls `RpmWriter::buildRpm()`. Includes the same postinstall actions via the `RPMTAG_POSTIN` tag (tag 1024, type STRING):
```bash
/sbin/ldconfig 2>/dev/null || true
mandb -q 2>/dev/null || true
```

Both share the same FHS path mapping from Phase 1's `linuxInstallPath()`.

## GPG Signing

RPM GPG signing is a **post-build step**, not part of the writer:
1. Acquire/generate a GPG key for the Viper project
2. After `buildRpm()`: `rpm --addsign --define "_gpg_name Viper Project" output.rpm`
3. Distribute public key: `rpm --import https://viper-lang.dev/RPM-GPG-KEY-viper`

The `RpmWriter` produces structurally valid RPMs with MD5+SHA256 integrity digests that pass `rpm -K` without GPG. Document this in the README.

## Modified Files

- `src/CMakeLists.txt` — add PkgSHA256.cpp, RpmWriter.cpp, ViperLinuxPackageBuilder.cpp to `viper_packaging`

## Testing

- `PkgSHA256`: NIST known-answer vectors:
  - SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  - SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
- `RpmWriter`:
  - Build minimal RPM with 2 files
  - Verify lead magic (0xEDABEEDB) at offset 0
  - Verify signature header magic (0x8EADE801) at offset 96
  - Verify 8-byte alignment padding after signature header
  - Verify main header magic at next position
  - Verify RPMTAG_NAME, RPMTAG_VERSION, RPMTAG_ARCH tags present
  - Decompress payload, verify cpio magic "070701"
  - Verify BASENAMES+DIRNAMES+DIRINDEXES reconstruct original paths
  - Verify per-file MD5 digests match FILEMD5S tag
- `ViperLinuxPackageBuilder`:
  - Build .deb from mock manifest, verify ar structure + control.tar.gz + data.tar.gz
  - Verify control file contains all required fields
  - Verify postinst script contains mandb call
  - Verify postrm script present
- Linux CI:
  - `dpkg-deb -I output.deb` + `dpkg-deb -c output.deb | grep usr/bin/viper`
  - `rpm -qip output.rpm` + `rpm -qlp output.rpm | grep usr/bin/viper`
  - `rpm -K output.rpm` (verify MD5/SHA256 digests)
  - Install/verify/uninstall cycle on both package managers

## Risks

- **RPM header alignment**: The #1 cause of corrupt RPMs. Every INT16/INT32/INT64 value in the data store must be naturally aligned. Implement and test `alignStore()` exhaustively.
- **Signature header 8-byte padding**: Easy to forget. The gap between the signature header's end and the main header's start must be padded to the next 8-byte boundary.
- **DIRNAMES trailing slash**: RPM requires directory names to end with `/`. Missing trailing slash → `rpm -qlp` shows garbled paths.
- **I18NSTRING vs STRING**: SUMMARY and DESCRIPTION use I18NSTRING (type 9) with a "C" locale prefix. Using plain STRING (type 6) may cause rpm warnings.
