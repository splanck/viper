//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/windows_installer/WindowsInstallerCleanup.cpp
// Purpose: Delete the native maintenance cache after its owning process exits.
//
// Key invariants:
//   - The helper receives only explicit absolute files/directories from the host.
//   - It waits for the parent process before attempting any deletion.
//   - The helper renames its mapped primary NTFS stream and deletes the base
//     file before waiting, so no second process or reboot residue is required.
//   - Directories are removed non-recursively and only when already empty.
//   - The launch self-test exits before the helper marks or deletes anything.
//
// Ownership/Lifetime:
//   - CommandLineToArgvW memory and the optional parent handle are always closed.
//
// Links: WindowsInstallerLifecycle.cpp, WindowsInstallerCleanup.rc.in
//
//===----------------------------------------------------------------------===//

#include <windows.h>

#include <shellapi.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool isAbsoluteSafePath(const std::wstring &path) {
    if (path.size() < 3 || path.size() >= 32760)
        return false;
    if (path.rfind(L"\\\\.\\", 0) == 0 || path.rfind(L"\\\\?\\GLOBALROOT", 0) == 0)
        return false;
    return (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
           path.rfind(L"\\\\?\\", 0) == 0;
}

bool waitForParent(DWORD processId) {
    if (processId == 0)
        return false;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (!process)
        return GetLastError() == ERROR_INVALID_PARAMETER;
    const DWORD result = WaitForSingleObject(process, 5U * 60U * 1000U);
    CloseHandle(process);
    return result == WAIT_OBJECT_0;
}

DWORD markSelfForDeletion() {
    std::vector<wchar_t> path(512);
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
            return GetLastError();
        if (length < path.size() - 1U) {
            path.resize(length + 1U);
            break;
        }
        path.resize(path.size() * 2U);
    }

    HANDLE file = CreateFileW(path.data(),
                              DELETE | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 1000U + GetLastError();
    constexpr wchar_t kDeletedStream[] = L":zanna-cleanup-deleted";
    constexpr DWORD kStreamBytes = sizeof(kDeletedStream) - sizeof(wchar_t);
    std::vector<unsigned char> renameBytes(offsetof(FILE_RENAME_INFO, FileName) +
                                           sizeof(kDeletedStream));
    auto *rename = reinterpret_cast<FILE_RENAME_INFO *>(renameBytes.data());
    rename->ReplaceIfExists = TRUE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = kStreamBytes;
    std::memcpy(rename->FileName, kDeletedStream, sizeof(kDeletedStream));
    const bool renamed =
        SetFileInformationByHandle(
            file, FileRenameInfo, rename, static_cast<DWORD>(renameBytes.size())) != FALSE;
    const DWORD renameError = renamed ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!renamed)
        return 2000U + renameError;

    file = CreateFileW(path.data(),
                       DELETE | SYNCHRONIZE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 3000U + GetLastError();
    FILE_DISPOSITION_INFO_EX disposition{FILE_DISPOSITION_FLAG_DELETE |
                                         FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                                         FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE};
    const bool deleted =
        SetFileInformationByHandle(
            file, FileDispositionInfoEx, &disposition, sizeof(disposition)) != FALSE;
    const DWORD deleteError = deleted ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    return deleted ? ERROR_SUCCESS : 4000U + deleteError;
}

bool deleteFileWithRetry(const std::wstring &path) {
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return false;
        if ((attributes & FILE_ATTRIBUTE_READONLY) != 0)
            SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
        if (DeleteFileW(path.c_str()))
            return true;
        const DWORD error = GetLastError();
        if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION &&
            error != ERROR_LOCK_VIOLATION) {
            return false;
        }
        Sleep(50);
    }
    return false;
}

bool removeEmptyDirectoryWithRetry(const std::wstring &path, bool allowNonEmpty) {
    DWORD lastError = ERROR_SUCCESS;
    for (unsigned attempt = 0; attempt < 600; ++attempt) {
        if (RemoveDirectoryW(path.c_str()))
            return true;
        const DWORD error = GetLastError();
        lastError = error;
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return true;
        // A successfully unlinked executable can remain visible briefly while
        // the image section and directory enumeration state drain.  Treating
        // ERROR_DIR_NOT_EMPTY as immediate success left an empty per-package
        // cache directory behind after otherwise successful uninstalls.
        if (error == ERROR_DIR_NOT_EMPTY) {
            if (allowNonEmpty)
                return true;
            Sleep(50);
            continue;
        }
        if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)
            return false;
        Sleep(50);
    }
    return allowNonEmpty && lastError == ERROR_DIR_NOT_EMPTY;
}

struct DirectoryRequest {
    std::wstring path;
    bool allowNonEmpty{false};
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return ERROR_INVALID_PARAMETER;
    if (argc == 2 &&
        (lstrcmpiW(argv[1], L"/selftest") == 0 || lstrcmpiW(argv[1], L"/self-test") == 0)) {
        LocalFree(argv);
        return ERROR_SUCCESS;
    }

    DWORD parentId = 0;
    std::vector<std::wstring> files;
    std::vector<DirectoryRequest> directories;
    bool valid = true;
    for (int i = 1; i < argc; ++i) {
        const std::wstring option(argv[i]);
        if ((option == L"/parent" || option == L"/delete" || option == L"/rmdir" ||
             option == L"/rmdir-if-empty") &&
            i + 1 < argc) {
            const std::wstring value(argv[++i]);
            if (option == L"/parent") {
                wchar_t *end = nullptr;
                errno = 0;
                const unsigned long parsed = std::wcstoul(value.c_str(), &end, 10);
                valid = valid && errno == 0 && end && *end == L'\0' && parsed != 0 &&
                        parsed <= MAXDWORD;
                parentId = static_cast<DWORD>(parsed);
            } else if (!isAbsoluteSafePath(value)) {
                valid = false;
            } else if (option == L"/delete") {
                files.push_back(value);
            } else {
                directories.push_back({value, option == L"/rmdir-if-empty"});
            }
        } else {
            valid = false;
        }
    }
    LocalFree(argv);
    if (!valid || parentId == 0 || files.empty())
        return ERROR_INVALID_PARAMETER;
    if (const DWORD selfDeleteError = markSelfForDeletion(); selfDeleteError != ERROR_SUCCESS)
        return static_cast<int>(selfDeleteError);
    if (!waitForParent(parentId))
        return WAIT_TIMEOUT;

    bool success = true;
    for (const std::wstring &file : files)
        success = deleteFileWithRetry(file) && success;
    for (const DirectoryRequest &directory : directories)
        success = removeEmptyDirectoryWithRetry(directory.path, directory.allowNonEmpty) && success;
    return success ? ERROR_SUCCESS : ERROR_CANNOT_MAKE;
}
