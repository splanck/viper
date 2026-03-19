# VAPS — Viper Application Package System Design Document

## Context

Viper developers who build applications (console, GUI, games, servers) currently have no way to distribute them as installable packages. `viper build -o myapp` produces a single statically-linked native binary, but end users expect platform-native installers — `.app` bundles on macOS, `.deb` packages on Linux, installable `.exe` on Windows. This design follows Viper's zero-dependency philosophy: we emit every byte ourselves, no NSIS, no WiX, no dpkg-deb, no rpmbuild.

### CRITICAL: GC Dependency Barrier

**The `viper` CLI binary does NOT link any runtime (`viper_rt_*`) libraries.** It links only compiler/IL libraries:

```
VIPER_CLI_LINK_LIBS = viper_common_opts, viper::il_full, il_vm, il_transform,
    il_link, fe_basic, fe_zia, il_api, il_tools_common, viper_native_compiler,
    viper_codegen_x86_64, il_codegen_aarch64, viper_cmd_arm64, viper_bytecode
```

All runtime APIs (`rt_archive_*`, `rt_compress_*`, `rt_bytes_*`, `rt_pixels_*`, `rt_hash_*`) allocate through `rt_obj_new_i64()` — the GC-tracked heap allocator in `rt_gc.c`. Without GC initialization, calling any of these functions will crash.

**Consequence:** The packaging library **must be entirely self-contained C++** with zero runtime dependencies. We cannot reuse the runtime's ZIP writer, DEFLATE implementation, PNG codec, or image resizer directly. However, the runtime code serves as a **reference implementation** to port from — the core algorithms (DEFLATE, CRC-32, PNG encoding) use `malloc` internally and only wrap results in GC objects at the final step.

### Existing Code — Reference Implementations (NOT directly callable)

| Capability | Location | Why not callable | Port effort |
|---|---|---|---|
| ZIP write | `rt_archive.c` | `archive_alloc()` → `rt_obj_new_i64` | ~200 LOC, well-structured |
| DEFLATE | `rt_compress.c` | `deflate_data()` wraps result in `rt_bytes_new` | ~500 LOC, core uses `malloc` via `bit_writer_t` |
| GZIP | `rt_compress.c` | Same GC wrapping | ~30 LOC on top of DEFLATE |
| CRC-32 | `rt_crc32.c` | **Actually usable** — no GC, pure static table + raw `uint8_t*` | 0 LOC (just link it) |
| MD5 | `rt_hash.c` | `static compute_md5()`, returns via GC-managed `rt_string` | ~100 LOC to expose raw version |
| PNG read/write | `rt_pixels.c` | `rt_pixels_save_png` → `rt_bytes_new` for DEFLATE buffer | ~300 LOC reader, ~200 LOC writer |
| Image resize | `rt_pixels.c` | `pixels_alloc()` → GC | ~80 LOC bilinear |

### Existing Code — Directly Usable

| Capability | Location | Notes |
|---|---|---|
| `rt_crc32_compute()` | `src/runtime/core/rt_crc32.c` | **No GC dep.** Pure function: `uint32_t rt_crc32_compute(const uint8_t*, size_t)`. Uses static lookup table with atomic init. Can link `rt_crc32.o` directly. |
| tar header struct | `viperdos/user/libc/include/tar.h` | `struct posix_header` (512 bytes), all constants |
| `viper.project` parser | `src/tools/common/project_loader.cpp` | Already in CLI link chain |
| `compileToNative()` | `src/tools/common/native_compiler.cpp` | Already in CLI link chain |
| `resolveProject()` | `src/tools/common/project_loader.cpp` | Already in CLI link chain |

### Existing ZIP Writer — Missing Unix Permissions (Critical Gap)

Even as a reference, the runtime's ZIP writer (`rt_archive.c:1208`) has a **critical gap for macOS**: it sets `external_file_attributes` to `0` for files and `0x10` (MS-DOS directory attribute) for directories. `ZIP_VERSION_MADE = 20` has no Unix OS byte in the upper nibble. This means:
- **No Unix execute bit** — macOS will NOT make the binary executable after extraction
- **No Unix file permissions at all** — everything extracts as default perms

Our ZipWriter MUST set:
- `version_made_by = (3 << 8) | 20` — Unix, version 2.0
- `external_file_attributes = (mode << 16)` — Unix mode in upper 16 bits:
  - `(0100755 << 16)` for executables
  - `(0100644 << 16)` for regular files
  - `(040755 << 16)` for directories
  - `(0120777 << 16)` for symlinks

### What Must Be Built From Scratch

**Self-contained C++ packaging library** (~3000 LOC total):
- CRC-32: Link `rt_crc32.o` directly (no GC dependency)
- DEFLATE compressor: Port ~500 lines from `rt_compress.c` (core uses `malloc`-based `bit_writer_t`)
- GZIP framing: ~30 lines on top of DEFLATE
- MD5 digest: ~100 lines (RFC 1321, or port `compute_md5` from `rt_hash.c`)
- ZIP writer with Unix permissions: ~200 lines
- ar writer: ~80 lines
- tar USTAR writer: ~200 lines
- PNG reader (for icon source): ~300 lines (port from `rt_pixels.c`)
- PNG writer (for icon generation): ~200 lines (port from `rt_pixels.c`)
- Bilinear image resize: ~80 lines (port from `rt_pixels.c`)
- ICNS container writer: ~80 lines
- ICO container writer: ~100 lines
- PE32+ header emitter: ~500 lines
- .lnk shortcut writer: ~300 lines
- Info.plist XML generator: ~100 lines
- .desktop file generator: ~50 lines
- `cmd_package.cpp` CLI entry: ~200 lines

---

## 1. Package Manifest Format

Extend the existing `viper.project` with `package-*` directives. The parser already rejects unknown directives, so adding new ones is backward-compatible — existing projects without `package-*` lines continue to work unchanged.

### Complete Example: Hypothetical GUI App

```
# === Build Config (existing) ===
project viperide
version 1.2.0
lang zia
entry main.zia
sources src
optimize O2

# === Package Metadata (new) ===
package-name ViperIDE
package-author "Stephen Smith"
package-description "A lightweight code editor built with Viper"
package-homepage https://viper-lang.org/viperide
package-license GPL-3.0
package-identifier org.viper-lang.viperide
package-icon assets/icon-512.png

# === Assets (new, repeatable) ===
# Format: asset <source-path> <target-relative-dir>
asset assets/themes themes
asset assets/fonts fonts
asset assets/syntax syntax
asset README.md .
asset LICENSE .

# === File Associations (new, repeatable) ===
# Format: file-assoc <extension> <description> <mime-type>
file-assoc .zia "Zia Source File" text/x-zia
file-assoc .vproj "Viper Project" application/x-viper-project

# === Shortcuts (new) ===
shortcut-desktop on
shortcut-menu on

# === Platform Requirements (new) ===
min-os-windows 10.0
min-os-macos 11.0

# === Architectures (new, repeatable) ===
target-arch x64
target-arch arm64
```

### Manifest Data Structure

```cpp
// In project_loader.hpp, embedded within ProjectConfig
struct AssetEntry {
    std::string sourcePath;   // Relative to project root
    std::string targetPath;   // Relative to install dir
};

struct FileAssoc {
    std::string extension;    // ".zia"
    std::string description;  // "Zia Source File"
    std::string mimeType;     // "text/x-zia"
};

struct PackageConfig {
    std::string displayName;       // package-name (defaults to project name)
    std::string author;            // package-author
    std::string description;       // package-description
    std::string homepage;          // package-homepage
    std::string license;           // package-license (SPDX)
    std::string identifier;        // package-identifier (reverse DNS)
    std::string iconPath;          // package-icon (relative path to PNG)

    std::vector<AssetEntry> assets;
    std::vector<FileAssoc> fileAssociations;

    bool shortcutDesktop = false;
    bool shortcutMenu = true;

    std::string minOsWindows;      // "10.0"
    std::string minOsMacos;        // "11.0"

    std::vector<std::string> targetArchitectures; // "x64", "arm64"

    std::string postInstallScript;  // post-install directive
    std::string preUninstallScript; // pre-uninstall directive
};
```

### CLI Interface

```
viper package [project-dir-or-file]       # auto-detect host platform
viper package --target macos [project]    # .app in .zip
viper package --target linux [project]    # .deb
viper package --target windows [project]  # self-extracting .exe
viper package --target tarball [project]  # portable .tar.gz

Options:
  --arch arm64|x64       Override architecture (default: host)
  -o <path>              Output path (default: {name}-{version}-{platform}.{ext})
```

Cross-packaging is fully supported since we emit bytes ourselves.

---

## 2. Platform Deep Dives

### 2A. macOS — `.app` Bundle in `.zip`

**Recommendation: `.app`-in-`.zip`** (not `.dmg`, not `.pkg`)

**Why not .dmg?** The UDIF (Universal Disk Image Format) is a disk image containing an HFS+/APFS filesystem with partition maps, GPT headers, and filesystem B-trees. Generating one from scratch is a 6-month project. Apple's `hdiutil` is the only reliable tool.

**Why not .pkg?** The `.pkg` format is a `xar` archive (XML header + heap) containing `Bom` (Bill of Materials, undocumented binary format), `Payload` (cpio.gz), and `Scripts` (cpio.gz). The `Bom` format is undocumented.

**Why `.app`-in-`.zip`?** A `.zip` containing a `.app` bundle is universally accepted: Homebrew casks use it, direct downloads use it, macOS Finder natively handles it. Users double-click the .zip, drag the .app to /Applications or wherever they want. It's the standard indie distribution format.

**`.app` Bundle Structure:**

```
ViperIDE.app/
  Contents/
    PkgInfo                          # 8 bytes: "APPL????"
    Info.plist                       # XML property list
    MacOS/
      viperide                       # The native executable
    Resources/
      viperide.icns                  # Application icon
      themes/                        # Asset directories
      fonts/
      syntax/
      README.md
      LICENSE
```

**Info.plist** — XML template with fields from PackageConfig:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>viperide</string>
  <key>CFBundleIdentifier</key><string>org.viper-lang.viperide</string>
  <key>CFBundleName</key><string>ViperIDE</string>
  <key>CFBundleVersion</key><string>1.2.0</string>
  <key>CFBundleShortVersionString</key><string>1.2.0</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleSignature</key><string>????</string>
  <key>CFBundleIconFile</key><string>viperide.icns</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeExtensions</key><array><string>zia</string></array>
      <key>CFBundleTypeName</key><string>Zia Source File</string>
      <key>CFBundleTypeRole</key><string>Editor</string>
    </dict>
  </array>
</dict>
</plist>
```

**Code signing:** Unsigned apps trigger Gatekeeper. Users right-click → Open to bypass on first launch. For serious distribution, an Apple Developer ID ($99/year) is required for notarization — this is outside Viper's scope but we document the path. The `codesign` tool is a macOS system utility; if available, we can optionally call it.

**Quarantine attribute:** macOS sets `com.apple.quarantine` xattr on downloaded files. The right-click→Open bypass clears it. This is the standard experience for indie Mac apps.

**Size overhead:** ZIP central directory + local headers ≈ 46 bytes per entry + filenames. For a 10-file app: ~1KB overhead. The ZIP uses DEFLATE for compression.

---

### 2B. Linux — `.deb` Primary, `.tar.gz` Fallback

**Recommendation: `.deb` as primary** (covers Debian, Ubuntu, Mint, Pop!_OS — ~60% of desktop Linux). `.tar.gz` as universal fallback. Skip `.rpm` in v1 (complex indexed binary header format, not worth the effort).

**Why not AppImage?** AppImage requires creating a squashfs filesystem image (its own compression format), plus an ELF runtime stub. Feasible but lower priority than .deb.

**`.deb` Format — Fully from-scratch feasible:**

A `.deb` is an `ar` archive containing exactly three members in order:

```
!<arch>\n                           # ar magic (8 bytes)
debian-binary   [60-byte header]    # content: "2.0\n"
control.tar.gz  [60-byte header]    # gzip'd tar of control files
data.tar.gz     [60-byte header]    # gzip'd tar of installed files
```

**`ar` format** — trivially simple:

```
Global header: "!<arch>\n" (8 bytes)
Per member (60 bytes):
  ar_name[16]   "/"-terminated, space-padded
  ar_date[12]   decimal mtime, space-padded
  ar_uid[6]     "0     "
  ar_gid[6]     "0     "
  ar_mode[8]    octal, e.g. "100644  "
  ar_size[10]   decimal byte count, space-padded
  ar_fmag[2]    "`\n"
[member data]
[pad byte if size is odd]
```

**`control.tar.gz`** contains:

- `./control` — Debian package metadata:
  ```
  Package: viperide
  Version: 1.2.0
  Section: utils
  Priority: optional
  Architecture: arm64
  Maintainer: Stephen Smith
  Installed-Size: 2048
  Description: A lightweight code editor built with Viper
  Homepage: https://viper-lang.org/viperide
  ```
- `./md5sums` — MD5 hex digest + two-space + path for every file in data.tar
- `./postinst` (optional) — post-install script (mode 0755)
- `./prerm` (optional) — pre-uninstall script (mode 0755)

**`data.tar.gz`** contains installed files in FHS layout:

```
./usr/bin/viperide                                  # executable (0755)
./usr/share/viperide/themes/...                     # assets (0644)
./usr/share/viperide/fonts/...
./usr/share/applications/viperide.desktop           # menu entry
./usr/share/icons/hicolor/256x256/apps/viperide.png # icon
./usr/share/icons/hicolor/48x48/apps/viperide.png   # icon (small)
./usr/share/mime/packages/viperide.xml              # MIME types (optional)
```

**tar USTAR format** — we already have the header struct in `viperdos/user/libc/include/tar.h`:

```
512-byte header per entry:
  name[100], mode[8], uid[8], gid[8], size[12], mtime[12],
  chksum[8], typeflag[1], linkname[100], magic[6]="ustar",
  version[2]="00", uname[32], gname[32], devmajor[8],
  devminor[8], prefix[155], pad[12]
File data padded to 512-byte boundary
Archive ends with two zero-filled 512-byte blocks
Checksum: sum of all 512 header bytes treating chksum field as spaces
```

**`.desktop` file:**
```ini
[Desktop Entry]
Type=Application
Name=ViperIDE
Comment=A lightweight code editor built with Viper
Exec=/usr/bin/viperide
Icon=viperide
Categories=Development;TextEditor;
Terminal=false
```

**MIME type XML** (for file associations):
```xml
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="text/x-zia">
    <comment>Zia Source File</comment>
    <glob pattern="*.zia"/>
  </mime-type>
</mime-info>
```

**Architecture mapping:** `TargetArch::X64` → `"amd64"`, `TargetArch::ARM64` → `"arm64"` (Debian convention).

**MD5 for md5sums:** Expose `compute_md5()` in `rt_hash.c` as `rt_hash_md5_raw()` (currently `static`, one-line change).

**Size overhead:** ar headers (8 + 3×60 = 188 bytes) + tar headers (512 per file) + gzip overhead (~20 bytes per stream). For a 10-file app: ~6KB overhead.

---

### 2C. Windows — Self-Extracting `.exe`

**Recommendation: Self-extracting PE32+ with embedded ZIP payload.** Not `.msi` (COM Structured Storage is an undocumented nightmare requiring OLE32 — absolute rabbit hole, avoid entirely).

**Strategy:** Generate a minimal PE32+ executable containing:
1. PE headers pointing to a small `.text` section
2. Pre-assembled x86-64 machine code that extracts the appended ZIP payload
3. A `.rsrc` section with RT_MANIFEST for UAC elevation
4. The ZIP payload appended after the last section

**PE32+ Header Layout — Exact Byte Offsets** (verified from [Microsoft PE Format spec](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)):

```
Offset  Size  Field
──────────────────────────────────────────────────────
DOS Header (64 bytes):
0x0000    2   e_magic = "MZ" (0x5A4D)
0x0002   58   (zero-filled DOS header fields)
0x003C    4   e_lfanew = 0x00000080 (offset to PE sig)

DOS Stub (64 bytes):
0x0040   64   "This program cannot be run in DOS mode.\r\n$" + padding

PE Signature (4 bytes):
0x0080    4   "PE\0\0" (0x00004550)

COFF Header (20 bytes):
0x0084    2   Machine = 0x8664 (AMD64)
0x0086    2   NumberOfSections = 3
0x0088    4   TimeDateStamp
0x008C    4   PointerToSymbolTable = 0
0x0090    4   NumberOfSymbols = 0
0x0094    2   SizeOfOptionalHeader = 240
0x0096    2   Characteristics = 0x0022 (EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE)

Optional Header PE32+ (240 bytes):
0x0098    2   Magic = 0x020B (PE32+)
0x009A    2   LinkerVersion = 0x0E00
0x009C    4   SizeOfCode
0x00A0    4   SizeOfInitializedData
0x00A4    4   SizeOfUninitializedData = 0
0x00A8    4   AddressOfEntryPoint (RVA of .text)
0x00AC    4   BaseOfCode = 0x1000
0x00B0    8   ImageBase = 0x0000000140000000
0x00B8    4   SectionAlignment = 0x1000
0x00BC    4   FileAlignment = 0x200
0x00C0    4   MajorOperatingSystemVersion = 6, Minor = 0
0x00C4    4   MajorImageVersion = 0, Minor = 0
0x00C8    4   MajorSubsystemVersion = 6, Minor = 0
0x00CC    4   Win32VersionValue = 0
0x00D0    4   SizeOfImage (multiple of SectionAlignment)
0x00D4    4   SizeOfHeaders (multiple of FileAlignment)
0x00D8    4   CheckSum = 0 (not required for EXEs)
0x00DC    2   Subsystem = 2 (WINDOWS_GUI)
0x00DE    2   DllCharacteristics = 0x8160 (HIGH_ENTROPY_VA|DYNAMIC_BASE|NX_COMPAT|TERMINAL_SERVER_AWARE)
0x00E0    8   SizeOfStackReserve = 0x100000
0x00E8    8   SizeOfStackCommit = 0x1000
0x00F0    8   SizeOfHeapReserve = 0x100000
0x00F8    8   SizeOfHeapCommit = 0x1000
0x0100    4   LoaderFlags = 0
0x0104    4   NumberOfRvaAndSizes = 16

Data Directories (16 × 8 = 128 bytes, starting at 0x0108):
0x0108    8   [0] Export Table = 0,0
0x0110    8   [1] Import Table = RVA, Size
0x0118    8   [2] Resource Table = RVA, Size  ← RT_MANIFEST lives here
0x0120    8   [3]-[15] remaining directories = 0,0 (most unused)

Section Headers (3 × 40 = 120 bytes, starting at 0x0188):
0x0188   40   .text  (CODE | EXECUTE | READ: 0x60000020)
0x01B0   40   .rdata (INITIALIZED_DATA | READ: 0x40000040)
0x01D8   40   .rsrc  (INITIALIZED_DATA | READ: 0x40000040)

0x0200        First section data (file-aligned to 0x200)
```

**Resource Section (.rsrc) internal layout** for embedding RT_MANIFEST:
```
Resource Directory (Type level):   16 bytes header + 8 bytes entry
  → NumberOfIdEntries=1, Entry: Id=24 (RT_MANIFEST), Offset=subdirectory|0x80000000
Resource Directory (Name level):   16 bytes header + 8 bytes entry
  → NumberOfIdEntries=1, Entry: Id=1, Offset=subdirectory|0x80000000
Resource Directory (Language level): 16 bytes header + 8 bytes entry
  → NumberOfIdEntries=1, Entry: Id=0x0409 (en-US), Offset=data entry
Resource Data Entry:               16 bytes
  → RVAOfData, Size, CodePage=0, Reserved=0
[manifest XML bytes]
```

**Import Directory** for the installer stub (imports `kernel32.dll`, `user32.dll`, `shell32.dll`, `advapi32.dll`):
```
Import Directory Table: 5 entries × 20 bytes (4 DLLs + null terminator)
  Per entry: ImportLookupTableRVA(4), TimeDateStamp(4)=0, ForwarderChain(4)=-1,
             NameRVA(4), ImportAddressTableRVA(4)
Import Lookup/Address Tables: array of 8-byte entries (PE32+), high bit=0 for name
Hint/Name entries: Hint(2) + null-terminated ASCII name + pad to even
```

**What the installer stub does (x86-64 machine code, ~3KB):**

1. `GetModuleFileNameW` — get own .exe path
2. `CreateFileW` + `SetFilePointerEx` + `ReadFile` — read ZIP payload from self
3. Parse ZIP central directory (reuse the same logic from `rt_archive.c`, but as x86-64)
4. `SHGetFolderPathW(CSIDL_PROGRAM_FILES)` — get Program Files path
5. `CreateDirectoryW` — create install directory
6. For each ZIP entry: `CreateFileW` + `WriteFile` — extract files
7. `RegOpenKeyExW` + `RegSetValueExW` — write Add/Remove Programs entries
8. Create Start Menu .lnk shortcut
9. Optionally create desktop .lnk shortcut
10. Write `uninstall.log` to install dir
11. `MessageBoxW` — "Installation complete"

**The machine code challenge:** Hand-assembling ~3KB of x86-64 is tedious but bounded. Each Win32 API call follows the same pattern: `lea rcx, [rip+string]`, `mov rdx, ...`, `call [rip+IAT_entry]`. The Import Address Table in the PE imports `kernel32.dll` (file I/O, memory), `shell32.dll` (paths, shortcuts), `advapi32.dll` (registry), `user32.dll` (MessageBox).

**Realistic alternative if hand-assembly proves too painful:** Use Viper's own codegen. Write the installer logic as a Zia program, compile it to native for x64/Windows using `compileToNative()`, then append the ZIP payload. This requires a Windows cross-compilation target or running on Windows. The installer Zia program would use `Viper.IO.File`, `Viper.IO.Dir`, and FFI for registry/shortcut APIs.

**UAC Manifest** (embedded as RT_MANIFEST resource, ID=1):
```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security><requestedPrivileges>
      <requestedExecutionLevel level="requireAdministrator" uiAccess="false"/>
    </requestedPrivileges></security>
  </trustInfo>
</assembly>
```

**Resource section structure (.rsrc):**
```
Resource Directory Table (Type level):
  Entry: Type=24 (RT_MANIFEST) → subdirectory
Resource Directory Table (Name level):
  Entry: Name=1 → subdirectory
Resource Directory Table (Language level):
  Entry: Language=0x0409 (en-US) → data entry
Resource Data Entry:
  OffsetToData (RVA), Size, CodePage=0
[manifest XML bytes]
```

**Registry entries for Add/Remove Programs:**
```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\ViperIDE
  DisplayName       REG_SZ  "ViperIDE"
  DisplayVersion    REG_SZ  "1.2.0"
  Publisher         REG_SZ  "Stephen Smith"
  UninstallString   REG_SZ  "C:\Program Files\ViperIDE\uninstall.exe"
  DisplayIcon       REG_SZ  "C:\Program Files\ViperIDE\viperide.exe"
  InstallLocation   REG_SZ  "C:\Program Files\ViperIDE"
  URLInfoAbout      REG_SZ  "https://viper-lang.org/viperide"
```

**App Paths (so `viperide` works from Run dialog):**
```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\viperide.exe
  (Default)         REG_SZ  "C:\Program Files\ViperIDE\viperide.exe"
  Path              REG_SZ  "C:\Program Files\ViperIDE"
```

**File association registry:**
```
HKCR\.zia
  (Default)         REG_SZ  "ViperIDE.zia"
HKCR\ViperIDE.zia
  (Default)         REG_SZ  "Zia Source File"
HKCR\ViperIDE.zia\shell\open\command
  (Default)         REG_SZ  "\"C:\Program Files\ViperIDE\viperide.exe\" \"%1\""
```

**`.lnk` shortcut format** ([MS-SHLLINK] documented):

```
ShellLinkHeader (76 bytes):
  HeaderSize      = 0x0000004C
  LinkCLSID       = 00021401-0000-0000-C000-000000000046
  LinkFlags       = HasLinkTargetIDList | HasLinkInfo | IsUnicode | HasRelativePath
  FileAttributes  = FILE_ATTRIBUTE_NORMAL
  CreationTime, AccessTime, WriteTime = 0
  FileSize        = 0
  IconIndex       = 0
  ShowCommand     = SW_SHOWNORMAL (1)

LinkTargetIDList (variable):
  IDListSize (2 bytes)
  [CLSID items for Computer → Drive → Path]

LinkInfo (variable):
  VolumeID + LocalBasePath pointing to the .exe

StringData:
  NAME_STRING: "ViperIDE"
  RELATIVE_PATH: relative path from shortcut to .exe
  WORKING_DIR: install directory
```

~300-400 bytes per shortcut. The format is binary but fully documented. Implementation: ~300 lines.

**Uninstaller:** A second minimal PE (same PEBuilder code) that reads `uninstall.log` and reverses all operations — delete files, remove registry keys, remove directories, remove Start Menu shortcut, then delete itself. Embedded in the ZIP payload alongside the application.

**Size overhead:** PE headers + stub code + resource section ≈ 4-6KB before payload.

---

## 3. Cross-Cutting Concerns

### Compression
**Solution:** Port DEFLATE from `rt_compress.c` into self-contained C++ (`pkg::Deflate`).

The runtime's DEFLATE implementation uses a `malloc`-based `bit_writer_t` internally — only the final result wrapping calls `rt_bytes_new`. Porting requires:
1. Copy `bit_writer_t`, `deflate_stored()`, `deflate_fixed()` structs/functions (~400 lines)
2. Replace final `rt_bytes_new()` with `std::vector<uint8_t>` return
3. GZIP framing: 10-byte header + DEFLATE + 8-byte trailer (CRC-32 + size) — ~30 lines

This single compression implementation serves all three platforms:
- Windows: ZIP payload (DEFLATE method 8)
- macOS: ZIP containing .app (DEFLATE method 8)
- Linux: gzip over tar (GZIP = DEFLATE + framing)

### Checksums & Integrity
- **ZIP:** CRC-32 per entry — link `rt_crc32.o` directly (no GC dependency, pure function)
- **`.deb`:** MD5 checksums in `md5sums` — port `compute_md5()` from `rt_hash.c` (~100 lines, RFC 1321)
- **tar:** Header checksum — sum of all 512 bytes treating checksum field as spaces (`0x20`), store as 6 octal digits + space + null. Trivial.
- **No digital signatures in v1** — code signing is platform-toolchain-dependent

### File Permissions
- **tar:** Encodes Unix `mode` bits in octal, stored in `mode[8]` field. Checksum covers mode.
- **ZIP:** Unix permissions encoded in `external_file_attributes` upper 16 bits when `version_made_by` OS byte = 3 (Unix). **Critical for macOS .app bundles.**
- **Cross-platform defaults:** When generating on Windows for Linux: 0755 for executables, 0644 for data files, 040755 for directories.
- **Native mode detection:** On macOS/Linux, use `stat()` to read actual file permissions for tar entries.

### Icon Generation
Single source: high-res PNG (512×512 minimum, 1024×1024 recommended).

**PNG read/write must be ported** from `rt_pixels.c` into self-contained C++:
- Reader (~300 LOC): Parse 8-byte signature → IHDR → accumulate IDAT → inflate → apply filters (None, Sub, Up, Average, Paeth)
- Writer (~200 LOC): Build raw scanlines with filter=0 → deflate → wrap in zlib header (CMF=0x78, FLG=0x01) + Adler-32 → emit PNG chunks (IHDR + IDAT + IEND) with per-chunk CRC-32
- Bilinear resize (~80 LOC): For each destination pixel, map to source coordinates, interpolate 4 nearest source pixels with fractional weights

**macOS `.icns`** — Modern ICNS embeds PNG directly:
```
"icns" (4 bytes, magic) + total_size (4 bytes, big-endian)
Entries: type(4) + entry_size(4, big-endian, includes 8-byte header) + PNG_data
  "ic07" = 128×128 PNG
  "ic08" = 256×256 PNG
  "ic09" = 512×512 PNG
  "ic10" = 1024×1024 PNG (Retina 512)
  "ic11" = 32×32 PNG (Retina 16)
  "ic12" = 64×64 PNG (Retina 32)
  "ic13" = 256×256 PNG (Retina 128)
  "ic14" = 512×512 PNG (Retina 256)
```
Algorithm: Load source PNG → resize to each target size → encode each as PNG → write ICNS container.

**Windows `.ico`** — Modern ICO embeds PNG:
```
ICONDIR (6 bytes): reserved=0, type=1, count=N
ICONDIRENTRY[N] (16 bytes each):
  width(1), height(1), colorCount=0, reserved=0, planes=1, bitCount=32,
  sizeInBytes(4), fileOffset(4)
[PNG data for each size: 16, 24, 32, 48, 64, 128, 256]
```

**Linux:** Just scaled PNG files at standard sizes (16, 32, 48, 128, 256) placed in `hicolor/{NxN}/apps/`.

### Architecture Targeting
- `target-arch x64` / `target-arch arm64` in manifest
- Mapped to `TargetArch::X64` / `TargetArch::ARM64` in `native_compiler.hpp`
- Package metadata includes arch: `Architecture: amd64` (deb), PE Machine field (Windows)
- Default: host architecture via `detectHostArch()`
- Multi-arch: run `viper package` twice with different `--arch` flags

### Versioning & Upgrades
- **macOS:** No upgrade mechanism — user replaces .app
- **Linux `.deb`:** `dpkg -i` handles upgrades (same package name, higher version)
- **Windows:** Installer checks for existing install at same path, overwrites files, updates registry version. `uninstall.log` is replaced with new one.

### Size Budget
| Component | Overhead |
|---|---|
| ZIP central directory | ~46 bytes/entry + filenames |
| ar headers | 8 + 60 bytes/member |
| tar headers | 512 bytes/entry |
| gzip framing | ~20 bytes/stream |
| PE stub (Windows) | ~4-6 KB |
| Info.plist | ~800 bytes |
| control file | ~300 bytes |
| .desktop file | ~200 bytes |

**Total for a trivial single-file app:**
- macOS .zip: ~2KB overhead
- Linux .deb: ~8KB overhead
- Windows .exe: ~6KB overhead (before payload)

All well under the 100KB target.

---

## 4. Toolchain Integration

### Developer Workflow

```bash
# Initialize project (existing)
viper init myapp

# Edit viper.project, add package-* directives
# Develop the application

# Build and test locally (existing)
viper run myapp/
viper build myapp/ -o myapp

# Package for current platform
viper package myapp/
# → myapp-1.2.0-macos-arm64.zip

# Package for specific platforms
viper package myapp/ --target linux --arch x64
# → myapp-1.2.0-linux-amd64.deb

viper package myapp/ --target windows --arch x64
# → myapp-1.2.0-windows-x64.exe

viper package myapp/ --target tarball
# → myapp-1.2.0-linux-arm64.tar.gz
```

### Cross-Packaging

Since all format generation is byte-level, cross-packaging works:
- Generate `.deb` on macOS ✓ (ar + tar + gzip — all byte manipulation)
- Generate macOS `.zip` on Linux ✓ (ZIP writer — all byte manipulation)
- Generate Windows `.exe` on macOS ✓ (PE bytes — all byte manipulation)

The only platform-specific step is compiling the native binary. Viper's codegen currently requires the host `cc` for the target platform. Cross-compilation of the application binary itself depends on having a cross-compiler (e.g., `clang --target=x86_64-linux-gnu`), which is outside VAPS scope but worth documenting.

---

## 5. Architecture: New Library

The packaging library is **entirely self-contained C++** — no runtime (`viper_rt_*`) dependencies. It links only `rt_crc32.o` (no GC dependency) and the existing CLI infrastructure.

```
src/tools/common/packaging/
  # Core algorithms (ported from runtime, GC-free)
  PkgDeflate.hpp/.cpp          # DEFLATE compression (ported from rt_compress.c)
  PkgGzip.hpp/.cpp             # GZIP framing over DEFLATE
  PkgMD5.hpp/.cpp              # MD5 digest (ported from rt_hash.c)
  PkgPNG.hpp/.cpp              # PNG read/write (ported from rt_pixels.c)
  PkgImageResize.hpp/.cpp      # Bilinear interpolation resize

  # Archive format writers (new)
  ZipWriter.hpp/.cpp           # ZIP with Unix permissions support
  ArWriter.hpp/.cpp            # ar archive format
  TarWriter.hpp/.cpp           # USTAR tar format

  # Platform-specific generators (new)
  PlistGenerator.hpp/.cpp      # Info.plist XML generation
  DesktopEntryGenerator.hpp/.cpp  # .desktop + MIME XML
  IconGenerator.hpp/.cpp       # ICNS / ICO / multi-size PNG
  PEBuilder.hpp/.cpp           # PE32+ header + resource section
  LnkWriter.hpp/.cpp           # .lnk shortcut binary format

  # Package builders (new)
  PackageConfig.hpp            # Struct definitions
  MacOSPackageBuilder.hpp/.cpp # .app-in-zip assembly
  LinuxPackageBuilder.hpp/.cpp # .deb + .tar.gz assembly
  WindowsPackageBuilder.hpp/.cpp # Self-extracting .exe assembly

src/tools/viper/
  cmd_package.cpp              # CLI entry point
```

**CMake:**
```cmake
add_library(viper_packaging STATIC
    tools/common/packaging/PkgDeflate.cpp
    tools/common/packaging/PkgGzip.cpp
    tools/common/packaging/PkgMD5.cpp
    tools/common/packaging/PkgPNG.cpp
    tools/common/packaging/PkgImageResize.cpp
    tools/common/packaging/ZipWriter.cpp
    tools/common/packaging/ArWriter.cpp
    tools/common/packaging/TarWriter.cpp
    tools/common/packaging/PlistGenerator.cpp
    tools/common/packaging/DesktopEntryGenerator.cpp
    tools/common/packaging/IconGenerator.cpp
    tools/common/packaging/PEBuilder.cpp
    tools/common/packaging/LnkWriter.cpp
    tools/common/packaging/MacOSPackageBuilder.cpp
    tools/common/packaging/LinuxPackageBuilder.cpp
    tools/common/packaging/WindowsPackageBuilder.cpp)

# Only link CRC-32 (no GC dep) and common CLI infra
target_link_libraries(viper_packaging PRIVATE viper_common_opts)
target_sources(viper_packaging PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/core/rt_crc32.c)
```

Add `cmd_package.cpp` to `VIPER_CLI_SOURCES`, `viper_packaging` to `VIPER_CLI_LINK_LIBS`.

### Reuse Map

| Source | Ported To | Lines | Notes |
|---|---|---|---|
| `rt_compress.c` `bit_writer_t` + `deflate_stored` + `deflate_fixed` | `PkgDeflate.cpp` | ~500 | Core uses `malloc`, not GC. Replace `rt_bytes_new` with `std::vector<uint8_t>` return. |
| `rt_compress.c` `gzip_data()` | `PkgGzip.cpp` | ~30 | 10-byte header + deflate + 8-byte trailer |
| `rt_hash.c` `compute_md5()` | `PkgMD5.cpp` | ~100 | Remove `static`, change return from `rt_string` to raw `uint8_t[16]` |
| `rt_pixels.c` lines 683-895 | `PkgPNG.cpp` | ~500 | PNG reader: parse chunks, inflate IDAT, apply filters. Writer: filter 0, deflate, emit chunks. Remove all `rt_bytes_new`/`rt_obj_*` calls. |
| `rt_pixels.c` `rt_pixels_resize()` | `PkgImageResize.cpp` | ~80 | Bilinear interp. Replace `pixels_alloc()` with `std::vector<uint32_t>` |
| `rt_crc32.c` | Direct link | 0 | **No port needed** — no GC dependency, pure `uint32_t(const uint8_t*, size_t)` |
| `tar.h` `struct posix_header` | Reference for `TarWriter` | 0 | Field layout and constants only |
| `compileToNative()` | `cmd_package.cpp` | 0 | Already in CLI link chain |
| `resolveProject()` | `cmd_package.cpp` | 0 | Already in CLI link chain |

---

## 6. Risk Assessment

### Straightforward (days each)
- Manifest format extension — mechanical parser changes, follows existing `else if` pattern
- ar archive writer — 8-byte magic + 60-byte headers, simplest archive format (~80 LOC)
- tar USTAR writer — 512-byte headers with octal fields, struct layout exists in `tar.h` (~200 LOC)
- `.desktop` file generation — string template, freedesktop.org spec (~50 LOC)
- Info.plist generation — XML template with string interpolation (~100 LOC)
- ICNS generation — `"icns"` magic + size + type/size/PNG entries, big-endian only (~80 LOC)
- CRC-32 — already works, no GC dep, just link `rt_crc32.o`
- GZIP framing — 10-byte header + DEFLATE + 8-byte CRC/size trailer (~30 LOC)

### Moderate (1-2 weeks each)
- **DEFLATE port** — ~500 LOC of bit manipulation, Huffman coding, LZ77. Reference code is well-structured in `rt_compress.c`. Main risk: subtle bit-order bugs during port. Mitigation: test against known DEFLATE test vectors and roundtrip with system `gzip`/`gunzip`.
- **PNG read/write port** — ~500 LOC total. Reader handles 5 filter types, writer uses filter=0. Reference in `rt_pixels.c` lines 683-950. Risk: zlib header framing (CMF/FLG bytes, Adler-32 vs CRC-32 confusion).
- **ZIP writer with Unix permissions** — ~200 LOC. Must correctly set `version_made_by = (3<<8)|20` and `external_file_attributes = (mode<<16)`. Reference: `rt_archive.c` line 1188-1226. Risk: getting the endianness and field offsets wrong.
- **MD5 digest** — ~100 LOC. RFC 1321, well-known algorithm. Reference: `compute_md5` in `rt_hash.c`.
- **ICO generation** — ICONDIR + ICONDIRENTRY array + embedded PNG blobs (~100 LOC)
- **`.deb` assembly** — Composing ar(debian-binary + control.tar.gz + data.tar.gz). Must follow exact member ordering per dpkg validation. Risk: dpkg rejecting due to member name format (15-char limit, must end with `/` for the ar header).
- **PE32+ header emission** — Verified exact layout from Microsoft Learn docs: DOS(64) + COFF(20) + OptHeader(240) + SectionHeaders(40×N). Risk: RVA calculations, section alignment to 0x1000/0x200.
- **Bilinear image resize** — ~80 LOC. Straightforward but must handle edge cases (fractional pixel mapping, RGBA channel separation).

### Complex (2-4 weeks)
- **Windows installer x86-64 stub** — ~3KB of hand-assembled machine code. Must correctly: set up stack alignment (16-byte for Win64 ABI), use shadow space (32 bytes), call through IAT entries. Each Win32 API call is `lea rcx,[rip+str]; mov rdx,...; sub rsp,32; call [rip+IAT]; add rsp,32`. Risk: one wrong byte corrupts everything. Mitigation: build + test on actual Windows.
- **`.lnk` shortcut format** — [MS-SHLLINK] spec is public. ShellLinkHeader(76 bytes) + LinkTargetIDList(variable, CLSID-based) + LinkInfo(variable) + StringData(UTF-16LE). The ItemIDList is the hardest part — encoding the Computer→Drive→Folder→File hierarchy as nested ItemID structures. Risk: Windows silently ignoring malformed .lnk files.
- **Windows uninstaller** — Second PE reading `uninstall.log` and reversing operations. Same complexity as the installer stub.
- **PE Import Table** — The installer needs to import `kernel32.dll`, `shell32.dll`, `advapi32.dll`, `user32.dll`. The Import Directory Table has specific structure: ILT (Import Lookup Table) + IAT (Import Address Table) + hint/name entries. Risk: loader rejecting malformed import tables.

### Potential Compromises
- **Windows installer stub:** If hand-assembled x86-64 proves too painful, write the installer in C and compile with the host `cc` (Clang/MSVC). This is a ~200 line C program using Win32 APIs directly. Less zero-dep but pragmatic. Alternative: write installer as a Zia program, compile via `compileToNative()` on Windows.
- **`.rpm`:** Skip entirely in v1. Complex indexed binary header with 16-byte lead, signature block, and header block — each with their own alignment and padding rules. `.deb` + `.tar.gz` covers the vast majority of Linux users.
- **AppImage:** Skip in v1. Requires squashfs generation (block-compressed filesystem image with inode table, directory table, fragment table). Consider for v2.
- **Code signing:** Document the process but don't implement. Provide a `--sign` flag that shells out to `codesign` (macOS) / `signtool` (Windows) if available. Apple notarization requires `xcrun altool --notarize-app` + `xcrun stapler staple`.
- **`.lnk` shortcuts:** If the binary format proves too finicky, generate a PowerShell one-liner: `$ws=New-Object -ComObject WScript.Shell;$s=$ws.CreateShortcut('path.lnk');$s.TargetPath='exe';$s.Save()`. The installer stub can exec this via `CreateProcessW("powershell.exe", "-Command ...")`. Less elegant but bulletproof.
- **PNG codec:** If porting the full PNG reader/writer is too time-consuming, start with a minimal "read any PNG, write unfiltered PNG" that supports only 8-bit RGBA. Add filter support incrementally. The ICNS/ICO formats accept embedded PNG regardless of filter type used.

---

## 7. Implementation Phases

### Phase 1: Core Algorithms + Manifest + macOS `.app`-in-`.zip` (2–2.5 weeks)

**Goal:** End-to-end `viper package --target macos` producing a working `.app` bundle.

**Sub-phase 1a: Port core algorithms (1 week)**
1. Port `PkgDeflate` from `rt_compress.c` (~500 LOC) — copy `bit_writer_t`, `deflate_stored`, `deflate_fixed`; replace `rt_bytes_new` with `std::vector<uint8_t>` return
2. Implement `PkgGzip` (~30 LOC) — 10-byte header, DEFLATE, 8-byte CRC/size trailer
3. Port `PkgPNG` from `rt_pixels.c` (~500 LOC) — read: parse IHDR/IDAT/IEND, inflate, apply 5 filter types; write: filter=0, deflate, zlib framing, CRC-32 per chunk
4. Port `PkgImageResize` from `rt_pixels.c` (~80 LOC) — bilinear interpolation on RGBA pixel buffers
5. Implement `ZipWriter` with Unix permissions (~200 LOC) — `version_made_by=(3<<8)|20`, `ext_attrs=(mode<<16)`
6. **Tests:** DEFLATE roundtrip (compress → decompress matches original), GZIP roundtrip (`gunzip` on output), PNG roundtrip (write → read matches pixels), ZIP with `unzip -l` validation

**Sub-phase 1b: Manifest + macOS builder (1–1.5 weeks)**
7. Add `PackageConfig` struct to `project_loader.hpp`
8. Extend `parseManifest()` with `package-*` directive parsing (~20 new `else if` branches)
9. Create `cmd_package.cpp` (CLI entry: parse args → resolve project → compile → package)
10. Implement `PlistGenerator` (Info.plist XML template)
11. Implement `MacOSPackageBuilder` (.app structure in ZIP)
12. Tests: verify ZIP contains correct .app paths, Info.plist valid XML, PkgInfo = "APPL????", executable has Unix 0755 permission in ZIP external attributes

**Files modified:**
- `src/tools/common/project_loader.hpp` — add `PackageConfig` struct
- `src/tools/common/project_loader.cpp` — parse `package-*` directives
- `src/tools/viper/cli.hpp` — declare `cmdPackage`
- `src/tools/viper/main.cpp` — add `"package"` dispatch
- `src/CMakeLists.txt` — add `viper_packaging` library + `cmd_package.cpp`

**Files created:**
- `src/tools/common/packaging/PkgDeflate.hpp/.cpp`
- `src/tools/common/packaging/PkgGzip.hpp/.cpp`
- `src/tools/common/packaging/PkgPNG.hpp/.cpp`
- `src/tools/common/packaging/PkgImageResize.hpp/.cpp`
- `src/tools/common/packaging/ZipWriter.hpp/.cpp`
- `src/tools/common/packaging/PackageConfig.hpp`
- `src/tools/common/packaging/PlistGenerator.hpp/.cpp`
- `src/tools/common/packaging/MacOSPackageBuilder.hpp/.cpp`
- `src/tools/viper/cmd_package.cpp`
- `src/tests/tools/test_pkg_deflate.cpp`
- `src/tests/tools/test_pkg_zip.cpp`
- `src/tests/tools/test_package_macos.cpp`

### Phase 2: Linux `.deb` + `.tar.gz` (1.5–2 weeks)

**Goal:** `viper package --target linux` producing a valid `.deb`.

1. Port `PkgMD5` from `rt_hash.c` (~100 LOC) — `void pkg_md5(const uint8_t*, size_t, uint8_t[16])`
2. Implement `ArWriter` (~80 LOC) — 8-byte magic, 60-byte headers, odd-size padding
3. Implement `TarWriter` (~200 LOC) — USTAR headers, octal fields, checksum calculation, 512-byte padding, two zero end blocks
4. Implement `DesktopEntryGenerator` (~50 LOC) — `.desktop` + MIME XML templates
5. Implement `LinuxPackageBuilder` — .deb: ar(debian-binary + control.tar.gz + data.tar.gz); .tar.gz: FHS paths
6. Tests: ar magic + member headers, tar checksum + padding, dpkg-deb can read our .deb, md5sums match

**Files created:**
- `src/tools/common/packaging/PkgMD5.hpp/.cpp`
- `src/tools/common/packaging/ArWriter.hpp/.cpp`
- `src/tools/common/packaging/TarWriter.hpp/.cpp`
- `src/tools/common/packaging/DesktopEntryGenerator.hpp/.cpp`
- `src/tools/common/packaging/LinuxPackageBuilder.hpp/.cpp`
- `src/tests/tools/test_pkg_ar.cpp`
- `src/tests/tools/test_pkg_tar.cpp`
- `src/tests/tools/test_package_linux.cpp`

### Phase 3: Icon Generation (1–1.5 weeks)

**Goal:** Auto-generate `.icns`, `.ico`, and multi-size PNGs from a single source PNG.

1. Implement ICNS writer — `"icns"` big-endian magic/size + type/size/PNG entries (~80 LOC)
2. Implement ICO writer — ICONDIR(6) + ICONDIRENTRY[N](16 each) + PNG data (~100 LOC)
3. Implement multi-size PNG generator — resize to 16,32,48,64,128,256,512,1024 → encode each as PNG
4. Integrate into MacOS/Linux/Windows builders
5. Tests: ICNS validated by `file` command, ICO validated by `file` command, `iconutil` on macOS

**Files created:**
- `src/tools/common/packaging/IconGenerator.hpp/.cpp`
- `src/tests/tools/test_icon_generator.cpp`

### Phase 4: Windows Self-Extracting `.exe` (3–5 weeks)

**Goal:** `viper package --target windows` producing a working installer .exe.

**Sub-phase 4a: PE32+ emission (1.5–2 weeks)**
1. Implement `PEBuilder` — emit PE32+ with exact layout:
   - DOS Header (64 bytes): `e_magic="MZ"`, `e_lfanew=0x80`
   - DOS Stub (64 bytes)
   - PE Signature (4 bytes): `"PE\0\0"` at offset 0x80
   - COFF Header (20 bytes): `Machine=0x8664`, `SizeOfOptionalHeader=240`
   - Optional Header PE32+ (240 bytes): `Magic=0x020B`, `ImageBase=0x140000000`, `SectionAlignment=0x1000`, `FileAlignment=0x200`, `Subsystem=2` (GUI), `NumberOfRvaAndSizes=16`, `DataDirectory[2]` = resource RVA
   - Section Headers (3 × 40 = 120 bytes): `.text`, `.rdata`, `.rsrc`
   - Resource section: Directory(16 bytes) → Entry(8 bytes per) → Data Entry(16 bytes) → manifest XML
2. Tests: `file` command identifies as PE32+, validate with a PE parser

**Sub-phase 4b: Installer stub + .lnk + assembly (1.5–3 weeks)**
3. Implement x86-64 installer stub machine code or C installer program
4. Implement `LnkWriter` — ShellLinkHeader(76) + LinkTargetIDList + LinkInfo + StringData(UTF-16LE)
5. Implement `WindowsPackageBuilder` — assemble PE + append ZIP payload
6. Generate uninstaller PE (same PEBuilder, different stub)
7. Tests: PE header validation, end-to-end install on Windows

**Files created:**
- `src/tools/common/packaging/PEBuilder.hpp/.cpp`
- `src/tools/common/packaging/LnkWriter.hpp/.cpp`
- `src/tools/common/packaging/WindowsPackageBuilder.hpp/.cpp`
- `src/tests/tools/test_pe_builder.cpp`
- `src/tests/tools/test_lnk_writer.cpp`

### Phase 5: File Associations + Polish (1–1.5 weeks)

1. MIME type XML generation for Linux (`/usr/share/mime/packages/{name}.xml`)
2. `postinst` script: `update-mime-database /usr/share/mime; update-desktop-database`
3. `prerm` script: cleanup
4. File association registry entries for Windows (in Phase 4 stub)
5. `CFBundleDocumentTypes` in Info.plist (in Phase 1 template)

### Phase 6: Uninstaller + Documentation (1 week)

1. Windows uninstaller PE (log-based reversal of files, registry, shortcuts)
2. `post-install` / `pre-uninstall` manifest hooks → .deb control scripts
3. `viper help package` documentation
4. Example package manifests for console app, GUI app, game

---

## 8. Format Recommendation Summary

| Platform | Format | Rationale |
|---|---|---|
| macOS | `.app` in `.zip` | Simple, universal, native Finder support. `.dmg` is an undocumented nightmare. `.pkg` requires undocumented BOM format. |
| Linux (primary) | `.deb` | Covers ~60% desktop Linux. Format is fully documented: ar + tar + gzip. All primitives exist or are trivial. |
| Linux (fallback) | `.tar.gz` | Universal, works everywhere, zero special tooling needed. |
| Windows | Self-extracting `.exe` | `.msi` is COM Structured Storage — absolute rabbit hole. Self-extracting PE with embedded ZIP is well-documented and bounded. |
| Linux (v2) | AppImage | Skip in v1. Revisit once squashfs emission is feasible. |
| Linux (skip) | `.rpm` | Skip. Complex indexed binary header format not worth from-scratch effort. |

---

## 9. Verification Plan

### Phase 1 Verification
```bash
viper package demos/zia/viperide/ --target macos -o test.zip
unzip -l test.zip                    # Verify .app structure
plutil -lint ViperIDE.app/Contents/Info.plist  # Validate plist (macOS)
open ViperIDE.app                    # Launch test (macOS)
```

### Phase 2 Verification
```bash
viper package demos/zia/viperide/ --target linux -o test.deb
ar t test.deb                        # Verify: debian-binary, control.tar.gz, data.tar.gz
dpkg-deb --info test.deb             # Validate control metadata
dpkg-deb --contents test.deb         # List installed files
sudo dpkg -i test.deb                # Install test (Linux)
which viperide                       # Verify binary in PATH
```

### Phase 3 Verification
```bash
# Generate icons from source
file output.icns                     # Verify ICNS format
file output.ico                      # Verify ICO format
iconutil -c iconset output.icns      # Validate ICNS (macOS)
```

### Phase 4 Verification
```bash
viper package demos/zia/viperide/ --target windows -o test.exe
file test.exe                        # Verify PE32+ executable
# On Windows: double-click test.exe → verify UAC prompt, installation, Start Menu
```

### Automated Tests
```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "package"
```

---

## 10. Reference Sources

- [PE Format — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format) — Authoritative PE32+ specification with exact header offsets, section alignment rules, resource section structure
- [MS-SHLLINK — Shell Link Binary Format](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-shllink/16cb4ca1-9339-4d0c-a68d-bf1d6cc0f943) — .lnk shortcut format specification
- [MS-SHLLINK PDF](https://winprotocoldoc.blob.core.windows.net/productionwindowsarchives/MS-SHLLINK/%5BMS-SHLLINK%5D.pdf) — Complete spec with ItemIDList structure details
- [deb(5) — Debian binary package format](https://man7.org/linux/man-pages/man5/deb.5.html) — Authoritative .deb format: ar archive, member ordering, tar format requirements
- [deb(5) — dpkg-dev manpage](https://manpages.debian.org/testing/dpkg-dev/deb.5.en.html) — Supported compression formats, tar variants accepted by dpkg
- [PE Headers Explained — CybB0rg](https://www.cybb0rg.com/2024/07/20/pe-headers-and-sections-explained/) — Practical walkthrough of PE section alignment
- [OSDev PE Wiki](https://wiki.osdev.org/PE) — Minimal PE generation reference
- [Kaitai Struct .lnk spec](https://formats.kaitai.io/windows_lnk_file/index.html) — Machine-readable .lnk format specification
- [Making a Mac Application Bundle manually](https://tmewett.com/making-macos-bundle-info-plist/) — Practical guide to Info.plist and bundle structure
- [Debian Packaging from First Principles](https://mikecoats.com/debian-packaging-first-principles-part-1-simple/) — Building .deb with ar directly

### Internal Codebase References

| File | Lines | Relevance |
|---|---|---|
| `src/runtime/io/rt_compress.c` | 1-1072 | DEFLATE algorithm to port (bit_writer, LZ77, Huffman) |
| `src/runtime/io/rt_archive.c` | 1188-1226 | ZIP central directory layout reference |
| `src/runtime/core/rt_crc32.c` | 78-88 | CRC-32 (no GC dep, link directly) |
| `src/runtime/text/rt_hash.c` | static `compute_md5` | MD5 algorithm to port |
| `src/runtime/graphics/rt_pixels.c` | 683-950 | PNG read/write to port |
| `src/runtime/graphics/rt_pixels.c` | 1681-1710 | Bilinear resize to port |
| `src/tools/common/project_loader.cpp` | 350-450 | Manifest parser to extend |
| `src/tools/viper/main.cpp` | 350-400 | CLI dispatch pattern |
| `src/tools/viper/cmd_run.cpp` | 407-503 | Build pipeline pattern (resolve → compile → native) |
| `viperdos/user/libc/include/tar.h` | 117-135 | `struct posix_header` layout |
| `src/CMakeLists.txt` | 376-394 | CLI link libraries to extend |
