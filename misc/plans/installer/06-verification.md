# Phase 6: Verification and Testing

## Goal

Comprehensive structural validation and end-to-end testing for all installer formats. Extend the existing `test_packaging.cpp` (71 tests) with new test cases.

## New Verification Functions

### `src/tools/common/packaging/PkgVerify.hpp` — Extensions

```cpp
/// Verify xar archive structure (magic, TOC decompression, heap integrity).
bool verifyXar(const std::vector<uint8_t>& data, std::ostream& err);

/// Verify RPM structure (lead magic, header magics, signature/8-byte alignment, payload).
bool verifyRpm(const std::vector<uint8_t>& data, std::ostream& err);

/// Verify macOS .pkg (xar containing Distribution + component pkg with Payload).
bool verifyMacOSPkg(const std::vector<uint8_t>& data, std::ostream& err);

/// Verify Viper Windows installer (PE + ZIP overlay + meta/manifest.ini + meta/license.txt).
bool verifyViperWindowsInstaller(const std::vector<uint8_t>& data, std::ostream& err);

/// Verify cpio SVR4/newc archive structure (magic per entry, TRAILER!!!).
bool verifyCpio(const std::vector<uint8_t>& data, std::ostream& err);
```

## Unit Tests

All added to `src/tests/unit/test_packaging.cpp` (extending the existing 71 tests).

### CpioWriter Tests
- `TEST(Cpio, NewcMagicAndTrailer)` — verify "070701" magic on each entry, TRAILER!!! at end
- `TEST(Cpio, FileEntryPadding)` — verify 4-byte alignment of name and data regions
- `TEST(Cpio, DirectoryEntry)` — verify mode bits (040755) and zero filesize
- `TEST(Cpio, SymlinkEntry)` — verify mode (0120777), target stored as data, filesize = target length
- `TEST(Cpio, ExecutableBit)` — verify 0100755 mode preserved for executables
- `TEST(Cpio, ParentBeforeChild)` — verify directories sorted before their children
- `TEST(Cpio, EmptyArchive)` — verify just TRAILER!!! entry

### XarWriter Tests
- `TEST(Xar, MagicAndHeader)` — verify 0x78617221 magic, header size 28, version 1, cksum_algo 1
- `TEST(Xar, HeaderBigEndian)` — verify all header fields are big-endian
- `TEST(Xar, TOCDecompression)` — zlib-decompress TOC, verify valid XML with `<xar><toc>` root
- `TEST(Xar, HeapChecksumPresent)` — verify first 20 bytes of heap are SHA-1 of uncompressed TOC
- `TEST(Xar, FileDataIntegrity)` — verify archived-checksum/extracted-checksum match actual data
- `TEST(Xar, MultiFileArchive)` — verify 3+ files with correct heap offsets and sizes
- `TEST(Xar, NestedDirectories)` — verify directory structure in TOC XML
- `TEST(Xar, EmptyFile)` — verify zero-length file entry handled correctly

### PkgZlib Tests
- `TEST(Zlib, RoundTrip)` — compress then decompress, verify data matches
- `TEST(Zlib, HeaderBytes)` — verify CMF=0x78, FLG=0x9C for default compression
- `TEST(Zlib, Adler32Empty)` — verify adler32("") = 1
- `TEST(Zlib, Adler32ABC)` — verify adler32("abc") = 0x024d0127
- `TEST(Zlib, Adler32Long)` — verify adler32 on 1MB buffer against reference
- `TEST(Zlib, TrailingChecksum)` — verify last 4 bytes are big-endian Adler-32

### PkgSHA1 Tests
- `TEST(SHA1, Empty)` — SHA-1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
- `TEST(SHA1, ABC)` — SHA-1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
- `TEST(SHA1, NIST448)` — SHA-1("abcdbcde...nopq") = 84983e441c3bd26ebaae4aa1f95129e5e54670f1
- `TEST(SHA1, MillionA)` — SHA-1(1M × 'a') = 34aa973cd4c4daa4f61eeb2bdbad27316534016f

### PkgSHA256 Tests
- `TEST(SHA256, Empty)` — SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
- `TEST(SHA256, ABC)` — SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
- `TEST(SHA256, Long)` — SHA-256("abcdbcde...nopq") = 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1

### RpmWriter Tests
- `TEST(Rpm, LeadMagic)` — verify 0xEDABEEDB at offset 0
- `TEST(Rpm, LeadArchnum)` — verify archnum 1 for x86_64, 12 for aarch64
- `TEST(Rpm, SigHeaderMagic)` — verify 0x8EADE801 at offset 96
- `TEST(Rpm, SigHeaderPadding)` — verify sig header padded to 8-byte boundary before main header
- `TEST(Rpm, MainHeaderMagic)` — verify 0x8EADE801 at correct offset after padded sig
- `TEST(Rpm, RequiredTags)` — verify NAME, VERSION, RELEASE, ARCH, OS, PAYLOADFORMAT tags in main header
- `TEST(Rpm, FilePathReconstruction)` — verify BASENAMES+DIRNAMES+DIRINDEXES reconstruct paths
- `TEST(Rpm, FileDigests)` — verify FILEMD5S matches actual MD5 of file contents
- `TEST(Rpm, PayloadIsCpio)` — gunzip payload, verify cpio magic "070701"
- `TEST(Rpm, DataStoreAlignment)` — verify INT16 on 2-byte, INT32 on 4-byte boundaries
- `TEST(Rpm, SigMD5Matches)` — verify RPMSIGTAG_MD5 matches computed MD5 of header+payload
- `TEST(Rpm, SigSHA256Matches)` — verify RPMSIGTAG_SHA256 matches computed SHA-256 of main header

### PlatformInstallConfig Tests
- `TEST(PlatformInstallConfig, GatherMockTree)` — create temp install tree, verify manifest completeness
- `TEST(PlatformInstallConfig, WindowsPathMapping)` — verify bin/viper → bin\viper.exe
- `TEST(PlatformInstallConfig, MacOSPathMapping)` — verify bin/viper → lib/viper/bin/viper
- `TEST(PlatformInstallConfig, LinuxPathMapping)` — verify bin/viper → usr/bin/viper
- `TEST(PlatformInstallConfig, TotalSizeBytes)` — verify sum across all categories
- `TEST(PlatformInstallConfig, FileAssociations)` — verify .zia/.bas/.il entries populated
- `TEST(PlatformInstallConfig, VersionParsing)` — verify version from buildmeta/VERSION
- `TEST(PlatformInstallConfig, AllFilesFlattened)` — verify allFiles() count equals sum of categories
- `TEST(PlatformInstallConfig, EmptyPrefixFails)` — verify error on empty/missing install prefix

### MacOSPkgBuilder Tests
- `TEST(MacOSPkg, BuildMinimal)` — build .pkg from 3-file manifest, verify xar structure
- `TEST(MacOSPkg, DistributionPresent)` — verify Distribution file in xar TOC
- `TEST(MacOSPkg, PayloadIsCpio)` — extract and gunzip Payload, verify cpio entries
- `TEST(MacOSPkg, PostinstallPresent)` — verify Scripts/postinstall in xar
- `TEST(MacOSPkg, PostinstallExecutable)` — verify postinstall has correct cpio mode (0100755)
- `TEST(MacOSPkg, LicensePresent)` — verify Resources/en.lproj/license.txt in xar
- `TEST(MacOSPkg, PackageInfoValid)` — verify PackageInfo XML contains identifier, version, installKBytes

### ViperWindowsPackageBuilder Tests
- `TEST(ViperWindows, BuildWithFallbackStub)` — cross-platform build, verify PE+ZIP structure
- `TEST(ViperWindows, ManifestInZip)` — verify meta/manifest.ini present in ZIP overlay
- `TEST(ViperWindows, LicenseInZip)` — verify meta/license.txt present
- `TEST(ViperWindows, UninstallerInZip)` — verify app/uninstall.exe present (when provided)
- `TEST(ViperWindows, BinariesInZip)` — verify app/bin/*.exe entries for all tools
- `TEST(ViperWindows, DPIManifest)` — verify RT_MANIFEST contains dpiAware element

### ViperLinuxPackageBuilder Tests
- `TEST(ViperLinuxDeb, ControlFields)` — verify Package, Version, Architecture, Section, Depends
- `TEST(ViperLinuxDeb, PostinstMandb)` — verify postinst script contains mandb call
- `TEST(ViperLinuxDeb, PostrmPresent)` — verify postrm script present
- `TEST(ViperLinuxDeb, MimeXml)` — verify MIME type XML for .zia/.bas/.il
- `TEST(ViperLinuxDeb, DataContainsBinaries)` — verify data.tar.gz contains usr/bin/viper
- `TEST(ViperLinuxDeb, ManPagesPresent)` — verify usr/share/man/man1/*.1 entries

### Verification Function Tests
- `TEST(Verify, XarValid)` — valid xar passes verifyXar
- `TEST(Verify, XarInvalidMagic)` — bad magic rejected
- `TEST(Verify, RpmValid)` — valid RPM passes verifyRpm
- `TEST(Verify, RpmInvalidLead)` — bad lead magic rejected
- `TEST(Verify, RpmMissingPadding)` — missing 8-byte sig padding detected
- `TEST(Verify, MacOSPkgValid)` — valid .pkg passes verifyMacOSPkg
- `TEST(Verify, CpioValid)` — valid cpio passes verifyCpio
- `TEST(Verify, CpioInvalidMagic)` — bad magic rejected

## CI Integration Tests

### macOS CI
```bash
# Build .pkg
./scripts/build_installer.sh build macos

# Structural verification (runs as part of build)
# Additional Apple tool verification:
pkgutil --check-signature build/installers/viper-*.pkg
pkgutil --payload-files build/installers/viper-*.pkg | head -20

# Full install test (requires sudo in CI)
sudo installer -pkg build/installers/viper-*.pkg -target / -dumplog
test -x /usr/local/viper/bin/viper
test -L /usr/local/bin/viper
readlink /usr/local/bin/viper  # should be /usr/local/viper/bin/viper
/usr/local/bin/viper --version

# Verify man pages accessible
man -w viper  # should find the man page

# Cleanup
sudo rm -rf /usr/local/viper
sudo rm -f /usr/local/bin/{viper,zia,vbasic,ilrun,il-verify,il-dis,zia-server,vbasic-server}
```

### Linux CI
```bash
# Build .deb and .rpm
./scripts/build_installer.sh build linux-deb
./scripts/build_installer.sh build linux-rpm

# .deb verification
dpkg-deb -I build/installers/viper_*.deb
dpkg-deb -c build/installers/viper_*.deb | grep usr/bin/viper
dpkg-deb -c build/installers/viper_*.deb | grep usr/share/man
sudo dpkg -i build/installers/viper_*.deb
viper --version
which viper  # should be /usr/bin/viper
man -w viper  # should find the man page
sudo dpkg -r viper

# .rpm verification (on RPM-based CI, or with rpm tool)
rpm -qip build/installers/viper-*.rpm
rpm -qlp build/installers/viper-*.rpm | grep usr/bin/viper
rpm -qlp build/installers/viper-*.rpm | grep usr/share/man
rpm -K build/installers/viper-*.rpm  # verify MD5/SHA256 digests
# If RPM-based CI:
# sudo rpm -i build/installers/viper-*.rpm
# viper --version
# sudo rpm -e viper
```

### Windows CI
```cmd
REM Build installer
scripts\build_installer.cmd build windows

REM Silent install test
build\installers\viper-*.exe /S /D=C:\Temp\ViperTest
if not exist "C:\Temp\ViperTest\bin\viper.exe" (echo FAIL: viper.exe missing & exit /b 1)
"C:\Temp\ViperTest\bin\viper.exe" --version

REM Verify PATH modification
reg query "HKCU\Environment" /v Path | findstr /i "ViperTest"

REM Verify uninstall registry
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Viper" /v DisplayVersion

REM Verify file association (if opted in)
reg query "HKCR\.zia" /ve

REM Verify Start Menu shortcut
if not exist "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Viper" (echo WARN: Start Menu missing)

REM Uninstall test
"C:\Temp\ViperTest\uninstall.exe" /S
timeout /t 2
if exist "C:\Temp\ViperTest\bin\viper.exe" (echo FAIL: uninstall incomplete & exit /b 1)
echo UNINSTALL OK

REM Verify cleanup
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Viper" 2>nul && (echo FAIL: registry not cleaned)
reg query "HKCR\.zia" 2>nul && (echo FAIL: file assoc not cleaned)
```

## Complete New File Inventory

```
Source files (25):
  src/tools/common/packaging/
    PlatformInstallConfig.hpp          Phase 1
    PlatformInstallConfig.cpp          Phase 1
    CpioWriter.hpp                     Phase 3
    CpioWriter.cpp                     Phase 3
    XarWriter.hpp                      Phase 3
    XarWriter.cpp                      Phase 3
    PkgZlib.hpp                        Phase 3
    PkgZlib.cpp                        Phase 3
    PkgSHA1.hpp                        Phase 3
    PkgSHA1.cpp                        Phase 3
    PkgSHA256.hpp                      Phase 4
    PkgSHA256.cpp                      Phase 4
    RpmWriter.hpp                      Phase 4
    RpmWriter.cpp                      Phase 4
    MacOSPkgBuilder.hpp                Phase 3
    MacOSPkgBuilder.cpp                Phase 3
    ViperWindowsPackageBuilder.hpp     Phase 2
    ViperWindowsPackageBuilder.cpp     Phase 2
    ViperLinuxPackageBuilder.hpp       Phase 4
    ViperLinuxPackageBuilder.cpp       Phase 4
    win32/ViperInstaller.cpp           Phase 2
    win32/ViperUninstaller.cpp         Phase 2

  src/tools/viper/
    cmd_install_package.cpp            Phase 5

  scripts/
    build_installer.sh                 Phase 5
    build_installer.cmd                Phase 5

Modified files (5):
  src/codegen/common/LinkerSupport.cpp   Phase 0 (runtime lib discovery)
  src/codegen/common/LinkerSupport.hpp   Phase 0
  src/CMakeLists.txt                     Phases 1-5
  src/tools/viper/main.cpp               Phase 5
  src/tools/common/packaging/PkgVerify.hpp/cpp  Phase 6
  src/tools/common/packaging/PEBuilder.cpp      Phase 2 (DPI manifest)
  src/tests/unit/test_packaging.cpp             Phase 6

Test additions (~70 new test cases):
  Added to existing src/tests/unit/test_packaging.cpp
```

Total: 25 new files + ~7 modified files, ~5050 LOC new code, ~70 new tests
