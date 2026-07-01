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
//   - Windows package overlays use stored bootstrap entries. The main install
//     payload may be a DEFLATE-compressed inner ZIP expanded through Windows'
//     built-in PowerShell archive support.
//   - All direct Win32 API calls go through the IAT. Compressed payload
//     expansion invokes Windows' System32 PowerShell explicitly.
//
// Ownership/Lifetime:
//   - Pure functions. No state.
//
// Links: InstallerStub.hpp, InstallerStubGen.hpp, WindowsPackageBuilder.hpp
//
//===----------------------------------------------------------------------===//

#include "InstallerStub.hpp"
#include "InstallerStubGen.hpp"
#include "InstallerStubGenA64.hpp"
#include "PkgUtils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
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
constexpr uint32_t kCreateNoWindow = 0x08000000u;
constexpr uint32_t kWaitInfinite = 0xFFFFFFFFu;
constexpr uint32_t kRegNone = 0u;
constexpr uint32_t kRegSz = 1u;
constexpr uint32_t kRegExpandSz = 2u;
constexpr uint32_t kRegDword = 4u;
constexpr uint32_t kErrorFileNotFound = 2u;
constexpr uint32_t kErrorPathNotFound = 3u;
constexpr uint32_t kErrorDirNotEmpty = 145u;
constexpr uint32_t kErrorAlreadyExists = 183u;
constexpr uint32_t kErrorMoreData = 234u;
constexpr uint32_t kInstallerCopyChunkBytes = 1024u * 1024u;
constexpr uint32_t kLocaleUserDefault = 0x0400u;
constexpr uint64_t kHkeyCurrentUser = 0x80000001ull;
constexpr uint64_t kHkeyLocalMachine = 0x80000002ull;

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
constexpr int32_t kQuietModeOff = -0x80060;
constexpr int32_t kNoRestartModeOff = -0x80068;
constexpr int32_t kInitCommonControlsOff = -0x80078;
constexpr int32_t kFileOpStructOff = -0x800A0;
constexpr int32_t kCommandLineOff = -0x800B0;
constexpr int32_t kKnownFolderPtrOff = -0x800B8;
constexpr int32_t kCommandBufferOff = -0x90000;
constexpr int32_t kStartupInfoOff = -0x90200;
constexpr int32_t kProcessInfoOff = -0x90280;
constexpr uint32_t kFrameSize = 0x90400;

constexpr uint32_t kDlgIdOk = 1;
constexpr uint32_t kDlgIdCancel = 2;
constexpr uint32_t kDlgIdLicense = 1001;
constexpr uint32_t kDlgIdAccept = 1002;
constexpr uint32_t kDlgIdProgress = 1003;
constexpr uint32_t kDlgIdScopeUser = 1004;
constexpr uint32_t kDlgIdScopeMachine = 1005;
constexpr uint32_t kDlgIdBanner = 1006;
constexpr uint32_t kDlgIdBack = 1007;
constexpr uint32_t kWmClose = 0x0010;
constexpr uint32_t kWmCommand = 0x0111;
constexpr uint32_t kWmInitDialog = 0x0110;
constexpr uint32_t kBmGetCheck = 0x00F0;
constexpr uint32_t kBmSetCheck = 0x00F1;
constexpr uint32_t kBstChecked = 1;
constexpr uint32_t kIccProgressClass = 0x00000020;
constexpr int32_t kDlgProcFrameSize = 0x48;
constexpr int32_t kDlgProcHwndOff = -0x08;
constexpr int32_t kDlgProcMsgOff = -0x10;
constexpr int32_t kDlgProcWparamOff = -0x18;

/// @brief Flat IAT slot indices for the Win32 APIs the installer stub imports.
/// @details Each value is the function's position across all imported DLLs and is
///          passed to InstallerStubGen::callIATSlot to emit a `call [rip+disp]`.
///          The order must match the import list built in buildInstallerStub.
enum InstallerIAT : uint32_t {
    kI_ExitProcess = 0,
    kI_GetModuleFileNameW = 1,
    kI_CreateFileW = 2,
    kI_ReadFile = 3,
    kI_WriteFile = 4,
    kI_CloseHandle = 5,
    kI_LocalAlloc = 6,
    kI_LocalFree = 7,
    kI_SetFilePointerEx = 8,
    kI_CreateDirectoryW = 9,
    kI_DeleteFileW = 10,
    kI_RemoveDirectoryW = 11,
    kI_lstrcpyW = 12,
    kI_lstrcatW = 13,
    kI_lstrlenW = 14,
    kI_lstrcmpW = 15,
    kI_lstrcmpiW = 16,
    kI_GetLastError = 17,
    kI_GetCommandLineW = 18,
    kI_SHGetKnownFolderPath = 19,
    kI_SHFileOperationW = 20,
    kI_RegCreateKeyW = 21,
    kI_RegSetValueExW = 22,
    kI_RegCloseKey = 23,
    kI_RegOpenKeyW = 24,
    kI_RegQueryValueExW = 25,
    kI_RegDeleteValueW = 26,
    kI_RegDeleteTreeW = 27,
    kI_MessageBoxW = 28,
    kI_SendMessageTimeoutW = 29,
    kI_RtlComputeCrc32 = 30,
    kI_StrStrIW = 31,
    kI_CoTaskMemFree = 32,
    kI_GetDateFormatW = 33,
    kI_GetSystemDirectoryW = 34,
    kI_CreateProcessW = 35,
    kI_WaitForSingleObject = 36,
    kI_GetExitCodeProcess = 37,
    kI_InitCommonControlsEx = 38,
    kI_DialogBoxIndirectParamW = 39,
    kI_CreateDialogIndirectParamW = 40,
    kI_EndDialog = 41,
    kI_GetDlgItem = 42,
    kI_SendMessageW = 43,
    kI_SetDlgItemTextW = 44,
    kI_EnableWindow = 45,
    kI_DestroyWindow = 46,
    kI_CreateDIBSection = 47,
    kI_CreateDIBitmap = 48,
    kI_DeleteObject = 49,
    kI_CreateThread = 50,
    kI_Count, ///< Sentinel: total IAT slot count (must equal installerImports() function count).
};

/// @brief Flat IAT slot indices for the Win32 APIs the uninstaller stub imports.
/// @details Mirrors InstallerIAT for the (smaller) uninstaller import list; the
///          order must match the import list built in buildUninstallerStub.
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
    kU_GetCommandLineW = 11,
    kU_SHGetKnownFolderPath = 12,
    kU_RegOpenKeyW = 13,
    kU_RegCreateKeyW = 14,
    kU_RegCloseKey = 15,
    kU_RegQueryValueExW = 16,
    kU_RegDeleteValueW = 17,
    kU_RegDeleteTreeW = 18,
    kU_RegSetValueExW = 19,
    kU_MessageBoxW = 20,
    kU_SendMessageTimeoutW = 21,
    kU_StrStrIW = 22,
    kU_CoTaskMemFree = 23,
    kU_InitCommonControlsEx = 24,
    kU_DialogBoxIndirectParamW = 25,
    kU_CreateDialogIndirectParamW = 26,
    kU_EndDialog = 27,
    kU_GetDlgItem = 28,
    kU_SendMessageW = 29,
    kU_SetDlgItemTextW = 30,
    kU_EnableWindow = 31,
    kU_DestroyWindow = 32,
    kU_CreateDIBSection = 33,
    kU_CreateDIBitmap = 34,
    kU_DeleteObject = 35,
    kU_CreateThread = 36,
    kU_Count, ///< Sentinel: total IAT slot count (must equal uninstallerImports() function count).
};

/// @brief Validate that the flat function count in @p imports matches @p expectedSlots.
/// @details The IAT enum (InstallerIAT/UninstallerIAT) and the PEImport list are a
///          load-bearing pairing: codegen references imports by numeric slot
///          (callIATSlot), so a count mismatch means an enum entry or an import was
///          added without the other — which would silently call the wrong DLL
///          function. Caught here at packaging time rather than as a runtime crash.
inline void verifyImportSlotCount(const std::vector<PEImport> &imports,
                                  std::size_t expectedSlots,
                                  const char *label) {
    std::size_t total = 0;
    for (const PEImport &imp : imports)
        total += imp.functions.size();
    if (total != expectedSlots)
        throw std::runtime_error(std::string("packaging: ") + label + " IAT enum/import drift — " +
                                 std::to_string(total) + " imported functions vs " +
                                 std::to_string(expectedSlots) +
                                 " enum slots; keep the enum and the import list in sync");
}

/// @brief Return the ordered PEImport list for the installer PE.
/// Slot indices must match the InstallerIAT enum — the pairing is load-bearing
/// because the codegen references imports by numeric slot, not by name.
std::vector<PEImport> installerImports() {
    std::vector<PEImport> imports = {
        {"kernel32.dll",
         {"ExitProcess",
          "GetModuleFileNameW",
          "CreateFileW",
          "ReadFile",
          "WriteFile",
          "CloseHandle",
          "LocalAlloc",
          "LocalFree",
          "SetFilePointerEx",
          "CreateDirectoryW",
          "DeleteFileW",
          "RemoveDirectoryW",
          "lstrcpyW",
          "lstrcatW",
          "lstrlenW",
          "lstrcmpW",
          "lstrcmpiW",
          "GetLastError",
          "GetCommandLineW"}},
        {"shell32.dll", {"SHGetKnownFolderPath", "SHFileOperationW"}},
        {"advapi32.dll",
         {"RegCreateKeyW",
          "RegSetValueExW",
          "RegCloseKey",
          "RegOpenKeyW",
          "RegQueryValueExW",
          "RegDeleteValueW",
          "RegDeleteTreeW"}},
        {"user32.dll", {"MessageBoxW", "SendMessageTimeoutW"}},
        {"ntdll.dll", {"RtlComputeCrc32"}},
        {"shlwapi.dll", {"StrStrIW"}},
        {"ole32.dll", {"CoTaskMemFree"}},
        {"kernel32.dll",
         {"GetDateFormatW",
          "GetSystemDirectoryW",
          "CreateProcessW",
          "WaitForSingleObject",
          "GetExitCodeProcess"}},
        {"comctl32.dll", {"InitCommonControlsEx"}},
        {"user32.dll",
         {"DialogBoxIndirectParamW",
          "CreateDialogIndirectParamW",
          "EndDialog",
          "GetDlgItem",
          "SendMessageW",
          "SetDlgItemTextW",
          "EnableWindow",
          "DestroyWindow"}},
        {"gdi32.dll", {"CreateDIBSection", "CreateDIBitmap", "DeleteObject"}},
        {"kernel32.dll", {"CreateThread"}},
    };
    verifyImportSlotCount(imports, kI_Count, "installer");
    return imports;
}

/// @brief Return the ordered PEImport list for the uninstaller PE.
/// Slot indices must match the UninstallerIAT enum for the same reason as installerImports().
std::vector<PEImport> uninstallerImports() {
    std::vector<PEImport> imports = {
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
          "GetLastError",
          "GetCommandLineW"}},
        {"shell32.dll", {"SHGetKnownFolderPath"}},
        {"advapi32.dll",
         {"RegOpenKeyW",
          "RegCreateKeyW",
          "RegCloseKey",
          "RegQueryValueExW",
          "RegDeleteValueW",
          "RegDeleteTreeW",
          "RegSetValueExW"}},
        {"user32.dll", {"MessageBoxW", "SendMessageTimeoutW"}},
        {"shlwapi.dll", {"StrStrIW"}},
        {"ole32.dll", {"CoTaskMemFree"}},
        {"comctl32.dll", {"InitCommonControlsEx"}},
        {"user32.dll",
         {"DialogBoxIndirectParamW",
          "CreateDialogIndirectParamW",
          "EndDialog",
          "GetDlgItem",
          "SendMessageW",
          "SetDlgItemTextW",
          "EnableWindow",
          "DestroyWindow"}},
        {"gdi32.dll", {"CreateDIBSection", "CreateDIBitmap", "DeleteObject"}},
        {"kernel32.dll", {"CreateThread"}},
    };
    verifyImportSlotCount(imports, kU_Count, "uninstaller");
    return imports;
}

/// @brief Round `value` up to the nearest multiple of `alignment` (must be a power of two).
uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

std::string installDirNameFor(const WindowsPackageLayout &layout);
std::string uninstallKeyPathFor(const WindowsPackageLayout &layout);
std::string registryIdFor(const WindowsPackageLayout &layout);
bool needsMenuPath(const WindowsPackageLayout &layout);

void appendDlgLE16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void appendDlgLE32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void alignDialogDword(std::vector<uint8_t> &out) {
    while ((out.size() & 3u) != 0)
        out.push_back(0);
}

void appendDialogWideString(std::vector<uint8_t> &out, const std::string &text) {
    const auto encoded = utf8ToUtf16LEBytes(text, true);
    out.insert(out.end(), encoded.begin(), encoded.end());
}

std::string normalizeDialogText(std::string text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (char ch : text) {
        if (ch == '\n') {
            if (out.empty() || out.back() != '\r')
                out.push_back('\r');
            out.push_back('\n');
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::string base64Encode(const std::vector<uint8_t> &bytes) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2u) / 3u) * 4u);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t b0 = bytes[i];
        const uint32_t b1 = (i + 1u < bytes.size()) ? bytes[i + 1u] : 0u;
        const uint32_t b2 = (i + 2u < bytes.size()) ? bytes[i + 2u] : 0u;
        const uint32_t triplet = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kAlphabet[(triplet >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triplet >> 12) & 0x3Fu]);
        out.push_back((i + 1u < bytes.size()) ? kAlphabet[(triplet >> 6) & 0x3Fu] : '=');
        out.push_back((i + 2u < bytes.size()) ? kAlphabet[triplet & 0x3Fu] : '=');
    }
    return out;
}

std::string powershellSingleQuote(const std::string &text) {
    std::string out;
    out.reserve(text.size() + 2u);
    out.push_back('\'');
    for (const char ch : text) {
        if (ch == '\'')
            out += "''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

const char *powershellBool(bool value) {
    return value ? "$true" : "$false";
}

const char *powershellRootCode(WindowsInstallRoot root) {
    switch (root) {
        case WindowsInstallRoot::DesktopDir:
            return "D";
        case WindowsInstallRoot::StartMenuDir:
            return "M";
        case WindowsInstallRoot::InstallDir:
        default:
            return "I";
    }
}

std::string powershellRelPath(std::string path) {
    for (char &ch : path) {
        if (ch == '/')
            ch = '\\';
    }
    return path;
}

std::string powershellPathJoinLiteral(const std::string &baseVar, const std::string &relativePath) {
    if (relativePath.empty())
        return baseVar;
    return "(Join-Path " + baseVar + " " + powershellSingleQuote(powershellRelPath(relativePath)) +
           ")";
}

std::string buildArm64PowerShellScript(const WindowsPackageLayout &layout, bool uninstallDialog) {
    const std::string installDir = installDirNameFor(layout);
    const std::string version = layout.version.empty() ? "0.0.0" : layout.version;
    const std::string publisher = layout.publisher.empty() ? "Viper" : layout.publisher;
    const std::string uninstallKey = uninstallKeyPathFor(layout);
    const std::string hive = layout.perUserInstall ? "HKCU:" : "HKLM:";
    const std::string scope = layout.perUserInstall ? "User" : "Machine";
    const std::string iconRel = layout.displayIconRelativePath.empty()
                                    ? layout.executableName
                                    : layout.displayIconRelativePath;
    const std::string pathEntryExpr =
        layout.pathRelativePath.empty()
            ? "$install"
            : powershellPathJoinLiteral("$install", layout.pathRelativePath);
    const std::string assocExeRel = layout.fileAssociationExecutableRelativePath.empty()
                                        ? layout.executableName
                                        : layout.fileAssociationExecutableRelativePath;
    const std::string assocIconRel =
        layout.displayIconRelativePath.empty() ? assocExeRel : layout.displayIconRelativePath;

    std::ostringstream ps;
    ps << "$ErrorActionPreference='Stop'\n";
    ps << "$self=$args[0]\n";
    ps << "$mode=$args[1]\n";
    ps << "$display=" << powershellSingleQuote(layout.displayName) << "\n";
    ps << "$installLeaf=" << powershellSingleQuote(installDir) << "\n";
    ps << "$version=" << powershellSingleQuote(version) << "\n";
    ps << "$publisher=" << powershellSingleQuote(publisher) << "\n";
    ps << "$homepage=" << powershellSingleQuote(layout.homepage) << "\n";
    ps << "$comments=" << powershellSingleQuote(layout.description) << "\n";
    ps << "$contact=" << powershellSingleQuote(layout.contact) << "\n";
    ps << "$identifier=" << powershellSingleQuote(registryIdFor(layout)) << "\n";
    ps << "$perUser=" << powershellBool(layout.perUserInstall) << "\n";
    ps << "$addPath=" << powershellBool(layout.addToPath) << "\n";
    ps << "$baseOffset=[int64]" << layout.overlayFileOffset << "\n";
    ps << "$rootBase=if($perUser){[Environment]::GetFolderPath('LocalApplicationData')}else{["
          "Environment]::GetFolderPath('ProgramFiles')}\n";
    ps << "$install=Join-Path $rootBase $installLeaf\n";
    ps << "$desktop=if($perUser){[Environment]::GetFolderPath('Desktop')}else{[Environment]::"
          "GetFolderPath('CommonDesktopDirectory')}\n";
    ps << "$programs=if($perUser){[Environment]::GetFolderPath('Programs')}else{[Environment]::"
          "GetFolderPath('CommonPrograms')}\n";
    ps << "$menu=Join-Path $programs $installLeaf\n";
    ps << "$scope=" << powershellSingleQuote(scope) << "\n";
    ps << "$uninstallReg=" << powershellSingleQuote(hive + "\\" + uninstallKey) << "\n";
    ps << "function Root($r){if($r -eq 'D'){$desktop}elseif($r -eq 'M'){$menu}else{$install}}\n";
    ps << "function "
          "Parent($p){$d=[IO.Path]::GetDirectoryName($p);if($d){[IO.Directory]::CreateDirectory($d)"
          "|Out-Null}}\n";
    ps << "function CopyPart($fs,$r,$rel,[int64]$off,[int64]$len){$dst=Join-Path (Root $r) "
          "$rel;Parent "
          "$dst;$out=[IO.File]::Create($dst);try{$null=$fs.Seek($baseOffset+$off,[IO.SeekOrigin]::"
          "Begin);$buf=New-Object byte[] 65536;while($len -gt 0){$want=$buf.Length;if($len -lt "
          "$want){$want=[int]$len};$n=$fs.Read($buf,0,$want);if($n -le 0){throw 'installer overlay "
          "ended early'};$out.Write($buf,0,$n);$len-=$n}}finally{$out.Dispose()}}\n";
    ps << "function SetS($n,$v){if($null -ne $v -and $v.Length -gt 0){New-ItemProperty -Path "
          "$uninstallReg -Name $n -Value $v -PropertyType String -Force|Out-Null}}\n";
    ps << "function SetD($n,[int]$v){New-ItemProperty -Path $uninstallReg -Name $n -Value $v "
          "-PropertyType DWord -Force|Out-Null}\n";
    ps << "function AddPath($p){if(-not "
          "$addPath){return};$old=[Environment]::GetEnvironmentVariable('Path',$scope);if($null "
          "-eq $old){$old=''};$parts=@($old -split ';'|Where-Object{$_.Length -gt 0});if(-not "
          "($parts|Where-Object{[String]::Equals($_,$p,'OrdinalIgnoreCase')})){[Environment]::"
          "SetEnvironmentVariable('Path',(($parts+$p)-join ';'),$scope)};SetS 'VAPSOriginalPath' "
          "$old;SetS 'VAPSPathEntry' $p}\n";
    ps << "function RemovePath(){if(-not $addPath){return};$entry=(Get-ItemProperty -Path "
          "$uninstallReg -Name 'VAPSPathEntry' -ErrorAction "
          "SilentlyContinue).VAPSPathEntry;if(-not $entry){$entry="
       << pathEntryExpr
       << "};$old=[Environment]::GetEnvironmentVariable('Path',$scope);if($null -eq "
          "$old){return};$parts=@($old -split ';'|Where-Object{$_.Length -gt 0 -and -not "
          "[String]::Equals($_,$entry,'OrdinalIgnoreCase')});[Environment]::SetEnvironmentVariable("
          "'Path',($parts -join ';'),$scope)}\n";
    ps << "function BroadcastEnv(){try{Add-Type -TypeDefinition 'using System; using "
          "System.Runtime.InteropServices; public static class ViperEnv { "
          "[DllImport(\"user32.dll\", "
          "SetLastError=true, CharSet=CharSet.Auto)] public static extern IntPtr "
          "SendMessageTimeout("
          "IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam, uint flags, uint timeout, out "
          "UIntPtr "
          "result); }' -ErrorAction SilentlyContinue|Out-Null;$r=[UIntPtr]::Zero;[void][ViperEnv]::"
          "SendMessageTimeout([IntPtr]0xffff,0x1a,[UIntPtr]::Zero,'Environment',2,5000,[ref]$r)}"
          "catch{}}\n";
    ps << "function RemoveOne($r,$rel){$p=Join-Path (Root $r) $rel;Remove-Item -LiteralPath $p "
          "-Force -ErrorAction SilentlyContinue}\n";
    ps << "$classRoot=Join-Path " << powershellSingleQuote(hive) << " 'Software\\Classes'\n";
    ps << "$assocExe=" << powershellPathJoinLiteral("$install", assocExeRel) << "\n";
    ps << "$assocIcon=" << powershellPathJoinLiteral("$install", assocIconRel) << "\n";
    ps << "$assocs=@(\n";
    for (const auto &assoc : layout.fileAssociations) {
        ps << "@{Ext=" << powershellSingleQuote(assoc.extension)
           << ";Desc=" << powershellSingleQuote(assoc.description)
           << ";Mime=" << powershellSingleQuote(assoc.mimeType)
           << ";Prog=" << powershellSingleQuote(assoc.progId)
           << ";Args=" << powershellSingleQuote(assoc.openCommandArguments) << "},\n";
    }
    ps << ")\n";
    ps << "function RegDefault($path,$value){New-Item -Path $path -Force|Out-Null;Set-Item -Path "
          "$path -Value $value}\n";
    ps << "function RegisterAssoc(){foreach($a in $assocs){$ext=Join-Path $classRoot $a.Ext;"
          "New-Item -Path $ext -Force|Out-Null;if($a.Mime){$props=Get-ItemProperty -Path $ext "
          "-ErrorAction SilentlyContinue;$owned=$props.VAPSContentTypeOwner;if(-not "
          "$props.'Content Type' -or $owned -eq $identifier){New-ItemProperty -Path $ext -Name "
          "'Content Type' -Value $a.Mime -PropertyType String -Force|Out-Null;New-ItemProperty "
          "-Path "
          "$ext -Name 'VAPSContentTypeOwner' -Value $identifier -PropertyType String "
          "-Force|Out-Null}}"
          "$owp=Join-Path $ext 'OpenWithProgids';New-Item -Path $owp -Force|Out-Null;"
          "New-ItemProperty -Path $owp -Name $a.Prog -Value '' -PropertyType String "
          "-Force|Out-Null;"
          "$prog=Join-Path $classRoot $a.Prog;RegDefault $prog $a.Desc;New-ItemProperty -Path "
          "$prog "
          "-Name 'VAPSOwner' -Value $identifier -PropertyType String "
          "-Force|Out-Null;$icon=Join-Path "
          "$prog 'DefaultIcon';RegDefault $icon $assocIcon;$cmd=Join-Path $prog "
          "'shell\\open\\command';"
          "$open='\"'+$assocExe+'\"';if($a.Args){$open+=' '+$a.Args};$open+=' \"%1\"';RegDefault "
          "$cmd $open}}\n";
    ps << "function UnregisterAssoc(){foreach($a in $assocs){$ext=Join-Path $classRoot $a.Ext;"
          "$owp=Join-Path $ext 'OpenWithProgids';Remove-ItemProperty -Path $owp -Name $a.Prog "
          "-ErrorAction SilentlyContinue;$props=Get-ItemProperty -Path $ext -ErrorAction "
          "SilentlyContinue;if($props.VAPSContentTypeOwner -eq $identifier){Remove-ItemProperty "
          "-Path "
          "$ext -Name 'Content Type' -ErrorAction SilentlyContinue;Remove-ItemProperty -Path $ext "
          "-Name 'VAPSContentTypeOwner' -ErrorAction SilentlyContinue};$prog=Join-Path $classRoot "
          "$a.Prog;$owner=(Get-ItemProperty -Path $prog -Name 'VAPSOwner' -ErrorAction "
          "SilentlyContinue).VAPSOwner;if($owner -eq $identifier){Remove-Item -LiteralPath $prog "
          "-Recurse -Force -ErrorAction SilentlyContinue}}}\n";

    ps << "$remove=@(\n";
    for (const auto &file : layout.uninstallFiles) {
        if (file.root == WindowsInstallRoot::InstallDir)
            continue;
        ps << "@(" << powershellSingleQuote(powershellRootCode(file.root)) << ","
           << powershellSingleQuote(powershellRelPath(file.relativePath)) << "),\n";
    }
    ps << ")\n";

    ps << "$dirs=@(\n";
    for (const auto &dir : layout.uninstallDirectories) {
        if (dir.relativePath.empty())
            continue;
        ps << "@(" << powershellSingleQuote(powershellRootCode(dir.root)) << ","
           << powershellSingleQuote(powershellRelPath(dir.relativePath)) << "),\n";
    }
    ps << ")\n";

    ps << "if($mode -eq 'uninstall'){\n";
    ps << "RemovePath\n";
    ps << "UnregisterAssoc\n";
    ps << "$manifest=Join-Path $install "
       << powershellSingleQuote(powershellRelPath(layout.installedManifestRelativePath)) << "\n";
    ps << "if(Test-Path -LiteralPath $manifest){Get-Content -LiteralPath $manifest|Sort-Object "
          "Length -Descending|ForEach-Object{if($_){Remove-Item -LiteralPath (Join-Path $install "
          "$_) -Force -ErrorAction SilentlyContinue}}}\n";
    ps << "foreach($f in $remove){RemoveOne $f[0] $f[1]}\n";
    ps << "foreach($d in $dirs){Remove-Item -LiteralPath (Join-Path (Root $d[0]) $d[1]) -Force "
          "-ErrorAction SilentlyContinue}\n";
    ps << "Remove-Item -LiteralPath $uninstallReg -Recurse -Force -ErrorAction SilentlyContinue\n";
    ps << "Remove-Item -LiteralPath $menu -Force -ErrorAction SilentlyContinue\n";
    ps << "Remove-Item -LiteralPath $install -Force -ErrorAction SilentlyContinue\n";
    ps << "BroadcastEnv\n";
    ps << "exit 0\n";
    ps << "}\n";

    if (uninstallDialog) {
        ps << "exit 0\n";
    } else {
        ps << "[IO.Directory]::CreateDirectory($install)|Out-Null\n";
        if (needsMenuPath(layout))
            ps << "[IO.Directory]::CreateDirectory($menu)|Out-Null\n";
        ps << "$files=@(\n";
        for (const auto &file : layout.installFiles) {
            ps << "@(" << powershellSingleQuote(powershellRootCode(file.root)) << ","
               << powershellSingleQuote(powershellRelPath(file.relativePath)) << ",[int64]"
               << file.overlayDataOffset << ",[int64]" << file.sizeBytes << "),\n";
        }
        ps << ")\n";
        ps << "$fs=[IO.File]::OpenRead($self);try{foreach($f in $files){CopyPart $fs $f[0] $f[1] "
              "$f[2] $f[3]}}finally{$fs.Dispose()}\n";
        if (!layout.compressedPayloadRelativePath.empty()) {
            ps << "$payload=Join-Path $install "
               << powershellSingleQuote(powershellRelPath(layout.compressedPayloadRelativePath))
               << "\n";
            ps << "if(Test-Path -LiteralPath $payload){$oldManifest=Join-Path $install "
               << powershellSingleQuote(powershellRelPath(layout.installedManifestRelativePath))
               << ";$newManifest=Join-Path $install "
               << powershellSingleQuote(
                      powershellRelPath(layout.compressedPayloadManifestRelativePath))
               << ";$oldLines=@();if(Test-Path -LiteralPath $oldManifest -PathType Leaf){"
                  "$oldLines=Get-Content -LiteralPath $oldManifest|Where-Object{$_ -and "
                  "-not [IO.Path]::IsPathRooted($_) -and -not $_.Contains(':') -and "
                  "-not $_.Contains('..')}};$newLines=Get-Content -LiteralPath $newManifest|"
                  "Where-Object{$_ -and -not [IO.Path]::IsPathRooted($_) -and -not "
                  "$_.Contains(':') "
                  "-and -not $_.Contains('..')};Expand-Archive -LiteralPath $payload "
                  "-DestinationPath $install -Force;$owned=@{};foreach($n in $newLines){"
                  "$owned[$n.ToLowerInvariant()]=$true};foreach($o in $oldLines){if(-not "
                  "$owned.ContainsKey($o.ToLowerInvariant())){$f=Join-Path $install "
                  "$o;if(Test-Path "
                  "-LiteralPath $f -PathType Leaf){Remove-Item -LiteralPath $f -Force -ErrorAction "
                  "SilentlyContinue}}};Remove-Item -LiteralPath $payload -Force -ErrorAction "
                  "SilentlyContinue}\n";
        }
        if (!layout.compressedPayloadManifestRelativePath.empty()) {
            ps << "Remove-Item -LiteralPath (Join-Path $install "
               << powershellSingleQuote(
                      powershellRelPath(layout.compressedPayloadManifestRelativePath))
               << ") -Force -ErrorAction SilentlyContinue\n";
        }
        ps << "New-Item -Path $uninstallReg -Force|Out-Null\n";
        ps << "$uninst=Join-Path $install 'uninstall.exe'\n";
        ps << "SetS 'DisplayName' $display\n";
        ps << "SetS 'DisplayVersion' $version\n";
        ps << "SetS 'Publisher' $publisher\n";
        ps << "SetS 'InstallLocation' $install\n";
        ps << "SetS 'UninstallString' ('\"'+$uninst+'\"')\n";
        ps << "SetS 'QuietUninstallString' ('\"'+$uninst+'\" /quiet')\n";
        if (!iconRel.empty())
            ps << "SetS 'DisplayIcon' " << powershellPathJoinLiteral("$install", iconRel) << "\n";
        ps << "SetD 'NoModify' 1\n";
        ps << "SetD 'NoRepair' 1\n";
        if (layout.estimatedSizeKb != 0)
            ps << "SetD 'EstimatedSize' " << layout.estimatedSizeKb << "\n";
        if (!layout.installDate.empty())
            ps << "SetS 'InstallDate' " << powershellSingleQuote(layout.installDate) << "\n";
        ps << "SetS 'URLInfoAbout' $homepage\n";
        ps << "SetS 'URLUpdateInfo' $homepage\n";
        ps << "SetS 'HelpLink' $homepage\n";
        ps << "SetS 'Comments' $comments\n";
        ps << "SetS 'Contact' $contact\n";
        ps << "AddPath " << pathEntryExpr << "\n";
        ps << "RegisterAssoc\n";
        ps << "BroadcastEnv\n";
        ps << "exit 0\n";
    }

    return ps.str();
}

std::string encodedArm64PowerShellCommand(const WindowsPackageLayout &layout,
                                          bool uninstallDialog) {
    return base64Encode(
        utf8ToUtf16LEBytes(buildArm64PowerShellScript(layout, uninstallDialog), false));
}

void appendDialogAtomClass(std::vector<uint8_t> &out, uint16_t atom) {
    appendDlgLE16(out, 0xFFFFu);
    appendDlgLE16(out, atom);
}

void appendDialogControl(std::vector<uint8_t> &out,
                         uint32_t style,
                         uint32_t exStyle,
                         int16_t x,
                         int16_t y,
                         int16_t cx,
                         int16_t cy,
                         uint16_t id,
                         uint16_t classAtom,
                         const std::string &title) {
    alignDialogDword(out);
    appendDlgLE32(out, style);
    appendDlgLE32(out, exStyle);
    appendDlgLE16(out, static_cast<uint16_t>(x));
    appendDlgLE16(out, static_cast<uint16_t>(y));
    appendDlgLE16(out, static_cast<uint16_t>(cx));
    appendDlgLE16(out, static_cast<uint16_t>(cy));
    appendDlgLE16(out, id);
    appendDialogAtomClass(out, classAtom);
    appendDialogWideString(out, title);
    appendDlgLE16(out, 0);
}

void appendDialogControl(std::vector<uint8_t> &out,
                         uint32_t style,
                         uint32_t exStyle,
                         int16_t x,
                         int16_t y,
                         int16_t cx,
                         int16_t cy,
                         uint16_t id,
                         const std::string &className,
                         const std::string &title) {
    alignDialogDword(out);
    appendDlgLE32(out, style);
    appendDlgLE32(out, exStyle);
    appendDlgLE16(out, static_cast<uint16_t>(x));
    appendDlgLE16(out, static_cast<uint16_t>(y));
    appendDlgLE16(out, static_cast<uint16_t>(cx));
    appendDlgLE16(out, static_cast<uint16_t>(cy));
    appendDlgLE16(out, id);
    appendDialogWideString(out, className);
    appendDialogWideString(out, title);
    appendDlgLE16(out, 0);
}

std::vector<uint8_t> buildWizardDialogTemplate(const WindowsPackageLayout &layout,
                                               bool uninstallDialog) {
    constexpr uint16_t kButtonClass = 0x0080;
    constexpr uint16_t kEditClass = 0x0081;
    constexpr uint16_t kStaticClass = 0x0082;
    constexpr uint32_t kWsChildVisible = 0x50000000u;
    constexpr uint32_t kWsTabStop = 0x00010000u;
    constexpr uint32_t kWsDisabled = 0x08000000u;
    constexpr uint32_t kDsModalFrame = 0x00000080u;
    constexpr uint32_t kDsSetFont = 0x00000040u;
    constexpr uint32_t kDsCenter = 0x00000800u;
    constexpr uint32_t kWsPopup = 0x80000000u;
    constexpr uint32_t kWsCaption = 0x00C00000u;
    constexpr uint32_t kWsSysMenu = 0x00080000u;
    constexpr uint32_t kWsBorder = 0x00800000u;
    constexpr uint32_t kWsVScroll = 0x00200000u;
    constexpr uint32_t kSsLeft = 0x00000000u;
    constexpr uint32_t kSsBitmap = 0x0000000Eu;
    constexpr uint32_t kEsMultiline = 0x00000004u;
    constexpr uint32_t kEsAutoVScroll = 0x00000040u;
    constexpr uint32_t kEsReadOnly = 0x00000800u;
    constexpr uint32_t kBsPushButton = 0x00000000u;
    constexpr uint32_t kBsDefPushButton = 0x00000001u;
    constexpr uint32_t kBsAutoCheckBox = 0x00000003u;
    constexpr uint32_t kBsAutoRadioButton = 0x00000009u;
    constexpr uint32_t kPbsSmooth = 0x00000001u;

    const std::string title = layout.displayName + (uninstallDialog ? " Uninstall" : " Setup");
    const std::string intro =
        uninstallDialog ? "Review the removal summary, accept the confirmation, then choose Next."
                        : "Review the license and install scope, then choose Next.";
    std::string body = uninstallDialog
                           ? ("This will remove " + layout.displayName + " from this computer.")
                           : layout.licenseText;
    if (body.empty())
        body = "GPL-3.0-only";
    body = normalizeDialogText(body);

    std::vector<uint8_t> out;
    appendDlgLE32(out, kDsModalFrame | kDsSetFont | kDsCenter | kWsPopup | kWsCaption | kWsSysMenu);
    appendDlgLE32(out, 0);
    appendDlgLE16(out, 11);
    appendDlgLE16(out, 10);
    appendDlgLE16(out, 10);
    appendDlgLE16(out, 290);
    appendDlgLE16(out, 210);
    appendDlgLE16(out, 0);
    appendDlgLE16(out, 0);
    appendDialogWideString(out, title);
    appendDlgLE16(out, 9);
    appendDialogWideString(out, "Segoe UI");

    appendDialogControl(
        out, kWsChildVisible | kSsBitmap, 0, 0, 0, 290, 40, kDlgIdBanner, kStaticClass, "");
    appendDialogControl(
        out, kWsChildVisible | kSsLeft, 0, 8, 7, 270, 10, 2001, kStaticClass, title);
    appendDialogControl(
        out, kWsChildVisible | kSsLeft, 0, 8, 22, 270, 14, 2002, kStaticClass, intro);
    appendDialogControl(out,
                        kWsChildVisible | kWsTabStop | kWsBorder | kWsVScroll | kEsMultiline |
                            kEsAutoVScroll | kEsReadOnly,
                        0,
                        8,
                        43,
                        274,
                        93,
                        kDlgIdLicense,
                        kEditClass,
                        body);
    appendDialogControl(out,
                        kWsChildVisible | kWsTabStop | kBsAutoCheckBox,
                        0,
                        8,
                        141,
                        180,
                        10,
                        kDlgIdAccept,
                        kButtonClass,
                        uninstallDialog ? "I understand and want to continue"
                                        : "I accept the license agreement");
    appendDialogControl(out,
                        kWsChildVisible | kWsTabStop | kBsAutoRadioButton,
                        0,
                        8,
                        155,
                        116,
                        10,
                        kDlgIdScopeUser,
                        kButtonClass,
                        "Current user");
    appendDialogControl(out,
                        kWsChildVisible | kWsTabStop | kBsAutoRadioButton,
                        0,
                        132,
                        155,
                        116,
                        10,
                        kDlgIdScopeMachine,
                        kButtonClass,
                        "All users");
    appendDialogControl(out,
                        kWsChildVisible | kPbsSmooth,
                        0,
                        8,
                        171,
                        274,
                        10,
                        kDlgIdProgress,
                        "msctls_progress32",
                        "");
    appendDialogControl(out,
                        kWsChildVisible | kWsDisabled | kBsPushButton,
                        0,
                        88,
                        188,
                        56,
                        14,
                        kDlgIdBack,
                        kButtonClass,
                        "< Back");
    appendDialogControl(out,
                        kWsChildVisible | kWsTabStop | kBsDefPushButton,
                        0,
                        150,
                        188,
                        56,
                        14,
                        kDlgIdOk,
                        kButtonClass,
                        "Next");
    appendDialogControl(out,
                        kWsChildVisible | kWsTabStop | kBsPushButton,
                        0,
                        214,
                        188,
                        56,
                        14,
                        kDlgIdCancel,
                        kButtonClass,
                        "Cancel");

    alignDialogDword(out);
    return out;
}

/// @brief Resolve the payload architecture to the bootstrap PE machine type.
/// ARM64 payloads use a native ARM64 PE bootstrap; x64 payloads use the x64 emitter.
std::string resolveBootstrapArch(const std::string &payloadArch) {
    if (payloadArch.empty() || payloadArch == "x64")
        return "x64";
    if (payloadArch == "arm64")
        return "arm64";
    throw std::runtime_error("unsupported Windows package architecture '" + payloadArch + "'");
}

/// @brief Compute the byte offset within the .rdata section where the IAT array begins.
/// Layout: IDT (20 × (N+1) bytes) + ILTs + hint/name table + DLL name strings, rounded
/// up to 8-byte alignment. This offset is required to patch RIP-relative IAT call
/// targets in the already-emitted machine code.
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

/// @brief Finalize section RVAs, patch all IAT call offsets, and populate stub.textSection.
/// Must be called exactly once after all emit* functions have run. Places .text at kTextRVA
/// and .rdata immediately after (aligned to kSectionAlignment); computes the IAT base RVA
/// and stubData RVA offset, then calls gen.finishText() to apply all fixups.
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

void finalizeStubRVAs(StubResult &stub, InstallerStubGenA64 &gen) {
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

/// @brief Emit code to zero an 8-byte local variable at [RBP+off] via XOR-zero + MOV.
void zeroLocalQword(InstallerStubGen &gen, int32_t off) {
    gen.xorRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.movMemReg(X64Reg::RBP, off, X64Reg::RAX);
}

/// @brief Zero a small fixed local range using 8-byte stores.
void zeroLocalRange(InstallerStubGen &gen, int32_t off, uint32_t bytes) {
    for (uint32_t i = 0; i < bytes; i += 8)
        zeroLocalQword(gen, off + static_cast<int32_t>(i));
}

/// @brief Emit code to store a 64-bit immediate into [RSP+off].
/// Used to pass the 5th+ arguments in a Windows x64 call (beyond the four shadow-space registers).
void storeStackImm64(InstallerStubGen &gen, int32_t off, uint64_t imm) {
    gen.movRegImm64(X64Reg::RAX, imm);
    gen.movMemReg(X64Reg::RSP, off, X64Reg::RAX);
}

/// @brief Emit code to compute &[RBP+localOff] into RAX and store it at [RSP+stackOff].
/// Used to pass a pointer to a stack-resident buffer as a 5th+ Win32 argument.
void storeStackPtrToLocal(InstallerStubGen &gen, int32_t stackOff, int32_t localOff) {
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, localOff);
    gen.movMemReg(X64Reg::RSP, stackOff, X64Reg::RAX);
}

/// @brief Return the byte size of `text` as a UTF-16LE string including the null terminator.
/// Throws if the result exceeds UINT32_MAX — RegSetValueExW takes a DWORD cbData.
uint32_t wideBytesFor(const std::string &text) {
    const size_t bytes = (utf16CodeUnitCountFromUtf8(text) + 1) * 2;
    if (bytes > UINT32_MAX)
        throw std::runtime_error("Windows installer string is too large");
    return static_cast<uint32_t>(bytes);
}

/// @brief Return the install directory name, using `displayName` as fallback when `installDirName`
/// is empty.
std::string installDirNameFor(const WindowsPackageLayout &layout) {
    return layout.installDirName.empty() ? layout.displayName : layout.installDirName;
}

/// @brief Return the registry hive used for package-owned metadata.
uint64_t registryRootFor(const WindowsPackageLayout &layout) {
    return layout.perUserInstall ? kHkeyCurrentUser : kHkeyLocalMachine;
}

using KnownFolderGuid = std::array<uint8_t, 16>;

constexpr KnownFolderGuid kFolderIdProgramFiles = {
    0xb6, 0x63, 0x5e, 0x90, 0xbf, 0xc1, 0x4e, 0x49, 0xb2, 0x9c, 0x65, 0xb7, 0x32, 0xd3, 0xd2, 0x1a};
constexpr KnownFolderGuid kFolderIdLocalAppData = {
    0x85, 0x27, 0xb3, 0xf1, 0xba, 0x6f, 0xcf, 0x4f, 0x9d, 0x55, 0x7b, 0x8e, 0x7f, 0x15, 0x70, 0x91};
constexpr KnownFolderGuid kFolderIdDesktop = {
    0x3a, 0xcc, 0xbf, 0xb4, 0x2c, 0xdb, 0x4c, 0x42, 0xb0, 0x29, 0x7f, 0xe9, 0x9a, 0x87, 0xc6, 0x41};
constexpr KnownFolderGuid kFolderIdPublicDesktop = {
    0x0d, 0x34, 0xaa, 0xc4, 0x0f, 0xf2, 0x63, 0x48, 0xaf, 0xef, 0xf8, 0x7e, 0xf2, 0xe6, 0xba, 0x25};
constexpr KnownFolderGuid kFolderIdPrograms = {
    0x77, 0x5d, 0x7f, 0xa7, 0x2b, 0x2e, 0xc3, 0x44, 0xa6, 0xa2, 0xab, 0xa6, 0x01, 0x05, 0x4a, 0x51};
constexpr KnownFolderGuid kFolderIdCommonPrograms = {
    0x4e, 0xd4, 0x39, 0x01, 0xfe, 0x6a, 0xf2, 0x49, 0x86, 0x90, 0x3d, 0xaf, 0xca, 0xe6, 0xff, 0xb8};

/// @brief KNOWNFOLDERID for the install root: LocalAppData (per-user) or ProgramFiles.
const KnownFolderGuid &installRootFolderIdFor(const WindowsPackageLayout &layout) {
    return layout.perUserInstall ? kFolderIdLocalAppData : kFolderIdProgramFiles;
}

/// @brief KNOWNFOLDERID for the desktop: the user's Desktop (per-user) or PublicDesktop.
const KnownFolderGuid &desktopFolderIdFor(const WindowsPackageLayout &layout) {
    return layout.perUserInstall ? kFolderIdDesktop : kFolderIdPublicDesktop;
}

/// @brief KNOWNFOLDERID for Start Menu Programs: per-user Programs or CommonPrograms.
const KnownFolderGuid &programsFolderIdFor(const WindowsPackageLayout &layout) {
    return layout.perUserInstall ? kFolderIdPrograms : kFolderIdCommonPrograms;
}

/// @brief Return the registry key used for PATH changes.
std::string environmentKeyPathFor(const WindowsPackageLayout &layout) {
    return layout.perUserInstall
               ? "Environment"
               : "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
}

/// @brief Return the registry key identifier for this package.
/// Preference order: explicit `identifier`, normalized `executableName`, normalized `displayName`.
std::string registryIdFor(const WindowsPackageLayout &layout) {
    if (!layout.identifier.empty())
        return layout.identifier;
    if (!layout.executableName.empty())
        return normalizeExecName(layout.executableName);
    return normalizeExecName(layout.displayName);
}

/// @brief Convert all forward slashes in `text` to backslashes for use as a Windows path component.
std::string windowsPathFragment(std::string text) {
    for (char &ch : text) {
        if (ch == '/')
            ch = '\\';
    }
    return text;
}

/// @brief Build the full HKLM registry key path for the application's Add/Remove Programs entry.
std::string uninstallKeyPathFor(const WindowsPackageLayout &layout) {
    return "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + registryIdFor(layout);
}

/// @brief Return true if the Desktop path buffer must be resolved at runtime.
/// True when a desktop shortcut is requested or any install/uninstall file entry
/// targets WindowsInstallRoot::DesktopDir.
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

/// @brief Return true if the Start Menu path buffer must be resolved at runtime.
/// True when a Start Menu shortcut is requested or any install/uninstall file or
/// directory entry targets WindowsInstallRoot::StartMenuDir.
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

/// @brief Map a WindowsInstallRoot anchor to its corresponding stack-frame local buffer offset.
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

/// @brief Emit a null-guarded CloseHandle for the HANDLE at [RBP+handleOff], then zero the slot.
/// Safe to call when the slot is already zero; the null check skips the call.
void emitCloseLocalHandleIfSet(InstallerStubGen &gen, int32_t handleOff, uint32_t closeSlot) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, handleOff);
    gen.testRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.jz(lblSkip);
    gen.callIATSlot(closeSlot);
    zeroLocalQword(gen, handleOff);
    gen.bindLabel(lblSkip);
}

/// @brief Emit a null-guarded LocalFree for the heap pointer at [RBP+ptrOff], then zero the slot.
void emitLocalFreeIfSet(InstallerStubGen &gen, int32_t ptrOff, uint32_t freeSlot) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, ptrOff);
    gen.testRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.jz(lblSkip);
    gen.callIATSlot(freeSlot);
    zeroLocalQword(gen, ptrOff);
    gen.bindLabel(lblSkip);
}

/// @brief Emit a null-guarded RegCloseKey for the HKEY at [RBP+keyOff], then zero the slot.
void emitRegCloseIfSet(InstallerStubGen &gen, int32_t keyOff, uint32_t closeSlot) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, keyOff);
    gen.testRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.jz(lblSkip);
    gen.callIATSlot(closeSlot);
    zeroLocalQword(gen, keyOff);
    gen.bindLabel(lblSkip);
}

/// @brief Emit lstrcatW of a RIP-relative embedded wide string onto [RBP+destOff].
/// Checks combined length against kMaxPathChars before concatenating; jumps to errorLabel on
/// overflow.
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

/// @brief Emit lstrcatW of the stack buffer at [RBP+srcOff] onto [RBP+destOff].
/// Length-checks against kMaxPathChars before concatenating; jumps to errorLabel on overflow.
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

/// @brief Emit lstrcatW of the wide string in `srcReg` onto [RBP+destOff].
/// Length-checks against kMaxPathChars before concatenating; jumps to errorLabel on overflow.
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

/// @brief Emit a CreateDirectoryW call for the path already in RCX.
/// Treats ERROR_ALREADY_EXISTS as success; jumps to errorLabel on any other failure.
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

/// @brief Emit `cmp reg, value` using R10 as scratch for the immediate.
/// The direct cmp-reg-imm32 encoding sign-extends the immediate; loading it into R10
/// first allows correct unsigned comparison for values with bit 31 set.
void emitCmpRegU32(InstallerStubGen &gen, X64Reg reg, uint32_t value) {
    gen.movRegImm32(X64Reg::R10, value);
    gen.cmpRegReg(reg, X64Reg::R10);
}

/// @brief Emit code to compose a full path into [RBP+tempOff].
/// Copies the resolved root path (install/desktop/menu) for `root`, then appends
/// "\" and the embedded relative path string; jumps to errorLabel on overflow.
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

/// @brief Emit a MessageBoxW call with a NULL parent window using embedded title and message
/// strings. `flags` selects icon/button style (e.g. MB_ICONINFORMATION=0x40, MB_ICONERROR=0x10).
void emitMessageBox(
    InstallerStubGen &gen, uint32_t slot, uint32_t titleOff, uint32_t messageOff, uint32_t flags) {
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.leaRipData(X64Reg::RDX, messageOff);
    gen.leaRipData(X64Reg::R8, titleOff);
    gen.movRegImm32(X64Reg::R9, flags);
    gen.callIATSlot(slot);
}

/// @brief Emit a MessageBoxW call unless `/quiet` or `/silent` was detected.
void emitMessageBoxUnlessQuiet(
    InstallerStubGen &gen, uint32_t slot, uint32_t titleOff, uint32_t messageOff, uint32_t flags) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kQuietModeOff);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblSkip);
    emitMessageBox(gen, slot, titleOff, messageOff, flags);
    gen.bindLabel(lblSkip);
}

/// @brief Describes an embedded command-line flag string for token detection.
struct FlagModeSpec {
    uint32_t stringOff;    ///< Offset of the UTF-16 flag text in the data section.
    uint32_t utf16ByteLen; ///< Length of that flag text in bytes (UTF-16).
};

/// @brief Detect automation flags using case-insensitive token-boundary checks.
/// The stub stays dependency-free but avoids matching flag text inside the executable
/// path or inside longer arguments.
void emitDetectFlagMode(InstallerStubGen &gen,
                        uint32_t getCommandLineSlot,
                        uint32_t strstrSlot,
                        const std::vector<FlagModeSpec> &flags,
                        int32_t destModeOff) {
    const auto lblDone = gen.newLabel();
    const auto lblFound = gen.newLabel();

    zeroLocalQword(gen, destModeOff);
    zeroLocalQword(gen, kCommandLineOff);

    gen.callIATSlot(getCommandLineSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblDone);
    gen.movMemReg(X64Reg::RBP, kCommandLineOff, X64Reg::RAX);

    for (const auto &flag : flags) {
        const auto lblFindNext = gen.newLabel();
        const auto lblCheckEnd = gen.newLabel();
        const auto lblStartOk = gen.newLabel();
        const auto lblInvalidMatch = gen.newLabel();
        const auto lblNextFlag = gen.newLabel();
        gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kCommandLineOff);
        gen.movMemReg(X64Reg::RBP, kBytesReadOff, X64Reg::RAX);

        gen.bindLabel(lblFindNext);
        gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kBytesReadOff);
        gen.leaRipData(X64Reg::RDX, flag.stringOff);
        gen.callIATSlot(strstrSlot);
        gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
        gen.jz(lblNextFlag);
        gen.movMemReg(X64Reg::RBP, kBytesWrittenOff, X64Reg::RAX);

        gen.movRegMem(X64Reg::R10, X64Reg::RBP, kCommandLineOff);
        gen.cmpRegReg(X64Reg::RAX, X64Reg::R10);
        gen.jz(lblCheckEnd);
        gen.movRegReg(X64Reg::R10, X64Reg::RAX);
        gen.subRegImm32(X64Reg::R10, 2);
        gen.xorRegReg(X64Reg::RAX, X64Reg::RAX);
        gen.movzxRegMemIndex16(X64Reg::R11, X64Reg::R10, X64Reg::RAX, 0, 0);
        gen.cmpRegImm32(X64Reg::R11, ' ');
        gen.jz(lblStartOk);
        gen.cmpRegImm32(X64Reg::R11, '\t');
        gen.jz(lblStartOk);
        gen.cmpRegImm32(X64Reg::R11, '"');
        gen.jnz(lblInvalidMatch);

        gen.bindLabel(lblStartOk);
        gen.bindLabel(lblCheckEnd);
        gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kBytesWrittenOff);
        gen.addRegImm32(X64Reg::RAX, flag.utf16ByteLen);
        gen.xorRegReg(X64Reg::R10, X64Reg::R10);
        gen.movzxRegMemIndex16(X64Reg::R11, X64Reg::RAX, X64Reg::R10, 0, 0);
        gen.testRegReg(X64Reg::R11, X64Reg::R11);
        gen.jz(lblFound);
        gen.cmpRegImm32(X64Reg::R11, ' ');
        gen.jz(lblFound);
        gen.cmpRegImm32(X64Reg::R11, '\t');
        gen.jz(lblFound);
        gen.cmpRegImm32(X64Reg::R11, '"');
        gen.jz(lblFound);

        gen.bindLabel(lblInvalidMatch);
        gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kBytesWrittenOff);
        gen.addRegImm32(X64Reg::RAX, 2);
        gen.movMemReg(X64Reg::RBP, kBytesReadOff, X64Reg::RAX);
        gen.jmp(lblFindNext);
        gen.bindLabel(lblNextFlag);
    }
    gen.jmp(lblDone);

    gen.bindLabel(lblFound);
    gen.movRegImm32(X64Reg::RAX, 1);
    gen.movMemReg(X64Reg::RBP, destModeOff, X64Reg::RAX);
    gen.bindLabel(lblDone);
}

/// @brief Detect quiet automation flags.
void emitDetectQuietMode(InstallerStubGen &gen,
                         uint32_t getCommandLineSlot,
                         uint32_t strstrSlot,
                         const std::vector<FlagModeSpec> &flags) {
    emitDetectFlagMode(gen, getCommandLineSlot, strstrSlot, flags, kQuietModeOff);
}

void emitWizardDialog(InstallerStubGen &gen,
                      uint32_t templateOff,
                      uint32_t dialogProcLabel,
                      uint32_t initCommonControlsSlot,
                      uint32_t dialogBoxSlot,
                      uint32_t cancelLabel) {
    const auto lblSkip = gen.newLabel();
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kQuietModeOff);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblSkip);

    gen.movMemImm32(X64Reg::RBP, kInitCommonControlsOff, 8);
    gen.movMemImm32(X64Reg::RBP, kInitCommonControlsOff + 4, kIccProgressClass);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInitCommonControlsOff);
    gen.callIATSlot(initCommonControlsSlot);

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.leaRipData(X64Reg::RDX, templateOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.leaCodeLabel(X64Reg::R9, dialogProcLabel);
    storeStackImm64(gen, 0x20, 0);
    gen.callIATSlot(dialogBoxSlot);
    gen.cmpRegImm32(X64Reg::RAX, kDlgIdOk);
    gen.jnz(cancelLabel);
    gen.bindLabel(lblSkip);
}

void emitEndDialogAndReturnTrue(InstallerStubGen &gen, uint32_t endDialogSlot, uint32_t result) {
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kDlgProcHwndOff);
    gen.movRegImm32(X64Reg::RDX, result);
    gen.callIATSlot(endDialogSlot);
    gen.movRegImm32(X64Reg::RAX, 1);
}

void emitDialogProcEpilogue(InstallerStubGen &gen) {
    gen.addRegImm32(X64Reg::RSP, kDlgProcFrameSize);
    gen.pop(X64Reg::RBX);
    gen.pop(X64Reg::RBP);
    gen.ret();
}

void emitWizardDialogProc(InstallerStubGen &gen,
                          uint32_t label,
                          uint32_t endDialogSlot,
                          uint32_t getDlgItemSlot,
                          uint32_t sendMessageSlot,
                          uint32_t enableWindowSlot,
                          bool perUserInstall) {
    const auto lblInit = gen.newLabel();
    const auto lblCommand = gen.newLabel();
    const auto lblClose = gen.newLabel();
    const auto lblAccept = gen.newLabel();
    const auto lblOk = gen.newLabel();
    const auto lblCancel = gen.newLabel();
    const auto lblReturnTrue = gen.newLabel();
    const auto lblReturnFalse = gen.newLabel();
    const auto lblDone = gen.newLabel();

    gen.bindLabel(label);
    gen.push(X64Reg::RBP);
    gen.push(X64Reg::RBX);
    gen.movRegReg(X64Reg::RBP, X64Reg::RSP);
    gen.subRegImm32(X64Reg::RSP, kDlgProcFrameSize);
    gen.movMemReg(X64Reg::RBP, kDlgProcHwndOff, X64Reg::RCX);
    gen.movMemReg(X64Reg::RBP, kDlgProcMsgOff, X64Reg::RDX);
    gen.movMemReg(X64Reg::RBP, kDlgProcWparamOff, X64Reg::R8);

    gen.cmpRegImm32(X64Reg::RDX, kWmInitDialog);
    gen.jz(lblInit);
    gen.cmpRegImm32(X64Reg::RDX, kWmCommand);
    gen.jz(lblCommand);
    gen.cmpRegImm32(X64Reg::RDX, kWmClose);
    gen.jz(lblClose);
    gen.jmp(lblReturnFalse);

    gen.bindLabel(lblInit);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kDlgProcHwndOff);
    gen.movRegImm32(X64Reg::RDX, kDlgIdOk);
    gen.callIATSlot(getDlgItemSlot);
    gen.movRegReg(X64Reg::RCX, X64Reg::RAX);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.callIATSlot(enableWindowSlot);
    const uint32_t checkedScopeId = perUserInstall ? kDlgIdScopeUser : kDlgIdScopeMachine;
    const uint32_t uncheckedScopeId = perUserInstall ? kDlgIdScopeMachine : kDlgIdScopeUser;
    for (uint32_t scopeId : {checkedScopeId, uncheckedScopeId}) {
        gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kDlgProcHwndOff);
        gen.movRegImm32(X64Reg::RDX, scopeId);
        gen.callIATSlot(getDlgItemSlot);
        gen.movRegReg(X64Reg::RBX, X64Reg::RAX);
        gen.movRegReg(X64Reg::RCX, X64Reg::RAX);
        gen.movRegImm32(X64Reg::RDX, kBmSetCheck);
        gen.movRegImm32(X64Reg::R8, scopeId == checkedScopeId ? kBstChecked : 0);
        gen.xorRegReg(X64Reg::R9, X64Reg::R9);
        gen.callIATSlot(sendMessageSlot);
        gen.movRegReg(X64Reg::RCX, X64Reg::RBX);
        gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
        gen.callIATSlot(enableWindowSlot);
    }
    gen.jmp(lblReturnTrue);

    gen.bindLabel(lblCommand);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kDlgProcWparamOff);
    gen.cmpRegImm32(X64Reg::RAX, kDlgIdOk);
    gen.jz(lblOk);
    gen.cmpRegImm32(X64Reg::RAX, kDlgIdCancel);
    gen.jz(lblCancel);
    gen.cmpRegImm32(X64Reg::RAX, kDlgIdAccept);
    gen.jz(lblAccept);
    gen.jmp(lblReturnFalse);

    gen.bindLabel(lblAccept);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kDlgProcHwndOff);
    gen.movRegImm32(X64Reg::RDX, kDlgIdAccept);
    gen.callIATSlot(getDlgItemSlot);
    gen.movRegReg(X64Reg::RCX, X64Reg::RAX);
    gen.movRegImm32(X64Reg::RDX, kBmGetCheck);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.xorRegReg(X64Reg::R9, X64Reg::R9);
    gen.callIATSlot(sendMessageSlot);
    gen.movRegReg(X64Reg::RBX, X64Reg::RAX);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kDlgProcHwndOff);
    gen.movRegImm32(X64Reg::RDX, kDlgIdOk);
    gen.callIATSlot(getDlgItemSlot);
    gen.movRegReg(X64Reg::RCX, X64Reg::RAX);
    gen.movRegReg(X64Reg::RDX, X64Reg::RBX);
    gen.callIATSlot(enableWindowSlot);
    gen.jmp(lblReturnTrue);

    gen.bindLabel(lblOk);
    emitEndDialogAndReturnTrue(gen, endDialogSlot, kDlgIdOk);
    gen.jmp(lblDone);

    gen.bindLabel(lblCancel);
    emitEndDialogAndReturnTrue(gen, endDialogSlot, kDlgIdCancel);
    gen.jmp(lblDone);

    gen.bindLabel(lblClose);
    emitEndDialogAndReturnTrue(gen, endDialogSlot, kDlgIdCancel);
    gen.jmp(lblDone);

    gen.bindLabel(lblReturnTrue);
    gen.movRegImm32(X64Reg::RAX, 1);
    gen.jmp(lblDone);

    gen.bindLabel(lblReturnFalse);
    gen.xorRegReg(X64Reg::RAX, X64Reg::RAX);

    gen.bindLabel(lblDone);
    emitDialogProcEpilogue(gen);
}

/// @brief Emit code to create a directory at root\relPath.
/// Composes the full path into kTempPathOff via emitComposePath, then calls
/// CreateDirectoryW (ERROR_ALREADY_EXISTS is silently tolerated).
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

/// @brief Emit RegSetValueExW to write a named REG_SZ value from an embedded wide string.
/// `valueBytes` is the full UTF-16LE byte count including the null terminator.
/// Jumps to errorLabel if the API returns non-zero.
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

/// @brief Emit RegSetValueExW to write a named REG_DWORD value.
void emitRegSetConstDword(InstallerStubGen &gen,
                          uint32_t regSetSlot,
                          uint32_t valueNameOff,
                          uint32_t value,
                          uint32_t errorLabel) {
    gen.movMemImm32(X64Reg::RBP, kBytesWrittenOff, value);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.movRegImm32(X64Reg::R9, kRegDword);
    gen.leaRegMem(X64Reg::RAX, X64Reg::RBP, kBytesWrittenOff);
    gen.movMemReg(X64Reg::RSP, 0x20, X64Reg::RAX);
    gen.movRegImm32(X64Reg::RAX, 4);
    gen.movMemReg(X64Reg::RSP, 0x28, X64Reg::RAX);
    gen.callIATSlot(regSetSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
}

/// @brief Emit RegSetValueExW to write a named REG_NONE value with zero length and NULL data.
/// Used to stamp a ProgID name into Software\Classes\.<ext>\OpenWithProgids.
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

/// @brief Emit RegSetValueExW to write the default (unnamed, empty-string key) value as REG_SZ.
/// Used to set the human-readable ProgID description shown in the Open With dialog.
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

/// @brief Emit RegSetValueExW to write a named value from a stack-resident wide string.
/// Computes the byte length at runtime via lstrlenW (* 2 + 2 for null terminator).
/// `valueType` selects REG_SZ or REG_EXPAND_SZ; jumps to errorLabel on failure.
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

/// @brief Convenience overload of emitRegSetStackString that defaults `valueType` to REG_SZ.
void emitRegSetStackString(InstallerStubGen &gen,
                           uint32_t regSetSlot,
                           uint32_t strlenSlot,
                           uint32_t valueNameOff,
                           int32_t stackBufOff,
                           uint32_t errorLabel) {
    emitRegSetStackString(
        gen, regSetSlot, strlenSlot, valueNameOff, stackBufOff, kRegSz, errorLabel);
}

/// @brief Emit InstallDate as the current local date in YYYYMMDD form.
/// Falls back to the generator-supplied date if GetDateFormatW fails.
void emitRegSetCurrentInstallDate(InstallerStubGen &gen,
                                  uint32_t getDateFormatSlot,
                                  uint32_t copySlot,
                                  uint32_t regSetSlot,
                                  uint32_t strlenSlot,
                                  uint32_t valueNameOff,
                                  uint32_t formatOff,
                                  uint32_t fallbackDateOff,
                                  uint32_t errorLabel) {
    const auto lblUseStackDate = gen.newLabel();

    gen.movRegImm32(X64Reg::RCX, kLocaleUserDefault);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.leaRipData(X64Reg::R9, formatOff);
    storeStackPtrToLocal(gen, 0x20, kTempPathOff);
    storeStackImm64(gen, 0x28, 9);
    gen.callIATSlot(getDateFormatSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblUseStackDate);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.leaRipData(X64Reg::RDX, fallbackDateOff);
    gen.callIATSlot(copySlot);

    gen.bindLabel(lblUseStackDate);
    emitRegSetStackString(gen, regSetSlot, strlenSlot, valueNameOff, kTempPathOff, errorLabel);
}

/// @brief Emit RegSetValueExW to write the default (unnamed) value from a stack-resident wide
/// string. Computes the byte length via lstrlenW at runtime. Used for shell Open command strings.
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

/// @brief Emit RegQueryValueExW to read a named value from kRegKeyOff into a stack buffer.
/// `bufferBytes` is the buffer's capacity in bytes. Only missing-value errors jump
/// to missingLabel when a distinct errorLabel is supplied; malformed, oversized, or
/// unreadable values jump to errorLabel so PATH updates cannot overwrite unknown data.
void emitRegQueryStackString(InstallerStubGen &gen,
                             uint32_t querySlot,
                             uint32_t valueNameOff,
                             int32_t stackBufOff,
                             uint32_t bufferBytes,
                             uint32_t missingLabel,
                             uint32_t errorLabel = UINT32_MAX) {
    const auto lblTypeOk = gen.newLabel();
    const auto lblQueryOk = gen.newLabel();
    const uint32_t fatalLabel = errorLabel == UINT32_MAX ? missingLabel : errorLabel;
    gen.movMemImm32(X64Reg::RBP, stackBufOff, 0);
    gen.movMemImm32(X64Reg::RBP, stackBufOff + static_cast<int32_t>(bufferBytes) - 4, 0);
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
    gen.jz(lblQueryOk);
    if (fatalLabel == missingLabel) {
        gen.jmp(missingLabel);
    } else {
        gen.cmpRegImm32(X64Reg::RAX, kErrorFileNotFound);
        gen.jz(missingLabel);
        gen.cmpRegImm32(X64Reg::RAX, kErrorPathNotFound);
        gen.jz(missingLabel);
        gen.jmp(fatalLabel);
    }
    gen.bindLabel(lblQueryOk);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kBytesWrittenOff);
    gen.cmpRegImm32(X64Reg::RAX, kRegSz);
    gen.jz(lblTypeOk);
    gen.cmpRegImm32(X64Reg::RAX, kRegExpandSz);
    gen.jnz(fatalLabel);
    gen.bindLabel(lblTypeOk);
    gen.movMemImm32(X64Reg::RBP, stackBufOff + static_cast<int32_t>(bufferBytes) - 4, 0);
}

/// @brief Emit code to append the separator string to [RBP+destOff] only when the buffer is
/// non-empty. Used to insert ";" between PATH tokens without creating a spurious leading separator.
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

/// @brief Emit SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, "Environment", ...).
/// Forces running applications (including Explorer) to reload the system environment
/// block so PATH changes take effect immediately without a reboot.
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

/// @brief Emit RegCreateKeyW under HKLM for the embedded key path, closing any previously open key
/// first. Jumps to errorLabel if creation fails.
void emitRegCreateConstKey(InstallerStubGen &gen,
                           uint32_t createSlot,
                           uint32_t keyOff,
                           uint32_t errorLabel,
                           uint64_t rootHkey = kHkeyLocalMachine) {
    emitRegCloseIfSet(gen, kRegKeyOff, kI_RegCloseKey);
    gen.movRegImm64(X64Reg::RCX, rootHkey);
    gen.leaRipData(X64Reg::RDX, keyOff);
    gen.leaRegMem(X64Reg::R8, X64Reg::RBP, kRegKeyOff);
    gen.callIATSlot(createSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);
}

/// @brief Emit RegDeleteTreeW for the embedded key path. Errors are silently ignored since
/// the subtree may already be absent during rollback or uninstall.
void emitRegDeleteConstKey(InstallerStubGen &gen,
                           uint32_t deleteSlot,
                           uint32_t keyOff,
                           uint64_t rootHkey = kHkeyLocalMachine) {
    gen.movRegImm64(X64Reg::RCX, rootHkey);
    gen.leaRipData(X64Reg::RDX, keyOff);
    gen.callIATSlot(deleteSlot);
}

/// @brief Build the HKLM Software\Classes\.<ext> registry key path for a file association.
/// Normalizes the extension to have a leading dot if it is missing.
std::string extensionKeyFor(const WindowsFileAssociationEntry &assoc) {
    std::string ext = assoc.extension;
    if (ext.empty() || ext.front() != '.')
        ext.insert(ext.begin(), '.');
    return "Software\\Classes\\" + ext;
}

/// @brief Build the HKLM Software\Classes\<ProgID> registry key path for a file association.
std::string progIdKeyFor(const WindowsFileAssociationEntry &assoc) {
    return "Software\\Classes\\" + assoc.progId;
}

/// @brief Build the HKLM Software\Classes\.<ext>\OpenWithProgids registry key path.
std::string openWithProgIdsKeyFor(const WindowsFileAssociationEntry &assoc) {
    return extensionKeyFor(assoc) + "\\OpenWithProgids";
}

/// @brief Emit RegOpenKeyW under HKLM for an embedded key path, closing the current open key first.
/// Jumps to missingLabel if the key does not exist (non-zero return).
void emitRegOpenConstKeyIfExists(InstallerStubGen &gen,
                                 uint32_t openSlot,
                                 uint32_t closeSlot,
                                 uint32_t keyOff,
                                 uint32_t missingLabel,
                                 uint64_t rootHkey = kHkeyLocalMachine) {
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.movRegImm64(X64Reg::RCX, rootHkey);
    gen.leaRipData(X64Reg::RDX, keyOff);
    gen.leaRegMem(X64Reg::R8, X64Reg::RBP, kRegKeyOff);
    gen.callIATSlot(openSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(missingLabel);
}

/// @brief Emit conditional RegDeleteValueW: opens the key, deletes the named value, then closes.
/// Silently skips the entire sequence if the key does not exist.
void emitRegDeleteNamedValueIfPresent(InstallerStubGen &gen,
                                      uint32_t openSlot,
                                      uint32_t closeSlot,
                                      uint32_t deleteValueSlot,
                                      uint32_t keyOff,
                                      uint32_t valueNameOff,
                                      uint64_t rootHkey = kHkeyLocalMachine) {
    const auto lblDone = gen.newLabel();
    emitRegOpenConstKeyIfExists(gen, openSlot, closeSlot, keyOff, lblDone, rootHkey);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, valueNameOff);
    gen.callIATSlot(deleteValueSlot);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.bindLabel(lblDone);
}

/// @brief Emit code to query a registry value and compare it to an embedded expected string.
/// Reads into kTempPathOff via RegQueryValueExW, checks the returned byte count,
/// then calls lstrcmpW. Jumps to notEqualLabel on query failure, size mismatch, or inequality.
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

/// @brief Emit code to test whether a named registry value exists under kRegKeyOff.
/// Jumps to existsLabel if RegQueryValueExW returns success (0) or ERROR_MORE_DATA.
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

/// @brief Emit code to delete the ProgID subtree only when owned by this application.
/// Ownership is determined by comparing the VAPSOwner registry value against the
/// embedded identifier string; deletes shell/open/command, shell/open, shell, and
/// the ProgID key itself. Silently skips if the key is absent or owned by another app.
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
                                 uint32_t shellKeyOff,
                                 uint64_t rootHkey = kHkeyLocalMachine) {
    const auto lblSkip = gen.newLabel();
    const auto lblNotOwned = gen.newLabel();
    emitRegOpenConstKeyIfExists(gen, openSlot, closeSlot, progKeyOff, lblSkip, rootHkey);
    emitRegQueryConstStringEquals(
        gen, querySlot, strcmpSlot, markerNameOff, markerValueOff, markerValueBytes, lblNotOwned);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kRegKeyOff);
    gen.leaRipData(X64Reg::RDX, markerNameOff);
    gen.callIATSlot(deleteValueSlot);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    emitRegDeleteConstKey(gen, deleteSlot, commandKeyOff, rootHkey);
    emitRegDeleteConstKey(gen, deleteSlot, openKeyOff, rootHkey);
    emitRegDeleteConstKey(gen, deleteSlot, shellKeyOff, rootHkey);
    emitRegDeleteConstKey(gen, deleteSlot, progKeyOff, rootHkey);
    gen.jmp(lblSkip);
    gen.bindLabel(lblNotOwned);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.bindLabel(lblSkip);
}

/// @brief Emit code to register all file associations from layout.fileAssociations in the Windows
/// registry. For each association creates: Software\Classes\.<ext> with Content Type and
/// VAPSContentTypeOwner, Software\Classes\.<ext>\OpenWithProgids\<ProgID> = REG_NONE,
/// Software\Classes\<ProgID> with VAPSOwner marker and description,
/// and Software\Classes\<ProgID>\shell\open\command = "&lt;exe&gt;" [args] "%1".
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
    const uint64_t rootHkey = registryRootFor(layout);

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
    const std::string associationIcon = layout.displayIconRelativePath.empty()
                                            ? associationExecutable
                                            : windowsPathFragment(layout.displayIconRelativePath);
    const uint32_t iconNameOff = gen.embedStringW(associationIcon);
    const uint32_t quoteOff = gen.embedStringW("\"");
    const uint32_t quotedFileArgOff = gen.embedStringW(" \"%1\"");

    for (const auto &assoc : layout.fileAssociations) {
        const uint32_t commandArgsOff = assoc.openCommandArguments.empty()
                                            ? 0
                                            : gen.embedStringW(" " + assoc.openCommandArguments);
        const uint32_t extKeyOff = gen.embedStringW(extensionKeyFor(assoc));
        const uint32_t progIdOff = gen.embedStringW(assoc.progId);
        emitRegCreateConstKey(gen, createSlot, extKeyOff, errorLabel, rootHkey);
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
        emitRegCreateConstKey(gen, createSlot, openWithKeyOff, errorLabel, rootHkey);
        emitRegSetConstNoneValue(gen, setValueSlot, progIdOff, errorLabel);
        emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);

        const uint32_t progKeyOff = gen.embedStringW(progIdKeyFor(assoc));
        const std::string description =
            assoc.description.empty() ? layout.displayName : assoc.description;
        const uint32_t descriptionOff = gen.embedStringW(description);
        emitRegCreateConstKey(gen, createSlot, progKeyOff, errorLabel, rootHkey);
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
        const uint32_t defaultIconKeyOff = gen.embedStringW(progIdKeyFor(assoc) + "\\DefaultIcon");
        emitRegCreateConstKey(gen, createSlot, defaultIconKeyOff, errorLabel, rootHkey);
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
        gen.leaRipData(X64Reg::RDX, quoteOff);
        gen.callIATSlot(copySlot);
        emitCheckedCatStack(gen, kTempPathOff, kInstallPathOff, catSlot, strlenSlot, errorLabel);
        emitCheckedCatEmbedded(gen, kTempPathOff, slashOff, catSlot, strlenSlot, errorLabel);
        emitCheckedCatEmbedded(gen, kTempPathOff, iconNameOff, catSlot, strlenSlot, errorLabel);
        emitCheckedCatEmbedded(gen, kTempPathOff, quoteOff, catSlot, strlenSlot, errorLabel);
        emitRegSetDefaultStackString(gen, setValueSlot, strlenSlot, kTempPathOff, errorLabel);
        emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);

        emitRegCreateConstKey(gen, createSlot, commandKeyOff, errorLabel, rootHkey);
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

/// @brief Emit code to unregister all file associations owned by this application.
/// Removes OpenWithProgids entries, Content Type if VAPSContentTypeOwner matches,
/// and the full ProgID subtree if the VAPSOwner marker matches our identifier.
/// Silently skips any association whose key is missing or owned by another app.
void emitUnregisterFileAssociations(InstallerStubGen &gen,
                                    const WindowsPackageLayout &layout,
                                    uint32_t openSlot,
                                    uint32_t closeSlot,
                                    uint32_t querySlot,
                                    uint32_t strcmpSlot,
                                    uint32_t deleteValueSlot,
                                    uint32_t deleteSlot) {
    const uint64_t rootHkey = registryRootFor(layout);
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
                                         gen.embedStringW(assoc.progId),
                                         rootHkey);
        if (!assoc.mimeType.empty()) {
            const auto lblContentDone = gen.newLabel();
            const auto lblContentNotOwned = gen.newLabel();
            emitRegOpenConstKeyIfExists(gen,
                                        openSlot,
                                        closeSlot,
                                        gen.embedStringW(extensionKeyFor(assoc)),
                                        lblContentDone,
                                        rootHkey);
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
        emitRegDeleteConstKey(
            gen, deleteSlot, gen.embedStringW(progIdKeyFor(assoc) + "\\DefaultIcon"), rootHkey);
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
            gen.embedStringW(progIdKeyFor(assoc) + "\\shell"),
            rootHkey);
    }
}

/// @brief Emit SHGetKnownFolderPath into a stack WCHAR buffer and free the shell string.
void emitResolveKnownFolderPath(InstallerStubGen &gen,
                                const KnownFolderGuid &folderId,
                                int32_t destOff,
                                uint32_t copySlot,
                                uint32_t folderSlot,
                                uint32_t freeSlot,
                                uint32_t errorLabel) {
    const uint32_t folderIdOff = gen.embedBytes(folderId.data(), folderId.size());
    zeroLocalQword(gen, kKnownFolderPtrOff);
    gen.leaRipData(X64Reg::RCX, folderIdOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.leaRegMem(X64Reg::R9, X64Reg::RBP, kKnownFolderPtrOff);
    gen.callIATSlot(folderSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(errorLabel);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, destOff);
    gen.movRegMem(X64Reg::RDX, X64Reg::RBP, kKnownFolderPtrOff);
    gen.callIATSlot(copySlot);
    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kKnownFolderPtrOff);
    gen.callIATSlot(freeSlot);
    zeroLocalQword(gen, kKnownFolderPtrOff);
}

/// @brief Emit code to resolve all required install root paths via SHGetKnownFolderPath.
/// Populates kInstallPathOff (always), kDesktopPathOff (if needsDesktopPath), and
/// kMenuPathOff (if needsMenuPath); appends the install-dir subdirectory to kInstallPathOff.
/// If `createRoots` is true, also emits CreateDirectoryW for each resolved root.
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
                        uint32_t freeSlot,
                        bool createRoots,
                        uint32_t errorLabel) {
    emitResolveKnownFolderPath(gen,
                               installRootFolderIdFor(layout),
                               kInstallPathOff,
                               copySlot,
                               folderSlot,
                               freeSlot,
                               errorLabel);

    emitCheckedCatEmbedded(gen, kInstallPathOff, slashOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatEmbedded(gen, kInstallPathOff, installDirOff, catSlot, strlenSlot, errorLabel);
    if (createRoots) {
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kInstallPathOff);
        gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
        emitCreateDirectoryChecked(gen, createDirSlot, getLastErrorSlot, errorLabel);
    }

    if (needsDesktopPath(layout)) {
        emitResolveKnownFolderPath(gen,
                                   desktopFolderIdFor(layout),
                                   kDesktopPathOff,
                                   copySlot,
                                   folderSlot,
                                   freeSlot,
                                   errorLabel);
    }

    if (needsMenuPath(layout)) {
        emitResolveKnownFolderPath(gen,
                                   programsFolderIdFor(layout),
                                   kMenuPathOff,
                                   copySlot,
                                   folderSlot,
                                   freeSlot,
                                   errorLabel);

        emitCheckedCatEmbedded(gen, kMenuPathOff, slashOff, catSlot, strlenSlot, errorLabel);
        emitCheckedCatEmbedded(gen, kMenuPathOff, installDirOff, catSlot, strlenSlot, errorLabel);
        if (createRoots) {
            gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kMenuPathOff);
            gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
            emitCreateDirectoryChecked(gen, createDirSlot, getLastErrorSlot, errorLabel);
        }
    }
}

/// @brief Emit code to compose the system PATH entry string into [RBP+destOff].
/// Copies kInstallPathOff, then appends "\<pathRelativePath>" when configured.
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

/// @brief Emit code to delete all contents of the install root before extraction.
/// Builds an "<installDir>\*" double-null-terminated glob, fills an SHFILEOPSTRUCT
/// with FO_DELETE | FOF_NOCONFIRMATION | FOF_NOERRORUI, and calls SHFileOperationW.
/// No-op when layout.cleanInstallRootBeforeInstall is false.
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

/// @brief Emit code to add the install path entry to the system PATH registry value.
/// Idempotent: checks VAPSPathEntry in the uninstall key first and skips if already present.
/// Otherwise reads the current PATH, strips any stale entry via emitRemovePathEntryTokens,
/// appends the new entry, writes back as REG_EXPAND_SZ, and broadcasts WM_SETTINGCHANGE.
/// No-op when layout.addToPath is false.
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
    const uint64_t rootHkey = registryRootFor(layout);

    const auto lblDoAppend = gen.newLabel();
    const auto lblPathMissing = gen.newLabel();
    const auto lblCheckCurrentPath = gen.newLabel();
    const auto lblUseCleanedPath = gen.newLabel();
    const auto lblWritePath = gen.newLabel();
    const auto lblSkipUpdateClose = gen.newLabel();
    const auto lblSkipUpdate = gen.newLabel();

    emitComposeInstallPathEntry(
        gen, layout, kUninstallPathOff, slashOff, copySlot, catSlot, strlenSlot, errorLabel);

    emitRegOpenConstKeyIfExists(
        gen, openSlot, closeSlot, uninstallKeyOff, lblCheckCurrentPath, rootHkey);
    emitRegQueryStackString(gen,
                            querySlot,
                            regPathEntryOff,
                            kTempPathOff,
                            kMaxPathChars * 2u,
                            lblCheckCurrentPath,
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
    emitRegOpenConstKeyIfExists(gen, openSlot, closeSlot, envKeyOff, lblDoAppend, rootHkey);
    emitRegQueryStackString(gen,
                            querySlot,
                            regPathValueOff,
                            kPathOriginalOff,
                            kMaxPathChars * 2u,
                            lblDoAppend,
                            lblSkipUpdateClose);
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
    gen.jnz(lblUseCleanedPath);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.jmp(lblDoAppend);

    gen.bindLabel(lblUseCleanedPath);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kPathExpectedOff);
    gen.callIATSlot(copySlot);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.jmp(lblWritePath);

    gen.bindLabel(lblDoAppend);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    emitRegCreateConstKey(gen, createSlot, envKeyOff, errorLabel, rootHkey);
    gen.movMemImm32(X64Reg::RBP, kPathOriginalOff, 0);
    emitRegQueryStackString(gen,
                            querySlot,
                            regPathValueOff,
                            kPathOriginalOff,
                            kMaxPathChars * 2u,
                            lblPathMissing,
                            lblSkipUpdateClose);
    gen.bindLabel(lblPathMissing);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kPathOriginalOff);
    gen.callIATSlot(copySlot);
    gen.bindLabel(lblWritePath);
    emitAppendSeparatorIfNonEmpty(gen, kTempPathOff, semicolonOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatStack(gen, kTempPathOff, kUninstallPathOff, catSlot, strlenSlot, errorLabel);

    emitRegSetStackString(
        gen, setValueSlot, strlenSlot, regPathValueOff, kTempPathOff, kRegExpandSz, errorLabel);
    emitBroadcastEnvironmentChange(gen, sendSlot, environmentOff);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.movRegImm32(X64Reg::RAX, 1);
    gen.movMemReg(X64Reg::RBP, kPathUpdatedOff, X64Reg::RAX);
    gen.jmp(lblSkipUpdate);
    gen.bindLabel(lblSkipUpdateClose);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.bindLabel(lblSkipUpdate);
}

/// @brief Emit rollback code to restore the system PATH from kPathOriginalOff.
/// Only executes if kPathUpdatedOff is non-zero, meaning PATH was actually modified
/// earlier in the install sequence. No-op when layout.addToPath is false.
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

    const uint64_t rootHkey = registryRootFor(layout);
    const auto lblSkip = gen.newLabel();
    const auto lblClose = gen.newLabel();
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kPathUpdatedOff);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblSkip);
    emitRegCreateConstKey(gen, createSlot, envKeyOff, lblSkip, rootHkey);
    emitRegSetStackString(
        gen, setValueSlot, strlenSlot, regPathValueOff, kPathOriginalOff, kRegExpandSz, lblClose);
    emitBroadcastEnvironmentChange(gen, sendSlot, environmentOff);
    gen.bindLabel(lblClose);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.bindLabel(lblSkip);
}

/// @brief Emit code to rebuild the PATH string at [RBP+outputPathOff] without our entry.
/// Iterates the semicolon-delimited tokens in [RBP+currentPathOff], copying each token to
/// the output except those that match [RBP+entryOff] (case-insensitive via lstrcmpiW).
/// Used on install (remove stale entry before re-appending) and uninstall (remove our entry).
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

/// @brief Emit code to remove our PATH entry during uninstall.
/// Reads VAPSPathEntry from the uninstall registry key to recover the exact entry
/// string that was added, strips it from the current system PATH via
/// emitRemovePathEntryTokens, writes the cleaned PATH back as REG_EXPAND_SZ,
/// and broadcasts WM_SETTINGCHANGE. No-op when layout.addToPath is false or
/// the uninstall key does not exist.
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

    const uint64_t rootHkey = registryRootFor(layout);
    const auto lblSkip = gen.newLabel();
    const auto lblEntryMissing = gen.newLabel();
    const auto lblEnvMissing = gen.newLabel();

    emitRegOpenConstKeyIfExists(gen, openSlot, closeSlot, uninstallKeyOff, lblSkip, rootHkey);
    emitRegQueryStackString(gen,
                            querySlot,
                            regPathEntryOff,
                            kUninstallPathOff,
                            kMaxPathChars * 2u,
                            lblEntryMissing,
                            lblEntryMissing);
    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);

    emitRegCloseIfSet(gen, kRegKeyOff, closeSlot);
    gen.movRegImm64(X64Reg::RCX, rootHkey);
    gen.leaRipData(X64Reg::RDX, envKeyOff);
    gen.leaRegMem(X64Reg::R8, X64Reg::RBP, kRegKeyOff);
    gen.callIATSlot(createSlot);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblEnvMissing);
    emitRegQueryStackString(gen,
                            querySlot,
                            regPathValueOff,
                            kTempPathOff,
                            kMaxPathChars * 2u,
                            lblEnvMissing,
                            lblEnvMissing);
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
                          kPathExpectedOff,
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

/// @brief Emit code to extract a single file from the ZIP overlay appended to the installer PE.
/// Seeks to `overlayFileOffset + entry.overlayDataOffset` via SetFilePointerEx, reads in
/// kInstallerCopyChunkBytes chunks with incremental CRC-32 via RtlComputeCrc32, creates
/// the destination file, writes each chunk, and verifies the final CRC against entry.crc32.
/// Zero-byte files skip the read/CRC loop and only create the destination file.
void emitExtractFile(InstallerStubGen &gen,
                     const WindowsPackageFileEntry &entry,
                     uint64_t overlayFileOffset,
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
    const uint32_t entrySize = static_cast<uint32_t>(entry.sizeBytes);
    const uint64_t entryOffset = overlayFileOffset + entry.overlayDataOffset;
    if (entryOffset < overlayFileOffset)
        throw std::runtime_error("Windows installer overlay offset overflow: " +
                                 entry.relativePath);
    const uint32_t relPathOff = gen.embedStringW(entry.relativePath);

    if (entrySize == 0) {
        emitComposePath(gen,
                        entry.root,
                        kTempPathOff,
                        slashOff,
                        relPathOff,
                        copySlot,
                        catSlot,
                        strlenSlot,
                        errorLabel);

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

    emitComposePath(gen,
                    entry.root,
                    kTempPathOff,
                    slashOff,
                    relPathOff,
                    copySlot,
                    catSlot,
                    strlenSlot,
                    errorLabel);

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
    gen.movRegImm64(X64Reg::RAX, entryOffset);
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
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kBytesWrittenOff);
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

/// @brief Emit code to delete a single file at entry.root\entry.relativePath.
/// Treats ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND, and ERROR_DIR_NOT_EMPTY as
/// non-fatal (file already absent); jumps to errorLabel on any other failure.
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
    emitComposePath(gen,
                    entry.root,
                    kTempPathOff,
                    slashOff,
                    relPathOff,
                    copySlot,
                    catSlot,
                    strlenSlot,
                    errorLabel);
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

/// @brief Emit code to remove a directory at entry.root\entry.relativePath.
/// Treats missing and non-empty directories as non-fatal so uninstall can leave
/// unrelated user content in place.
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
    emitComposePath(gen,
                    entry.root,
                    kTempPathOff,
                    slashOff,
                    relPathOff,
                    copySlot,
                    catSlot,
                    strlenSlot,
                    errorLabel);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
    gen.callIATSlot(removeSlot);
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

/// @brief Expand a stored compressed inner payload ZIP into the install root.
///
/// The bootstrap still extracts this ZIP from the PE overlay with direct byte
/// copies. The command then invokes the PowerShell that ships with Windows from
/// System32, expands the inner ZIP, compares the previous installed manifest
/// with the next manifest, and deletes only Viper-owned stale files after the
/// new payload has expanded successfully.
void emitExpandCompressedPayload(InstallerStubGen &gen,
                                 const WindowsPackageLayout &layout,
                                 uint32_t slashOff,
                                 uint32_t copySlot,
                                 uint32_t catSlot,
                                 uint32_t strlenSlot,
                                 uint32_t deleteSlot,
                                 uint32_t getLastErrorSlot,
                                 uint32_t closeSlot,
                                 uint32_t errorLabel) {
    if (layout.compressedPayloadRelativePath.empty())
        return;
    if (layout.compressedPayloadManifestRelativePath.empty() ||
        layout.installedManifestRelativePath.empty()) {
        throw std::runtime_error("compressed Windows payload requires install manifests");
    }

    const uint32_t payloadRelOff = gen.embedStringW(layout.compressedPayloadRelativePath);
    const uint32_t installedManifestRelOff = gen.embedStringW(layout.installedManifestRelativePath);
    const uint32_t nextManifestRelOff =
        gen.embedStringW(layout.compressedPayloadManifestRelativePath);
    const uint32_t commandQuoteOff = gen.embedStringW("\"");
    const uint32_t commandArgSepOff = gen.embedStringW("\" \"");
    const uint32_t powershellTailOff = gen.embedStringW(
        "\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -NonInteractive "
        "-ExecutionPolicy Bypass -Command \"& { param([string]$p,[string]$d,[string]$old,"
        "[string]$new) $ErrorActionPreference='Stop'; "
        "$base=[IO.Path]::GetFullPath($d); "
        "function Test-Rel([string]$s){ "
        "if([string]::IsNullOrWhiteSpace($s)){ return $false; } "
        "if([IO.Path]::IsPathRooted($s) -or $s.Contains(':')){ return $false; } "
        "foreach($part in ($s -split '[\\\\/]+')){ "
        "if($part -eq '' -or $part -eq '.' -or $part -eq '..'){ return $false; } } "
        "return $true; } "
        "function Join-Owned([string]$s){ $f=[IO.Path]::GetFullPath((Join-Path $d $s)); "
        "$prefix=$base + [IO.Path]::DirectorySeparatorChar; "
        "if(-not $f.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){ return $null; } "
        "return $f; } $oldLines=@(); "
        "if(Test-Path -LiteralPath $old -PathType Leaf){ $oldLines=Get-Content "
        "-LiteralPath $old | Where-Object { Test-Rel $_ }; } "
        "$newLines=Get-Content -LiteralPath $new | Where-Object { Test-Rel $_ }; "
        "Expand-Archive -LiteralPath $p -DestinationPath $d -Force; $owned=@{}; "
        "foreach($n in $newLines){ $owned[$n.ToLowerInvariant()]=$true; } "
        "foreach($o in $oldLines){ if(-not $owned.ContainsKey($o.ToLowerInvariant())){ "
        "$f=Join-Owned $o; if($f -and (Test-Path -LiteralPath $f -PathType Leaf)){ "
        "try { Remove-Item -LiteralPath $f -Force -ErrorAction Stop } catch {} } } } "
        "$dirs=$oldLines | ForEach-Object { Split-Path -Parent $_ } | Where-Object { "
        "$_ -and (Test-Rel $_) } | Sort-Object Length -Descending -Unique; "
        "foreach($dir in $dirs){ $full=Join-Owned $dir; "
        "if($full -and (Test-Path -LiteralPath $full -PathType Container)){ try { Remove-Item "
        "-LiteralPath $full -Force -ErrorAction Stop } catch {} } } }\" \"");

    emitComposePath(gen,
                    WindowsInstallRoot::InstallDir,
                    kTempPathOff,
                    slashOff,
                    payloadRelOff,
                    copySlot,
                    catSlot,
                    strlenSlot,
                    errorLabel);
    emitComposePath(gen,
                    WindowsInstallRoot::InstallDir,
                    kPathOriginalOff,
                    slashOff,
                    installedManifestRelOff,
                    copySlot,
                    catSlot,
                    strlenSlot,
                    errorLabel);
    emitComposePath(gen,
                    WindowsInstallRoot::InstallDir,
                    kPathExpectedOff,
                    slashOff,
                    nextManifestRelOff,
                    copySlot,
                    catSlot,
                    strlenSlot,
                    errorLabel);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kUninstallPathOff);
    gen.movRegImm32(X64Reg::RDX, kMaxPathChars);
    gen.callIATSlot(kI_GetSystemDirectoryW);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);
    gen.cmpRegImm32(X64Reg::RAX, kMaxPathChars - 1);
    gen.ja(errorLabel);

    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kCommandBufferOff);
    gen.leaRipData(X64Reg::RDX, commandQuoteOff);
    gen.callIATSlot(copySlot);
    emitCheckedCatStack(gen, kCommandBufferOff, kUninstallPathOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatEmbedded(
        gen, kCommandBufferOff, powershellTailOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatStack(gen, kCommandBufferOff, kTempPathOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatEmbedded(
        gen, kCommandBufferOff, commandArgSepOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatStack(gen, kCommandBufferOff, kInstallPathOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatEmbedded(
        gen, kCommandBufferOff, commandArgSepOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatStack(gen, kCommandBufferOff, kPathOriginalOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatEmbedded(
        gen, kCommandBufferOff, commandArgSepOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatStack(gen, kCommandBufferOff, kPathExpectedOff, catSlot, strlenSlot, errorLabel);
    emitCheckedCatEmbedded(
        gen, kCommandBufferOff, commandQuoteOff, catSlot, strlenSlot, errorLabel);

    zeroLocalRange(gen, kStartupInfoOff, 0x68);
    zeroLocalRange(gen, kProcessInfoOff, 0x18);
    zeroLocalQword(gen, kBytesReadOff);
    gen.movMemImm32(X64Reg::RBP, kStartupInfoOff, 0x68);

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kCommandBufferOff);
    gen.xorRegReg(X64Reg::R8, X64Reg::R8);
    gen.xorRegReg(X64Reg::R9, X64Reg::R9);
    storeStackImm64(gen, 0x20, 0);
    storeStackImm64(gen, 0x28, kCreateNoWindow);
    storeStackImm64(gen, 0x30, 0);
    storeStackImm64(gen, 0x38, 0);
    storeStackPtrToLocal(gen, 0x40, kStartupInfoOff);
    storeStackPtrToLocal(gen, 0x48, kProcessInfoOff);
    gen.callIATSlot(kI_CreateProcessW);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(errorLabel);

    const auto lblProcessError = gen.newLabel();
    const auto lblProcessDone = gen.newLabel();

    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kProcessInfoOff);
    gen.movRegImm32(X64Reg::RDX, kWaitInfinite);
    gen.callIATSlot(kI_WaitForSingleObject);
    emitCmpRegU32(gen, X64Reg::RAX, kWaitInfinite);
    gen.jz(lblProcessError);

    gen.movRegMem(X64Reg::RCX, X64Reg::RBP, kProcessInfoOff);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kBytesReadOff);
    gen.callIATSlot(kI_GetExitCodeProcess);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblProcessError);
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kBytesReadOff);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblProcessError);

    emitCloseLocalHandleIfSet(gen, kProcessInfoOff + 8, closeSlot);
    emitCloseLocalHandleIfSet(gen, kProcessInfoOff, closeSlot);
    gen.jmp(lblProcessDone);

    gen.bindLabel(lblProcessError);
    emitCloseLocalHandleIfSet(gen, kProcessInfoOff + 8, closeSlot);
    emitCloseLocalHandleIfSet(gen, kProcessInfoOff, closeSlot);
    gen.jmp(errorLabel);

    gen.bindLabel(lblProcessDone);
    emitDeleteFile(
        gen,
        WindowsPackageFileEntry{
            WindowsInstallRoot::InstallDir, layout.compressedPayloadRelativePath, 0, 0, 0},
        slashOff,
        copySlot,
        catSlot,
        strlenSlot,
        deleteSlot,
        getLastErrorSlot,
        errorLabel);
    emitDeleteFile(
        gen,
        WindowsPackageFileEntry{
            WindowsInstallRoot::InstallDir, layout.compressedPayloadManifestRelativePath, 0, 0, 0},
        slashOff,
        copySlot,
        catSlot,
        strlenSlot,
        deleteSlot,
        getLastErrorSlot,
        errorLabel);
}

constexpr uint32_t kArm64FrameSize = 0x33000;
constexpr uint32_t kArm64SelfPathOff = 0x1000;
constexpr uint32_t kArm64PowerShellPathOff = 0x11000;
constexpr uint32_t kArm64CommandLineOff = 0x12000;
constexpr uint32_t kArm64CommandLineBytes = 0x20000;
constexpr uint32_t kArm64StartupInfoOff = 0x32000;
constexpr uint32_t kArm64ProcessInfoOff = 0x32100;
constexpr uint32_t kArm64ExitCodeOff = 0x32120;
constexpr uint32_t kArm64StartupInfoBytes = 0x80;
constexpr uint32_t kArm64ProcessInfoBytes = 0x20;
constexpr uint32_t kStartupInfoWSize = 104;

void emitA64LeaFrame(InstallerStubGenA64 &gen, A64Reg dst, uint32_t off) {
    const uint32_t page = off & ~0xFFFu;
    const uint32_t rem = off & 0xFFFu;
    if (page != 0) {
        gen.addRegImm(dst, A64Reg::X19, page);
        if (rem != 0)
            gen.addRegImm(dst, dst, rem);
    } else {
        gen.addRegImm(dst, A64Reg::X19, rem);
    }
}

void emitA64ZeroFrameRange(InstallerStubGenA64 &gen, uint32_t off, uint32_t bytes) {
    emitA64LeaFrame(gen, A64Reg::X17, off);
    for (uint32_t i = 0; i < bytes; i += 8)
        gen.storeMem64(A64Reg::X17, i, A64Reg::SP);
}

void emitA64StoreFrameImm32(InstallerStubGenA64 &gen, uint32_t off, uint32_t value) {
    emitA64LeaFrame(gen, A64Reg::X17, off);
    gen.storeMemImm32(A64Reg::X17, 0, value);
}

void emitA64BuildPowerShellCommand(InstallerStubGenA64 &gen,
                                   uint32_t quoteOff,
                                   uint32_t psSuffixOff,
                                   uint32_t afterExeOff,
                                   uint32_t afterSelfOff) {
    gen.movRegImm32(A64Reg::X0, 0);
    emitA64LeaFrame(gen, A64Reg::X1, kArm64SelfPathOff);
    gen.movRegImm32(A64Reg::X2, kMaxPathChars);
    gen.callIATSlot(kI_GetModuleFileNameW);

    emitA64LeaFrame(gen, A64Reg::X0, kArm64PowerShellPathOff);
    gen.movRegImm32(A64Reg::X1, 2048);
    gen.callIATSlot(kI_GetSystemDirectoryW);

    emitA64LeaFrame(gen, A64Reg::X0, kArm64PowerShellPathOff);
    gen.leaData(A64Reg::X1, psSuffixOff);
    gen.callIATSlot(kI_lstrcatW);

    emitA64LeaFrame(gen, A64Reg::X0, kArm64CommandLineOff);
    gen.leaData(A64Reg::X1, quoteOff);
    gen.callIATSlot(kI_lstrcpyW);

    emitA64LeaFrame(gen, A64Reg::X0, kArm64CommandLineOff);
    emitA64LeaFrame(gen, A64Reg::X1, kArm64PowerShellPathOff);
    gen.callIATSlot(kI_lstrcatW);

    emitA64LeaFrame(gen, A64Reg::X0, kArm64CommandLineOff);
    gen.leaData(A64Reg::X1, afterExeOff);
    gen.callIATSlot(kI_lstrcatW);

    emitA64LeaFrame(gen, A64Reg::X0, kArm64CommandLineOff);
    emitA64LeaFrame(gen, A64Reg::X1, kArm64SelfPathOff);
    gen.callIATSlot(kI_lstrcatW);

    emitA64LeaFrame(gen, A64Reg::X0, kArm64CommandLineOff);
    gen.leaData(A64Reg::X1, afterSelfOff);
    gen.callIATSlot(kI_lstrcatW);
}

} // namespace

StubResult buildArm64InstallerStub(const WindowsPackageLayout &layout, bool uninstallDialog) {
    StubResult result;
    result.imports = installerImports();
    result.peArch = "arm64";

    InstallerStubGenA64 gen;
    const std::string encodedCommand = encodedArm64PowerShellCommand(layout, uninstallDialog);
    const size_t minCommandChars = encodedCommand.size() + kMaxPathChars + 256u;
    if (minCommandChars * 2u > kArm64CommandLineBytes)
        throw std::runtime_error("ARM64 Windows installer command line is too large");

    const uint32_t quoteOff = gen.embedStringW("\"");
    const uint32_t psSuffixOff = gen.embedStringW("\\WindowsPowerShell\\v1.0\\powershell.exe");
    const uint32_t afterExeOff = gen.embedStringW(
        "\" -NoProfile -ExecutionPolicy Bypass -EncodedCommand " + encodedCommand + " -- \"");
    const uint32_t afterSelfOff = gen.embedStringW(uninstallDialog ? "\" uninstall" : "\" install");
    gen.embedStringW(layout.displayName + (uninstallDialog ? " Uninstall" : " Setup"));
    const auto wizardTemplate = buildWizardDialogTemplate(layout, uninstallDialog);
    gen.embedBytes(wizardTemplate.data(), wizardTemplate.size());

    const auto lblFail = gen.newLabel();

    gen.subRegImm(A64Reg::SP, A64Reg::SP, kArm64FrameSize);
    gen.movRegReg(A64Reg::X19, A64Reg::SP);
    emitA64ZeroFrameRange(gen, kArm64StartupInfoOff, kArm64StartupInfoBytes);
    emitA64ZeroFrameRange(gen, kArm64ProcessInfoOff, kArm64ProcessInfoBytes);
    emitA64ZeroFrameRange(gen, kArm64ExitCodeOff, 8);
    emitA64StoreFrameImm32(gen, kArm64StartupInfoOff, kStartupInfoWSize);

    emitA64BuildPowerShellCommand(gen, quoteOff, psSuffixOff, afterExeOff, afterSelfOff);

    emitA64LeaFrame(gen, A64Reg::X17, kArm64StartupInfoOff);
    gen.storeMem64(A64Reg::X19, 0, A64Reg::X17);
    emitA64LeaFrame(gen, A64Reg::X17, kArm64ProcessInfoOff);
    gen.storeMem64(A64Reg::X19, 8, A64Reg::X17);

    emitA64LeaFrame(gen, A64Reg::X0, kArm64PowerShellPathOff);
    emitA64LeaFrame(gen, A64Reg::X1, kArm64CommandLineOff);
    gen.movRegImm32(A64Reg::X2, 0);
    gen.movRegImm32(A64Reg::X3, 0);
    gen.movRegImm32(A64Reg::X4, 0);
    gen.movRegImm32(A64Reg::X5, kCreateNoWindow);
    gen.movRegImm32(A64Reg::X6, 0);
    gen.movRegImm32(A64Reg::X7, 0);
    gen.callIATSlot(kI_CreateProcessW);
    gen.cbz(A64Reg::X0, lblFail);

    emitA64LeaFrame(gen, A64Reg::X20, kArm64ProcessInfoOff);
    gen.loadMem64(A64Reg::X0, A64Reg::X20, 0);
    gen.movRegImm32(A64Reg::X1, kWaitInfinite);
    gen.callIATSlot(kI_WaitForSingleObject);

    gen.loadMem64(A64Reg::X0, A64Reg::X20, 0);
    emitA64LeaFrame(gen, A64Reg::X1, kArm64ExitCodeOff);
    gen.callIATSlot(kI_GetExitCodeProcess);
    gen.cbz(A64Reg::X0, lblFail);

    gen.loadMem64(A64Reg::X0, A64Reg::X20, 8);
    gen.callIATSlot(kI_CloseHandle);
    gen.loadMem64(A64Reg::X0, A64Reg::X20, 0);
    gen.callIATSlot(kI_CloseHandle);

    emitA64LeaFrame(gen, A64Reg::X17, kArm64ExitCodeOff);
    gen.loadMem64(A64Reg::X0, A64Reg::X17, 0);
    gen.callIATSlot(kI_ExitProcess);

    gen.bindLabel(lblFail);
    gen.movRegImm32(A64Reg::X0, 1);
    gen.callIATSlot(kI_ExitProcess);

    finalizeStubRVAs(result, gen);
    result.stubData = gen.dataSection();
    return result;
}

/// @brief Build the complete x86-64 installer stub for the given package layout.
/// Assembles all install steps: root path resolution, optional clean of existing install,
/// directory creation, file extraction with CRC verification, registry entries (uninstall key,
/// file associations, shortcuts), and PATH update. Jumps to an error handler on any failure
/// that displays a MessageBox and exits with code 1.
StubResult buildInstallerStub(const WindowsPackageLayout &layout, const std::string &arch) {
    if (resolveBootstrapArch(arch) == "arm64")
        return buildArm64InstallerStub(layout, false);

    StubResult result;
    result.imports = installerImports();
    result.peArch = resolveBootstrapArch(arch);

    InstallerStubGen gen;

    const std::string installDir = installDirNameFor(layout);
    const std::string version = layout.version.empty() ? "0.0.0" : layout.version;
    const std::string publisher = layout.publisher.empty() ? "Viper" : layout.publisher;
    const std::string uninstallKey = uninstallKeyPathFor(layout);
    const uint64_t registryRoot = registryRootFor(layout);

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
    const uint32_t environmentKeyOff = gen.embedStringW(environmentKeyPathFor(layout));
    const uint32_t environmentOff = gen.embedStringW("Environment");
    const uint32_t quietSlashOff = gen.embedStringW("/quiet");
    const uint32_t silentSlashOff = gen.embedStringW("/silent");
    const uint32_t quietDashOff = gen.embedStringW("-quiet");
    const uint32_t silentDashOff = gen.embedStringW("-silent");
    const uint32_t noRestartSlashOff = gen.embedStringW("/norestart");
    const uint32_t noRestartDashOff = gen.embedStringW("-norestart");
    const uint32_t quietUninstallArgsOff = gen.embedStringW(" /quiet");

    const uint32_t regDisplayNameOff = gen.embedStringW("DisplayName");
    const uint32_t regDisplayVersionOff = gen.embedStringW("DisplayVersion");
    const uint32_t regPublisherOff = gen.embedStringW("Publisher");
    const uint32_t regInstallLocationOff = gen.embedStringW("InstallLocation");
    const uint32_t regUninstallStringOff = gen.embedStringW("UninstallString");
    const uint32_t regQuietUninstallStringOff = gen.embedStringW("QuietUninstallString");
    const uint32_t regDisplayIconOff = gen.embedStringW("DisplayIcon");
    const uint32_t regNoModifyOff = gen.embedStringW("NoModify");
    const uint32_t regNoRepairOff = gen.embedStringW("NoRepair");
    const uint32_t regEstimatedSizeOff = gen.embedStringW("EstimatedSize");
    const uint32_t regInstallDateOff = gen.embedStringW("InstallDate");
    const uint32_t regUrlInfoAboutOff = gen.embedStringW("URLInfoAbout");
    const uint32_t regUrlUpdateInfoOff = gen.embedStringW("URLUpdateInfo");
    const uint32_t regHelpLinkOff = gen.embedStringW("HelpLink");
    const uint32_t regCommentsOff = gen.embedStringW("Comments");
    const uint32_t regContactOff = gen.embedStringW("Contact");
    const uint32_t regPathValueOff = gen.embedStringW("Path");
    const uint32_t regOriginalPathOff = gen.embedStringW("VAPSOriginalPath");
    const uint32_t regPathEntryOff = gen.embedStringW("VAPSPathEntry");
    const uint32_t installDateFormatOff = gen.embedStringW("yyyyMMdd");

    const uint32_t successTitleOff = gen.embedStringW(layout.displayName + " Setup");
    const uint32_t successMsgOff = gen.embedStringW(
        "Installation complete. " + layout.displayName +
        " has been installed. Open a new terminal to use PATH updates, or launch ViperIDE from "
        "the Start Menu if shortcuts were enabled.");
    const uint32_t errorTitleOff = gen.embedStringW("Setup Error");
    const uint32_t errorMsgOff =
        gen.embedStringW("Installation failed. The package could not be extracted.");
    const auto wizardTemplate = buildWizardDialogTemplate(layout, false);
    const uint32_t wizardTemplateOff = gen.embedBytes(wizardTemplate.data(), wizardTemplate.size());

    const auto lblError = gen.newLabel();
    const auto lblRollbackError = gen.newLabel();
    const auto lblCleanupSuccess = gen.newLabel();
    const auto lblCleanupError = gen.newLabel();
    const auto lblCleanupRollback = gen.newLabel();
    const auto lblExitSuccess = gen.newLabel();
    const auto lblExitError = gen.newLabel();
    const auto lblWizardDialogProc = gen.newLabel();

    gen.push(X64Reg::RBP);
    gen.movRegReg(X64Reg::RBP, X64Reg::RSP);
    gen.subRegImm32(X64Reg::RSP, kFrameSize);

    zeroLocalQword(gen, kHFileOff);
    zeroLocalQword(gen, kHOutOff);
    zeroLocalQword(gen, kPFileBufOff);
    zeroLocalQword(gen, kRegKeyOff);
    zeroLocalQword(gen, kPathUpdatedOff);
    zeroLocalQword(gen, kQuietModeOff);
    zeroLocalQword(gen, kNoRestartModeOff);
    zeroLocalQword(gen, kKnownFolderPtrOff);

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kSelfPathOff);
    gen.movRegImm32(X64Reg::R8, kMaxPathChars);
    gen.callIATSlot(kI_GetModuleFileNameW);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblError);
    gen.cmpRegImm32(X64Reg::RAX, kMaxPathChars - 2);
    gen.ja(lblError);

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

    emitDetectQuietMode(
        gen,
        kI_GetCommandLineW,
        kI_StrStrIW,
        {{quietSlashOff, 12}, {silentSlashOff, 14}, {quietDashOff, 12}, {silentDashOff, 14}});
    emitDetectFlagMode(gen,
                       kI_GetCommandLineW,
                       kI_StrStrIW,
                       {{noRestartSlashOff, 20}, {noRestartDashOff, 20}},
                       kNoRestartModeOff);

    emitWizardDialog(gen,
                     wizardTemplateOff,
                     lblWizardDialogProc,
                     kI_InitCommonControlsEx,
                     kI_DialogBoxIndirectParamW,
                     lblCleanupSuccess);

    emitBuildRootPaths(gen,
                       layout,
                       slashOff,
                       installDirOff,
                       kI_lstrcpyW,
                       kI_lstrcatW,
                       kI_lstrlenW,
                       kI_CreateDirectoryW,
                       kI_GetLastError,
                       kI_SHGetKnownFolderPath,
                       kI_CoTaskMemFree,
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
                        kI_SetFilePointerEx,
                        kI_RtlComputeCrc32,
                        lblRollbackError);
    }

    emitExpandCompressedPayload(gen,
                                layout,
                                slashOff,
                                kI_lstrcpyW,
                                kI_lstrcatW,
                                kI_lstrlenW,
                                kI_DeleteFileW,
                                kI_GetLastError,
                                kI_CloseHandle,
                                lblRollbackError);

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

    gen.movRegImm64(X64Reg::RCX, registryRoot);
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
    emitRegSetConstString(gen,
                          kI_RegSetValueExW,
                          regDisplayVersionOff,
                          versionOff,
                          wideBytesFor(version),
                          lblRollbackError);
    emitRegSetConstString(gen,
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
    emitCheckedCatEmbedded(gen, kTempPathOff, quoteOff, kI_lstrcatW, kI_lstrlenW, lblRollbackError);
    emitRegSetStackString(
        gen, kI_RegSetValueExW, kI_lstrlenW, regUninstallStringOff, kTempPathOff, lblRollbackError);
    emitCheckedCatEmbedded(
        gen, kTempPathOff, quietUninstallArgsOff, kI_lstrcatW, kI_lstrlenW, lblRollbackError);
    emitRegSetStackString(gen,
                          kI_RegSetValueExW,
                          kI_lstrlenW,
                          regQuietUninstallStringOff,
                          kTempPathOff,
                          lblRollbackError);

    if (!layout.displayIconRelativePath.empty()) {
        gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kTempPathOff);
        gen.leaRipData(X64Reg::RDX, quoteOff);
        gen.callIATSlot(kI_lstrcpyW);
        emitCheckedCatStack(
            gen, kTempPathOff, kInstallPathOff, kI_lstrcatW, kI_lstrlenW, lblRollbackError);
        emitCheckedCatEmbedded(
            gen, kTempPathOff, slashOff, kI_lstrcatW, kI_lstrlenW, lblRollbackError);
        emitCheckedCatEmbedded(gen,
                               kTempPathOff,
                               gen.embedStringW(layout.displayIconRelativePath),
                               kI_lstrcatW,
                               kI_lstrlenW,
                               lblRollbackError);
        emitCheckedCatEmbedded(
            gen, kTempPathOff, quoteOff, kI_lstrcatW, kI_lstrlenW, lblRollbackError);
        emitRegSetStackString(
            gen, kI_RegSetValueExW, kI_lstrlenW, regDisplayIconOff, kTempPathOff, lblRollbackError);
    }
    emitRegSetConstDword(gen, kI_RegSetValueExW, regNoModifyOff, 1, lblRollbackError);
    emitRegSetConstDword(gen, kI_RegSetValueExW, regNoRepairOff, 1, lblRollbackError);
    if (layout.estimatedSizeKb != 0)
        emitRegSetConstDword(
            gen, kI_RegSetValueExW, regEstimatedSizeOff, layout.estimatedSizeKb, lblRollbackError);
    if (!layout.installDate.empty()) {
        const uint32_t installDateOff = gen.embedStringW(layout.installDate);
        emitRegSetCurrentInstallDate(gen,
                                     kI_GetDateFormatW,
                                     kI_lstrcpyW,
                                     kI_RegSetValueExW,
                                     kI_lstrlenW,
                                     regInstallDateOff,
                                     installDateFormatOff,
                                     installDateOff,
                                     lblRollbackError);
    }
    if (!layout.homepage.empty()) {
        const uint32_t homepageOff = gen.embedStringW(layout.homepage);
        emitRegSetConstString(gen,
                              kI_RegSetValueExW,
                              regUrlInfoAboutOff,
                              homepageOff,
                              wideBytesFor(layout.homepage),
                              lblRollbackError);
        emitRegSetConstString(gen,
                              kI_RegSetValueExW,
                              regUrlUpdateInfoOff,
                              homepageOff,
                              wideBytesFor(layout.homepage),
                              lblRollbackError);
        emitRegSetConstString(gen,
                              kI_RegSetValueExW,
                              regHelpLinkOff,
                              homepageOff,
                              wideBytesFor(layout.homepage),
                              lblRollbackError);
    }
    if (!layout.description.empty()) {
        const uint32_t commentsOff = gen.embedStringW(layout.description);
        emitRegSetConstString(gen,
                              kI_RegSetValueExW,
                              regCommentsOff,
                              commentsOff,
                              wideBytesFor(layout.description),
                              lblRollbackError);
    }
    if (!layout.contact.empty()) {
        const uint32_t contactOff = gen.embedStringW(layout.contact);
        emitRegSetConstString(gen,
                              kI_RegSetValueExW,
                              regContactOff,
                              contactOff,
                              wideBytesFor(layout.contact),
                              lblRollbackError);
    }
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

    emitMessageBoxUnlessQuiet(gen, kI_MessageBoxW, successTitleOff, successMsgOff, 0x40);
    gen.jmp(lblCleanupSuccess);

    gen.bindLabel(lblError);
    emitMessageBoxUnlessQuiet(gen, kI_MessageBoxW, errorTitleOff, errorMsgOff, 0x10);
    gen.jmp(lblCleanupError);

    gen.bindLabel(lblRollbackError);
    emitMessageBoxUnlessQuiet(gen, kI_MessageBoxW, errorTitleOff, errorMsgOff, 0x10);
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
    for (const auto &file : layout.uninstallFiles) {
        const auto lblNextRollbackDelete = gen.newLabel();
        emitDeleteFile(gen,
                       file,
                       slashOff,
                       kI_lstrcpyW,
                       kI_lstrcatW,
                       kI_lstrlenW,
                       kI_DeleteFileW,
                       kI_GetLastError,
                       lblNextRollbackDelete);
        gen.bindLabel(lblNextRollbackDelete);
    }
    {
        const auto lblNextRollbackDelete = gen.newLabel();
        emitDeleteFile(
            gen,
            WindowsPackageFileEntry{WindowsInstallRoot::InstallDir, "uninstall.exe", 0, 0},
            slashOff,
            kI_lstrcpyW,
            kI_lstrcatW,
            kI_lstrlenW,
            kI_DeleteFileW,
            kI_GetLastError,
            lblNextRollbackDelete);
        gen.bindLabel(lblNextRollbackDelete);
    }
    for (const auto &dir : layout.uninstallDirectories) {
        const auto lblNextRollbackRemoveDir = gen.newLabel();
        emitRemoveDirectory(gen,
                            dir,
                            slashOff,
                            kI_lstrcpyW,
                            kI_lstrcatW,
                            kI_lstrlenW,
                            kI_RemoveDirectoryW,
                            kI_GetLastError,
                            lblNextRollbackRemoveDir);
        gen.bindLabel(lblNextRollbackRemoveDir);
    }
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
                                   kI_RegDeleteTreeW);
    emitRegDeleteConstKey(gen, kI_RegDeleteTreeW, uninstallKeyOff, registryRoot);
    gen.jmp(lblExitError);

    gen.bindLabel(lblExitSuccess);
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.callIATSlot(kI_ExitProcess);

    gen.bindLabel(lblExitError);
    gen.movRegImm32(X64Reg::RCX, 1);
    gen.callIATSlot(kI_ExitProcess);

    emitWizardDialogProc(gen,
                         lblWizardDialogProc,
                         kI_EndDialog,
                         kI_GetDlgItem,
                         kI_SendMessageW,
                         kI_EnableWindow,
                         layout.perUserInstall);

    finalizeStubRVAs(result, gen);
    result.stubData = gen.dataSection();
    return result;
}

/// @brief Build the complete x86-64 uninstaller stub for the given package layout.
/// Assembles all uninstall steps: root path resolution, file deletion, directory removal,
/// registry cleanup (uninstall key, file associations, shortcuts), and PATH restoration.
/// On failure shows a MessageBox and exits with code 1.
StubResult buildUninstallerStub(const WindowsPackageLayout &layout, const std::string &arch) {
    if (resolveBootstrapArch(arch) == "arm64")
        return buildArm64InstallerStub(layout, true);

    StubResult result;
    result.imports = uninstallerImports();
    result.peArch = resolveBootstrapArch(arch);

    InstallerStubGen gen;

    const std::string installDir = installDirNameFor(layout);
    const std::string uninstallKey = uninstallKeyPathFor(layout);
    const uint64_t registryRoot = registryRootFor(layout);

    const uint32_t slashOff = gen.embedStringW("\\");
    const uint32_t semicolonOff = gen.embedStringW(";");
    const uint32_t installDirOff = gen.embedStringW(installDir);
    const uint32_t uninstallKeyOff = gen.embedStringW(uninstallKey);
    const uint32_t environmentKeyOff = gen.embedStringW(environmentKeyPathFor(layout));
    const uint32_t environmentOff = gen.embedStringW("Environment");
    const uint32_t regPathValueOff = gen.embedStringW("Path");
    const uint32_t regPathEntryOff = gen.embedStringW("VAPSPathEntry");
    const uint32_t quietSlashOff = gen.embedStringW("/quiet");
    const uint32_t silentSlashOff = gen.embedStringW("/silent");
    const uint32_t quietDashOff = gen.embedStringW("-quiet");
    const uint32_t silentDashOff = gen.embedStringW("-silent");
    const uint32_t noRestartSlashOff = gen.embedStringW("/norestart");
    const uint32_t noRestartDashOff = gen.embedStringW("-norestart");
    const uint32_t successTitleOff = gen.embedStringW(layout.displayName + " Uninstall");
    const uint32_t successMsgOff = gen.embedStringW(layout.displayName + " has been uninstalled.");
    const uint32_t errorTitleOff = gen.embedStringW("Uninstall Error");
    const uint32_t errorMsgOff =
        gen.embedStringW("Uninstall failed. Required installation paths could not be resolved.");
    const auto wizardTemplate = buildWizardDialogTemplate(layout, true);
    const uint32_t wizardTemplateOff = gen.embedBytes(wizardTemplate.data(), wizardTemplate.size());
    const auto lblError = gen.newLabel();
    const auto lblExitSuccess = gen.newLabel();
    const auto lblExitError = gen.newLabel();
    const auto lblWizardDialogProc = gen.newLabel();

    gen.push(X64Reg::RBP);
    gen.movRegReg(X64Reg::RBP, X64Reg::RSP);
    gen.subRegImm32(X64Reg::RSP, kFrameSize);
    zeroLocalQword(gen, kRegKeyOff);
    zeroLocalQword(gen, kQuietModeOff);
    zeroLocalQword(gen, kNoRestartModeOff);
    zeroLocalQword(gen, kKnownFolderPtrOff);

    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.leaRegMem(X64Reg::RDX, X64Reg::RBP, kSelfPathOff);
    gen.movRegImm32(X64Reg::R8, kMaxPathChars);
    gen.callIATSlot(kU_GetModuleFileNameW);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jz(lblError);
    gen.cmpRegImm32(X64Reg::RAX, kMaxPathChars - 2);
    gen.ja(lblError);

    emitDetectQuietMode(
        gen,
        kU_GetCommandLineW,
        kU_StrStrIW,
        {{quietSlashOff, 12}, {silentSlashOff, 14}, {quietDashOff, 12}, {silentDashOff, 14}});
    emitDetectFlagMode(gen,
                       kU_GetCommandLineW,
                       kU_StrStrIW,
                       {{noRestartSlashOff, 20}, {noRestartDashOff, 20}},
                       kNoRestartModeOff);

    emitWizardDialog(gen,
                     wizardTemplateOff,
                     lblWizardDialogProc,
                     kU_InitCommonControlsEx,
                     kU_DialogBoxIndirectParamW,
                     lblExitSuccess);

    emitBuildRootPaths(gen,
                       layout,
                       slashOff,
                       installDirOff,
                       kU_lstrcpyW,
                       kU_lstrcatW,
                       kU_lstrlenW,
                       kU_RemoveDirectoryW,
                       0,
                       kU_SHGetKnownFolderPath,
                       kU_CoTaskMemFree,
                       false,
                       lblError);

    for (const auto &file : layout.uninstallFiles) {
        emitDeleteFile(gen,
                       file,
                       slashOff,
                       kU_lstrcpyW,
                       kU_lstrcatW,
                       kU_lstrlenW,
                       kU_DeleteFileW,
                       kU_GetLastError,
                       lblError);
    }

    const auto lblSkipSelfDelete = gen.newLabel();
    gen.movRegMem(X64Reg::RAX, X64Reg::RBP, kNoRestartModeOff);
    gen.testRegReg(X64Reg::RAX, X64Reg::RAX);
    gen.jnz(lblSkipSelfDelete);
    gen.leaRegMem(X64Reg::RCX, X64Reg::RBP, kSelfPathOff);
    gen.xorRegReg(X64Reg::RDX, X64Reg::RDX);
    gen.movRegImm32(X64Reg::R8, kMoveFileDelayUntilReboot);
    gen.callIATSlot(kU_MoveFileExW);
    gen.bindLabel(lblSkipSelfDelete);

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
    gen.jz(lblInstallPathDone);
    gen.jmp(lblError);
    gen.bindLabel(lblInstallPathDone);

    emitUnregisterFileAssociations(gen,
                                   layout,
                                   kU_RegOpenKeyW,
                                   kU_RegCloseKey,
                                   kU_RegQueryValueExW,
                                   kU_lstrcmpW,
                                   kU_RegDeleteValueW,
                                   kU_RegDeleteTreeW);

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

    gen.movRegImm64(X64Reg::RCX, registryRoot);
    gen.leaRipData(X64Reg::RDX, uninstallKeyOff);
    gen.callIATSlot(kU_RegDeleteTreeW);

    emitMessageBoxUnlessQuiet(gen, kU_MessageBoxW, successTitleOff, successMsgOff, 0x40);
    gen.jmp(lblExitSuccess);

    gen.bindLabel(lblError);
    emitMessageBoxUnlessQuiet(gen, kU_MessageBoxW, errorTitleOff, errorMsgOff, 0x10);
    gen.jmp(lblExitError);

    gen.bindLabel(lblExitSuccess);
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX);
    gen.callIATSlot(kU_ExitProcess);

    gen.bindLabel(lblExitError);
    gen.movRegImm32(X64Reg::RCX, 1);
    gen.callIATSlot(kU_ExitProcess);

    emitWizardDialogProc(gen,
                         lblWizardDialogProc,
                         kU_EndDialog,
                         kU_GetDlgItem,
                         kU_SendMessageW,
                         kU_EnableWindow,
                         layout.perUserInstall);

    finalizeStubRVAs(result, gen);
    result.stubData = gen.dataSection();
    return result;
}

} // namespace viper::pkg
