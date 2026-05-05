//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/InstallerStub.cpp
// Purpose: Generate complete Windows installer and uninstaller x86-64 stubs
//          using the InstallerStubGen instruction emitter.
//
// Key invariants:
//   - Windows x64 ABI: 32-byte shadow space, RCX/RDX/R8/R9 for args.
//   - Stack aligned to 16 bytes before every call instruction.
//   - Windows package overlays use stored ZIP entries only; file contents are
//     copied directly from precomputed local-data offsets.
//   - All Win32 API calls go through the IAT; no external tools are invoked.
//
// Ownership/Lifetime:
//   - Pure functions. No state.
//
// Links: InstallerStub.hpp, InstallerStubGen.hpp, WindowsPackageBuilder.hpp
//
//===----------------------------------------------------------------------===//

#include "InstallerStub.hpp"
#include "InstallerStubGen.hpp"
#include "PkgUtils.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace viper::pkg {

namespace {

constexpr uint32_t kSectionAlignment = 0x1000;
constexpr uint32_t kTextRVA = 0x1000;
constexpr uint32_t kRdataBaseRVA = 0x2000;

constexpr uint32_t kMaxPathChars = 32768;

constexpr uint32_t kGenericRead = 0x80000000u;
constexpr uint32_t kGenericWrite = 0x40000000u;
constexpr uint32_t kFileShareRead = 1u;
constexpr uint32_t kOpenExisting = 3u;
constexpr uint32_t kCreateAlways = 2u;
constexpr uint32_t kFileAttributeNormal = 0x80u;
constexpr uint32_t kMoveFileDelayUntilReboot = 0x00000004u;
constexpr uint32_t kRegSz = 1u;
constexpr uint32_t kErrorAlreadyExists = 183u;
constexpr uint64_t kHkeyLocalMachine = 0x80000002ull;

constexpr uint32_t kCsidlProgramFiles = 0x0026u;
constexpr uint32_t kCsidlCommonDesktopDirectory = 0x0019u;
constexpr uint32_t kCsidlCommonPrograms = 0x0017u;

// Stack frame layout (negative offsets from RBP)
constexpr int32_t kSelfPathOff = -0x10000;
constexpr int32_t kInstallPathOff = -0x20000;
constexpr int32_t kDesktopPathOff = -0x30000;
constexpr int32_t kMenuPathOff = -0x40000;
constexpr int32_t kTempPathOff = -0x50000;
constexpr int32_t kUninstallPathOff = -0x60000;
constexpr int32_t kHFileOff = -0x60008;
constexpr int32_t kHOutOff = -0x60010;
constexpr int32_t kPFileBufOff = -0x60018;
constexpr int32_t kBytesReadOff = -0x60020;
constexpr int32_t kBytesWrittenOff = -0x60028;
constexpr int32_t kRegKeyOff = -0x60030;
constexpr uint32_t kFrameSize = 0x60050;

enum InstallerIAT : uint32_t {
    kI_ExitProcess = 0,
    kI_GetModuleFileNameW = 1,
    kI_CreateFileW = 2,
    kI_ReadFile = 3,
    kI_WriteFile = 4,
    kI_CloseHandle = 5,
    kI_LocalAlloc = 6,
    kI_LocalFree = 7,
    kI_SetFilePointer = 8,
    kI_CreateDirectoryW = 9,
    kI_DeleteFileW = 10,
    kI_RemoveDirectoryW = 11,
    kI_lstrcpyW = 12,
    kI_lstrcatW = 13,
    kI_lstrlenW = 14,
    kI_GetLastError = 15,
    kI_SHGetFolderPathW = 16,
    kI_RegCreateKeyW = 17,
    kI_RegSetValueExW = 18,
    kI_RegCloseKey = 19,
    kI_RegOpenKeyW = 20,
    kI_RegDeleteValueW = 21,
    kI_RegDeleteKeyW = 22,
    kI_MessageBoxW = 23,
    kI_RtlComputeCrc32 = 24,
};

enum UninstallerIAT : uint32_t {
    kU_ExitProcess = 0,
    kU_GetModuleFileNameW = 1,
    kU_DeleteFileW = 2,
    kU_RemoveDirectoryW = 3,
    kU_MoveFileExW = 4,
    kU_lstrcpyW = 5,
    kU_lstrcatW = 6,
    kU_lstrlenW = 7,
    kU_SHGetFolderPathW = 8,
    kU_RegOpenKeyW = 9,
    kU_RegCloseKey = 10,
    kU_RegDeleteValueW = 11,
    kU_RegDeleteKeyW = 12,
    kU_MessageBoxW = 13,
};

std::vector<PEImport> installerImports() {
    return {
        {"kernel32.dll",
         {"ExitProcess",
          "GetModuleFileNameW",
          "CreateFileW",
          "ReadFile",
          "WriteFile",
          "CloseHandle",
          "LocalAlloc",
          "LocalFree",
          "SetFilePointer",
          "CreateDirectoryW",
          "DeleteFileW",
          "RemoveDirectoryW",
          "lstrcpyW",
          "lstrcatW",
          "lstrlenW",
          "GetLastError"}},
        {"shell32.dll", {"SHGetFolderPathW"}},
        {"advapi32.dll",
         {"RegCreateKeyW",
          "RegSetValueExW",
          "RegCloseKey",
          "RegOpenKeyW",
          "RegDeleteValueW",
          "RegDeleteKeyW"}},
        {"user32.dll", {"MessageBoxW"}},
        {"ntdll.dll", {"RtlComputeCrc32"}},
    };
}

std::vector<PEImport> uninstallerImports() {
    return {
        {"kernel32.dll",
         {"ExitProcess",
          "GetModuleFileNameW",
          "DeleteFileW",
          "RemoveDirectoryW",
          "MoveFileExW",
          "lstrcpyW",
          "lstrcatW",
          "lstrlenW"}},
        {"shell32.dll", {"SHGetFolderPathW"}},
        {"advapi32.dll", {"RegOpenKeyW", "RegCloseKey", "RegDeleteValueW", "RegDeleteKeyW"}},
        {"user32.dll", {"MessageBoxW"}},
    };
}

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

std::string resolveBootstrapArch(const std::string &payloadArch) {
    if (payloadArch.empty() || payloadArch == "x64")
        return "x64";
    if (payloadArch == "arm64")
        return "x64";
    throw std::runtime_error("unsupported Windows package architecture '" + payloadArch + "'");
}

uint32_t computeIATOffset(const std::vector<PEImport> &imports) {
    if (imports.empty())
        return 0;

    uint32_t idtSize = static_cast<uint32_t>((imports.size() + 1) * 20);
    uint32_t iltSize = 0;
    for (const auto &imp : imports)
        iltSize += static_cast<uint32_t>((imp.functions.size() + 1) * 8);
    uint32_t hintNameSize = 0;
    for (const auto &imp : imports) {
        for (const auto &fn : imp.functions) {
            const uint32_t entryLen = static_cast<uint32_t>(2 + fn.size() + 1);
            hintNameSize += (entryLen + 1) & ~1u;
        }
    }
    uint32_t dllNameSize = 0;
    for (const auto &imp : imports)
        dllNameSize += static_cast<uint32_t>(imp.dllName.size() + 1);

    uint32_t iatOff = idtSize + iltSize + hintNameSize + dllNameSize;
    return alignUp(iatOff, 8);
}

void finalizeStubRVAs(StubResult &stub, InstallerStubGen &gen) {
    const uint32_t textRVA = kTextRVA;
    const uint32_t rdataRVA =
        kRdataBaseRVA +
        (alignUp(static_cast<uint32_t>(gen.codeSize()), kSectionAlignment) - kSectionAlignment);

    const uint32_t iatOff = computeIATOffset(stub.imports);
    const uint32_t iatBaseRVA = rdataRVA + iatOff;

    uint32_t iatSize = 0;
    for (const auto &imp : stub.imports)
        iatSize += static_cast<uint32_t>((imp.functions.size() + 1) * 8);

    const uint32_t dataBaseRVA = iatBaseRVA + iatSize;
    stub.stubDataRVAOffset = dataBaseRVA - rdataRVA;
    stub.textSection = gen.finishText(textRVA, iatBaseRVA, stub.imports, dataBaseRVA);
}

void zeroLocalQword(InstallerStubGen &gen, int32_t off) {
    gen.xorRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.movMemReg(X64Reg::RBP, off, X64Reg::RAX);
}

void storeStackImm64(InstallerStubGen &gen, int32_t off, uint64_t imm) {
    gen.movRegImm64(X64Reg::RAX, imm);
    gen.movMemReg(X64Reg::RSP, off, X64Reg::RAX);
}

void storeStackPtrToLocal(InstallerStubGen &gen, int32_t stackOff, int32_t localOff) {
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, localOff);
    gen.movMemReg(X64Reg::RSP, stackOff, X64Reg::RAX);
}

uint32_t wideBytesFor(const std::string &text) {
    const size_t bytes = (utf16CodeUnitCountFromUtf8(text) + 1) * 2;
    if (bytes > UINT32_MAX)
        throw std::runtime_error("Windows installer string is too large");
    return static_cast<uint32_t>(bytes);
}

std::string installDirNameFor(const WindowsPackageLayout &layout) {
    return layout.installDirName.empty() ? layout.displayName : layout.installDirName;
}

std::string registryIdFor(const WindowsPackageLayout &layout) {
    if (!layout.identifier.empty())
        return layout.identifier;
    if (!layout.executableName.empty())
        return normalizeExecName(layout.executableName);
    return normalizeExecName(layout.displayName);
}

std::string uninstallKeyPathFor(const WindowsPackageLayout &layout) {
    return "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + registryIdFor(layout);
}

bool needsDesktopPath(const WindowsPackageLayout &layout) {
    if (layout.createDesktopShortcut)
        return true;
    return std::any_of(layout.installFiles.begin(),
                       layout.installFiles.end(),
                       [](const WindowsPackageFileEntry &entry) {
                           return entry.root == WindowsInstallRoot::DesktopDir;
                       }) ||
           std::any_of(layout.uninstallFiles.begin(),
                       layout.uninstallFiles.end(),
                       [](const WindowsPackageFileEntry &entry) {
                           return entry.root == WindowsInstallRoot::DesktopDir;
                       });
}

bool needsMenuPath(const WindowsPackageLayout &layout) {
    if (layout.createStartMenuShortcut)
        return true;
    return std::any_of(layout.installFiles.begin(),
                       layout.installFiles.end(),
                       [](const WindowsPackageFileEntry &entry) {
                           return entry.root == WindowsInstallRoot::StartMenuDir;
                       }) ||
           std::any_of(layout.uninstallFiles.begin(),
                       layout.uninstallFiles.end(),
                       [](const WindowsPackageFileEntry &entry) {
                           return entry.root == WindowsInstallRoot::StartMenuDir;
                       }) ||
           std::any_of(layout.installDirectories.begin(),
                       layout.installDirectories.end(),
                       [](const WindowsPackageDirEntry &entry) {
                           return entry.root == WindowsInstallRoot::StartMenuDir;
                       }) ||
           std::any_of(layout.uninstallDirectories.begin(),
                       layout.uninstallDirectories.end(),
                       [](const WindowsPackageDirEntry &entry) {
                           return entry.root == WindowsInstallRoot::StartMenuDir;
                       });
}

int32_t rootBufferOffset(WindowsInstallRoot root) {
    switch (root) {
        case WindowsInstallRoot::DesktopDir:
            return kDesktopPathOff;
        case WindowsInstallRoot::StartMenuDir:
            return kMenuPathOff;
        case WindowsInstallRoot::InstallDir:
        default:
            return kInstallPathOff;
    }
}

void emitCloseLocalHandleIfSet(InstallerStubGen &gen, int32_t handleOff, uint32_t closeSlot) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, handleOff);
    gen.testRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.jz(lblSkip);
    gen.callIATSlot(closeSlot);
    zeroLocalQword(gen, handleOff);
    gen.bindLabel(lblSkip);
}

void emitLocalFreeIfSet(InstallerStubGen &gen, int32_t ptrOff, uint32_t freeSlot) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, ptrOff);
    gen.testRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.jz(lblSkip);
    gen.callIATSlot(freeSlot);
    zeroLocalQword(gen, ptrOff);
    gen.bindLabel(lblSkip);
}

void emitRegCloseIfSet(InstallerStubGen &gen, int32_t keyOff, uint32_t closeSlot) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, keyOff);
    gen.testRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.jz(lblSkip);
    gen.callIATSlot(closeSlot);
    zeroLocalQword(gen, keyOff);
    gen.bindLabel(lblSkip);
}

void emitCheckedCatEmbedded(InstallerStubGen &gen,
                            int32_t destOff,
                            uint32_t srcDataOff,
                            uint32_t catSlot,
                            uint32_t strlenSlot,
                            uint32_t errorLabel) {
    gen.leaRipData(X64Reg::RAX, srcDataOff);
    gen.movMemReg(X64Reg::RBP, kBytesWrittenOff, X64Reg::RAX);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, destOff);
    gen.callIATSlot(strlenSlot);
    gen.movMemReg(X64Reg::RBP, kBytesReadOff, X64Reg::RAX);

    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kBytesWrittenOff);
    gen.callIATSlot(strlenSlot);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kBytesReadOff);
    gen.addRegReg(X64Reg::RAX, X64Reg::RDX);
    gen.cmpRegImm32(X64Reg::RAX, kMaxPathChars - 1);
    gen.ja(errorLabel);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, destOff);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kBytesWrittenOff);
    gen.callIATSlot(catSlot);
}

void emitCreateDirectoryChecked(InstallerStubGen &gen,
                                uint32_t createDirSlot,
                                uint32_t getLastErrorSlot,
                                uint32_t errorLabel) {
    const auto lblOk = gen.newLabel();
    gen.callIATSlot(createDirSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblOk);
    gen.callIATSlot(getLastErrorSlot);
    gen.cmpRegImm32(X64Reg::RAX, kErrorAlreadyExists);
    gen.jnz(errorLabel);
    gen.bindLabel(lblOk);
}

void emitCmpRegU32(InstallerStubGen &gen, X64Reg reg, uint32_t value) {
    gen.movRegImm32(X64Reg::R10, value);
    gen.cmpRegReg(reg, X64Reg::R10);
}

void emitComposePath(InstallerStubGen &gen,
                     WindowsInstallRoot root,
                     int32_t tempOff,
                     uint32_t slashOff,
                     uint32_t relPathOff,
                     uint32_t copySlot,
                     uint32_t catSlot,
                     uint32_t strlenSlot,
                     uint32_t errorLabel) {
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, tempOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, rootBufferOffset(root));
    gen.callIATSlot(copySlot);

    emitCheckedCatEmbedded(gen, tempOff, slashOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatEmbedded(gen, tempOff, relPathOff, catSlot, strlenSlot, errorLabel);
}

void emitMessageBox(
    InstallerStubGen &gen, uint32_t slot, uint32_t titleOff, uint32_t messageOff, uint32_t flags) {
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.leaRipData(X64Reg::RDX, messageOff);
    gen.leaRipData(X64Reg::R8, titleOff);
    gen.movRegImm32(X64Reg::R9, flags);
    gen.callIATSlot(slot);
}

void emitCreateDirectoryAtRoot(InstallerStubGen &gen,
                               WindowsInstallRoot root,
                               uint32_t slashOff,
                               uint32_t relPathOff,
                               uint32_t copySlot,
                               uint32_t catSlot,
                               uint32_t strlenSlot,
                               uint32_t createDirSlot,
                               uint32_t getLastErrorSlot,
                               uint32_t errorLabel) {
    emitComposePath(
        gen, root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot, strlenSlot, errorLabel);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    emitCreateDirectoryChecked(gen, createDirSlot, getLastErrorSlot, errorLabel);
}

void emitRegSetConstString(InstallerStubGen &gen,
                           uint32_t regSetSlot,
                           uint32_t valueNameOff,
                           uint32_t valueOff,
                           uint32_t valueBytes,
                           uint32_t errorLabel) {
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.movRegImm32(X64Reg::R9, kRegSz);
    gen.leaRipData(X64Reg::RAX, valueOff);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);
    gen.movRegImm32(X64Reg::RAX, valueBytes);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);
    gen.callIATSlot(regSetSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
}

void emitRegSetDefaultConstString(InstallerStubGen &gen,
                                  uint32_t regSetSlot,
                                  uint32_t valueOff,
                                  uint32_t valueBytes,
                                  uint32_t errorLabel) {
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.movRegImm32(X64Reg::R9, kRegSz);
    gen.leaRipData(X64Reg::RAX, valueOff);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);
    gen.movRegImm32(X64Reg::RAX, valueBytes);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);
    gen.callIATSlot(regSetSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
}

void emitRegSetStackString(InstallerStubGen &gen,
                           uint32_t regSetSlot,
                           uint32_t strlenSlot,
                           uint32_t valueNameOff,
                           int32_t stackBufOff,
                           uint32_t errorLabel) {
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.movRegImm32(X64Reg::R9, kRegSz);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, stackBufOff);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, stackBufOff);
    gen.callIATSlot(strlenSlot);
    gen.addRegImm32(X64Reg::RAX, 1);
    gen.addRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);

    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.movRegImm32(X64Reg::R9, kRegSz);
    gen.callIATSlot(regSetSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
}

void emitRegSetDefaultStackString(InstallerStubGen &gen,
                                  uint32_t regSetSlot,
                                  uint32_t strlenSlot,
                                  int32_t stackBufOff,
                                  uint32_t errorLabel) {
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, stackBufOff);
    gen.callIATSlot(strlenSlot);
    gen.addRegImm32(X64Reg::RAX, 1);
    gen.addRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);

    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.movRegImm32(X64Reg::R9, kRegSz);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, stackBufOff);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);
    gen.callIATSlot(regSetSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
}

void emitRegCreateConstKey(InstallerStubGen &gen,
                           uint32_t createSlot,
                           uint32_t keyOff,
                           uint32_t errorLabel) {
    emitRegCloseIfSet(gen, kRegKeyOff, kI_RegCloseKey);
    gen.movRegImm64(X64Reg::RCX, kHkeyLocalMachine);
    gen.leaRipData(X64Reg::RDX, keyOff);
    gen.leaRegMem(X64Reg::R8, X64Reg::RBP, kRegKeyOff);
    gen.callIATSlot(createSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
}

void emitRegDeleteConstKey(InstallerStubGen &gen, uint32_t deleteSlot, uint32_t keyOff) {
    gen.movRegImm64(X64Reg::RCX, kHkeyLocalMachine);
    gen.leaRipData(X64Reg::RDX, keyOff);
    gen.callIATSlot(deleteSlot);
}

std::string extensionKeyFor(const WindowsFileAssociationEntry &assoc) {
    std::string ext = assoc.extension;
    if (ext.empty() || ext.front() != '.')
        ext.insert(ext.begin(), '.');
    return "Software\\Classes\\" + ext;
}

std::string progIdKeyFor(const WindowsFileAssociationEntry &assoc) {
    return "Software\\Classes\\" + assoc.progId;
}

std::string openWithProgIdsKeyFor(const WindowsFileAssociationEntry &assoc) {
    return extensionKeyFor(assoc) + "\\OpenWithProgids";
}

void emitRegOpenConstKeyIfExists(InstallerStubGen &gen,
                                 uint32_t openSlot,
                                 uint32_t closeSlot,
                                 uint32_t keyOff,
                                 uint32_t missingLabel) {
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.movRegImm64(X64Reg::RCX, kHkeyLocalMachine);
    gen.leaRipData(X64Reg::RDX, keyOff);
    gen.leaRegMem(X64Reg::R8, X64Reg::RBP, kRegKeyOff);
    gen.callIATSlot(openSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(missingLabel);
}

void emitRegDeleteNamedValueIfPresent(InstallerStubGen &gen,
                                      uint32_t openSlot,
                                      uint32_t closeSlot,
                                      uint32_t deleteValueSlot,
                                      uint32_t keyOff,
                                      uint32_t valueNameOff) {
    const auto lblDone = gen.newLabel();
    emitRegOpenConstKeyIfExists(gen, openSlot, closeSlot, keyOff, lblDone);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.callIATSlot(deleteValueSlot);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.bindLabel(lblDone);
}

void emitRegisterFileAssociations(InstallerStubGen &gen,
                                  const WindowsPackageLayout &layout,
                                  uint32_t slashOff,
                                  uint32_t copySlot,
                                  uint32_t catSlot,
                                  uint32_t createSlot,
                                  uint32_t setValueSlot,
                                  uint32_t closeSlot,
                                  uint32_t strlenSlot,
                                  uint32_t errorLabel) {
    if (layout.fileAssociations.empty())
        return;

    const uint32_t regContentTypeOff = gen.embedStringW("Content Type");
    const uint32_t exeNameOff = gen.embedStringW(layout.executableName);
    const uint32_t quoteOff = gen.embedStringW("\"");
    const uint32_t quotedArgOff = gen.embedStringW("\" \"%1\"");
    const uint32_t emptyOff = gen.embedStringW("");

    for (const auto &assoc : layout.fileAssociations) {
        const uint32_t extKeyOff = gen.embedStringW(extensionKeyFor(assoc));
        const uint32_t progIdOff = gen.embedStringW(assoc.progId);
        emitRegCreateConstKey(gen, createSlot, extKeyOff, errorLabel);
        if (!assoc.mimeType.empty()) {
            const uint32_t mimeOff = gen.embedStringW(assoc.mimeType);
            emitRegSetConstString(gen,
                                  setValueSlot,
                                  regContentTypeOff,
                                  mimeOff,
                                  wideBytesFor(assoc.mimeType),
                                  errorLabel);
        }
        emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);

        const uint32_t openWithKeyOff = gen.embedStringW(openWithProgIdsKeyFor(assoc));
        emitRegCreateConstKey(gen, createSlot, openWithKeyOff, errorLabel);
        emitRegSetConstString(
            gen, setValueSlot, progIdOff, emptyOff, wideBytesFor(""), errorLabel);
        emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);

        const uint32_t progKeyOff = gen.embedStringW(progIdKeyFor(assoc));
        const std::string description =
            assoc.description.empty() ? layout.displayName : assoc.description;
        const uint32_t descriptionOff = gen.embedStringW(description);
        emitRegCreateConstKey(gen, createSlot, progKeyOff, errorLabel);
        emitRegSetDefaultConstString(
            gen, setValueSlot, descriptionOff, wideBytesFor(description), errorLabel);
        emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);

        const uint32_t commandKeyOff =
            gen.embedStringW(progIdKeyFor(assoc) + "\\shell\\open\\command");
        emitRegCreateConstKey(gen, createSlot, commandKeyOff, errorLabel);
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
        gen.leaRipData(X64Reg::RDX, quoteOff);
        gen.callIATSlot(copySlot);
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
        gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kInstallPathOff);
        gen.callIATSlot(catSlot);
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
        gen.leaRipData(X64Reg::RDX, slashOff);
        gen.callIATSlot(catSlot);
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
        gen.leaRipData(X64Reg::RDX, exeNameOff);
        gen.callIATSlot(catSlot);
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
        gen.leaRipData(X64Reg::RDX, quotedArgOff);
        gen.callIATSlot(catSlot);
        emitRegSetDefaultStackString(gen, setValueSlot, strlenSlot, kTempPathOff, errorLabel);
        emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    }
}

void emitUnregisterFileAssociations(InstallerStubGen &gen,
                                    const WindowsPackageLayout &layout,
                                    uint32_t openSlot,
                                    uint32_t closeSlot,
                                    uint32_t deleteValueSlot,
                                    uint32_t deleteSlot) {
    for (const auto &assoc : layout.fileAssociations) {
        emitRegDeleteNamedValueIfPresent(gen,
                                         openSlot,
                                         closeSlot,
                                         deleteValueSlot,
                                         gen.embedStringW(openWithProgIdsKeyFor(assoc)),
                                         gen.embedStringW(assoc.progId));
        emitRegDeleteConstKey(
            gen, deleteSlot, gen.embedStringW(progIdKeyFor(assoc) + "\\shell\\open\\command"));
        emitRegDeleteConstKey(
            gen, deleteSlot, gen.embedStringW(progIdKeyFor(assoc) + "\\shell\\open"));
        emitRegDeleteConstKey(gen, deleteSlot, gen.embedStringW(progIdKeyFor(assoc) + "\\shell"));
        emitRegDeleteConstKey(gen, deleteSlot, gen.embedStringW(progIdKeyFor(assoc)));
    }
}

void emitBuildRootPaths(InstallerStubGen &gen,
                        const WindowsPackageLayout &layout,
                        uint32_t slashOff,
                        uint32_t installDirOff,
                        uint32_t copySlot,
                        uint32_t catSlot,
                        uint32_t strlenSlot,
                        uint32_t createDirSlot,
                        uint32_t getLastErrorSlot,
                        uint32_t folderSlot,
                        bool createRoots,
                        uint32_t errorLabel) {
    (void)copySlot;
    // installPath = %ProgramFiles%\InstallDir
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.movRegImm32(X64Reg::RDX, kCsidlProgramFiles);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.xorRegReg(X64Reg::R9, X64Reg::R9);
    storeStackPtrToLocal(gen, 0x20, kInstallPathOff);
    gen.callIATSlot(folderSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);

    emitCheckedCatEmbedded(gen, kInstallPathOff, slashOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatEmbedded(gen, kInstallPathOff, installDirOff, catSlot, strlenSlot, errorLabel);
    if (createRoots) {
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInstallPathOff);
        gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
        emitCreateDirectoryChecked(gen, createDirSlot, getLastErrorSlot, errorLabel);
    }

    if (needsDesktopPath(layout)) {
        gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
        gen.movRegImm32(X64Reg::RDX, kCsidlCommonDesktopDirectory);
        gen.xorRegReg(X64Reg::R8, X64Reg::R8);
        gen.xorRegReg(X64Reg::R9, X64Reg::R9);
        storeStackPtrToLocal(gen, 0x20, kDesktopPathOff);
        gen.callIATSlot(folderSlot);
        gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
        gen.jnz(errorLabel);
    }

    if (needsMenuPath(layout)) {
        gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
        gen.movRegImm32(X64Reg::RDX, kCsidlCommonPrograms);
        gen.xorRegReg(X64Reg::R8, X64Reg::R8);
        gen.xorRegReg(X64Reg::R9, X64Reg::R9);
        storeStackPtrToLocal(gen, 0x20, kMenuPathOff);
        gen.callIATSlot(folderSlot);
        gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
        gen.jnz(errorLabel);

        emitCheckedCatEmbedded(gen, kMenuPathOff, slashOff, catSlot, strlenSlot, errorLabel);
        emitCheckedCatEmbedded(gen, kMenuPathOff, installDirOff, catSlot, strlenSlot, errorLabel);
        if (createRoots) {
            gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kMenuPathOff);
            gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
            emitCreateDirectoryChecked(gen, createDirSlot, getLastErrorSlot, errorLabel);
        }
    }
}

void emitExtractFile(InstallerStubGen &gen,
                     const WindowsPackageFileEntry &entry,
                     uint32_t overlayFileOffset,
                     uint32_t slashOff,
                     uint32_t copySlot,
                     uint32_t catSlot,
                     uint32_t strlenSlot,
                     uint32_t createFileSlot,
                     uint32_t readSlot,
                     uint32_t writeSlot,
                     uint32_t closeSlot,
                     uint32_t allocSlot,
                     uint32_t freeSlot,
                     uint32_t setFilePointerSlot,
                     uint32_t crcSlot,
                     uint32_t errorLabel) {
    if (entry.sizeBytes > UINT32_MAX)
        throw std::runtime_error("Windows installer entry is too large: " + entry.relativePath);
    if (entry.overlayDataOffset > UINT32_MAX ||
        overlayFileOffset > UINT32_MAX - static_cast<uint32_t>(entry.overlayDataOffset)) {
        throw std::runtime_error("Windows installer overlay offset is too large: " +
                                 entry.relativePath);
    }

    const uint32_t entrySize = static_cast<uint32_t>(entry.sizeBytes);
    const uint32_t entryOffset = overlayFileOffset + static_cast<uint32_t>(entry.overlayDataOffset);
    if (entrySize != 0 && entryOffset == UINT32_MAX)
        throw std::runtime_error("Windows installer overlay offset is ambiguous for SetFilePointer: " +
                                 entry.relativePath);
    const uint32_t relPathOff = gen.embedStringW(entry.relativePath);

    if (entrySize == 0) {
        emitComposePath(
            gen, entry.root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot, strlenSlot, errorLabel);

        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
        gen.movRegImm32(X64Reg::RDX, kGenericWrite);
        gen.xorRegReg(X64Reg::R8, X64Reg::R8);
        gen.xorRegReg(X64Reg::R9, X64Reg::R9);
        storeStackImm64(gen, 0x20, kCreateAlways);
        storeStackImm64(gen, 0x28, kFileAttributeNormal);
        storeStackImm64(gen, 0x30, 0);
        gen.callIATSlot(createFileSlot);
        gen.cmpRegImm32(X64Reg::RAX, 0xFFFFFFFFu);
        gen.jz(errorLabel);
        gen.movMemReg(X64Reg::RBP, kHOutOff, X64Reg::RAX);
        emitCloseLocalHandleIfSet(gen, kHOutOff, closeSlot);
        return;
    }

    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kHFileOff);
    gen.movRegImm32(X64Reg::RDX, entryOffset);
    zeroLocalQword(gen, kBytesWrittenOff);
    gen.leaRegMem(X64Reg::R8, X64Reg::RBP, kBytesWrittenOff);
    gen.xorRegReg(X64Reg::R9, X64Reg::R9);
    gen.callIATSlot(setFilePointerSlot);
    emitCmpRegU32(gen, X64Reg::RAX, entryOffset);
    gen.jnz(errorLabel);

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.movRegImm32(X64Reg::RDX, entrySize);
    gen.callIATSlot(allocSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);
    gen.movMemReg(X64Reg::RBP, kPFileBufOff, X64Reg::RAX);

    zeroLocalQword(gen, kBytesReadOff);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kHFileOff);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kPFileBufOff);
    gen.movRegImm32(X64Reg::R8, entrySize);
    gen.leaRegMem(X64Reg::R9, X64Reg::RBP, kBytesReadOff);
    storeStackImm64(gen, 0x20, 0);
    gen.callIATSlot(readSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kBytesReadOff);
    emitCmpRegU32(gen, X64Reg::RAX, entrySize);
    gen.jnz(errorLabel);

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kPFileBufOff);
    gen.movRegImm32(X64Reg::R8, entrySize);
    gen.callIATSlot(crcSlot);
    emitCmpRegU32(gen, X64Reg::RAX, entry.crc32);
    gen.jnz(errorLabel);

    emitComposePath(
        gen, entry.root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot, strlenSlot, errorLabel);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.movRegImm32(X64Reg::RDX, kGenericWrite);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.xorRegReg(X64Reg::R9, X64Reg::R9);
    storeStackImm64(gen, 0x20, kCreateAlways);
    storeStackImm64(gen, 0x28, kFileAttributeNormal);
    storeStackImm64(gen, 0x30, 0);
    gen.callIATSlot(createFileSlot);
    gen.cmpRegImm32(X64Reg::RAX, 0xFFFFFFFFu);
    gen.jz(errorLabel);
    gen.movMemReg(X64Reg::RBP, kHOutOff, X64Reg::RAX);

    zeroLocalQword(gen, kBytesWrittenOff);
    gen.movRegReg(X64Reg::RCX, X64Reg::RAX);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kPFileBufOff);
    gen.movRegImm32(X64Reg::R8, entrySize);
    gen.leaRegMem(X64Reg::R9, X64Reg::RBP, kBytesWrittenOff);
    storeStackImm64(gen, 0x20, 0);
    gen.callIATSlot(writeSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kBytesWrittenOff);
    emitCmpRegU32(gen, X64Reg::RAX, entrySize);
    gen.jnz(errorLabel);

    emitCloseLocalHandleIfSet(gen, kHOutOff, closeSlot);
    emitLocalFreeIfSet(gen, kPFileBufOff, freeSlot);
}

void emitDeleteFile(InstallerStubGen &gen,
                    const WindowsPackageFileEntry &entry,
                    uint32_t slashOff,
                    uint32_t copySlot,
                    uint32_t catSlot,
                    uint32_t strlenSlot,
                    uint32_t deleteSlot,
                    uint32_t errorLabel) {
    const uint32_t relPathOff = gen.embedStringW(entry.relativePath);
    emitComposePath(
        gen, entry.root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot, strlenSlot, errorLabel);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.callIATSlot(deleteSlot);
}

void emitRemoveDirectory(InstallerStubGen &gen,
                         const WindowsPackageDirEntry &entry,
                         uint32_t slashOff,
                         uint32_t copySlot,
                         uint32_t catSlot,
                         uint32_t strlenSlot,
                         uint32_t removeSlot,
                         uint32_t errorLabel) {
    const uint32_t relPathOff = gen.embedStringW(entry.relativePath);
    emitComposePath(
        gen, entry.root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot, strlenSlot, errorLabel);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.callIATSlot(removeSlot);
}

} // namespace

StubResult buildInstallerStub(const WindowsPackageLayout &layout, const std::string &arch) {
    StubResult result;
    result.imports = installerImports();
    result.peArch = resolveBootstrapArch(arch);

    InstallerStubGen gen;

    const std::string installDir = installDirNameFor(layout);
    const std::string version = layout.version.empty() ? "0.0.0" : layout.version;
    const std::string publisher = layout.publisher.empty() ? "Viper" : layout.publisher;
    const std::string uninstallKey = uninstallKeyPathFor(layout);

    const uint32_t slashOff = gen.embedStringW("\\");
    const uint32_t installDirOff = gen.embedStringW(installDir);
    const uint32_t displayNameOff = gen.embedStringW(layout.displayName);
    const uint32_t versionOff = gen.embedStringW(version);
    const uint32_t publisherOff = gen.embedStringW(publisher);
    const uint32_t uninstallKeyOff = gen.embedStringW(uninstallKey);
    const uint32_t uninstallExeOff = gen.embedStringW("uninstall.exe");

    const uint32_t regDisplayNameOff = gen.embedStringW("DisplayName");
    const uint32_t regDisplayVersionOff = gen.embedStringW("DisplayVersion");
    const uint32_t regPublisherOff = gen.embedStringW("Publisher");
    const uint32_t regInstallLocationOff = gen.embedStringW("InstallLocation");
    const uint32_t regUninstallStringOff = gen.embedStringW("UninstallString");

    const uint32_t successTitleOff = gen.embedStringW(layout.displayName + " Setup");
    const uint32_t successMsgOff =
        gen.embedStringW("Installation complete! " + layout.displayName + " has been installed.");
    const uint32_t errorTitleOff = gen.embedStringW("Setup Error");
    const uint32_t errorMsgOff =
        gen.embedStringW("Installation failed. The package could not be extracted.");

    const auto lblError = gen.newLabel();
    const auto lblRollbackError = gen.newLabel();
    const auto lblCleanupSuccess = gen.newLabel();
    const auto lblCleanupError = gen.newLabel();
    const auto lblCleanupRollback = gen.newLabel();
    const auto lblExitSuccess = gen.newLabel();
    const auto lblExitError = gen.newLabel();

    gen.push(X64Reg::RBP);
    gen.movRegReg(X64Reg::RBP, X64Reg::RSP);
    gen.subRegImm32(X64Reg::RSP, kFrameSize);

    zeroLocalQword(gen, kHFileOff);
    zeroLocalQword(gen, kHOutOff);
    zeroLocalQword(gen, kPFileBufOff);
    zeroLocalQword(gen, kRegKeyOff);

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kSelfPathOff);
    gen.movRegImm32(X64Reg::R8, kMaxPathChars);
    gen.callIATSlot(kI_GetModuleFileNameW);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblError);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kSelfPathOff);
    gen.movRegImm32(X64Reg::RDX, kGenericRead);
    gen.movRegImm32(X64Reg::R8, kFileShareRead);
    gen.xorRegReg(X64Reg::R9, X64Reg::R9);
    storeStackImm64(gen, 0x20, kOpenExisting);
    storeStackImm64(gen, 0x28, 0);
    storeStackImm64(gen, 0x30, 0);
    gen.callIATSlot(kI_CreateFileW);
    gen.cmpRegImm32(X64Reg::RAX, 0xFFFFFFFFu);
    gen.jz(lblError);
    gen.movMemReg(X64Reg::RBP, kHFileOff, X64Reg::RAX);

    emitBuildRootPaths(gen,
                       layout,
                       slashOff,
                       installDirOff,
                       kI_lstrcpyW,
                       kI_lstrcatW,
                       kI_lstrlenW,
                       kI_CreateDirectoryW,
                       kI_GetLastError,
                       kI_SHGetFolderPathW,
                       true,
                       lblError);

    for (const auto &dir : layout.installDirectories) {
        emitCreateDirectoryAtRoot(gen,
                                  dir.root,
                                  slashOff,
                                  gen.embedStringW(dir.relativePath),
                                  kI_lstrcpyW,
                                  kI_lstrcatW,
                                  kI_lstrlenW,
                                  kI_CreateDirectoryW,
                                  kI_GetLastError,
                                  lblRollbackError);
    }

    for (const auto &file : layout.installFiles) {
        emitExtractFile(gen,
                        file,
                        layout.overlayFileOffset,
                        slashOff,
                        kI_lstrcpyW,
                        kI_lstrcatW,
                        kI_lstrlenW,
                        kI_CreateFileW,
                        kI_ReadFile,
                        kI_WriteFile,
                        kI_CloseHandle,
                        kI_LocalAlloc,
                        kI_LocalFree,
                        kI_SetFilePointer,
                        kI_RtlComputeCrc32,
                        lblRollbackError);
    }

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kUninstallPathOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kInstallPathOff);
    gen.callIATSlot(kI_lstrcpyW);
    emitCheckedCatEmbedded(
        gen, kUninstallPathOff, slashOff, kI_lstrcatW, kI_lstrlenW, lblRollbackError);
    emitCheckedCatEmbedded(
        gen, kUninstallPathOff, uninstallExeOff, kI_lstrcatW, kI_lstrlenW, lblRollbackError);

    gen.movRegImm64(X64Reg::RCX, kHkeyLocalMachine);
    gen.leaRipData(X64Reg::RDX, uninstallKeyOff);
    gen.leaRegMem(X64Reg::R8, X64Reg::RBP, kRegKeyOff);
    gen.callIATSlot(kI_RegCreateKeyW);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblRollbackError);

    emitRegSetConstString(gen,
                          kI_RegSetValueExW,
                          regDisplayNameOff,
                          displayNameOff,
                          wideBytesFor(layout.displayName),
                          lblRollbackError);
    emitRegSetConstString(
        gen,
        kI_RegSetValueExW,
        regDisplayVersionOff,
        versionOff,
        wideBytesFor(version),
        lblRollbackError);
    emitRegSetConstString(
        gen,
        kI_RegSetValueExW,
        regPublisherOff,
        publisherOff,
        wideBytesFor(publisher),
        lblRollbackError);
    emitRegSetStackString(gen,
                          kI_RegSetValueExW,
                          kI_lstrlenW,
                          regInstallLocationOff,
                          kInstallPathOff,
                          lblRollbackError);
    emitRegSetStackString(gen,
                          kI_RegSetValueExW,
                          kI_lstrlenW,
                          regUninstallStringOff,
                          kUninstallPathOff,
                          lblRollbackError);
    emitRegCloseIfSet(gen, kRegKeyOff, kI_RegCloseKey);

    emitRegisterFileAssociations(gen,
                                 layout,
                                 slashOff,
                                 kI_lstrcpyW,
                                 kI_lstrcatW,
                                 kI_RegCreateKeyW,
                                 kI_RegSetValueExW,
                                 kI_RegCloseKey,
                                 kI_lstrlenW,
                                 lblRollbackError);

    emitMessageBox(gen, kI_MessageBoxW, successTitleOff, successMsgOff, 0x40);
    gen.jmp(lblCleanupSuccess);

    gen.bindLabel(lblError);
    emitMessageBox(gen, kI_MessageBoxW, errorTitleOff, errorMsgOff, 0x10);
    gen.jmp(lblCleanupError);

    gen.bindLabel(lblRollbackError);
    emitMessageBox(gen, kI_MessageBoxW, errorTitleOff, errorMsgOff, 0x10);
    gen.jmp(lblCleanupRollback);

    gen.bindLabel(lblCleanupSuccess);
    emitCloseLocalHandleIfSet(gen, kHOutOff, kI_CloseHandle);
    emitLocalFreeIfSet(gen, kPFileBufOff, kI_LocalFree);
    emitRegCloseIfSet(gen, kRegKeyOff, kI_RegCloseKey);
    emitCloseLocalHandleIfSet(gen, kHFileOff, kI_CloseHandle);
    gen.jmp(lblExitSuccess);

    gen.bindLabel(lblCleanupError);
    emitCloseLocalHandleIfSet(gen, kHOutOff, kI_CloseHandle);
    emitLocalFreeIfSet(gen, kPFileBufOff, kI_LocalFree);
    emitRegCloseIfSet(gen, kRegKeyOff, kI_RegCloseKey);
    emitCloseLocalHandleIfSet(gen, kHFileOff, kI_CloseHandle);
    gen.jmp(lblExitError);

    gen.bindLabel(lblCleanupRollback);
    emitCloseLocalHandleIfSet(gen, kHOutOff, kI_CloseHandle);
    emitLocalFreeIfSet(gen, kPFileBufOff, kI_LocalFree);
    emitRegCloseIfSet(gen, kRegKeyOff, kI_RegCloseKey);
    emitCloseLocalHandleIfSet(gen, kHFileOff, kI_CloseHandle);
    for (const auto &file : layout.uninstallFiles)
        emitDeleteFile(
            gen, file, slashOff, kI_lstrcpyW, kI_lstrcatW, kI_lstrlenW, kI_DeleteFileW, lblExitError);
    emitDeleteFile(gen,
                   WindowsPackageFileEntry{WindowsInstallRoot::InstallDir, "uninstall.exe", 0, 0},
                   slashOff,
                   kI_lstrcpyW,
                   kI_lstrcatW,
                   kI_lstrlenW,
                   kI_DeleteFileW,
                   lblExitError);
    for (const auto &dir : layout.uninstallDirectories)
        emitRemoveDirectory(gen,
                            dir,
                            slashOff,
                            kI_lstrcpyW,
                            kI_lstrcatW,
                            kI_lstrlenW,
                            kI_RemoveDirectoryW,
                            lblExitError);
    if (needsMenuPath(layout)) {
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kMenuPathOff);
        gen.callIATSlot(kI_RemoveDirectoryW);
    }
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInstallPathOff);
    gen.callIATSlot(kI_RemoveDirectoryW);
    emitUnregisterFileAssociations(gen,
                                   layout,
                                   kI_RegOpenKeyW,
                                   kI_RegCloseKey,
                                   kI_RegDeleteValueW,
                                   kI_RegDeleteKeyW);
    emitRegDeleteConstKey(gen, kI_RegDeleteKeyW, uninstallKeyOff);
    gen.jmp(lblExitError);

    gen.bindLabel(lblExitSuccess);
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.callIATSlot(kI_ExitProcess);

    gen.bindLabel(lblExitError);
    gen.movRegImm32(X64Reg::RCX, 1);
    gen.callIATSlot(kI_ExitProcess);

    finalizeStubRVAs(result, gen);
    result.stubData = gen.dataSection();
    return result;
}

StubResult buildUninstallerStub(const WindowsPackageLayout &layout, const std::string &arch) {
    StubResult result;
    result.imports = uninstallerImports();
    result.peArch = resolveBootstrapArch(arch);

    InstallerStubGen gen;

    const std::string installDir = installDirNameFor(layout);
    const std::string uninstallKey = uninstallKeyPathFor(layout);

    const uint32_t slashOff = gen.embedStringW("\\");
    const uint32_t installDirOff = gen.embedStringW(installDir);
    const uint32_t uninstallKeyOff = gen.embedStringW(uninstallKey);
    const uint32_t successTitleOff = gen.embedStringW(layout.displayName + " Uninstall");
    const uint32_t successMsgOff = gen.embedStringW(layout.displayName + " has been uninstalled.");
    const uint32_t errorTitleOff = gen.embedStringW("Uninstall Error");
    const uint32_t errorMsgOff =
        gen.embedStringW("Uninstall failed. Required installation paths could not be resolved.");
    const auto lblError = gen.newLabel();
    const auto lblExitSuccess = gen.newLabel();
    const auto lblExitError = gen.newLabel();

    gen.push(X64Reg::RBP);
    gen.movRegReg(X64Reg::RBP, X64Reg::RSP);
    gen.subRegImm32(X64Reg::RSP, kFrameSize);
    zeroLocalQword(gen, kRegKeyOff);

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kSelfPathOff);
    gen.movRegImm32(X64Reg::R8, kMaxPathChars);
    gen.callIATSlot(kU_GetModuleFileNameW);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblError);

    emitBuildRootPaths(gen,
                       layout,
                       slashOff,
                       installDirOff,
                       kU_lstrcpyW,
                       kU_lstrcatW,
                       kU_lstrlenW,
                       kU_RemoveDirectoryW,
                       0,
                       kU_SHGetFolderPathW,
                       false,
                       lblError);

    for (const auto &file : layout.uninstallFiles) {
        emitDeleteFile(
            gen, file, slashOff, kU_lstrcpyW, kU_lstrcatW, kU_lstrlenW, kU_DeleteFileW, lblError);
    }

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kSelfPathOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.movRegImm32(X64Reg::R8, kMoveFileDelayUntilReboot);
    gen.callIATSlot(kU_MoveFileExW);

    for (const auto &dir : layout.uninstallDirectories) {
        emitRemoveDirectory(gen,
                            dir,
                            slashOff,
                            kU_lstrcpyW,
                            kU_lstrcatW,
                            kU_lstrlenW,
                            kU_RemoveDirectoryW,
                            lblError);
    }

    if (needsMenuPath(layout)) {
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kMenuPathOff);
        gen.callIATSlot(kU_RemoveDirectoryW);
    }

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInstallPathOff);
    gen.callIATSlot(kU_RemoveDirectoryW);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInstallPathOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.movRegImm32(X64Reg::R8, kMoveFileDelayUntilReboot);
    gen.callIATSlot(kU_MoveFileExW);

    emitUnregisterFileAssociations(gen,
                                   layout,
                                   kU_RegOpenKeyW,
                                   kU_RegCloseKey,
                                   kU_RegDeleteValueW,
                                   kU_RegDeleteKeyW);

    gen.movRegImm64(X64Reg::RCX, kHkeyLocalMachine);
    gen.leaRipData(X64Reg::RDX, uninstallKeyOff);
    gen.callIATSlot(kU_RegDeleteKeyW);

    emitMessageBox(gen, kU_MessageBoxW, successTitleOff, successMsgOff, 0x40);
    gen.jmp(lblExitSuccess);

    gen.bindLabel(lblError);
    emitMessageBox(gen, kU_MessageBoxW, errorTitleOff, errorMsgOff, 0x10);
    gen.jmp(lblExitError);

    gen.bindLabel(lblExitSuccess);
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.callIATSlot(kU_ExitProcess);

    gen.bindLabel(lblExitError);
    gen.movRegImm32(X64Reg::RCX, 1);
    gen.callIATSlot(kU_ExitProcess);

    finalizeStubRVAs(result, gen);
    result.stubData = gen.dataSection();
    return result;
}

} // namespace viper::pkg
