//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/windows_installer/main.cpp
// Purpose: Provide the Unicode Win32 entry point for setup, maintenance,
//          repair, and uninstall operations.
//
// Key invariants:
//   - Help and the launch self-test are available without a package overlay.
//   - Package verification and log initialization precede lifecycle mutation.
//   - Automation output is complete or reported as an error; an explicit output
//     path is replaced atomically and never exposes a partial JSON document.
//   - Fatal diagnostics are visible interactively and written to stderr when
//     inherited by automation.
//
// Ownership/Lifetime:
//   - CommandLineToArgvW memory is released before process exit.
//
// Links: WindowsInstallerHost.hpp, WindowsInstallerLifecycle.cpp
//
//===----------------------------------------------------------------------===//

#include "WindowsInstallerHost.hpp"
#include "WindowsInstallerUpdate.hpp"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <climits>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

class ComApartment {
  public:
    ComApartment() {
        result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        initialized_ = SUCCEEDED(result_);
    }

    ~ComApartment() {
        if (initialized_)
            CoUninitialize();
    }

    bool available() const {
        return SUCCEEDED(result_);
    }

  private:
    HRESULT result_{E_FAIL};
    bool initialized_{false};
};

namespace fs = std::filesystem;

bool writeUtf8(HANDLE handle, const std::string &utf8) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        return false;
    size_t offset = 0;
    while (offset < utf8.size()) {
        const DWORD requested = static_cast<DWORD>(
            std::min<size_t>(utf8.size() - offset, static_cast<size_t>(MAXDWORD)));
        DWORD written = 0;
        if (!WriteFile(handle, utf8.data() + offset, requested, &written, nullptr))
            return false;
        if (written == 0 || written > requested) {
            SetLastError(ERROR_WRITE_FAULT);
            return false;
        }
        offset += written;
    }
    return true;
}

std::wstring safeWideDiagnostic(const char *message) noexcept {
    try {
        return message ? zanna::installer::utf8ToWide(message)
                       : L"The installer encountered an empty diagnostic message.";
    } catch (...) {
        return L"The installer encountered an invalid diagnostic message.";
    }
}

bool writeInherited(HANDLE handle, std::wstring_view text) {
    return writeUtf8(handle, zanna::installer::wideToUtf8(text));
}

[[noreturn]] void throwOutputError(std::wstring_view action, DWORD error) {
    throw std::runtime_error(zanna::installer::wideToUtf8(
        std::wstring(action) + L": " + zanna::installer::formatWindowsError(error)));
}

void writeFileAtomically(const fs::path &destination, std::wstring_view text) {
    const std::string utf8 = zanna::installer::wideToUtf8(text);
    fs::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD createError = ERROR_FILE_EXISTS;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        temporary = destination;
        temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
        file = CreateFileW(temporary.c_str(),
                           GENERIC_WRITE,
                           0,
                           nullptr,
                           CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                           nullptr);
        if (file != INVALID_HANDLE_VALUE)
            break;
        createError = GetLastError();
        if (createError != ERROR_FILE_EXISTS && createError != ERROR_ALREADY_EXISTS)
            throwOutputError(L"Cannot create the temporary automation output file", createError);
    }
    if (file == INVALID_HANDLE_VALUE)
        throwOutputError(L"Cannot allocate a unique temporary automation output file", createError);

    DWORD failure = ERROR_SUCCESS;
    if (!writeUtf8(file, utf8))
        failure = GetLastError();
    if (failure == ERROR_SUCCESS && !FlushFileBuffers(file))
        failure = GetLastError();
    if (!CloseHandle(file) && failure == ERROR_SUCCESS)
        failure = GetLastError();
    file = INVALID_HANDLE_VALUE;
    if (failure == ERROR_SUCCESS &&
        !MoveFileExW(temporary.c_str(),
                     destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        failure = GetLastError();
    }
    if (failure != ERROR_SUCCESS) {
        DeleteFileW(temporary.c_str());
        throwOutputError(L"Cannot publish the automation output file", failure);
    }
}

void writeAutomationOutput(const zanna::installer::HostOptions &options, std::wstring_view text) {
    if (!options.outputPath.empty()) {
        writeFileAtomically(options.outputPath, text);
        return;
    }
    if (!writeInherited(GetStdHandle(STD_OUTPUT_HANDLE), text)) {
        throw std::runtime_error(
            "standard output is unavailable; pass /output <path> for automation JSON");
    }
}

bool samePath(const fs::path &left, const fs::path &right) {
    const std::wstring leftText = fs::absolute(left).lexically_normal().wstring();
    const std::wstring rightText = fs::absolute(right).lexically_normal().wstring();
    if (leftText.size() <= static_cast<size_t>(INT_MAX) &&
        rightText.size() <= static_cast<size_t>(INT_MAX) &&
        CompareStringOrdinal(leftText.data(),
                             static_cast<int>(leftText.size()),
                             rightText.data(),
                             static_cast<int>(rightText.size()),
                             TRUE) == CSTR_EQUAL) {
        return true;
    }

    HANDLE leftHandle = CreateFileW(left.c_str(),
                                    FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS,
                                    nullptr);
    HANDLE rightHandle = CreateFileW(right.c_str(),
                                     FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr,
                                     OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS,
                                     nullptr);
    BY_HANDLE_FILE_INFORMATION leftInfo{};
    BY_HANDLE_FILE_INFORMATION rightInfo{};
    const bool equal = leftHandle != INVALID_HANDLE_VALUE && rightHandle != INVALID_HANDLE_VALUE &&
                       GetFileInformationByHandle(leftHandle, &leftInfo) &&
                       GetFileInformationByHandle(rightHandle, &rightInfo) &&
                       leftInfo.dwVolumeSerialNumber == rightInfo.dwVolumeSerialNumber &&
                       leftInfo.nFileIndexHigh == rightInfo.nFileIndexHigh &&
                       leftInfo.nFileIndexLow == rightInfo.nFileIndexLow;
    if (leftHandle != INVALID_HANDLE_VALUE)
        CloseHandle(leftHandle);
    if (rightHandle != INVALID_HANDLE_VALUE)
        CloseHandle(rightHandle);
    return equal;
}

void validateSessionPaths(const zanna::installer::HostOptions &options,
                          const fs::path &executable) {
    if (!options.logPath.empty() && samePath(options.logPath, executable))
        throw std::runtime_error("the installer log path cannot name the running installer");
    if (!options.outputPath.empty() && samePath(options.outputPath, executable))
        throw std::runtime_error("the automation output path cannot name the running installer");
    if (!options.logPath.empty() && !options.outputPath.empty() &&
        samePath(options.logPath, options.outputPath)) {
        throw std::runtime_error("the installer log and automation output paths must differ");
    }
}

void showFatal(const zanna::installer::HostOptions *options,
               std::wstring_view title,
               std::wstring_view message) noexcept {
    try {
        (void)writeInherited(GetStdHandle(STD_ERROR_HANDLE), std::wstring(message) + L"\r\n");
        if (!options || options->uiLevel != zanna::installer::UiLevel::Quiet) {
            MessageBoxW(nullptr,
                        std::wstring(message).c_str(),
                        std::wstring(title).c_str(),
                        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        }
    } catch (...) {
        static constexpr wchar_t kFallback[] = L"Zanna setup encountered a fatal error.";
        if (!options || options->uiLevel != zanna::installer::UiLevel::Quiet)
            MessageBoxW(nullptr, kFallback, L"Zanna Tools Installer", MB_OK | MB_ICONERROR);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const ComApartment comApartment;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    const bool commonControlsAvailable = InitCommonControlsEx(&controls) != FALSE;

    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return zanna::installer::kExitInvalidCommandLine;
    zanna::installer::HostOptions options;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"/quiet") == 0 || _wcsicmp(argv[i], L"/silent") == 0) {
            options.uiLevel = zanna::installer::UiLevel::Quiet;
            break;
        }
    }
    try {
        options = zanna::installer::parseCommandLine(argc, argv);
    } catch (const std::exception &ex) {
        const std::wstring message = safeWideDiagnostic(ex.what());
        showFatal(&options, L"Zanna Tools Installer", message);
        LocalFree(argv);
        return zanna::installer::kExitInvalidCommandLine;
    }
    LocalFree(argv);
    if (options.operation == zanna::installer::Operation::Help) {
        const std::wstring help = zanna::installer::commandLineHelp();
        if (!writeInherited(GetStdHandle(STD_OUTPUT_HANDLE), help))
            MessageBoxW(nullptr, help.c_str(), L"Zanna Tools Installer Help", MB_OK);
        return zanna::installer::kExitSuccess;
    }
    if (options.operation == zanna::installer::Operation::SelfTest)
        return zanna::installer::kExitSuccess;
    if (!comApartment.available()) {
        showFatal(&options,
                  L"Zanna Tools Installer",
                  L"Windows COM initialization failed; setup cannot safely use Shell services.");
        return zanna::installer::kExitFatalError;
    }
    if (!commonControlsAvailable) {
        showFatal(&options,
                  L"Zanna Tools Installer",
                  L"Windows common controls could not be initialized.");
        return zanna::installer::kExitFatalError;
    }

    try {
        const auto path = zanna::installer::currentExecutablePath();
        validateSessionPaths(options, path);
        const auto package = zanna::installer::loadHostPackage(path);
        if (options.operation == zanna::installer::Operation::Inspect) {
            writeAutomationOutput(options, zanna::installer::inspectPackageJson(package));
            return zanna::installer::kExitSuccess;
        }
        zanna::installer::Logger logger;
        logger.open(options.logPath.empty()
                        ? zanna::installer::defaultLogPath(package.metadata.identifier)
                        : options.logPath);
        logger.info(L"Zanna native installer session started");
        logger.info(L"Package: " + zanna::installer::utf8ToWide(package.metadata.identifier) +
                    L" " + zanna::installer::utf8ToWide(package.metadata.version) + L" " +
                    zanna::installer::utf8ToWide(package.metadata.architecture) + L" " +
                    zanna::installer::utf8ToWide(package.metadata.channel));
        logger.info(L"Package SHA-256: " + zanna::installer::utf8ToWide(package.executableSha256));
        logger.info(L"Session log: " + logger.path().wstring());
        try {
            if (options.operation == zanna::installer::Operation::CheckUpdates) {
                logger.info(L"Checking the pinned signed update manifest");
                const auto update = zanna::installer::checkForUpdates(package);
                writeAutomationOutput(options, zanna::installer::updateResultJson(update));
                if (options.uiLevel == zanna::installer::UiLevel::Full)
                    zanna::installer::showUpdateResult(instance, package, update);
                logger.info(L"Update discovery completed successfully");
                return zanna::installer::kExitSuccess;
            }
            const int result = zanna::installer::runLifecycle(instance, package, options, logger);
            logger.info(L"Zanna native installer session completed with exit code " +
                        std::to_wstring(result));
            return result;
        } catch (const zanna::installer::InstallerError &error) {
            const std::wstring message = safeWideDiagnostic(error.what());
            logger.error(message);
            const std::wstring diagnostic =
                message + L"\r\n\r\nDiagnostic log:\r\n" + logger.path().wstring();
            showFatal(&options, L"Zanna Tools Installer", diagnostic);
            return error.exitCode();
        } catch (const std::exception &error) {
            const std::wstring message = safeWideDiagnostic(error.what());
            logger.error(message);
            const std::wstring diagnostic =
                message + L"\r\n\r\nDiagnostic log:\r\n" + logger.path().wstring();
            showFatal(&options, L"Zanna Tools Installer", diagnostic);
            return zanna::installer::kExitFatalError;
        }
    } catch (const std::exception &ex) {
        const std::wstring message = safeWideDiagnostic(ex.what());
        showFatal(&options, L"Zanna Tools Installer", message);
        return zanna::installer::kExitFatalError;
    }
}
