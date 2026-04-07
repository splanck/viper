# Phase 6: Verification and Testing

## Goal

Comprehensive structural validation and end-to-end testing for all installer formats.

## New Verification Functions

### `src/tools/common/packaging/PkgVerify.hpp` — Extensions

```cpp
/// Verify xar archive structure (magic, TOC decompression, heap integrity).
bool verifyXar(const std::vector<uint8_t>& data, std::ostream& err);

/// Verify RPM structure (lead magic, header magic, signature tags, payload).
bool verifyRpm(const std::vector<uint8_t>& data, std::ostream& err);

/// Verify macOS .pkg (xar containing Distribution + viper.pkg/Payload).
bool verifyMacOSPkg(const std::vector<uint8_t>& data, std::ostream& err);

/// Verify Viper Windows installer (PE + ZIP overlay + meta/manifest.ini).
bool verifyViperWindowsInstaller(const std::vector<uint8_t>& data, std::ostream& err);
```

## Unit Tests

### `src/tests/unit/packaging/test_cpio_writer.cpp`
- `NewcMagicAndTrailer` — verify "070701" magic on each entry, TRAILER!!! at end
- `FileEntryPadding` — verify 4-byte alignment of name and data
- `DirectoryEntry` — verify mode bits and zero filesize
- `SymlinkEntry` — verify target stored as file data

### `src/tests/unit/packaging/test_xar_writer.cpp`
- `MagicAndHeader` — verify 0x78617221 magic, header size 28
- `TOCDecompression` — verify zlib-decompressed TOC is valid XML
- `HeapBlobIntegrity` — verify SHA-1 checksums match
- `MultiFileArchive` — verify multiple entries with correct offsets

### `src/tests/unit/packaging/test_pkg_zlib.cpp`
- `RoundTrip` — compress then decompress, verify match
- `HeaderBytes` — verify 0x78 0x9C header
- `Adler32Known` — verify known Adler-32 values

### `src/tests/unit/packaging/test_pkg_sha1.cpp`
- `EmptyString` — SHA-1 of "" matches da39a3ee...
- `ABC` — SHA-1 of "abc" matches a9993e36...
- `LongInput` — SHA-1 of 1M zeros matches known value

### `src/tests/unit/packaging/test_pkg_sha256.cpp`
- `EmptyString` — SHA-256 of "" matches e3b0c442...
- `ABC` — SHA-256 of "abc" matches ba7816bf...

### `src/tests/unit/packaging/test_rpm_writer.cpp`
- `LeadMagic` — verify 0xEDABEEDB at offset 0
- `HeaderMagic` — verify 0x8EADE801 at signature and main header starts
- `RequiredTags` — verify NAME, VERSION, ARCH tags present in main header
- `PayloadIsGzippedCpio` — decompress payload, verify cpio magic "070701"
- `FileListRoundTrip` — verify BASENAMES+DIRNAMES+DIRINDEXES reconstruct original paths

### `src/tests/unit/packaging/test_platform_install_config.cpp`
- `GatherMockTree` — create temp install tree, verify manifest completeness
- `WindowsPathMapping` — verify bin/viper maps to bin\viper.exe
- `MacOSPathMapping` — verify bin/viper maps to lib/viper/bin/viper
- `LinuxPathMapping` — verify bin/viper maps to usr/bin/viper
- `TotalSizeBytes` — verify sum across all categories

### `src/tests/unit/packaging/test_macos_pkg_builder.cpp`
- `BuildMinimalPkg` — build .pkg from 3-file manifest, verify xar structure
- `DistributionXMLPresent` — verify Distribution file in xar TOC
- `PayloadIsCpio` — extract and decompress Payload, verify cpio entries
- `PostinstallPresent` — verify Scripts/postinstall in xar

## CI Integration Tests

### macOS CI
```bash
# Build .pkg, verify with Apple tools
pkgutil --check-signature viper-*.pkg
pkgutil --payload-files viper-*.pkg | grep viper
# Install to temp root
sudo installer -pkg viper-*.pkg -target / -dumplog
# Verify installed files
test -x /usr/local/viper/bin/viper
test -L /usr/local/bin/viper
viper --version
```

### Linux CI
```bash
# .deb verification
dpkg-deb -I viper-*.deb
dpkg-deb -c viper-*.deb | grep usr/bin/viper
sudo dpkg -i viper-*.deb
viper --version
sudo dpkg -r viper

# .rpm verification
rpm -qip viper-*.rpm
rpm -qlp viper-*.rpm | grep usr/bin/viper
rpm -K viper-*.rpm  # verify signatures
sudo rpm -i viper-*.rpm
viper --version
sudo rpm -e viper
```

### Windows CI
```cmd
REM Silent install
viper-installer.exe /S /D=C:\Temp\ViperTest
dir C:\Temp\ViperTest\bin\viper.exe
C:\Temp\ViperTest\bin\viper.exe --version
REM Verify PATH
echo %PATH% | findstr /i "ViperTest"
REM Uninstall
C:\Temp\ViperTest\uninstall.exe /S
if not exist C:\Temp\ViperTest\bin\viper.exe echo UNINSTALL OK
```

## Complete New File Inventory

```
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

src/tests/unit/packaging/
  test_cpio_writer.cpp               Phase 6
  test_xar_writer.cpp                Phase 6
  test_pkg_zlib.cpp                  Phase 6
  test_pkg_sha1.cpp                  Phase 6
  test_pkg_sha256.cpp                Phase 6
  test_rpm_writer.cpp                Phase 6
  test_platform_install_config.cpp   Phase 6
  test_macos_pkg_builder.cpp         Phase 6
```

Total: 31 new files
