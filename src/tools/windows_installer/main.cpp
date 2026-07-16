//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

class ComApartment {
  public:
    ComApartment() {
        const HRESULT result =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        initialized_ = SUCCEEDED(result);
    }

    ~ComApartment() {
        if (initialized_)
            CoUninitialize();
    }

  private:
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
        if (written == 0) {
            SetLastError(ERROR_WRITE_FAULT);
            return false;
        }
        offset += written;
    }
    return true;
}

bool writeInherited(HANDLE handle, std::wstring_view text) {
    return writeUtf8(handle, viper::installer::wideToUtf8(text));
}

[[noreturn]] void throwOutputError(std::wstring_view action, DWORD error) {
    throw std::runtime_error(viper::installer::wideToUtf8(
        std::wstring(action) + L": " + viper::installer::formatWindowsError(error)));
}

void writeFileAtomically(const fs::path &destination, std::wstring_view text) {
    const std::string utf8 = viper::installer::wideToUtf8(text);
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

void writeAutomationOutput(const viper::installer::HostOptions &options, std::wstring_view text) {
    if (!options.outputPath.empty()) {
        writeFileAtomically(options.outputPath, text);
        return;
    }
    if (!writeInherited(GetStdHandle(STD_OUTPUT_HANDLE), text)) {
        throw std::runtime_error(
            "standard output is unavailable; pass /output <path> for automation JSON");
    }
}

void showFatal(const viper::installer::HostOptions *options,
               std::wstring_view title,
               std::wstring_view message) {
    writeInherited(GetStdHandle(STD_ERROR_HANDLE), std::wstring(message) + L"\r\n");
    if (!options || options->uiLevel != viper::installer::UiLevel::Quiet) {
        MessageBoxW(nullptr,
                    std::wstring(message).c_str(),
                    std::wstring(title).c_str(),
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const ComApartment comApartment;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);

    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return viper::installer::kExitInvalidCommandLine;
    viper::installer::HostOptions options;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"/quiet") == 0 || _wcsicmp(argv[i], L"/silent") == 0) {
            options.uiLevel = viper::installer::UiLevel::Quiet;
            break;
        }
    }
    try {
        options = viper::installer::parseCommandLine(argc, argv);
    } catch (const std::exception &ex) {
        std::wstring message;
        try {
            message = viper::installer::utf8ToWide(ex.what());
        } catch (...) {
            message = L"The installer encountered an invalid diagnostic message.";
        }
        showFatal(&options, L"Viper Tools Installer", message);
        LocalFree(argv);
        return viper::installer::kExitInvalidCommandLine;
    }
    LocalFree(argv);
    if (options.operation == viper::installer::Operation::Help) {
        const std::wstring help = viper::installer::commandLineHelp();
        if (!writeInherited(GetStdHandle(STD_OUTPUT_HANDLE), help))
            MessageBoxW(nullptr, help.c_str(), L"Viper Tools Installer Help", MB_OK);
        return viper::installer::kExitSuccess;
    }
    if (options.operation == viper::installer::Operation::SelfTest)
        return viper::installer::kExitSuccess;

    try {
        const auto path = viper::installer::currentExecutablePath();
        const auto package = viper::installer::loadHostPackage(path);
        if (options.operation == viper::installer::Operation::Inspect) {
            writeAutomationOutput(options, viper::installer::inspectPackageJson(package));
            return viper::installer::kExitSuccess;
        }
        viper::installer::Logger logger;
        logger.open(options.logPath.empty()
                        ? viper::installer::defaultLogPath(package.metadata.identifier)
                        : options.logPath);
        logger.info(L"Viper native installer session started");
        logger.info(L"Package: " + viper::installer::utf8ToWide(package.metadata.identifier) +
                    L" " + viper::installer::utf8ToWide(package.metadata.version) + L" " +
                    viper::installer::utf8ToWide(package.metadata.architecture) + L" " +
                    viper::installer::utf8ToWide(package.metadata.channel));
        logger.info(L"Package SHA-256: " + viper::installer::utf8ToWide(package.executableSha256));
        logger.info(L"Session log: " + logger.path().wstring());
        try {
            if (options.operation == viper::installer::Operation::CheckUpdates) {
                logger.info(L"Checking the pinned signed update manifest");
                const auto update = viper::installer::checkForUpdates(package);
                writeAutomationOutput(options, viper::installer::updateResultJson(update));
                if (options.uiLevel == viper::installer::UiLevel::Full)
                    viper::installer::showUpdateResult(instance, package, update);
                logger.info(L"Update discovery completed successfully");
                return viper::installer::kExitSuccess;
            }
            const int result = viper::installer::runLifecycle(instance, package, options, logger);
            logger.info(L"Viper native installer session completed with exit code " +
                        std::to_wstring(result));
            return result;
        } catch (const viper::installer::InstallerError &error) {
            logger.error(viper::installer::utf8ToWide(error.what()));
            const std::wstring diagnostic = viper::installer::utf8ToWide(error.what()) +
                                            L"\r\n\r\nDiagnostic log:\r\n" +
                                            logger.path().wstring();
            showFatal(&options, L"Viper Tools Installer", diagnostic);
            return error.exitCode();
        } catch (const std::exception &error) {
            logger.error(viper::installer::utf8ToWide(error.what()));
            const std::wstring diagnostic = viper::installer::utf8ToWide(error.what()) +
                                            L"\r\n\r\nDiagnostic log:\r\n" +
                                            logger.path().wstring();
            showFatal(&options, L"Viper Tools Installer", diagnostic);
            return viper::installer::kExitFatalError;
        }
    } catch (const std::exception &ex) {
        std::wstring message;
        try {
            message = viper::installer::utf8ToWide(ex.what());
        } catch (...) {
            message = L"The installer encountered an invalid diagnostic message.";
        }
        showFatal(&options, L"Viper Tools Installer", message);
        return viper::installer::kExitFatalError;
    }
}
