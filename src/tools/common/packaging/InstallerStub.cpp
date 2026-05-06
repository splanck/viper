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
constexpr uint32_t kFileOperationDelete = 0x0003u;
constexpr uint32_t kFileOperationSilentFlags = 0x0414u;
constexpr uint32_t kRegNone = 0u;
constexpr uint32_t kRegSz = 1u;
constexpr uint32_t kRegExpandSz = 2u;
constexpr uint32_t kErrorFileNotFound = 2u;
constexpr uint32_t kErrorPathNotFound = 3u;
constexpr uint32_t kErrorDirNotEmpty = 145u;
constexpr uint32_t kErrorAlreadyExists = 183u;
constexpr uint32_t kErrorMoreData = 234u;
constexpr uint32_t kInstallerCopyChunkBytes = 1024u * 1024u;
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
constexpr int32_t kPathOriginalOff = -0x70000;
constexpr int32_t kPathExpectedOff = -0x80000;
constexpr int32_t kHFileOff = -0x80008;
constexpr int32_t kHOutOff = -0x80010;
constexpr int32_t kPFileBufOff = -0x80018;
constexpr int32_t kBytesReadOff = -0x80020;
constexpr int32_t kBytesWrittenOff = -0x80028;
constexpr int32_t kRegKeyOff = -0x80030;
constexpr int32_t kRemainingBytesOff = -0x80038;
constexpr int32_t kCurrentFileOffsetOff = -0x80040;
constexpr int32_t kCurrentChunkBytesOff = -0x80048;
constexpr int32_t kCrcOff = -0x80050;
constexpr int32_t kPathUpdatedOff = -0x80058;
constexpr int32_t kFileOpStructOff = -0x800A0;
constexpr uint32_t kFrameSize = 0x80100;

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
    kI_lstrcmpW = 15,
    kI_lstrcmpiW = 16,
    kI_GetLastError = 17,
    kI_SHGetFolderPathW = 18,
    kI_SHFileOperationW = 19,
    kI_RegCreateKeyW = 20,
    kI_RegSetValueExW = 21,
    kI_RegCloseKey = 22,
    kI_RegOpenKeyW = 23,
    kI_RegQueryValueExW = 24,
    kI_RegDeleteValueW = 25,
    kI_RegDeleteKeyW = 26,
    kI_MessageBoxW = 27,
    kI_SendMessageTimeoutW = 28,
    kI_RtlComputeCrc32 = 29,
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
    kU_lstrcmpW = 8,
    kU_lstrcmpiW = 9,
    kU_GetLastError = 10,
    kU_SHGetFolderPathW = 11,
    kU_RegOpenKeyW = 12,
    kU_RegCreateKeyW = 13,
    kU_RegCloseKey = 14,
    kU_RegQueryValueExW = 15,
    kU_RegDeleteValueW = 16,
    kU_RegDeleteKeyW = 17,
    kU_RegSetValueExW = 18,
    kU_MessageBoxW = 19,
    kU_SendMessageTimeoutW = 20,
};

// Returns the ordered PEImport list for the installer PE. Slot indices here
// must match the InstallerIAT enum above — the order is load-bearing.
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
          "lstrcmpW",
          "lstrcmpiW",
          "GetLastError"}},
        {"shell32.dll", {"SHGetFolderPathW", "SHFileOperationW"}},
        {"advapi32.dll",
         {"RegCreateKeyW",
          "RegSetValueExW",
          "RegCloseKey",
          "RegOpenKeyW",
          "RegQueryValueExW",
          "RegDeleteValueW",
          "RegDeleteKeyW"}},
        {"user32.dll", {"MessageBoxW", "SendMessageTimeoutW"}},
        {"ntdll.dll", {"RtlComputeCrc32"}},
    };
}

// Returns the ordered PEImport list for the uninstaller PE. Slot indices must
// match the UninstallerIAT enum above.
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
          "lstrlenW",
          "lstrcmpW",
          "lstrcmpiW",
          "GetLastError"}},
        {"shell32.dll", {"SHGetFolderPathW"}},
        {"advapi32.dll",
         {"RegOpenKeyW",
          "RegCreateKeyW",
          "RegCloseKey",
          "RegQueryValueExW",
          "RegDeleteValueW",
          "RegDeleteKeyW",
          "RegSetValueExW"}},
        {"user32.dll", {"MessageBoxW", "SendMessageTimeoutW"}},
    };
}

// Round value up to the next multiple of alignment (alignment must be a power of 2).
uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

// Resolve the payload architecture to the bootstrap PE machine type.
// ARM64 payloads still use an x64 bootstrap so the installer runs under
// Windows-on-ARM emulation while deploying an ARM64 application.
std::string resolveBootstrapArch(const std::string &payloadArch) {
    if (payloadArch.empty() || payloadArch == "x64")
        return "x64";
    if (payloadArch == "arm64")
        return "x64";
    throw std::runtime_error("unsupported Windows package architecture '" + payloadArch + "'");
}

// Compute the byte offset within the .rdata section where the IAT begins.
// Layout: IDT (20 bytes * (N+1)) + ILTs + hint/name table + DLL name strings,
// rounded up to 8-byte alignment. This offset is needed to patch RIP-relative
// IAT call targets in the emitted machine code.
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

// Finalize all section RVAs, patch IAT call offsets in the emitted code, and
// populate stub.textSection. Must be called once after all emit* calls are done.
// The .text section is placed at kTextRVA; .rdata immediately follows (aligned).
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

// Emit code to zero an 8-byte local variable at [RBP+off] using xor/mov.
void zeroLocalQword(InstallerStubGen &gen, int32_t off) {
    gen.xorRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.movMemReg(X64Reg::RBP, off, X64Reg::RAX);
}

// Emit code to store a 64-bit immediate to [RSP+off] (the Windows x64 shadow
// space / argument area for the 5th+ arguments in a call sequence).
void storeStackImm64(InstallerStubGen &gen, int32_t off, uint64_t imm) {
    gen.movRegImm64(X64Reg::RAX, imm);
    gen.movMemReg(X64Reg::RSP, off, X64Reg::RAX);
}

// Emit code to compute LEA [RBP+localOff] into RAX and store it at [RSP+stackOff].
// Used to pass a pointer to a stack-resident buffer as a 5th+ argument.
void storeStackPtrToLocal(InstallerStubGen &gen, int32_t stackOff, int32_t localOff) {
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, localOff);
    gen.movMemReg(X64Reg::RSP, stackOff, X64Reg::RAX);
}

// Return the byte count of the UTF-16LE encoding of text including the null terminator.
// Throws if the result exceeds UINT32_MAX (needed by RegSetValueExW cbData).
uint32_t wideBytesFor(const std::string &text) {
    const size_t bytes = (utf16CodeUnitCountFromUtf8(text) + 1) * 2;
    if (bytes > UINT32_MAX)
        throw std::runtime_error("Windows installer string is too large");
    return static_cast<uint32_t>(bytes);
}

// Return the install directory name, falling back to displayName if installDirName is empty.
std::string installDirNameFor(const WindowsPackageLayout &layout) {
    return layout.installDirName.empty() ? layout.displayName : layout.installDirName;
}

// Return the identifier used for registry keys, preferring layout.identifier,
// then normalizing the executable name, then the display name.
std::string registryIdFor(const WindowsPackageLayout &layout) {
    if (!layout.identifier.empty())
        return layout.identifier;
    if (!layout.executableName.empty())
        return normalizeExecName(layout.executableName);
    return normalizeExecName(layout.displayName);
}

// Convert forward slashes to backslashes for use as a Windows path component.
std::string windowsPathFragment(std::string text) {
    for (char &ch : text) {
        if (ch == '/')
            ch = '\\';
    }
    return text;
}

// Build the full HKLM registry key path for the application's uninstall entry.
std::string uninstallKeyPathFor(const WindowsPackageLayout &layout) {
    return "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + registryIdFor(layout);
}

// Return true if the desktop path buffer must be resolved at install/uninstall time —
// i.e. if a desktop shortcut is requested or any file entry targets DesktopDir.
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

// Return true if the Start Menu path buffer must be resolved — i.e. if a Start Menu
// shortcut is requested or any file/dir entry targets StartMenuDir.
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

// Map a WindowsInstallRoot enum value to the corresponding stack-frame buffer offset.
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

// Emit a null-guarded CloseHandle call for the HANDLE stored at [RBP+handleOff],
// then zero the slot. Safe to call even when the handle is already zero/null.
void emitCloseLocalHandleIfSet(InstallerStubGen &gen, int32_t handleOff, uint32_t closeSlot) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, handleOff);
    gen.testRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.jz(lblSkip);
    gen.callIATSlot(closeSlot);
    zeroLocalQword(gen, handleOff);
    gen.bindLabel(lblSkip);
}

// Emit a null-guarded LocalFree call for the pointer stored at [RBP+ptrOff],
// then zero the slot. Safe to call even if the pointer is already null.
void emitLocalFreeIfSet(InstallerStubGen &gen, int32_t ptrOff, uint32_t freeSlot) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, ptrOff);
    gen.testRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.jz(lblSkip);
    gen.callIATSlot(freeSlot);
    zeroLocalQword(gen, ptrOff);
    gen.bindLabel(lblSkip);
}

// Emit a null-guarded RegCloseKey call for the HKEY stored at [RBP+keyOff],
// then zero the slot. Safe to call even if the key is already zero/null.
void emitRegCloseIfSet(InstallerStubGen &gen, int32_t keyOff, uint32_t closeSlot) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, keyOff);
    gen.testRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.jz(lblSkip);
    gen.callIATSlot(closeSlot);
    zeroLocalQword(gen, keyOff);
    gen.bindLabel(lblSkip);
}

// Emit an lstrcatW of an embedded (RIP-relative) wide string onto [RBP+destOff],
// guarded by a length check against kMaxPathChars. Jumps to errorLabel on overflow.
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

// Emit an lstrcatW of a stack buffer at [RBP+srcOff] onto [RBP+destOff],
// guarded by a length check against kMaxPathChars. Jumps to errorLabel on overflow.
void emitCheckedCatStack(InstallerStubGen &gen,
                         int32_t destOff,
                         int32_t srcOff,
                         uint32_t catSlot,
                         uint32_t strlenSlot,
                         uint32_t errorLabel) {
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, srcOff);
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

// Emit an lstrcatW of the wide string pointed to by srcReg onto [RBP+destOff],
// guarded by a length check against kMaxPathChars. Jumps to errorLabel on overflow.
void emitCheckedCatReg(InstallerStubGen &gen,
                       int32_t destOff,
                       X64Reg srcReg,
                       uint32_t catSlot,
                       uint32_t strlenSlot,
                       uint32_t errorLabel) {
    gen.movMemReg(X64Reg::RBP, kBytesWrittenOff, srcReg);

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

// Emit a CreateDirectoryW call for RCX (already set by caller) treating
// ERROR_ALREADY_EXISTS as success and jumping to errorLabel on any other failure.
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

// Emit a cmp reg, value using R10 as scratch to hold the 32-bit immediate.
// Needed because the direct cmp-reg-imm32 encoding sign-extends; using R10
// for the immediate avoids that when comparing against unsigned values.
void emitCmpRegU32(InstallerStubGen &gen, X64Reg reg, uint32_t value) {
    gen.movRegImm32(X64Reg::R10, value);
    gen.cmpRegReg(reg, X64Reg::R10);
}

// Emit code to compose a full path into [RBP+tempOff]: copies the resolved root
// path (install/desktop/menu) then appends "\" and the relative path. Jumps to
// errorLabel on path overflow.
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

// Emit a MessageBoxW call with NULL parent window, given message/title embedded
// strings, and the specified flags (e.g. MB_ICONINFORMATION=0x40, MB_ICONERROR=0x10).
void emitMessageBox(
    InstallerStubGen &gen, uint32_t slot, uint32_t titleOff, uint32_t messageOff, uint32_t flags) {
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.leaRipData(X64Reg::RDX, messageOff);
    gen.leaRipData(X64Reg::R8, titleOff);
    gen.movRegImm32(X64Reg::R9, flags);
    gen.callIATSlot(slot);
}

// Emit code to create a directory at root\relPath, composing the full path into
// the temp buffer and calling CreateDirectoryW (ERROR_ALREADY_EXISTS is tolerated).
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

// Emit RegSetValueExW on kRegKeyOff to write a named REG_SZ value from an
// embedded (RIP-relative) wide string. valueBytes is the full byte count
// including null terminator. Jumps to errorLabel on failure.
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

// Emit RegSetValueExW on kRegKeyOff to write a named REG_NONE value with zero
// length and NULL data. Used to stamp a ProgID name into OpenWithProgids.
void emitRegSetConstNoneValue(InstallerStubGen &gen,
                              uint32_t regSetSlot,
                              uint32_t valueNameOff,
                              uint32_t errorLabel) {
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.movRegImm32(X64Reg::R9, kRegNone);
    gen.xorRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);
    gen.callIATSlot(regSetSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
}

// Emit RegSetValueExW on kRegKeyOff to write the default (unnamed) value as a
// REG_SZ from an embedded wide string. Used to set the ProgID description.
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

// Emit RegSetValueExW on kRegKeyOff to write a named value from a stack buffer,
// computing the byte length at runtime via lstrlenW. valueType selects
// REG_SZ vs REG_EXPAND_SZ. Jumps to errorLabel on failure.
void emitRegSetStackString(InstallerStubGen &gen,
                           uint32_t regSetSlot,
                           uint32_t strlenSlot,
                           uint32_t valueNameOff,
                           int32_t stackBufOff,
                           uint32_t valueType,
                           uint32_t errorLabel) {
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.movRegImm32(X64Reg::R9, valueType);
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
    gen.movRegImm32(X64Reg::R9, valueType);
    gen.callIATSlot(regSetSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
}

// Convenience overload of emitRegSetStackString that defaults valueType to REG_SZ.
void emitRegSetStackString(InstallerStubGen &gen,
                           uint32_t regSetSlot,
                           uint32_t strlenSlot,
                           uint32_t valueNameOff,
                           int32_t stackBufOff,
                           uint32_t errorLabel) {
    emitRegSetStackString(
        gen, regSetSlot, strlenSlot, valueNameOff, stackBufOff, kRegSz, errorLabel);
}

// Emit RegSetValueExW on kRegKeyOff to write the default (unnamed) value from a
// stack buffer, computing the byte length at runtime. Used for Open command strings.
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

// Emit RegQueryValueExW on kRegKeyOff to read a named value into a stack buffer.
// bufferBytes is the buffer capacity. Jumps to missingLabel if the key/value is
// absent or any error occurs (including ERROR_MORE_DATA).
void emitRegQueryStackString(InstallerStubGen &gen,
                             uint32_t querySlot,
                             uint32_t valueNameOff,
                             int32_t stackBufOff,
                             uint32_t bufferBytes,
                             uint32_t missingLabel) {
    gen.movMemImm32(X64Reg::RBP, stackBufOff, 0);
    zeroLocalQword(gen, kBytesReadOff);
    zeroLocalQword(gen, kBytesWrittenOff);
    gen.movMemImm32(X64Reg::RBP, kBytesReadOff, bufferBytes);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.leaRegMem(X64Reg::R9, X64Reg::RBP, kBytesWrittenOff);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, stackBufOff);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, kBytesReadOff);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);
    gen.callIATSlot(querySlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(missingLabel);
}

// Emit code to append the separator string only when [RBP+destOff] is non-empty.
// Used to insert ";" between PATH tokens without a leading separator.
void emitAppendSeparatorIfNonEmpty(InstallerStubGen &gen,
                                   int32_t destOff,
                                   uint32_t separatorOff,
                                   uint32_t catSlot,
                                   uint32_t strlenSlot,
                                   uint32_t errorLabel) {
    const auto lblSkip = gen.newLabel();
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, destOff);
    gen.callIATSlot(strlenSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblSkip);
    emitCheckedCatEmbedded(gen, destOff, separatorOff, catSlot, strlenSlot, errorLabel);
    gen.bindLabel(lblSkip);
}

// Emit SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, "Environment", ...)
// so that running applications (e.g. Explorer) pick up the updated PATH immediately.
void emitBroadcastEnvironmentChange(InstallerStubGen &gen,
                                    uint32_t sendSlot,
                                    uint32_t environmentOff) {
    gen.movRegImm32(X64Reg::RCX, 0xFFFFu);
    gen.movRegImm32(X64Reg::RDX, 0x001Au);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.leaRipData(X64Reg::R9, environmentOff);
    storeStackImm64(gen, 0x20, 0x0002u);
    storeStackImm64(gen, 0x28, 5000u);
    storeStackImm64(gen, 0x30, 0u);
    gen.callIATSlot(sendSlot);
}

// Emit RegCreateKeyW under HKLM for the key path at keyOff, closing any
// previously open key in kRegKeyOff first. Jumps to errorLabel on failure.
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

// Emit RegDeleteKeyW under HKLM for the key path at keyOff. Errors are ignored
// (key may not exist during rollback or uninstall).
void emitRegDeleteConstKey(InstallerStubGen &gen, uint32_t deleteSlot, uint32_t keyOff) {
    gen.movRegImm64(X64Reg::RCX, kHkeyLocalMachine);
    gen.leaRipData(X64Reg::RDX, keyOff);
    gen.callIATSlot(deleteSlot);
}

// Build the Software\Classes\.<ext> registry key path for a file association.
// Ensures the extension has a leading dot.
std::string extensionKeyFor(const WindowsFileAssociationEntry &assoc) {
    std::string ext = assoc.extension;
    if (ext.empty() || ext.front() != '.')
        ext.insert(ext.begin(), '.');
    return "Software\\Classes\\" + ext;
}

// Build the Software\Classes\<ProgID> registry key path for a file association.
std::string progIdKeyFor(const WindowsFileAssociationEntry &assoc) {
    return "Software\\Classes\\" + assoc.progId;
}

// Build the Software\Classes\.<ext>\OpenWithProgids registry key path.
std::string openWithProgIdsKeyFor(const WindowsFileAssociationEntry &assoc) {
    return extensionKeyFor(assoc) + "\\OpenWithProgids";
}

// Emit RegOpenKeyW under HKLM for the key path at keyOff, closing any previously
// open key first. Jumps to missingLabel if the key does not exist.
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

// Emit conditional RegDeleteValueW: opens the key at keyOff, deletes the named
// value, then closes the key. Silently skips if the key does not exist.
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

// Emit code to query a registry value and compare it against an embedded expected
// string. Reads into kTempPathOff, then calls lstrcmpW. Jumps to notEqualLabel
// if the query fails, the byte count mismatches, or the string comparison fails.
void emitRegQueryConstStringEquals(InstallerStubGen &gen,
                                   uint32_t querySlot,
                                   uint32_t strcmpSlot,
                                   uint32_t valueNameOff,
                                   uint32_t expectedValueOff,
                                   uint32_t expectedBytes,
                                   uint32_t notEqualLabel) {
    zeroLocalQword(gen, kBytesReadOff);
    zeroLocalQword(gen, kBytesWrittenOff);
    gen.movMemImm32(X64Reg::RBP, kBytesReadOff, expectedBytes);

    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.leaRegMem(X64Reg::R9, X64Reg::RBP, kBytesWrittenOff);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, kTempPathOff);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, kBytesReadOff);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);
    gen.callIATSlot(querySlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(notEqualLabel);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kBytesReadOff);
    emitCmpRegU32(gen, X64Reg::RAX, expectedBytes);
    gen.jnz(notEqualLabel);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.leaRipData(X64Reg::RDX, expectedValueOff);
    gen.callIATSlot(strcmpSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(notEqualLabel);
}

// Emit code to check whether a named registry value exists under kRegKeyOff.
// Jumps to existsLabel if the query succeeds or returns ERROR_MORE_DATA.
void emitRegQueryValueExists(InstallerStubGen &gen,
                             uint32_t querySlot,
                             uint32_t valueNameOff,
                             uint32_t existsLabel) {
    zeroLocalQword(gen, kBytesReadOff);
    zeroLocalQword(gen, kBytesWrittenOff);
    gen.movMemImm32(X64Reg::RBP, kBytesReadOff, kMaxPathChars * 2u);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.leaRegMem(X64Reg::R9, X64Reg::RBP, kBytesWrittenOff);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, kTempPathOff);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, kBytesReadOff);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);
    gen.callIATSlot(querySlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(existsLabel);
    gen.cmpRegImm32(X64Reg::RAX, kErrorMoreData);
    gen.jz(existsLabel);
}

// Emit code to delete the ProgID subtree (shell/open/command, shell/open, shell,
// and the ProgID key itself) only if we own it — i.e. only if the VAPSOwner marker
// matches our identifier. Silently skips if the key is absent or owned by another app.
void emitDeleteProgIdTreeIfOwned(InstallerStubGen &gen,
                                 uint32_t openSlot,
                                 uint32_t closeSlot,
                                 uint32_t querySlot,
                                 uint32_t strcmpSlot,
                                 uint32_t deleteValueSlot,
                                 uint32_t deleteSlot,
                                 uint32_t progKeyOff,
                                 uint32_t markerNameOff,
                                 uint32_t markerValueOff,
                                 uint32_t markerValueBytes,
                                 uint32_t commandKeyOff,
                                 uint32_t openKeyOff,
                                 uint32_t shellKeyOff) {
    const auto lblSkip = gen.newLabel();
    const auto lblNotOwned = gen.newLabel();
    emitRegOpenConstKeyIfExists(gen, openSlot, closeSlot, progKeyOff, lblSkip);
    emitRegQueryConstStringEquals(gen,
                                  querySlot,
                                  strcmpSlot,
                                  markerNameOff,
                                  markerValueOff,
                                  markerValueBytes,
                                  lblNotOwned);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, markerNameOff);
    gen.callIATSlot(deleteValueSlot);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    emitRegDeleteConstKey(gen, deleteSlot, commandKeyOff);
    emitRegDeleteConstKey(gen, deleteSlot, openKeyOff);
    emitRegDeleteConstKey(gen, deleteSlot, shellKeyOff);
    emitRegDeleteConstKey(gen, deleteSlot, progKeyOff);
    gen.jmp(lblSkip);
    gen.bindLabel(lblNotOwned);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.bindLabel(lblSkip);
}

// Emit code to register all file associations from layout.fileAssociations in
// the Windows registry. For each association this creates:
//   - Software\Classes\.<ext> with Content Type (if not already set) and VAPSContentTypeOwner
//   - Software\Classes\.<ext>\OpenWithProgids\<ProgID> = REG_NONE
//   - Software\Classes\<ProgID> with VAPSOwner marker, default description
//   - Software\Classes\<ProgID>\shell\open\command = "<exe>" [args] "%1"
void emitRegisterFileAssociations(InstallerStubGen &gen,
                                  const WindowsPackageLayout &layout,
                                  uint32_t slashOff,
                                  uint32_t copySlot,
                                  uint32_t catSlot,
                                  uint32_t createSlot,
                                  uint32_t setValueSlot,
                                  uint32_t querySlot,
                                  uint32_t strcmpSlot,
                                  uint32_t closeSlot,
                                  uint32_t strlenSlot,
                                  uint32_t errorLabel) {
    if (layout.fileAssociations.empty())
        return;
    (void)strcmpSlot;

    const uint32_t regContentTypeOff = gen.embedStringW("Content Type");
    const uint32_t regContentTypeOwnerOff = gen.embedStringW("VAPSContentTypeOwner");
    const uint32_t regOwnerMarkerOff = gen.embedStringW("VAPSOwner");
    const std::string ownerMarker =
        layout.identifier.empty() ? layout.displayName : layout.identifier;
    const uint32_t ownerMarkerBytes = wideBytesFor(ownerMarker);
    if (ownerMarkerBytes > kMaxPathChars * 2u)
        throw std::runtime_error("Windows registry owner marker is too long");
    const uint32_t ownerMarkerValueOff = gen.embedStringW(ownerMarker);
    const std::string associationExecutable =
        layout.fileAssociationExecutableRelativePath.empty()
            ? layout.executableName
            : windowsPathFragment(layout.fileAssociationExecutableRelativePath);
    const uint32_t exeNameOff = gen.embedStringW(associationExecutable);
    const uint32_t quoteOff = gen.embedStringW("\"");
    const uint32_t quotedFileArgOff = gen.embedStringW(" \"%1\"");

    for (const auto &assoc : layout.fileAssociations) {
        const uint32_t commandArgsOff =
            assoc.openCommandArguments.empty()
                ? 0
                : gen.embedStringW(" " + assoc.openCommandArguments);
        const uint32_t extKeyOff = gen.embedStringW(extensionKeyFor(assoc));
        const uint32_t progIdOff = gen.embedStringW(assoc.progId);
        emitRegCreateConstKey(gen, createSlot, extKeyOff, errorLabel);
        if (!assoc.mimeType.empty()) {
            const auto lblContentTypeExists = gen.newLabel();
            const auto lblContentTypeDone = gen.newLabel();
            const uint32_t mimeOff = gen.embedStringW(assoc.mimeType);
            emitRegQueryValueExists(gen, querySlot, regContentTypeOff, lblContentTypeExists);
            emitRegSetConstString(gen,
                                  setValueSlot,
                                  regContentTypeOff,
                                  mimeOff,
                                  wideBytesFor(assoc.mimeType),
                                  errorLabel);
            emitRegSetConstString(gen,
                                  setValueSlot,
                                  regContentTypeOwnerOff,
                                  ownerMarkerValueOff,
                                  ownerMarkerBytes,
                                  errorLabel);
            gen.jmp(lblContentTypeDone);
            gen.bindLabel(lblContentTypeExists);
            gen.bindLabel(lblContentTypeDone);
        }
        emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);

        const uint32_t openWithKeyOff = gen.embedStringW(openWithProgIdsKeyFor(assoc));
        emitRegCreateConstKey(gen, createSlot, openWithKeyOff, errorLabel);
        emitRegSetConstNoneValue(gen, setValueSlot, progIdOff, errorLabel);
        emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);

        const uint32_t progKeyOff = gen.embedStringW(progIdKeyFor(assoc));
        const std::string description =
            assoc.description.empty() ? layout.displayName : assoc.description;
        const uint32_t descriptionOff = gen.embedStringW(description);
        emitRegCreateConstKey(gen, createSlot, progKeyOff, errorLabel);
        emitRegSetConstString(gen,
                              setValueSlot,
                              regOwnerMarkerOff,
                              ownerMarkerValueOff,
                              ownerMarkerBytes,
                              errorLabel);
        emitRegSetDefaultConstString(
            gen, setValueSlot, descriptionOff, wideBytesFor(description), errorLabel);
        emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);

        const uint32_t commandKeyOff =
            gen.embedStringW(progIdKeyFor(assoc) + "\\shell\\open\\command");
        emitRegCreateConstKey(gen, createSlot, commandKeyOff, errorLabel);
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
        gen.leaRipData(X64Reg::RDX, quoteOff);
        gen.callIATSlot(copySlot);
        emitCheckedCatStack(gen, kTempPathOff, kInstallPathOff, catSlot, strlenSlot, errorLabel);
        emitCheckedCatEmbedded(gen, kTempPathOff, slashOff, catSlot, strlenSlot, errorLabel);
        emitCheckedCatEmbedded(gen, kTempPathOff, exeNameOff, catSlot, strlenSlot, errorLabel);
        emitCheckedCatEmbedded(gen, kTempPathOff, quoteOff, catSlot, strlenSlot, errorLabel);
        if (!assoc.openCommandArguments.empty())
            emitCheckedCatEmbedded(
                gen, kTempPathOff, commandArgsOff, catSlot, strlenSlot, errorLabel);
        emitCheckedCatEmbedded(
            gen, kTempPathOff, quotedFileArgOff, catSlot, strlenSlot, errorLabel);
        emitRegSetDefaultStackString(gen, setValueSlot, strlenSlot, kTempPathOff, errorLabel);
        emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    }
}

// Emit code to unregister all file associations owned by this application.
// Removes OpenWithProgids entries, Content Type if we set it (VAPSContentTypeOwner),
// and the full ProgID subtree if we own it (VAPSOwner marker matches our identifier).
// Silently skips any key that is missing or owned by another app.
void emitUnregisterFileAssociations(InstallerStubGen &gen,
                                    const WindowsPackageLayout &layout,
                                    uint32_t openSlot,
                                    uint32_t closeSlot,
                                    uint32_t querySlot,
                                    uint32_t strcmpSlot,
                                    uint32_t deleteValueSlot,
                                    uint32_t deleteSlot) {
    const uint32_t ownerMarkerOff = gen.embedStringW("VAPSOwner");
    const uint32_t contentTypeOwnerOff = gen.embedStringW("VAPSContentTypeOwner");
    const std::string ownerMarker =
        layout.identifier.empty() ? layout.displayName : layout.identifier;
    const uint32_t ownerMarkerValueOff = gen.embedStringW(ownerMarker);
    const uint32_t ownerMarkerValueBytes = wideBytesFor(ownerMarker);
    if (ownerMarkerValueBytes > kMaxPathChars * 2u)
        throw std::runtime_error("Windows registry owner marker is too long");
    for (const auto &assoc : layout.fileAssociations) {
        emitRegDeleteNamedValueIfPresent(gen,
                                         openSlot,
                                         closeSlot,
                                         deleteValueSlot,
                                         gen.embedStringW(openWithProgIdsKeyFor(assoc)),
                                         gen.embedStringW(assoc.progId));
        if (!assoc.mimeType.empty()) {
            const auto lblContentDone = gen.newLabel();
            const auto lblContentNotOwned = gen.newLabel();
            emitRegOpenConstKeyIfExists(
                gen, openSlot, closeSlot, gen.embedStringW(extensionKeyFor(assoc)), lblContentDone);
            emitRegQueryConstStringEquals(gen,
                                          querySlot,
                                          strcmpSlot,
                                          contentTypeOwnerOff,
                                          ownerMarkerValueOff,
                                          ownerMarkerValueBytes,
                                          lblContentNotOwned);
            gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
            gen.leaRipData(X64Reg::RDX, gen.embedStringW("Content Type"));
            gen.callIATSlot(deleteValueSlot);
            gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
            gen.leaRipData(X64Reg::RDX, contentTypeOwnerOff);
            gen.callIATSlot(deleteValueSlot);
            emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
            gen.jmp(lblContentDone);
            gen.bindLabel(lblContentNotOwned);
            emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
            gen.bindLabel(lblContentDone);
        }
        emitDeleteProgIdTreeIfOwned(
            gen,
            openSlot,
            closeSlot,
            querySlot,
            strcmpSlot,
            deleteValueSlot,
            deleteSlot,
            gen.embedStringW(progIdKeyFor(assoc)),
            ownerMarkerOff,
            ownerMarkerValueOff,
            ownerMarkerValueBytes,
            gen.embedStringW(progIdKeyFor(assoc) + "\\shell\\open\\command"),
            gen.embedStringW(progIdKeyFor(assoc) + "\\shell\\open"),
            gen.embedStringW(progIdKeyFor(assoc) + "\\shell"));
    }
}

// Emit code to resolve all install root paths via SHGetFolderPathW and store them
// in the stack-frame buffers (kInstallPathOff, kDesktopPathOff, kMenuPathOff).
// If createRoots is true, also emits CreateDirectoryW for each resolved path.
// Only resolves the paths actually needed per needsDesktopPath / needsMenuPath.
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

// Emit code to compose the PATH entry string into [RBP+destOff]: copies
// kInstallPathOff then appends "\<pathRelativePath>" if configured.
void emitComposeInstallPathEntry(InstallerStubGen &gen,
                                 const WindowsPackageLayout &layout,
                                 int32_t destOff,
                                 uint32_t slashOff,
                                 uint32_t copySlot,
                                 uint32_t catSlot,
                                 uint32_t strlenSlot,
                                 uint32_t errorLabel) {
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, destOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kInstallPathOff);
    gen.callIATSlot(copySlot);
    if (!layout.pathRelativePath.empty()) {
        emitCheckedCatEmbedded(gen, destOff, slashOff, catSlot, strlenSlot, errorLabel);
        emitCheckedCatEmbedded(gen,
                               destOff,
                               gen.embedStringW(windowsPathFragment(layout.pathRelativePath)),
                               catSlot,
                               strlenSlot,
                               errorLabel);
    }
}

// Emit code to delete all existing contents of the install root before extraction
// (upgrade path). Builds an "<installDir>\*" glob, constructs an SHFILEOPSTRUCT
// with FO_DELETE | FOF_NOCONFIRMATION, and calls SHFileOperationW. No-op if
// layout.cleanInstallRootBeforeInstall is false.
void emitCleanInstallRootContents(InstallerStubGen &gen,
                                  const WindowsPackageLayout &layout,
                                  uint32_t slashOff,
                                  uint32_t starOff,
                                  uint32_t copySlot,
                                  uint32_t catSlot,
                                  uint32_t strlenSlot,
                                  uint32_t fileOperationSlot,
                                  uint32_t errorLabel) {
    if (!layout.cleanInstallRootBeforeInstall)
        return;

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kPathExpectedOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kInstallPathOff);
    gen.callIATSlot(copySlot);
    emitCheckedCatEmbedded(gen, kPathExpectedOff, slashOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatEmbedded(gen, kPathExpectedOff, starOff, catSlot, strlenSlot, errorLabel);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kPathExpectedOff);
    gen.callIATSlot(strlenSlot);
    gen.cmpRegImm32(X64Reg::RAX, kMaxPathChars - 2);
    gen.ja(errorLabel);
    gen.movMemIndexImm16(X64Reg::RBP, X64Reg::RAX, 1, kPathExpectedOff + 2, 0);

    for (int32_t off = 0; off < 56; off += 8)
        zeroLocalQword(gen, kFileOpStructOff + off);
    gen.movMemImm32(X64Reg::RBP, kFileOpStructOff + 8, kFileOperationDelete);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, kPathExpectedOff);
    gen.movMemReg(X64Reg::RBP, kFileOpStructOff + 16, X64Reg::RAX);
    gen.movMemImm32(X64Reg::RBP, kFileOpStructOff + 32, kFileOperationSilentFlags);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kFileOpStructOff);
    gen.callIATSlot(fileOperationSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kFileOpStructOff + 36);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
}

// Forward declaration — implemented after emitRestorePathFromOriginalLocal
// to allow emitInstallPathUpdate to reference it.
void emitRemovePathEntryTokens(InstallerStubGen &gen,
                               int32_t currentPathOff,
                               int32_t entryOff,
                               int32_t outputPathOff,
                               uint32_t semicolonOff,
                               uint32_t copySlot,
                               uint32_t catSlot,
                               uint32_t strcmpSlot,
                               uint32_t strlenSlot,
                               uint32_t errorLabel);

// Emit code to add the install path entry to the system PATH registry value.
// Checks whether the entry is already present (idempotent). If the uninstall key
// already contains a matching VAPSPathEntry, skips. Otherwise reads the current
// PATH, strips any existing entry (via emitRemovePathEntryTokens), appends the
// new entry, writes it back as REG_EXPAND_SZ, and broadcasts WM_SETTINGCHANGE.
// No-op if layout.addToPath is false.
void emitInstallPathUpdate(InstallerStubGen &gen,
                           const WindowsPackageLayout &layout,
                           uint32_t slashOff,
                           uint32_t semicolonOff,
                           uint32_t uninstallKeyOff,
                           uint32_t envKeyOff,
                           uint32_t regPathValueOff,
                           uint32_t regOriginalPathOff,
                           uint32_t regPathEntryOff,
                           uint32_t environmentOff,
                           uint32_t createSlot,
                           uint32_t openSlot,
                           uint32_t querySlot,
                           uint32_t setValueSlot,
                           uint32_t closeSlot,
                           uint32_t copySlot,
                           uint32_t catSlot,
                           uint32_t strcmpSlot,
                           uint32_t strlenSlot,
                           uint32_t sendSlot,
                           uint32_t errorLabel) {
    if (!layout.addToPath)
        return;
    (void)regOriginalPathOff;

    const auto lblDoAppend = gen.newLabel();
    const auto lblPathMissing = gen.newLabel();
    const auto lblCheckCurrentPath = gen.newLabel();
    const auto lblSkipUpdateClose = gen.newLabel();
    const auto lblSkipUpdate = gen.newLabel();

    emitComposeInstallPathEntry(
        gen, layout, kUninstallPathOff, slashOff, copySlot, catSlot, strlenSlot, errorLabel);

    emitRegOpenConstKeyIfExists(gen, openSlot, closeSlot, uninstallKeyOff, lblCheckCurrentPath);
    emitRegQueryStackString(gen,
                            querySlot,
                            regPathEntryOff,
                            kTempPathOff,
                            kMaxPathChars * 2u,
                            lblCheckCurrentPath);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kUninstallPathOff);
    gen.callIATSlot(strcmpSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblCheckCurrentPath);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.jmp(lblSkipUpdate);

    gen.bindLabel(lblCheckCurrentPath);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    emitRegOpenConstKeyIfExists(gen, openSlot, closeSlot, envKeyOff, lblDoAppend);
    emitRegQueryStackString(
        gen, querySlot, regPathValueOff, kPathOriginalOff, kMaxPathChars * 2u, lblDoAppend);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kPathOriginalOff);
    gen.callIATSlot(copySlot);
    emitRemovePathEntryTokens(gen,
                              kTempPathOff,
                              kUninstallPathOff,
                              kPathExpectedOff,
                              semicolonOff,
                              copySlot,
                              catSlot,
                              strcmpSlot,
                              strlenSlot,
                              errorLabel);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kPathExpectedOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kPathOriginalOff);
    gen.callIATSlot(strcmpSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblSkipUpdateClose);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.jmp(lblDoAppend);

    gen.bindLabel(lblDoAppend);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    emitRegCreateConstKey(gen, createSlot, envKeyOff, errorLabel);
    emitRegQueryStackString(
        gen, querySlot, regPathValueOff, kPathOriginalOff, kMaxPathChars * 2u, lblPathMissing);
    gen.bindLabel(lblPathMissing);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kPathOriginalOff);
    gen.callIATSlot(copySlot);
    emitAppendSeparatorIfNonEmpty(
        gen, kTempPathOff, semicolonOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatStack(
        gen, kTempPathOff, kUninstallPathOff, catSlot, strlenSlot, errorLabel);

    emitRegSetStackString(gen,
                          setValueSlot,
                          strlenSlot,
                          regPathValueOff,
                          kTempPathOff,
                          kRegExpandSz,
                          errorLabel);
    emitBroadcastEnvironmentChange(gen, sendSlot, environmentOff);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.movRegImm32(X64Reg::RAX, 1);
    gen.movMemReg(X64Reg::RBP, kPathUpdatedOff, X64Reg::RAX);
    gen.jmp(lblSkipUpdate);
    gen.bindLabel(lblSkipUpdateClose);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.bindLabel(lblSkipUpdate);
}

// Emit rollback code to restore the system PATH from the saved original value
// in kPathOriginalOff (captured before installation modified PATH). Only runs
// if kPathUpdatedOff is non-zero (i.e. PATH was actually modified). No-op if
// layout.addToPath is false.
void emitRestorePathFromOriginalLocal(InstallerStubGen &gen,
                                      const WindowsPackageLayout &layout,
                                      uint32_t envKeyOff,
                                      uint32_t regPathValueOff,
                                      uint32_t environmentOff,
                                      uint32_t createSlot,
                                      uint32_t setValueSlot,
                                      uint32_t closeSlot,
                                      uint32_t strlenSlot,
                                      uint32_t sendSlot) {
    if (!layout.addToPath)
        return;

    const auto lblSkip = gen.newLabel();
    const auto lblClose = gen.newLabel();
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kPathUpdatedOff);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblSkip);
    emitRegCreateConstKey(gen, createSlot, envKeyOff, lblSkip);
    emitRegSetStackString(gen,
                          setValueSlot,
                          strlenSlot,
                          regPathValueOff,
                          kPathOriginalOff,
                          kRegExpandSz,
                          lblClose);
    emitBroadcastEnvironmentChange(gen, sendSlot, environmentOff);
    gen.bindLabel(lblClose);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.bindLabel(lblSkip);
}

// Emit code to rebuild the PATH string at [RBP+outputPathOff] by iterating the
// semicolon-delimited tokens in [RBP+currentPathOff] and skipping any token that
// matches [RBP+entryOff]. Used both during install (to remove stale entry before
// re-appending) and during uninstall (to remove our entry from PATH).
void emitRemovePathEntryTokens(InstallerStubGen &gen,
                               int32_t currentPathOff,
                               int32_t entryOff,
                               int32_t outputPathOff,
                               uint32_t semicolonOff,
                               uint32_t copySlot,
                               uint32_t catSlot,
                               uint32_t strcmpSlot,
                               uint32_t strlenSlot,
                               uint32_t errorLabel) {
    const auto lblLoop = gen.newLabel();
    const auto lblFindEnd = gen.newLabel();
    const auto lblTokenSeparator = gen.newLabel();
    const auto lblTokenEnd = gen.newLabel();
    const auto lblProcessToken = gen.newLabel();
    const auto lblAppendToken = gen.newLabel();
    const auto lblSkipToken = gen.newLabel();
    const auto lblContinue = gen.newLabel();
    const auto lblDone = gen.newLabel();

    (void)copySlot;
    gen.movMemImm32(X64Reg::RBP, outputPathOff, 0);

    gen.xorRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.bindLabel(lblLoop);
    gen.movMemReg(X64Reg::RBP, kCurrentFileOffsetOff, X64Reg::RAX);

    gen.bindLabel(lblFindEnd);
    gen.movzxRegMemIndex16(X64Reg::R11, X64Reg::RBP, X64Reg::RAX, 0, currentPathOff);
    gen.cmpRegImm32(X64Reg::R11, ';');
    gen.jz(lblTokenSeparator);
    gen.testRegReg(X64Reg::R11, X64Reg::R11);
    gen.jz(lblTokenEnd);
    gen.addRegImm32(X64Reg::RAX, 2);
    gen.jmp(lblFindEnd);

    gen.bindLabel(lblTokenSeparator);
    gen.movMemReg(X64Reg::RBP, kRemainingBytesOff, X64Reg::RAX);
    gen.movMemIndexImm16(X64Reg::RBP, X64Reg::RAX, 0, currentPathOff, 0);
    gen.addRegImm32(X64Reg::RAX, 2);
    gen.movMemReg(X64Reg::RBP, kCurrentChunkBytesOff, X64Reg::RAX);
    gen.jmp(lblProcessToken);

    gen.bindLabel(lblTokenEnd);
    gen.movMemReg(X64Reg::RBP, kRemainingBytesOff, X64Reg::RAX);
    gen.movMemReg(X64Reg::RBP, kCurrentChunkBytesOff, X64Reg::RAX);

    gen.bindLabel(lblProcessToken);
    gen.movRegMem(X64Reg::R10, X64Reg::RBP, kCurrentFileOffsetOff);
    gen.movRegMem(X64Reg::R11, X64Reg::RBP, kRemainingBytesOff);
    gen.cmpRegReg(X64Reg::R10, X64Reg::R11);
    gen.jz(lblSkipToken);
    gen.leaRegMemIndex(X64Reg::RCX, X64Reg::RBP, X64Reg::R10, 0, currentPathOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, entryOff);
    gen.callIATSlot(strcmpSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblSkipToken);

    gen.bindLabel(lblAppendToken);
    emitAppendSeparatorIfNonEmpty(
        gen, outputPathOff, semicolonOff, catSlot, strlenSlot, errorLabel);
    gen.movRegMem(X64Reg::R10, X64Reg::RBP, kCurrentFileOffsetOff);
    gen.leaRegMemIndex(X64Reg::RDX, X64Reg::RBP, X64Reg::R10, 0, currentPathOff);
    emitCheckedCatReg(gen, outputPathOff, X64Reg::RDX, catSlot, strlenSlot, errorLabel);

    gen.bindLabel(lblSkipToken);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kCurrentChunkBytesOff);
    gen.movzxRegMemIndex16(X64Reg::R11, X64Reg::RBP, X64Reg::RAX, 0, currentPathOff);
    gen.testRegReg(X64Reg::R11, X64Reg::R11);
    gen.jz(lblDone);
    gen.jmp(lblContinue);

    gen.bindLabel(lblContinue);
    gen.jmp(lblLoop);

    gen.bindLabel(lblDone);
}

// Emit code to remove our PATH entry during uninstall. Reads VAPSPathEntry from
// the uninstall registry key to find the exact entry that was added, reads the
// current system PATH, removes our entry via emitRemovePathEntryTokens, writes
// the cleaned PATH back as REG_EXPAND_SZ, and broadcasts WM_SETTINGCHANGE.
// No-op if layout.addToPath is false or the uninstall key is missing.
void emitRestorePathFromUninstallKey(InstallerStubGen &gen,
                                     const WindowsPackageLayout &layout,
                                     uint32_t slashOff,
                                     uint32_t semicolonOff,
                                     uint32_t uninstallKeyOff,
                                     uint32_t envKeyOff,
                                     uint32_t regPathValueOff,
                                     uint32_t regPathEntryOff,
                                     uint32_t environmentOff,
                                     uint32_t openSlot,
                                     uint32_t createSlot,
                                     uint32_t setValueSlot,
                                     uint32_t querySlot,
                                     uint32_t closeSlot,
                                     uint32_t copySlot,
                                     uint32_t catSlot,
                                     uint32_t strcmpSlot,
                                     uint32_t strlenSlot,
                                     uint32_t sendSlot,
                                     uint32_t errorLabel) {
    if (!layout.addToPath)
        return;

    const auto lblSkip = gen.newLabel();
    const auto lblEntryMissing = gen.newLabel();
    const auto lblEnvMissing = gen.newLabel();

    emitRegOpenConstKeyIfExists(gen, openSlot, closeSlot, uninstallKeyOff, lblSkip);
    emitRegQueryStackString(gen,
                            querySlot,
                            regPathEntryOff,
                            kUninstallPathOff,
                            kMaxPathChars * 2u,
                            lblEntryMissing);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);

    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.movRegImm64(X64Reg::RCX, kHkeyLocalMachine);
    gen.leaRipData(X64Reg::RDX, envKeyOff);
    gen.leaRegMem(X64Reg::R8, X64Reg::RBP, kRegKeyOff);
    gen.callIATSlot(createSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblEnvMissing);
    emitRegQueryStackString(
        gen, querySlot, regPathValueOff, kTempPathOff, kMaxPathChars * 2u, lblEnvMissing);
    emitRemovePathEntryTokens(gen,
                              kTempPathOff,
                              kUninstallPathOff,
                              kPathExpectedOff,
                              semicolonOff,
                              copySlot,
                              catSlot,
                              strcmpSlot,
                              strlenSlot,
                              errorLabel);
    emitRegSetStackString(gen,
                          setValueSlot,
                          strlenSlot,
                          regPathValueOff,
                          kPathOriginalOff,
                          kRegExpandSz,
                          lblEnvMissing);
    emitBroadcastEnvironmentChange(gen, sendSlot, environmentOff);
    gen.bindLabel(lblEnvMissing);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.jmp(lblSkip);

    gen.bindLabel(lblEntryMissing);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.bindLabel(lblSkip);
}

// Emit code to extract a single file from the ZIP overlay appended to the
// installer PE. Seeks to the precomputed file offset via SetFilePointer, reads
// the data in kInstallerCopyChunkBytes chunks, incrementally computes CRC-32 via
// RtlComputeCrc32, creates the destination file, writes it, and verifies the
// final CRC against entry.crc32. For zero-byte files, just creates the file.
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
    if (entrySize != 0 && entryOffset > UINT32_MAX - entrySize)
        throw std::runtime_error("Windows installer overlay range is too large: " +
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

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.movRegImm32(X64Reg::RDX, std::min(entrySize, kInstallerCopyChunkBytes));
    gen.callIATSlot(allocSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);
    gen.movMemReg(X64Reg::RBP, kPFileBufOff, X64Reg::RAX);

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

    gen.movRegImm32(X64Reg::RAX, entrySize);
    gen.movMemReg(X64Reg::RBP, kRemainingBytesOff, X64Reg::RAX);
    gen.movRegImm32(X64Reg::RAX, entryOffset);
    gen.movMemReg(X64Reg::RBP, kCurrentFileOffsetOff, X64Reg::RAX);
    zeroLocalQword(gen, kCrcOff);

    const auto lblLoop = gen.newLabel();
    const auto lblUseMaxChunk = gen.newLabel();
    const auto lblHaveChunk = gen.newLabel();
    const auto lblDone = gen.newLabel();

    gen.bindLabel(lblLoop);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kRemainingBytesOff);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblDone);
    gen.cmpRegImm32(X64Reg::RAX, kInstallerCopyChunkBytes);
    gen.ja(lblUseMaxChunk);
    gen.movMemReg(X64Reg::RBP, kCurrentChunkBytesOff, X64Reg::RAX);
    gen.jmp(lblHaveChunk);
    gen.bindLabel(lblUseMaxChunk);
    gen.movRegImm32(X64Reg::RAX, kInstallerCopyChunkBytes);
    gen.movMemReg(X64Reg::RBP, kCurrentChunkBytesOff, X64Reg::RAX);
    gen.bindLabel(lblHaveChunk);

    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kHFileOff);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kCurrentFileOffsetOff);
    zeroLocalQword(gen, kBytesWrittenOff);
    gen.leaRegMem(X64Reg::R8, X64Reg::RBP, kBytesWrittenOff);
    gen.xorRegReg(X64Reg::R9, X64Reg::R9);
    gen.callIATSlot(setFilePointerSlot);
    gen.movRegMem(X64Reg::R10, X64Reg::RBP, kCurrentFileOffsetOff);
    gen.cmpRegReg(X64Reg::RAX, X64Reg::R10);
    gen.jnz(errorLabel);

    zeroLocalQword(gen, kBytesReadOff);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kHFileOff);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kPFileBufOff);
    gen.movRegMem(X64Reg::R8, X64Reg::RBP, kCurrentChunkBytesOff);
    gen.leaRegMem(X64Reg::R9, X64Reg::RBP, kBytesReadOff);
    storeStackImm64(gen, 0x20, 0);
    gen.callIATSlot(readSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kBytesReadOff);
    gen.movRegMem(X64Reg::R10, X64Reg::RBP, kCurrentChunkBytesOff);
    gen.cmpRegReg(X64Reg::RAX, X64Reg::R10);
    gen.jnz(errorLabel);

    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kCrcOff);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kPFileBufOff);
    gen.movRegMem(X64Reg::R8, X64Reg::RBP, kCurrentChunkBytesOff);
    gen.callIATSlot(crcSlot);
    gen.movMemReg(X64Reg::RBP, kCrcOff, X64Reg::RAX);

    zeroLocalQword(gen, kBytesWrittenOff);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kHOutOff);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kPFileBufOff);
    gen.movRegMem(X64Reg::R8, X64Reg::RBP, kCurrentChunkBytesOff);
    gen.leaRegMem(X64Reg::R9, X64Reg::RBP, kBytesWrittenOff);
    storeStackImm64(gen, 0x20, 0);
    gen.callIATSlot(writeSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kBytesWrittenOff);
    gen.movRegMem(X64Reg::R10, X64Reg::RBP, kCurrentChunkBytesOff);
    gen.cmpRegReg(X64Reg::RAX, X64Reg::R10);
    gen.jnz(errorLabel);

    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kCurrentFileOffsetOff);
    gen.movRegMem(X64Reg::R10, X64Reg::RBP, kCurrentChunkBytesOff);
    gen.addRegReg(X64Reg::RAX, X64Reg::R10);
    gen.movMemReg(X64Reg::RBP, kCurrentFileOffsetOff, X64Reg::RAX);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kRemainingBytesOff);
    gen.movRegMem(X64Reg::R10, X64Reg::RBP, kCurrentChunkBytesOff);
    gen.subRegReg(X64Reg::RAX, X64Reg::R10);
    gen.movMemReg(X64Reg::RBP, kRemainingBytesOff, X64Reg::RAX);
    gen.jmp(lblLoop);

    gen.bindLabel(lblDone);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kCrcOff);
    emitCmpRegU32(gen, X64Reg::RAX, entry.crc32);
    gen.jnz(errorLabel);

    emitCloseLocalHandleIfSet(gen, kHOutOff, closeSlot);
    emitLocalFreeIfSet(gen, kPFileBufOff, freeSlot);
}

// Emit code to delete a single file at entry.root\entry.relativePath.
// Treats ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND, and ERROR_DIR_NOT_EMPTY
// as success (file already gone). Jumps to errorLabel on any other failure.
void emitDeleteFile(InstallerStubGen &gen,
                    const WindowsPackageFileEntry &entry,
                    uint32_t slashOff,
                    uint32_t copySlot,
                    uint32_t catSlot,
                    uint32_t strlenSlot,
                    uint32_t deleteSlot,
                    uint32_t getLastErrorSlot,
                    uint32_t errorLabel) {
    const auto lblOk = gen.newLabel();
    const uint32_t relPathOff = gen.embedStringW(entry.relativePath);
    emitComposePath(
        gen, entry.root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot, strlenSlot, errorLabel);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.callIATSlot(deleteSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblOk);
    gen.callIATSlot(getLastErrorSlot);
    gen.cmpRegImm32(X64Reg::RAX, kErrorFileNotFound);
    gen.jz(lblOk);
    gen.cmpRegImm32(X64Reg::RAX, kErrorPathNotFound);
    gen.jz(lblOk);
    gen.cmpRegImm32(X64Reg::RAX, kErrorDirNotEmpty);
    gen.jz(lblOk);
    gen.jmp(errorLabel);
    gen.bindLabel(lblOk);
}

// Emit code to remove a directory at entry.root\entry.relativePath.
// Treats ERROR_FILE_NOT_FOUND and ERROR_PATH_NOT_FOUND as success (already gone).
// Jumps to errorLabel on any other failure.
void emitRemoveDirectory(InstallerStubGen &gen,
                         const WindowsPackageDirEntry &entry,
                         uint32_t slashOff,
                         uint32_t copySlot,
                         uint32_t catSlot,
                         uint32_t strlenSlot,
                         uint32_t removeSlot,
                         uint32_t getLastErrorSlot,
                         uint32_t errorLabel) {
    const auto lblOk = gen.newLabel();
    const uint32_t relPathOff = gen.embedStringW(entry.relativePath);
    emitComposePath(
        gen, entry.root, kTempPathOff, slashOff, relPathOff, copySlot, catSlot, strlenSlot, errorLabel);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.callIATSlot(removeSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblOk);
    gen.callIATSlot(getLastErrorSlot);
    gen.cmpRegImm32(X64Reg::RAX, kErrorFileNotFound);
    gen.jz(lblOk);
    gen.cmpRegImm32(X64Reg::RAX, kErrorPathNotFound);
    gen.jz(lblOk);
    gen.jmp(errorLabel);
    gen.bindLabel(lblOk);
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
    const uint32_t starOff = gen.embedStringW("*");
    const uint32_t semicolonOff = gen.embedStringW(";");
    const uint32_t quoteOff = gen.embedStringW("\"");
    const uint32_t installDirOff = gen.embedStringW(installDir);
    const uint32_t displayNameOff = gen.embedStringW(layout.displayName);
    const uint32_t versionOff = gen.embedStringW(version);
    const uint32_t publisherOff = gen.embedStringW(publisher);
    const uint32_t uninstallKeyOff = gen.embedStringW(uninstallKey);
    const uint32_t uninstallExeOff = gen.embedStringW("uninstall.exe");
    const uint32_t environmentKeyOff =
        gen.embedStringW("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
    const uint32_t environmentOff = gen.embedStringW("Environment");

    const uint32_t regDisplayNameOff = gen.embedStringW("DisplayName");
    const uint32_t regDisplayVersionOff = gen.embedStringW("DisplayVersion");
    const uint32_t regPublisherOff = gen.embedStringW("Publisher");
    const uint32_t regInstallLocationOff = gen.embedStringW("InstallLocation");
    const uint32_t regUninstallStringOff = gen.embedStringW("UninstallString");
    const uint32_t regPathValueOff = gen.embedStringW("Path");
    const uint32_t regOriginalPathOff = gen.embedStringW("VAPSOriginalPath");
    const uint32_t regPathEntryOff = gen.embedStringW("VAPSPathEntry");

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
    zeroLocalQword(gen, kPathUpdatedOff);

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

    emitCleanInstallRootContents(gen,
                                 layout,
                                 slashOff,
                                 starOff,
                                 kI_lstrcpyW,
                                 kI_lstrcatW,
                                 kI_lstrlenW,
                                 kI_SHFileOperationW,
                                 lblRollbackError);

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

    emitInstallPathUpdate(gen,
                          layout,
                          slashOff,
                          semicolonOff,
                          uninstallKeyOff,
                          environmentKeyOff,
                          regPathValueOff,
                          regOriginalPathOff,
                          regPathEntryOff,
                          environmentOff,
                          kI_RegCreateKeyW,
                          kI_RegOpenKeyW,
                          kI_RegQueryValueExW,
                          kI_RegSetValueExW,
                          kI_RegCloseKey,
                          kI_lstrcpyW,
                          kI_lstrcatW,
                          kI_lstrcmpiW,
                          kI_lstrlenW,
                          kI_SendMessageTimeoutW,
                          lblRollbackError);

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
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.leaRipData(X64Reg::RDX, quoteOff);
    gen.callIATSlot(kI_lstrcpyW);
    emitCheckedCatStack(
        gen, kTempPathOff, kUninstallPathOff, kI_lstrcatW, kI_lstrlenW, lblRollbackError);
    emitCheckedCatEmbedded(
        gen, kTempPathOff, quoteOff, kI_lstrcatW, kI_lstrlenW, lblRollbackError);
    emitRegSetStackString(gen,
                          kI_RegSetValueExW,
                          kI_lstrlenW,
                          regUninstallStringOff,
                          kTempPathOff,
                          lblRollbackError);
    if (layout.addToPath) {
        const auto lblSkipPathMetadata = gen.newLabel();
        gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kPathUpdatedOff);
        gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
        gen.jz(lblSkipPathMetadata);
        emitRegSetStackString(gen,
                              kI_RegSetValueExW,
                              kI_lstrlenW,
                              regOriginalPathOff,
                              kPathOriginalOff,
                              kRegExpandSz,
                              lblRollbackError);
        emitComposeInstallPathEntry(gen,
                                    layout,
                                    kTempPathOff,
                                    slashOff,
                                    kI_lstrcpyW,
                                    kI_lstrcatW,
                                    kI_lstrlenW,
                                    lblRollbackError);
        emitRegSetStackString(gen,
                              kI_RegSetValueExW,
                              kI_lstrlenW,
                              regPathEntryOff,
                              kTempPathOff,
                              kRegExpandSz,
                              lblRollbackError);
        gen.bindLabel(lblSkipPathMetadata);
    }
    emitRegCloseIfSet(gen, kRegKeyOff, kI_RegCloseKey);

    emitRegisterFileAssociations(gen,
                                 layout,
                                 slashOff,
                                 kI_lstrcpyW,
                                 kI_lstrcatW,
                                 kI_RegCreateKeyW,
                                 kI_RegSetValueExW,
                                 kI_RegQueryValueExW,
                                 kI_lstrcmpW,
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
    emitRestorePathFromOriginalLocal(gen,
                                     layout,
                                     environmentKeyOff,
                                     regPathValueOff,
                                     environmentOff,
                                     kI_RegCreateKeyW,
                                     kI_RegSetValueExW,
                                     kI_RegCloseKey,
                                     kI_lstrlenW,
                                     kI_SendMessageTimeoutW);
    for (const auto &file : layout.uninstallFiles)
        emitDeleteFile(
            gen,
            file,
            slashOff,
            kI_lstrcpyW,
            kI_lstrcatW,
            kI_lstrlenW,
            kI_DeleteFileW,
            kI_GetLastError,
            lblExitError);
    emitDeleteFile(gen,
                   WindowsPackageFileEntry{WindowsInstallRoot::InstallDir, "uninstall.exe", 0, 0},
                   slashOff,
                   kI_lstrcpyW,
                   kI_lstrcatW,
                   kI_lstrlenW,
                   kI_DeleteFileW,
                   kI_GetLastError,
                   lblExitError);
    for (const auto &dir : layout.uninstallDirectories)
        emitRemoveDirectory(gen,
                            dir,
                            slashOff,
                            kI_lstrcpyW,
                            kI_lstrcatW,
                            kI_lstrlenW,
                            kI_RemoveDirectoryW,
                            kI_GetLastError,
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
                                   kI_RegQueryValueExW,
                                   kI_lstrcmpW,
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
    const uint32_t semicolonOff = gen.embedStringW(";");
    const uint32_t installDirOff = gen.embedStringW(installDir);
    const uint32_t uninstallKeyOff = gen.embedStringW(uninstallKey);
    const uint32_t environmentKeyOff =
        gen.embedStringW("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
    const uint32_t environmentOff = gen.embedStringW("Environment");
    const uint32_t regPathValueOff = gen.embedStringW("Path");
    const uint32_t regPathEntryOff = gen.embedStringW("VAPSPathEntry");
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
            gen,
            file,
            slashOff,
            kU_lstrcpyW,
            kU_lstrcatW,
            kU_lstrlenW,
            kU_DeleteFileW,
            kU_GetLastError,
            lblError);
    }

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kSelfPathOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.movRegImm32(X64Reg::R8, kMoveFileDelayUntilReboot);
    gen.callIATSlot(kU_MoveFileExW);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblError);

    for (const auto &dir : layout.uninstallDirectories) {
        emitRemoveDirectory(gen,
                            dir,
                            slashOff,
                            kU_lstrcpyW,
                            kU_lstrcatW,
                            kU_lstrlenW,
                            kU_RemoveDirectoryW,
                            kU_GetLastError,
                            lblError);
    }

    if (needsMenuPath(layout)) {
        const auto lblMenuPathDone = gen.newLabel();
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kMenuPathOff);
        gen.callIATSlot(kU_RemoveDirectoryW);
        gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
        gen.jnz(lblMenuPathDone);
        gen.callIATSlot(kU_GetLastError);
        gen.cmpRegImm32(X64Reg::RAX, kErrorFileNotFound);
        gen.jz(lblMenuPathDone);
        gen.cmpRegImm32(X64Reg::RAX, kErrorPathNotFound);
        gen.jz(lblMenuPathDone);
        gen.cmpRegImm32(X64Reg::RAX, kErrorDirNotEmpty);
        gen.jz(lblMenuPathDone);
        gen.jmp(lblError);
        gen.bindLabel(lblMenuPathDone);
    }

    const auto lblInstallPathDone = gen.newLabel();
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInstallPathOff);
    gen.callIATSlot(kU_RemoveDirectoryW);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblInstallPathDone);
    gen.callIATSlot(kU_GetLastError);
    gen.cmpRegImm32(X64Reg::RAX, kErrorFileNotFound);
    gen.jz(lblInstallPathDone);
    gen.cmpRegImm32(X64Reg::RAX, kErrorPathNotFound);
    gen.jz(lblInstallPathDone);
    gen.cmpRegImm32(X64Reg::RAX, kErrorDirNotEmpty);
    const auto lblScheduleInstallDir = gen.newLabel();
    gen.jz(lblScheduleInstallDir);
    gen.jmp(lblError);
    gen.bindLabel(lblScheduleInstallDir);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInstallPathOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.movRegImm32(X64Reg::R8, kMoveFileDelayUntilReboot);
    gen.callIATSlot(kU_MoveFileExW);
    gen.bindLabel(lblInstallPathDone);

    emitUnregisterFileAssociations(gen,
                                   layout,
                                   kU_RegOpenKeyW,
                                   kU_RegCloseKey,
                                   kU_RegQueryValueExW,
                                   kU_lstrcmpW,
                                   kU_RegDeleteValueW,
                                   kU_RegDeleteKeyW);

    emitRestorePathFromUninstallKey(gen,
                                    layout,
                                    slashOff,
                                    semicolonOff,
                                    uninstallKeyOff,
                                    environmentKeyOff,
                                    regPathValueOff,
                                    regPathEntryOff,
                                    environmentOff,
                                    kU_RegOpenKeyW,
                                    kU_RegCreateKeyW,
                                    kU_RegSetValueExW,
                                    kU_RegQueryValueExW,
                                    kU_RegCloseKey,
                                    kU_lstrcpyW,
                                    kU_lstrcatW,
                                    kU_lstrcmpiW,
                                    kU_lstrlenW,
                                    kU_SendMessageTimeoutW,
                                    lblError);

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
