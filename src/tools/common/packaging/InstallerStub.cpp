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

constexpr uint32_t kMaxPathChars = 260;
constexpr uint32_t kMaxPathBytes = kMaxPathChars * 2;

constexpr uint32_t kGenericRead = 0x80000000u;
constexpr uint32_t kGenericWrite = 0x40000000u;
constexpr uint32_t kFileShareRead = 1u;
constexpr uint32_t kOpenExisting = 3u;
constexpr uint32_t kCreateAlways = 2u;
constexpr uint32_t kFileAttributeNormal = 0x80u;
constexpr uint32_t kMoveFileDelayUntilReboot = 0x00000004u;
constexpr uint32_t kRegSz = 1u;
constexpr uint64_t kHkeyLocalMachine = 0x80000002ull;

constexpr uint32_t kCsidlProgramFiles = 0x0026u;
constexpr uint32_t kCsidlDesktopDirectory = 0x0010u;
constexpr uint32_t kCsidlCommonPrograms = 0x0017u;

// Stack frame layout (negative offsets from RBP)
constexpr int32_t kSelfPathOff = -0x210;
constexpr int32_t kInstallPathOff = -0x420;
constexpr int32_t kDesktopPathOff = -0x630;
constexpr int32_t kMenuPathOff = -0x840;
constexpr int32_t kTempPathOff = -0xA50;
constexpr int32_t kUninstallPathOff = -0xC60;
constexpr int32_t kHFileOff = -0xC68;
constexpr int32_t kHOutOff = -0xC70;
constexpr int32_t kPFileBufOff = -0xC78;
constexpr int32_t kBytesReadOff = -0xC80;
constexpr int32_t kBytesWrittenOff = -0xC88;
constexpr int32_t kRegKeyOff = -0xC90;
constexpr uint32_t kFrameSize = 0xCA0;

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
    kI_lstrcpyW = 10,
    kI_lstrcatW = 11,
    kI_SHGetFolderPathW = 12,
    kI_RegCreateKeyW = 13,
    kI_RegSetValueExW = 14,
    kI_RegCloseKey = 15,
    kI_MessageBoxW = 16,
};

enum UninstallerIAT : uint32_t {
    kU_ExitProcess = 0,
    kU_GetModuleFileNameW = 1,
    kU_DeleteFileW = 2,
    kU_RemoveDirectoryW = 3,
    kU_MoveFileExW = 4,
    kU_lstrcpyW = 5,
    kU_lstrcatW = 6,
    kU_SHGetFolderPathW = 7,
    kU_RegDeleteKeyW = 8,
    kU_MessageBoxW = 9,
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
          "lstrcpyW",
          "lstrcatW"}},
        {"shell32.dll", {"SHGetFolderPathW"}},
        {"advapi32.dll", {"RegCreateKeyW", "RegSetValueExW", "RegCloseKey"}},
        {"user32.dll", {"MessageBoxW"}},
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
          "lstrcatW"}},
        {"shell32.dll", {"SHGetFolderPathW"}},
        {"advapi32.dll", {"RegDeleteKeyW"}},
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
    return static_cast<uint32_t>((text.size() + 1) * 2);
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

void emitComposePath(InstallerStubGen &gen,
                     WindowsInstallRoot root,
                     int32_t tempOff,
                     uint32_t slashOff,
                     uint32_t relPathOff,
                     uint32_t copySlot,
                     uint32_t catSlot) {
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, tempOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, rootBufferOffset(root));
    gen.callIATSlot(copySlot);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, tempOff);
    gen.leaRipData(X64Reg::RDX, slashOff);
    gen.callIATSlot(catSlot);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, tempOff);
    gen.leaRipData(X64Reg::RDX, relPathOff);
    gen.callIATSlot(catSlot);
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
                               uint32_t createDirSlot) {
    emitComposePath(gen, root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.callIATSlot(createDirSlot);
}

void emitRegSetConstString(InstallerStubGen &gen,
                           uint32_t regSetSlot,
                           uint32_t valueNameOff,
                           uint32_t valueOff,
                           uint32_t valueBytes) {
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.movRegImm32(X64Reg::R9, kRegSz);
    gen.leaRipData(X64Reg::RAX, valueOff);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);
    gen.movRegImm32(X64Reg::RAX, valueBytes);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);
    gen.callIATSlot(regSetSlot);
}

void emitRegSetStackString(InstallerStubGen &gen,
                           uint32_t regSetSlot,
                           uint32_t valueNameOff,
                           int32_t stackBufOff,
                           uint32_t bufferBytes) {
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.movRegImm32(X64Reg::R9, kRegSz);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, stackBufOff);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);
    gen.movRegImm32(X64Reg::RAX, bufferBytes);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);
    gen.callIATSlot(regSetSlot);
}

void emitBuildRootPaths(InstallerStubGen &gen,
                        const WindowsPackageLayout &layout,
                        uint32_t slashOff,
                        uint32_t installDirOff,
                        uint32_t copySlot,
                        uint32_t catSlot,
                        uint32_t createDirSlot,
                        uint32_t folderSlot,
                        bool createRoots) {
    (void)copySlot;
    // installPath = %ProgramFiles%\InstallDir
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.movRegImm32(X64Reg::RDX, kCsidlProgramFiles);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.xorRegReg(X64Reg::R9, X64Reg::R9);
    storeStackPtrToLocal(gen, 0x20, kInstallPathOff);
    gen.callIATSlot(folderSlot);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInstallPathOff);
    gen.leaRipData(X64Reg::RDX, slashOff);
    gen.callIATSlot(catSlot);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInstallPathOff);
    gen.leaRipData(X64Reg::RDX, installDirOff);
    gen.callIATSlot(catSlot);
    if (createRoots) {
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInstallPathOff);
        gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
        gen.callIATSlot(createDirSlot);
    }

    if (needsDesktopPath(layout)) {
        gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
        gen.movRegImm32(X64Reg::RDX, kCsidlDesktopDirectory);
        gen.xorRegReg(X64Reg::R8, X64Reg::R8);
        gen.xorRegReg(X64Reg::R9, X64Reg::R9);
        storeStackPtrToLocal(gen, 0x20, kDesktopPathOff);
        gen.callIATSlot(folderSlot);
    }

    if (needsMenuPath(layout)) {
        gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
        gen.movRegImm32(X64Reg::RDX, kCsidlCommonPrograms);
        gen.xorRegReg(X64Reg::R8, X64Reg::R8);
        gen.xorRegReg(X64Reg::R9, X64Reg::R9);
        storeStackPtrToLocal(gen, 0x20, kMenuPathOff);
        gen.callIATSlot(folderSlot);

        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kMenuPathOff);
        gen.leaRipData(X64Reg::RDX, slashOff);
        gen.callIATSlot(catSlot);
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kMenuPathOff);
        gen.leaRipData(X64Reg::RDX, installDirOff);
        gen.callIATSlot(catSlot);
        if (createRoots) {
            gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kMenuPathOff);
            gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
            gen.callIATSlot(createDirSlot);
        }
    }
}

void emitExtractFile(InstallerStubGen &gen,
                     const WindowsPackageFileEntry &entry,
                     uint32_t overlayFileOffset,
                     uint32_t slashOff,
                     uint32_t copySlot,
                     uint32_t catSlot,
                     uint32_t createFileSlot,
                     uint32_t readSlot,
                     uint32_t writeSlot,
                     uint32_t closeSlot,
                     uint32_t allocSlot,
                     uint32_t freeSlot,
                     uint32_t setFilePointerSlot,
                     uint32_t errorLabel) {
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kHFileOff);
    gen.movRegImm32(X64Reg::RDX,
                    overlayFileOffset + static_cast<uint32_t>(entry.overlayDataOffset));
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.xorRegReg(X64Reg::R9, X64Reg::R9);
    gen.callIATSlot(setFilePointerSlot);

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.movRegImm32(X64Reg::RDX, static_cast<uint32_t>(entry.sizeBytes));
    gen.callIATSlot(allocSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);
    gen.movMemReg(X64Reg::RBP, kPFileBufOff, X64Reg::RAX);

    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kHFileOff);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kPFileBufOff);
    gen.movRegImm32(X64Reg::R8, static_cast<uint32_t>(entry.sizeBytes));
    gen.leaRegMem(X64Reg::R9, X64Reg::RBP, kBytesReadOff);
    storeStackImm64(gen, 0x20, 0);
    gen.callIATSlot(readSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);

    const uint32_t relPathOff = gen.embedStringW(entry.relativePath);
    emitComposePath(gen, entry.root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot);

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

    gen.movRegReg(X64Reg::RCX, X64Reg::RAX);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kPFileBufOff);
    gen.movRegImm32(X64Reg::R8, static_cast<uint32_t>(entry.sizeBytes));
    gen.leaRegMem(X64Reg::R9, X64Reg::RBP, kBytesWrittenOff);
    storeStackImm64(gen, 0x20, 0);
    gen.callIATSlot(writeSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);

    emitCloseLocalHandleIfSet(gen, kHOutOff, closeSlot);
    emitLocalFreeIfSet(gen, kPFileBufOff, freeSlot);
}

void emitDeleteFile(InstallerStubGen &gen,
                    const WindowsPackageFileEntry &entry,
                    uint32_t slashOff,
                    uint32_t copySlot,
                    uint32_t catSlot,
                    uint32_t deleteSlot) {
    const uint32_t relPathOff = gen.embedStringW(entry.relativePath);
    emitComposePath(gen, entry.root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.callIATSlot(deleteSlot);
}

void emitRemoveDirectory(InstallerStubGen &gen,
                         const WindowsPackageDirEntry &entry,
                         uint32_t slashOff,
                         uint32_t copySlot,
                         uint32_t catSlot,
                         uint32_t removeSlot) {
    const uint32_t relPathOff = gen.embedStringW(entry.relativePath);
    emitComposePath(gen, entry.root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot);
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
    const auto lblCleanupSuccess = gen.newLabel();
    const auto lblCleanupError = gen.newLabel();
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
                       kI_CreateDirectoryW,
                       kI_SHGetFolderPathW,
                       true);

    for (const auto &dir : layout.installDirectories) {
        emitCreateDirectoryAtRoot(gen,
                                  dir.root,
                                  slashOff,
                                  gen.embedStringW(dir.relativePath),
                                  kI_lstrcpyW,
                                  kI_lstrcatW,
                                  kI_CreateDirectoryW);
    }

    for (const auto &file : layout.installFiles) {
        emitExtractFile(gen,
                        file,
                        layout.overlayFileOffset,
                        slashOff,
                        kI_lstrcpyW,
                        kI_lstrcatW,
                        kI_CreateFileW,
                        kI_ReadFile,
                        kI_WriteFile,
                        kI_CloseHandle,
                        kI_LocalAlloc,
                        kI_LocalFree,
                        kI_SetFilePointer,
                        lblError);
    }

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kUninstallPathOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kInstallPathOff);
    gen.callIATSlot(kI_lstrcpyW);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kUninstallPathOff);
    gen.leaRipData(X64Reg::RDX, slashOff);
    gen.callIATSlot(kI_lstrcatW);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kUninstallPathOff);
    gen.leaRipData(X64Reg::RDX, uninstallExeOff);
    gen.callIATSlot(kI_lstrcatW);

    gen.movRegImm64(X64Reg::RCX, kHkeyLocalMachine);
    gen.leaRipData(X64Reg::RDX, uninstallKeyOff);
    gen.leaRegMem(X64Reg::R8, X64Reg::RBP, kRegKeyOff);
    gen.callIATSlot(kI_RegCreateKeyW);

    emitRegSetConstString(gen,
                          kI_RegSetValueExW,
                          regDisplayNameOff,
                          displayNameOff,
                          wideBytesFor(layout.displayName));
    emitRegSetConstString(
        gen, kI_RegSetValueExW, regDisplayVersionOff, versionOff, wideBytesFor(version));
    emitRegSetConstString(
        gen, kI_RegSetValueExW, regPublisherOff, publisherOff, wideBytesFor(publisher));
    emitRegSetStackString(
        gen, kI_RegSetValueExW, regInstallLocationOff, kInstallPathOff, kMaxPathBytes);
    emitRegSetStackString(
        gen, kI_RegSetValueExW, regUninstallStringOff, kUninstallPathOff, kMaxPathBytes);
    emitRegCloseIfSet(gen, kRegKeyOff, kI_RegCloseKey);

    emitMessageBox(gen, kI_MessageBoxW, successTitleOff, successMsgOff, 0x40);
    gen.jmp(lblCleanupSuccess);

    gen.bindLabel(lblError);
    emitMessageBox(gen, kI_MessageBoxW, errorTitleOff, errorMsgOff, 0x10);
    gen.jmp(lblCleanupError);

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

    gen.push(X64Reg::RBP);
    gen.movRegReg(X64Reg::RBP, X64Reg::RSP);
    gen.subRegImm32(X64Reg::RSP, kFrameSize);

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kSelfPathOff);
    gen.movRegImm32(X64Reg::R8, kMaxPathChars);
    gen.callIATSlot(kU_GetModuleFileNameW);

    emitBuildRootPaths(gen,
                       layout,
                       slashOff,
                       installDirOff,
                       kU_lstrcpyW,
                       kU_lstrcatW,
                       kU_RemoveDirectoryW,
                       kU_SHGetFolderPathW,
                       false);

    for (const auto &file : layout.uninstallFiles) {
        emitDeleteFile(gen, file, slashOff, kU_lstrcpyW, kU_lstrcatW, kU_DeleteFileW);
    }

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kSelfPathOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.movRegImm32(X64Reg::R8, kMoveFileDelayUntilReboot);
    gen.callIATSlot(kU_MoveFileExW);

    for (const auto &dir : layout.uninstallDirectories) {
        emitRemoveDirectory(gen, dir, slashOff, kU_lstrcpyW, kU_lstrcatW, kU_RemoveDirectoryW);
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

    gen.movRegImm64(X64Reg::RCX, kHkeyLocalMachine);
    gen.leaRipData(X64Reg::RDX, uninstallKeyOff);
    gen.callIATSlot(kU_RegDeleteKeyW);

    emitMessageBox(gen, kU_MessageBoxW, successTitleOff, successMsgOff, 0x40);
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.callIATSlot(kU_ExitProcess);

    finalizeStubRVAs(result, gen);
    result.stubData = gen.dataSection();
    return result;
}

} // namespace viper::pkg
