//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/windows_installer/WindowsInstallerLifecycle.cpp
// Purpose: Implement native Windows install, upgrade, modify, repair, and
//          removal with preflight checks and recoverable directory swaps.
//
// Key invariants:
//   - A package/scope/destination mutex serializes every lifecycle operation.
//   - The complete selected payload is verified in a same-volume staging tree
//     before the existing installation is renamed.
//   - Upgrades preserve files not listed in Viper's ownership manifest.
//   - A journal makes every pre-commit directory state recoverable.
//   - PATH, file associations, shortcuts, cache, and ARP values are changed only
//     after the new tree is active and are rolled back on synchronous failure.
//   - Uninstall retains the ownership manifest until the removal swap commits.
//
// Ownership/Lifetime:
//   - RAII wrappers close handles, registry keys, Restart Manager sessions, and
//     mutexes on all exits. Transaction directories are owned by one operation.
//
// Links: WindowsInstallerHost.hpp, WindowsInstallerMetadata.hpp, ZipReader.hpp
//
//===----------------------------------------------------------------------===//

#include "WindowsInstallerHost.hpp"
#include "WindowsInstallerWizard.hpp"

#include "PkgHash.hpp"
#include "ZipReader.hpp"

#include <restartmanager.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

namespace viper::installer {
namespace {

constexpr wchar_t kUninstallBase[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
constexpr wchar_t kUserEnvironment[] = L"Environment";
constexpr wchar_t kMachineEnvironment[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
constexpr wchar_t kClassesBase[] = L"Software\\Classes\\";
constexpr wchar_t kManifestHeader[] = L"VIPER-INSTALL-MANIFEST\t2";
constexpr wchar_t kStateHeader[] = L"VIPER-INSTALL-STATE\t2";

void cancellationPoint(Logger &logger) {
    if (logger.cancellationRequested())
        throw InstallerError(kExitUserCancelled, "installation was cancelled by the user");
}

class UniqueHandle {
  public:
    UniqueHandle() = default;

    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}

    ~UniqueHandle() {
        reset();
    }

    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;

    UniqueHandle(UniqueHandle &&other) noexcept : handle_(other.release()) {}

    UniqueHandle &operator=(UniqueHandle &&other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    HANDLE get() const {
        return handle_;
    }

    explicit operator bool() const {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE release() {
        const HANDLE result = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return result;
    }

    void reset(HANDLE replacement = INVALID_HANDLE_VALUE) {
        if (*this)
            CloseHandle(handle_);
        handle_ = replacement;
    }

  private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

template <typename T> class ComPtr {
  public:
    ComPtr() = default;

    ~ComPtr() {
        if (value_)
            value_->Release();
    }

    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    T **put() {
        if (value_) {
            value_->Release();
            value_ = nullptr;
        }
        return &value_;
    }

    T *operator->() const {
        return value_;
    }

    explicit operator bool() const {
        return value_ != nullptr;
    }

  private:
    T *value_{nullptr};
};

class RegKey {
  public:
    RegKey() = default;

    explicit RegKey(HKEY key) : key_(key) {}

    ~RegKey() {
        reset();
    }

    RegKey(const RegKey &) = delete;
    RegKey &operator=(const RegKey &) = delete;

    RegKey(RegKey &&other) noexcept : key_(other.release()) {}

    RegKey &operator=(RegKey &&other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    HKEY get() const {
        return key_;
    }

    explicit operator bool() const {
        return key_ != nullptr;
    }

    HKEY release() {
        const HKEY result = key_;
        key_ = nullptr;
        return result;
    }

    void reset(HKEY replacement = nullptr) {
        if (key_)
            RegCloseKey(key_);
        key_ = replacement;
    }

  private:
    HKEY key_{nullptr};
};

struct InstalledRecord {
    bool present{false};
    InstallScope scope{InstallScope::User};
    fs::path installRoot;
    fs::path cacheExecutable;
    std::string version;
    std::set<std::string> components;
    std::wstring pathEntry;
    std::vector<fs::path> shortcuts;
    bool settingsPresent{false};
    bool addToPath{false};
    bool registerAssociations{false};
    bool createShortcuts{false};
};

struct SelectedFile {
    std::string path;
    std::string sha256;
    uint64_t sizeBytes{0};
};

struct InstallationPlan {
    Operation operation{Operation::Install};
    InstallScope scope{InstallScope::User};
    fs::path installRoot;
    fs::path cacheExecutable;
    std::set<std::string> components;
    std::vector<SelectedFile> files;
    uint64_t selectedSizeBytes{0};
    bool addToPath{false};
    bool registerAssociations{false};
    bool createShortcuts{false};
    InstalledRecord existing;
};

std::wstring lowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string slashPath(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::wstring backslashPath(std::string_view value) {
    std::wstring result = utf8ToWide(value);
    std::replace(result.begin(), result.end(), L'/', L'\\');
    return result;
}

std::string normalizedPathKey(std::string value) {
    value = slashPath(std::move(value));
    return lowerAscii(std::move(value));
}

void validateRelativePath(std::string_view path) {
    if (path.empty() || path.size() >= 32760 || path.front() == '/' || path.front() == '\\' ||
        (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')) {
        throw std::runtime_error("unsafe install-relative path");
    }
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find_first_of("/\\", start);
        const std::string_view segment =
            path.substr(start, end == std::string_view::npos ? path.size() - start : end - start);
        if (segment.empty() || segment == "." || segment == "..")
            throw std::runtime_error("unsafe install-relative path");
        for (unsigned char ch : segment) {
            if (ch < 0x20U || ch == 0x7FU || ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
                ch == '<' || ch == '>' || ch == '|') {
                throw std::runtime_error("unsafe character in install-relative path");
            }
        }
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
}

fs::path safeJoin(const fs::path &root, std::string_view relative) {
    validateRelativePath(relative);
    fs::path result = root;
    size_t start = 0;
    while (start <= relative.size()) {
        const size_t end = relative.find_first_of("/\\", start);
        const std::string_view segment = relative.substr(
            start, end == std::string_view::npos ? relative.size() - start : end - start);
        result /= utf8ToWide(segment);
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return result;
}

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
    if (FAILED(result) || !raw)
        throw std::runtime_error("cannot resolve a required Windows known folder");
    std::wstring path(raw);
    CoTaskMemFree(raw);
    return path;
}

HKEY rootKey(InstallScope scope) {
    return scope == InstallScope::User ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

std::wstring uninstallSubkey(std::string_view identifier) {
    return std::wstring(kUninstallBase) + utf8ToWide(identifier);
}

RegKey openKey(HKEY root, std::wstring_view subkey, REGSAM access, bool create) {
    HKEY key = nullptr;
    LONG result = ERROR_SUCCESS;
    if (create) {
        result = RegCreateKeyExW(root,
                                 std::wstring(subkey).c_str(),
                                 0,
                                 nullptr,
                                 REG_OPTION_NON_VOLATILE,
                                 access,
                                 nullptr,
                                 &key,
                                 nullptr);
    } else {
        result = RegOpenKeyExW(root, std::wstring(subkey).c_str(), 0, access, &key);
    }
    if (result == ERROR_FILE_NOT_FOUND && !create)
        return {};
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("Windows registry operation failed: " +
                                 wideToUtf8(formatWindowsError(static_cast<DWORD>(result))));
    return RegKey(key);
}

std::optional<std::wstring> queryRegistryString(HKEY key, std::wstring_view name) {
    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegQueryValueExW(
        key, name.empty() ? nullptr : std::wstring(name).c_str(), nullptr, &type, nullptr, &size);
    if (result == ERROR_FILE_NOT_FOUND)
        return std::nullopt;
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return std::nullopt;
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1U, L'\0');
    result = RegQueryValueExW(key,
                              name.empty() ? nullptr : std::wstring(name).c_str(),
                              nullptr,
                              &type,
                              reinterpret_cast<BYTE *>(buffer.data()),
                              &size);
    if (result != ERROR_SUCCESS)
        return std::nullopt;
    return std::wstring(buffer.data());
}

void setRegistryString(HKEY key,
                       std::wstring_view name,
                       std::wstring_view value,
                       DWORD type = REG_SZ) {
    const LONG result = RegSetValueExW(key,
                                       name.empty() ? nullptr : std::wstring(name).c_str(),
                                       0,
                                       type,
                                       reinterpret_cast<const BYTE *>(value.data()),
                                       static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t)));
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("cannot write Windows registry string: " +
                                 wideToUtf8(formatWindowsError(static_cast<DWORD>(result))));
}

void setRegistryDword(HKEY key, std::wstring_view name, DWORD value) {
    const LONG result = RegSetValueExW(key,
                                       std::wstring(name).c_str(),
                                       0,
                                       REG_DWORD,
                                       reinterpret_cast<const BYTE *>(&value),
                                       sizeof(value));
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("cannot write Windows registry value: " +
                                 wideToUtf8(formatWindowsError(static_cast<DWORD>(result))));
}

std::optional<DWORD> queryRegistryDword(HKEY key, std::wstring_view name) {
    DWORD type = 0;
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LONG result = RegQueryValueExW(
        key, std::wstring(name).c_str(), nullptr, &type, reinterpret_cast<BYTE *>(&value), &size);
    if (result != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(value))
        return std::nullopt;
    return value;
}

std::vector<std::wstring> splitLines(std::wstring_view value) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t end = value.find(L'\n', start);
        std::wstring line(value.substr(
            start, end == std::wstring_view::npos ? value.size() - start : end - start));
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        if (!line.empty())
            lines.push_back(std::move(line));
        if (end == std::wstring_view::npos)
            break;
        start = end + 1;
    }
    return lines;
}

std::set<std::string> parseComponentList(std::wstring_view value) {
    std::set<std::string> result;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t comma = value.find(L',', start);
        std::wstring item(value.substr(
            start, comma == std::wstring_view::npos ? value.size() - start : comma - start));
        if (!item.empty())
            result.insert(lowerAscii(wideToUtf8(item)));
        if (comma == std::wstring_view::npos)
            break;
        start = comma + 1;
    }
    return result;
}

std::wstring joinComponents(const std::set<std::string> &components) {
    std::wstring result;
    for (const std::string &component : components) {
        if (!result.empty())
            result.push_back(L',');
        result += utf8ToWide(component);
    }
    return result;
}

InstalledRecord readInstalledRecord(std::string_view identifier, InstallScope scope) {
    InstalledRecord record;
    record.scope = scope;
    RegKey key = openKey(rootKey(scope), uninstallSubkey(identifier), KEY_READ, false);
    if (!key)
        return record;
    const auto marker = queryRegistryString(key.get(), L"ViperPackageIdentifier");
    if (!marker || wideToUtf8(*marker) != identifier)
        return record;
    const auto location = queryRegistryString(key.get(), L"InstallLocation");
    const auto version = queryRegistryString(key.get(), L"DisplayVersion");
    if (!location || location->empty() || !version)
        return record;
    record.present = true;
    record.installRoot = *location;
    record.version = wideToUtf8(*version);
    if (const auto cache = queryRegistryString(key.get(), L"ViperMaintenanceCache"))
        record.cacheExecutable = *cache;
    if (const auto components = queryRegistryString(key.get(), L"ViperComponents"))
        record.components = parseComponentList(*components);
    if (const auto path = queryRegistryString(key.get(), L"ViperPathEntry"))
        record.pathEntry = *path;
    if (const auto shortcuts = queryRegistryString(key.get(), L"ViperShortcutPaths")) {
        for (const std::wstring &line : splitLines(*shortcuts))
            record.shortcuts.emplace_back(line);
    }
    if (queryRegistryDword(key.get(), L"ViperSettingsVersion").value_or(0) == 1U) {
        record.settingsPresent = true;
        record.addToPath = queryRegistryDword(key.get(), L"ViperAddToPath").value_or(0) != 0;
        record.registerAssociations =
            queryRegistryDword(key.get(), L"ViperAssociations").value_or(0) != 0;
        record.createShortcuts =
            queryRegistryDword(key.get(), L"ViperCreateShortcuts").value_or(0) != 0;
    }
    return record;
}

uint64_t fnv1a64(std::wstring_view value) {
    uint64_t hash = 1469598103934665603ULL;
    for (wchar_t ch : value) {
        const wchar_t folded = static_cast<wchar_t>(std::towlower(ch));
        hash ^= static_cast<uint16_t>(folded);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::wstring hashHex(uint64_t value) {
    std::wostringstream out;
    out << std::hex << std::setw(16) << std::setfill(L'0') << value;
    return out.str();
}

fs::path cacheExecutablePath(InstallScope scope, std::string_view identifier) {
    const fs::path base = scope == InstallScope::User ? fs::path(knownFolder(FOLDERID_LocalAppData))
                                                      : fs::path(knownFolder(FOLDERID_ProgramData));
    return base / L"Viper" / L"InstallerCache" / hashHex(fnv1a64(utf8ToWide(identifier))) /
           L"maintenance.exe";
}

bool isProcessElevated() {
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
        return false;
    UniqueHandle token(rawToken);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size) &&
           elevation.TokenIsElevated != 0;
}

std::wstring operationSwitch(Operation operation) {
    switch (operation) {
        case Operation::Modify:
            return L"/modify";
        case Operation::Repair:
            return L"/repair";
        case Operation::Uninstall:
            return L"/uninstall";
        case Operation::Install:
        case Operation::Auto:
        default:
            return L"/install";
    }
}

int relaunchElevated(const HostPackage &package,
                     const HostOptions &options,
                     const InstallationPlan &plan,
                     Logger &logger) {
    std::vector<std::wstring> arguments = {operationSwitch(plan.operation),
                                           L"/scope",
                                           L"machine",
                                           L"/installDir",
                                           plan.installRoot.wstring(),
                                           L"/elevated-worker",
                                           L"/log",
                                           logger.path().wstring()};
    if (options.uiLevel == UiLevel::Quiet)
        arguments.push_back(L"/quiet");
    else if (options.uiLevel == UiLevel::Passive)
        arguments.push_back(L"/passive");
    if (options.allowDowngrade)
        arguments.push_back(L"/allowdowngrade");
    if (options.noRestart)
        arguments.push_back(L"/norestart");
    if (options.closeApplications)
        arguments.push_back(L"/closeapplications");
    if (options.addToPath)
        arguments.push_back(*options.addToPath ? L"/addToPath" : L"/noPath");
    if (options.registerAssociations) {
        arguments.push_back(*options.registerAssociations ? L"/associations" : L"/noAssociations");
    }
    if (options.createShortcuts)
        arguments.push_back(*options.createShortcuts ? L"/shortcuts" : L"/noShortcuts");
    if (!plan.components.empty()) {
        arguments.push_back(L"/components");
        arguments.push_back(joinComponents(plan.components));
    }
    std::wstring parameters;
    for (const std::wstring &argument : arguments) {
        if (!parameters.empty())
            parameters.push_back(L' ');
        parameters += quoteCommandLineArgument(argument);
    }
    SHELLEXECUTEINFOW execute{sizeof(execute)};
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    execute.lpVerb = L"runas";
    execute.lpFile = package.executablePath.c_str();
    execute.lpParameters = parameters.c_str();
    execute.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&execute)) {
        const DWORD error = GetLastError();
        if (error == ERROR_CANCELLED)
            return kExitUserCancelled;
        throw std::runtime_error("cannot start elevated installer: " +
                                 wideToUtf8(formatWindowsError(error)));
    }
    UniqueHandle process(execute.hProcess);
    WaitForSingleObject(process.get(), INFINITE);
    DWORD exitCode = kExitFatalError;
    if (!GetExitCodeProcess(process.get(), &exitCode))
        throw std::runtime_error("cannot read elevated installer exit code");
    return static_cast<int>(exitCode);
}

std::vector<int> parseVersion(std::string_view version, std::string &prerelease) {
    const size_t plus = version.find('+');
    if (plus != std::string_view::npos) {
        const std::string_view build = version.substr(plus + 1);
        if (build.empty() ||
            std::any_of(build.begin(),
                        build.end(),
                        [](char ch) {
                            return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' ||
                                     ch == '.');
                        }) ||
            build.front() == '.' || build.back() == '.' || build.find("..") != std::string::npos) {
            throw std::runtime_error("package version has invalid build metadata");
        }
    }
    version = version.substr(0, plus);
    const size_t dash = version.find('-');
    if (dash != std::string_view::npos) {
        prerelease = std::string(version.substr(dash + 1));
        version = version.substr(0, dash);
        if (prerelease.empty() || prerelease.front() == '.' || prerelease.back() == '.' ||
            prerelease.find("..") != std::string::npos ||
            std::any_of(prerelease.begin(), prerelease.end(), [](char ch) {
                return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '.');
            })) {
            throw std::runtime_error("package version has an invalid prerelease identifier");
        }
    }
    std::vector<int> parts;
    size_t start = 0;
    while (start <= version.size()) {
        const size_t dot = version.find('.', start);
        const std::string_view part = version.substr(
            start, dot == std::string_view::npos ? version.size() - start : dot - start);
        if (part.empty() || part.size() > 9)
            throw std::runtime_error("package version is not a supported semantic version");
        int value = 0;
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || value < 0)
            throw std::runtime_error("package version is not a supported semantic version");
        parts.push_back(value);
        if (dot == std::string_view::npos)
            break;
        start = dot + 1;
    }
    while (parts.size() < 3)
        parts.push_back(0);
    return parts;
}

void preflightWindowsVersion(const HostPackage &package, Logger &logger) {
    std::array<int, 3> installed{};
    bool testOverride = false;
#if defined(VIPER_INSTALLER_ENABLE_TEST_HOOKS)
    if (const wchar_t *value = _wgetenv(L"VIPER_INSTALLER_TEST_WINDOWS_VERSION")) {
        std::string testPrerelease;
        std::vector<int> testVersion = parseVersion(wideToUtf8(value), testPrerelease);
        if (!testPrerelease.empty() || testVersion.size() > installed.size())
            throw std::runtime_error("invalid Windows-version test override");
        testVersion.resize(installed.size());
        std::copy(testVersion.begin(), testVersion.end(), installed.begin());
        testOverride = true;
    }
#endif
    if (!testOverride) {
        using RtlGetVersionFunction = LONG(WINAPI *)(OSVERSIONINFOW *);
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto rtlGetVersion =
            ntdll ? reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"))
                  : nullptr;
        if (!rtlGetVersion)
            throw std::runtime_error("cannot determine the installed Windows version");
        OSVERSIONINFOW current{};
        current.dwOSVersionInfoSize = sizeof(current);
        if (rtlGetVersion(&current) != 0)
            throw std::runtime_error("cannot query the installed Windows version");
        installed = {static_cast<int>(current.dwMajorVersion),
                     static_cast<int>(current.dwMinorVersion),
                     static_cast<int>(current.dwBuildNumber)};
    }

    std::string prerelease;
    std::vector<int> minimum = parseVersion(package.metadata.minimumWindowsVersion, prerelease);
    if (!prerelease.empty() || minimum.size() > 3U)
        throw std::runtime_error("package contains an invalid minimum Windows version");
    minimum.resize(3U);
    logger.info(L"Windows version: " + std::to_wstring(installed[0]) + L"." +
                std::to_wstring(installed[1]) + L"." + std::to_wstring(installed[2]) +
                (testOverride ? L" (test override)" : L""));
    if (std::lexicographical_compare(
            installed.begin(), installed.end(), minimum.begin(), minimum.end())) {
        throw InstallerError(kExitFatalError,
                             "this package requires Windows " +
                                 package.metadata.minimumWindowsVersion + " or newer");
    }
}

fs::path canonicalDestination(const fs::path &requested) {
    if (requested.empty())
        throw std::runtime_error("installation destination is empty");
    std::error_code error;
    fs::path absolute = fs::absolute(requested, error).lexically_normal();
    if (error || !absolute.has_root_name() || absolute == absolute.root_path())
        throw std::runtime_error(
            "installation destination must be an absolute directory below a fixed volume");
    const std::wstring text = absolute.wstring();
    if (std::any_of(text.begin(), text.end(), [](wchar_t ch) { return ch < 0x20; }))
        throw std::runtime_error("installation destination contains a control character");
    if (text.size() >= 32760 || PathIsUNCW(text.c_str()) || text.rfind(L"\\\\.\\", 0) == 0 ||
        text.rfind(L"\\\\?\\GLOBALROOT", 0) == 0) {
        throw std::runtime_error("installation destination is not a supported fixed-volume path");
    }
    for (const fs::path &componentPath : absolute.relative_path()) {
        const std::wstring component = componentPath.wstring();
        if (component.empty() || component.back() == L'.' || component.back() == L' ' ||
            component.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos) {
            throw std::runtime_error("installation destination contains an invalid Windows name");
        }
        std::wstring base = component.substr(0, component.find(L'.'));
        base = lowerWide(std::move(base));
        const bool numberedDevice = base.size() == 4U &&
                                    (base.rfind(L"com", 0) == 0 || base.rfind(L"lpt", 0) == 0) &&
                                    base[3] >= L'1' && base[3] <= L'9';
        if (base == L"con" || base == L"prn" || base == L"aux" || base == L"nul" ||
            numberedDevice) {
            throw std::runtime_error("installation destination uses a reserved Windows name");
        }
    }
    wchar_t volumePath[32768]{};
    if (!GetVolumePathNameW(text.c_str(), volumePath, static_cast<DWORD>(std::size(volumePath))) ||
        GetDriveTypeW(volumePath) != DRIVE_FIXED) {
        throw std::runtime_error("installation destination must be on a fixed local volume");
    }
    const fs::path windowsDir = [] {
        std::wstring value(32768, L'\0');
        const UINT length = GetWindowsDirectoryW(value.data(), static_cast<UINT>(value.size()));
        if (length == 0 || length >= value.size())
            return fs::path{};
        value.resize(length);
        return fs::path(value).lexically_normal();
    }();
    auto pathBeginsWith = [](const fs::path &candidate, const fs::path &root) {
        std::wstring value = lowerWide(candidate.lexically_normal().wstring());
        std::wstring prefix = lowerWide(root.lexically_normal().wstring());
        while (prefix.size() > 3U && (prefix.back() == L'\\' || prefix.back() == L'/'))
            prefix.pop_back();
        return value == prefix ||
               (value.size() > prefix.size() && value.compare(0, prefix.size(), prefix) == 0 &&
                (value[prefix.size()] == L'\\' || value[prefix.size()] == L'/'));
    };
    if (!windowsDir.empty() &&
        (pathBeginsWith(absolute, windowsDir) || pathBeginsWith(windowsDir, absolute))) {
        throw std::runtime_error("installation in or above the Windows directory is prohibited");
    }
    const std::array<KNOWNFOLDERID, 9> protectedFolders = {FOLDERID_ProgramFiles,
                                                           FOLDERID_ProgramData,
                                                           FOLDERID_UserProfiles,
                                                           FOLDERID_Profile,
                                                           FOLDERID_LocalAppData,
                                                           FOLDERID_RoamingAppData,
                                                           FOLDERID_Desktop,
                                                           FOLDERID_Documents,
                                                           FOLDERID_Programs};
    for (REFKNOWNFOLDERID id : protectedFolders) {
        PWSTR raw = nullptr;
        if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) || !raw)
            continue;
        const fs::path protectedPath(raw);
        CoTaskMemFree(raw);
        if (pathBeginsWith(protectedPath, absolute)) {
            throw std::runtime_error(
                "installation destination is a protected Windows or user-profile folder");
        }
    }
    return absolute;
}

void rejectReparseAncestors(const fs::path &path) {
    fs::path current = path;
    while (!current.empty() && current != current.root_path()) {
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                throw std::runtime_error("installation path traverses a reparse point: " +
                                         wideToUtf8(current.wstring()));
        }
        current = current.parent_path();
    }
}

void ensureParentWritable(const fs::path &path) {
    fs::path existing = path.parent_path();
    while (!existing.empty() && !fs::exists(existing))
        existing = existing.parent_path();
    if (existing.empty())
        throw std::runtime_error("installation destination has no existing parent directory");
    const fs::path probe =
        existing / (L".viper-write-probe-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                    hashHex(GetTickCount64()));
    UniqueHandle file(CreateFileW(probe.c_str(),
                                  GENERIC_WRITE,
                                  0,
                                  nullptr,
                                  CREATE_NEW,
                                  FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                                  nullptr));
    if (!file)
        throw std::runtime_error("installation destination is not writable: " +
                                 wideToUtf8(formatWindowsError(GetLastError())));
}

std::set<std::string> selectComponents(const HostPackage &package,
                                       const HostOptions &options,
                                       const InstalledRecord &existing) {
    std::set<std::string> known;
    std::set<std::string> result;
    for (const auto &component : package.metadata.components) {
        known.insert(lowerAscii(component.id));
        if (component.required ||
            (options.componentPreset != ComponentPreset::Minimal &&
             options.componentPreset != ComponentPreset::SDK && component.defaultSelected) ||
            (options.componentPreset == ComponentPreset::SDK && lowerAscii(component.id) == "sdk"))
            result.insert(lowerAscii(component.id));
    }
    if (options.componentPreset == ComponentPreset::Complete) {
        result = known;
    } else if (existing.present && !existing.components.empty() &&
               options.componentPreset == ComponentPreset::Unspecified) {
        result = existing.components;
    }
    if (options.componentsSpecified)
        result = options.selectedComponents;
    for (const std::string &component : result) {
        if (known.find(component) == known.end())
            throw std::runtime_error("unknown installer component: " + component);
    }
    for (const auto &component : package.metadata.components) {
        if (component.required)
            result.insert(lowerAscii(component.id));
    }
    return result;
}

bool componentEnabled(std::string_view component, const std::set<std::string> &selected) {
    return component.empty() || selected.find(lowerAscii(std::string(component))) != selected.end();
}

InstallationPlan makePlan(const HostPackage &package,
                          const HostOptions &options,
                          const InstalledRecord *recoveryRecord = nullptr) {
    InstallationPlan plan;
    if (recoveryRecord) {
        plan.existing = *recoveryRecord;
        plan.scope = recoveryRecord->scope;
    } else {
        const InstalledRecord user =
            readInstalledRecord(package.metadata.identifier, InstallScope::User);
        const InstalledRecord machine =
            readInstalledRecord(package.metadata.identifier, InstallScope::Machine);
        if (options.scope) {
            plan.scope = *options.scope;
            plan.existing = plan.scope == InstallScope::User ? user : machine;
            const InstalledRecord &opposite = plan.scope == InstallScope::User ? machine : user;
            if (!plan.existing.present && opposite.present) {
                throw std::runtime_error(
                    "Viper is already installed in the other scope; use Modify to keep that "
                    "scope or uninstall it before changing scope");
            }
        } else if (user.present != machine.present) {
            plan.scope = user.present ? InstallScope::User : InstallScope::Machine;
            plan.existing = user.present ? user : machine;
        } else if (user.present && machine.present) {
            if (options.uiLevel != UiLevel::Full)
                throw std::runtime_error(
                    "Viper is registered for both scopes; specify /scope user or /scope machine");
            plan.scope = package.metadata.defaultScope == "machine" ? InstallScope::Machine
                                                                    : InstallScope::User;
            plan.existing = plan.scope == InstallScope::User ? user : machine;
        } else {
            plan.scope = package.metadata.defaultScope == "machine" ? InstallScope::Machine
                                                                    : InstallScope::User;
            plan.existing = {};
        }
    }
    plan.operation = options.operation;
    if (plan.operation == Operation::Auto) {
        if (package.metadata.packageMode == "maintenance") {
            const fs::path installedUninstaller =
                plan.existing.present
                    ? safeJoin(plan.existing.installRoot, package.metadata.uninstallerRelativePath)
                    : fs::path{};
            if (!installedUninstaller.empty() &&
                lowerWide(currentExecutablePath().lexically_normal().wstring()) ==
                    lowerWide(installedUninstaller.lexically_normal().wstring())) {
                plan.operation = Operation::Uninstall;
            } else if (options.uiLevel != UiLevel::Full) {
                throw std::runtime_error(
                    "maintenance mode requires /modify, /repair, or /uninstall");
            } else {
                plan.operation = Operation::Repair;
            }
        } else {
            plan.operation = Operation::Install;
        }
    }
    if ((plan.operation == Operation::Modify || plan.operation == Operation::Repair ||
         plan.operation == Operation::Uninstall) &&
        !plan.existing.present) {
        throw std::runtime_error("no matching Viper installation is registered for this scope");
    }
    if (!options.destination.empty())
        plan.installRoot = canonicalDestination(options.destination);
    else if (plan.existing.present)
        plan.installRoot = canonicalDestination(plan.existing.installRoot);
    else {
        const fs::path base = plan.scope == InstallScope::User
                                  ? fs::path(knownFolder(FOLDERID_LocalAppData)) / L"Programs"
                                  : fs::path(knownFolder(FOLDERID_ProgramFiles));
        plan.installRoot =
            canonicalDestination(base / utf8ToWide(package.metadata.defaultInstallDir));
    }
    if (plan.existing.present &&
        lowerWide(plan.installRoot.wstring()) != lowerWide(plan.existing.installRoot.wstring())) {
        throw std::runtime_error(
            "maintenance destination does not match the registered installation");
    }
    rejectReparseAncestors(plan.installRoot);
    plan.cacheExecutable = cacheExecutablePath(plan.scope, package.metadata.identifier);
    plan.components = selectComponents(package, options, plan.existing);
    const bool existingSettings = plan.existing.present && plan.existing.settingsPresent;
    plan.addToPath = options.addToPath.value_or(existingSettings ? plan.existing.addToPath
                                                                 : package.metadata.addToPath);
    plan.registerAssociations = options.registerAssociations.value_or(
        existingSettings ? plan.existing.registerAssociations
                         : package.metadata.registerFileAssociations);
    plan.createShortcuts = options.createShortcuts.value_or(
        existingSettings ? plan.existing.createShortcuts : package.metadata.createShortcuts);
    if (plan.registerAssociations && !package.metadata.associationExecutable.empty()) {
        const std::string associationPath =
            normalizedPathKey(package.metadata.associationExecutable);
        const auto associationPayload =
            std::find_if(package.metadata.payloadFiles.begin(),
                         package.metadata.payloadFiles.end(),
                         [&](const viper::pkg::WindowsInstallerPayloadMetadata &file) {
                             return normalizedPathKey(file.path) == associationPath;
                         });
        if (associationPayload != package.metadata.payloadFiles.end() &&
            !componentEnabled(associationPayload->componentId, plan.components)) {
            plan.registerAssociations = false;
        }
    }
    for (const auto &file : package.metadata.payloadFiles) {
        if (!componentEnabled(file.componentId, plan.components))
            continue;
        plan.files.push_back({file.path, file.sha256, file.sizeBytes});
        if (file.sizeBytes > std::numeric_limits<uint64_t>::max() - plan.selectedSizeBytes)
            throw std::runtime_error("selected installer payload size overflow");
        plan.selectedSizeBytes += file.sizeBytes;
    }
    for (const auto &file : package.metadata.outerFiles) {
        if (!componentEnabled(file.componentId, plan.components))
            continue;
        plan.files.push_back({file.path, file.sha256, file.sizeBytes});
        if (file.sizeBytes > std::numeric_limits<uint64_t>::max() - plan.selectedSizeBytes)
            throw std::runtime_error("selected installer payload size overflow");
        plan.selectedSizeBytes += file.sizeBytes;
    }
    if (package.metadata.packageMode == "maintenance" && plan.operation != Operation::Uninstall) {
        const std::string selfHash =
            viper::pkg::sha256Hex(package.executableBytes.data(), package.executableBytes.size());
        if (package.executableBytes.size() >
            std::numeric_limits<uint64_t>::max() - plan.selectedSizeBytes) {
            throw std::runtime_error("selected installer payload size overflow");
        }
        plan.files.push_back(
            {package.metadata.uninstallerRelativePath, selfHash, package.executableBytes.size()});
        plan.selectedSizeBytes += package.executableBytes.size();
    }
    return plan;
}

void preflightVersion(const HostPackage &package,
                      const HostOptions &options,
                      const InstallationPlan &plan) {
    if (!plan.existing.present || plan.operation == Operation::Uninstall)
        return;
    const int comparison =
        compareInstallerVersions(package.metadata.version, plan.existing.version);
    if (comparison < 0 && !options.allowDowngrade)
        throw InstallerError(
            kExitNewerVersionInstalled,
            "a newer Viper version is already installed; use /allowDowngrade to proceed");
}

std::set<std::string> loadOwnershipManifest(const fs::path &installRoot,
                                            std::string_view manifestRelative);

uint64_t preservedDirectoryBytes(const fs::path &root, const std::set<std::string> &ownedPaths) {
    if (!fs::exists(root))
        return 0;
    uint64_t total = 0;
    std::error_code error;
    for (fs::recursive_directory_iterator
             it(root, fs::directory_options::skip_permission_denied, error),
         end;
         it != end;
         it.increment(error)) {
        if (error)
            throw std::runtime_error("cannot enumerate existing installation for disk preflight");
        const DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            throw std::runtime_error("existing installation contains a reparse point");
        }
        if (it->is_regular_file(error)) {
            const fs::path relativePath = fs::relative(it->path(), root, error);
            if (error)
                throw std::runtime_error(
                    "cannot resolve an existing installation file for disk preflight");
            if (!ownedPaths.empty() && ownedPaths.find(normalizedPathKey(
                                           relativePath.generic_string())) != ownedPaths.end()) {
                continue;
            }
            const uint64_t size = it->file_size(error);
            if (error || size > std::numeric_limits<uint64_t>::max() - total)
                throw std::runtime_error("existing installation size overflow");
            total += size;
        }
    }
    return total;
}

uint64_t testLimitedFreeBytes(uint64_t available);

void preflightDisk(const HostPackage &package, const InstallationPlan &plan) {
    const std::set<std::string> owned =
        loadOwnershipManifest(plan.installRoot, package.metadata.installedManifestRelativePath);
    const uint64_t preserved = preservedDirectoryBytes(plan.installRoot, owned);
    const uint64_t selected = plan.operation == Operation::Uninstall ? 0 : plan.selectedSizeBytes;
    const uint64_t safety = plan.operation == Operation::Uninstall ? 16ULL * 1024ULL * 1024ULL
                                                                   : 64ULL * 1024ULL * 1024ULL;
    if (selected > std::numeric_limits<uint64_t>::max() - preserved)
        throw std::runtime_error("installer disk requirement overflow");
    const uint64_t transactionBytes = selected + preserved;
    const uint64_t transactionSafety = transactionBytes / 5U;
    if (transactionBytes > std::numeric_limits<uint64_t>::max() - safety ||
        transactionBytes + safety > std::numeric_limits<uint64_t>::max() - transactionSafety) {
        throw std::runtime_error("installer disk requirement overflow");
    }
    uint64_t required = transactionBytes + safety + transactionSafety;
    uint64_t cacheRequired = 0;
    if (plan.operation != Operation::Uninstall) {
        cacheRequired = package.metadata.outerFiles.empty()
                            ? static_cast<uint64_t>(package.executableBytes.size())
                            : package.metadata.outerFiles.front().sizeBytes;
        constexpr uint64_t kCacheSafety = 16ULL * 1024ULL * 1024ULL;
        if (cacheRequired > std::numeric_limits<uint64_t>::max() - kCacheSafety)
            throw std::runtime_error("installer cache disk requirement overflow");
        cacheRequired += kCacheSafety;
    }
    fs::path probe = plan.installRoot.parent_path();
    while (!probe.empty() && !fs::exists(probe))
        probe = probe.parent_path();
    wchar_t installVolume[32768]{};
    wchar_t cacheVolume[32768]{};
    if (probe.empty() || !GetVolumePathNameW(probe.c_str(),
                                             installVolume,
                                             static_cast<DWORD>(std::size(installVolume))))
        throw std::runtime_error("cannot determine the installation destination volume");
    fs::path cacheProbe = plan.cacheExecutable.parent_path();
    while (!cacheProbe.empty() && !fs::exists(cacheProbe))
        cacheProbe = cacheProbe.parent_path();
    if (cacheRequired != 0 &&
        (cacheProbe.empty() || !GetVolumePathNameW(cacheProbe.c_str(),
                                                   cacheVolume,
                                                   static_cast<DWORD>(std::size(cacheVolume))))) {
        throw std::runtime_error("cannot determine the maintenance cache volume");
    }
    if (cacheRequired != 0 && lowerWide(installVolume) == lowerWide(cacheVolume)) {
        if (cacheRequired > std::numeric_limits<uint64_t>::max() - required)
            throw std::runtime_error("installer disk requirement overflow");
        required += cacheRequired;
        cacheRequired = 0;
    }
    ULARGE_INTEGER freeBytes{};
    if (!GetDiskFreeSpaceExW(probe.c_str(), &freeBytes, nullptr, nullptr))
        throw std::runtime_error("cannot determine free space for the installation destination");
    const uint64_t availableInstallBytes = testLimitedFreeBytes(freeBytes.QuadPart);
    if (availableInstallBytes < required) {
        std::ostringstream message;
        message << "insufficient disk space: " << required << " bytes required, "
                << availableInstallBytes << " bytes available";
        throw std::runtime_error(message.str());
    }
    if (cacheRequired != 0) {
        ULARGE_INTEGER cacheFree{};
        if (!GetDiskFreeSpaceExW(cacheProbe.c_str(), &cacheFree, nullptr, nullptr))
            throw std::runtime_error("cannot determine free space for the maintenance cache");
        const uint64_t availableCacheBytes = testLimitedFreeBytes(cacheFree.QuadPart);
        if (availableCacheBytes < cacheRequired) {
            std::ostringstream message;
            message << "insufficient maintenance-cache disk space: " << cacheRequired
                    << " bytes required, " << availableCacheBytes << " bytes available";
            throw std::runtime_error(message.str());
        }
    }
}

class LifecycleMutex {
  public:
    LifecycleMutex(const InstallationPlan &plan, std::string_view identifier) {
        const std::wstring seed = utf8ToWide(identifier) + L"|" +
                                  (plan.scope == InstallScope::User ? L"user|" : L"machine|") +
                                  lowerWide(plan.installRoot.wstring());
        const std::wstring name = (plan.scope == InstallScope::Machine ? L"Global\\" : L"Local\\") +
                                  std::wstring(L"ViperInstaller-") + hashHex(fnv1a64(seed));
        handle_.reset(CreateMutexW(nullptr, FALSE, name.c_str()));
        if (!handle_)
            throw std::runtime_error("cannot create installer lifecycle mutex");
        const DWORD result = WaitForSingleObject(handle_.get(), 0);
        if (result == WAIT_TIMEOUT)
            active_ = false;
        else if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
            throw std::runtime_error("cannot acquire installer lifecycle mutex");
    }

    ~LifecycleMutex() {
        if (active_)
            ReleaseMutex(handle_.get());
    }

    bool acquired() const {
        return active_;
    }

  private:
    UniqueHandle handle_;
    bool active_{true};
};

class RestartManagerSession {
  public:
    ~RestartManagerSession() {
        if (started_)
            RmEndSession(session_);
    }

    std::vector<RM_PROCESS_INFO> inspect(const std::vector<fs::path> &paths) {
        if (paths.empty())
            return {};
        wchar_t key[CCH_RM_SESSION_KEY + 1]{};
        if (RmStartSession(&session_, 0, key) != ERROR_SUCCESS)
            throw std::runtime_error("Restart Manager could not start a session");
        started_ = true;
        std::vector<LPCWSTR> resources;
        resources.reserve(paths.size());
        for (const fs::path &path : paths)
            resources.push_back(path.c_str());
        const DWORD registerResult = RmRegisterResources(session_,
                                                         static_cast<UINT>(resources.size()),
                                                         resources.data(),
                                                         0,
                                                         nullptr,
                                                         0,
                                                         nullptr);
        if (registerResult != ERROR_SUCCESS)
            throw std::runtime_error("Restart Manager could not register installation files");
        UINT needed = 0;
        UINT count = 0;
        DWORD reasons = 0;
        DWORD result = RmGetList(session_, &needed, &count, nullptr, &reasons);
        if (result == ERROR_SUCCESS)
            return {};
        if (result != ERROR_MORE_DATA)
            throw std::runtime_error("Restart Manager could not inspect files in use");
        for (unsigned attempt = 0; attempt < 4U; ++attempt) {
            std::vector<RM_PROCESS_INFO> processes(needed);
            count = needed;
            result = RmGetList(session_, &needed, &count, processes.data(), &reasons);
            if (result == ERROR_SUCCESS) {
                processes.resize(count);
                return processes;
            }
            if (result != ERROR_MORE_DATA)
                break;
        }
        throw std::runtime_error("Restart Manager could not enumerate files in use");
    }

    void closeApplications() {
        const DWORD result = RmShutdown(session_, 0, nullptr);
        if (result != ERROR_SUCCESS)
            throw std::runtime_error("Restart Manager could not close all applications safely");
        applicationsClosed_ = true;
    }

    void restartApplications(bool enabled) {
        if (started_ && applicationsClosed_ && enabled)
            RmRestart(session_, 0, nullptr);
    }

  private:
    DWORD session_{0};
    bool started_{false};
    bool applicationsClosed_{false};
};

std::vector<fs::path> ownedExistingPaths(const HostPackage &package, const InstallationPlan &plan) {
    std::vector<fs::path> paths;
    const std::set<std::string> owned =
        loadOwnershipManifest(plan.installRoot, package.metadata.installedManifestRelativePath);
    for (const std::string &relative : owned) {
        const fs::path candidate = safeJoin(plan.installRoot, relative);
        if (fs::is_regular_file(candidate))
            paths.push_back(candidate);
    }
    return paths;
}

void handleFilesInUse(RestartManagerSession &restart,
                      const HostPackage &package,
                      const InstallationPlan &plan,
                      const HostOptions &options,
                      Logger &logger) {
    const auto processes = restart.inspect(ownedExistingPaths(package, plan));
    if (processes.empty())
        return;
    std::wstring names;
    for (const auto &process : processes) {
        if (!names.empty())
            names += L", ";
        names += process.strAppName[0] ? process.strAppName : L"Unknown application";
    }
    logger.warning(L"Files are in use by: " + names);
    bool close = options.closeApplications;
    if (!close && options.uiLevel == UiLevel::Full) {
        const std::wstring message = L"Viper files are in use by:\r\n\r\n" + names +
                                     L"\r\n\r\nClose these applications and continue?";
        close = MessageBoxW(nullptr,
                            message.c_str(),
                            L"Viper Tools Installer - Files in Use",
                            MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND) == IDYES;
    }
    if (!close)
        throw std::runtime_error("Viper files are in use; close applications and retry");
    restart.closeApplications();
}

std::wstring readTextFileWide(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::ostringstream bytes;
    bytes << input.rdbuf();
    return utf8ToWide(bytes.str());
}

void writeBytesAtomic(const fs::path &path, const std::vector<uint8_t> &bytes) {
    fs::create_directories(path.parent_path());
    const fs::path temporary = path.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) +
                               L"-" + hashHex(GetTickCount64());
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output ||
            (!bytes.empty() && !output.write(reinterpret_cast<const char *>(bytes.data()),
                                             static_cast<std::streamsize>(bytes.size())))) {
            throw std::runtime_error("cannot write staged installer file");
        }
        output.flush();
        if (!output)
            throw std::runtime_error("cannot flush staged installer file");
    }
    if (!MoveFileExW(
            temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("cannot commit staged installer file: " +
                                 wideToUtf8(formatWindowsError(error)));
    }
}

void writeTextAtomic(const fs::path &path, std::wstring_view text) {
    const std::string utf8 = wideToUtf8(text);
    writeBytesAtomic(path, std::vector<uint8_t>(utf8.begin(), utf8.end()));
}

fs::path recoveryMarkerPath(const fs::path &cacheExecutable) {
    return cacheExecutable.parent_path() / L"recovery-v2.txt";
}

void writeRecoveryMarker(const InstallationPlan &plan, std::string_view identifier) {
    std::wostringstream text;
    text << L"VIPER-RECOVERY\t2\r\n"
         << L"identifier\t" << utf8ToWide(identifier) << L"\r\n"
         << L"scope\t" << (plan.scope == InstallScope::User ? L"user" : L"machine") << L"\r\n"
         << L"root\t" << plan.installRoot.wstring() << L"\r\n";
    writeTextAtomic(recoveryMarkerPath(plan.cacheExecutable), text.str());
}

void removeRecoveryMarker(const InstallationPlan &plan) {
    std::error_code error;
    fs::remove(recoveryMarkerPath(plan.cacheExecutable), error);
}

std::optional<InstalledRecord> readRecoveryRecord(const HostPackage &package,
                                                  const HostOptions &options,
                                                  Logger &logger) {
    std::vector<InstallScope> scopes;
    if (options.scope) {
        scopes.push_back(*options.scope);
    } else {
        const InstallScope preferred =
            package.metadata.defaultScope == "machine" ? InstallScope::Machine : InstallScope::User;
        scopes.push_back(preferred);
        scopes.push_back(preferred == InstallScope::User ? InstallScope::Machine
                                                         : InstallScope::User);
    }
    for (const InstallScope scope : scopes) {
        const fs::path cache = cacheExecutablePath(scope, package.metadata.identifier);
        const fs::path markerPath = recoveryMarkerPath(cache);
        if (!fs::is_regular_file(markerPath))
            continue;
        const std::wstring text = readTextFileWide(markerPath);
        if (text.rfind(L"VIPER-RECOVERY\t2\r\n", 0) != 0)
            throw std::runtime_error("installer recovery marker has an invalid schema");
        std::wstring identifier;
        std::wstring scopeText;
        std::wstring rootText;
        for (const std::wstring &line : splitLines(text)) {
            const size_t tab = line.find(L'\t');
            if (tab == std::wstring::npos)
                continue;
            const std::wstring key = line.substr(0, tab);
            const std::wstring value = line.substr(tab + 1U);
            if (key == L"identifier")
                identifier = value;
            else if (key == L"scope")
                scopeText = value;
            else if (key == L"root")
                rootText = value;
        }
        const std::wstring expectedScope = scope == InstallScope::User ? L"user" : L"machine";
        if (wideToUtf8(identifier) != package.metadata.identifier || scopeText != expectedScope ||
            rootText.empty()) {
            throw std::runtime_error("installer recovery marker identity is invalid");
        }
        const fs::path root = canonicalDestination(rootText);
        const fs::path transaction =
            root.parent_path() / (L"." + root.filename().wstring() + L".viper-transaction-" +
                                  hashHex(fnv1a64(utf8ToWide(package.metadata.identifier))));
        if (!fs::is_directory(transaction)) {
            logger.warning(L"Removed a stale installer recovery marker");
            std::error_code error;
            fs::remove(markerPath, error);
            continue;
        }
        InstalledRecord record;
        record.present = true;
        record.scope = scope;
        record.installRoot = root;
        record.cacheExecutable = cache;
        record.version = package.metadata.version;
        logger.warning(L"Found an interrupted installer transaction requiring recovery");
        return record;
    }
    return std::nullopt;
}

std::set<std::string> loadOwnershipManifest(const fs::path &installRoot,
                                            std::string_view manifestRelative) {
    const fs::path path = safeJoin(installRoot, manifestRelative);
    const std::wstring text = readTextFileWide(path);
    std::set<std::string> owned;
    if (text.empty())
        return owned;
    const auto lines = splitLines(text);
    size_t start = 0;
    if (!lines.empty() && lines.front() == kManifestHeader)
        start = 1;
    for (size_t i = start; i < lines.size(); ++i) {
        std::wstring relative;
        if (start == 1) {
            const size_t first = lines[i].find(L'\t');
            const size_t second =
                first == std::wstring::npos ? std::wstring::npos : lines[i].find(L'\t', first + 1);
            if (second == std::wstring::npos)
                throw std::runtime_error("installed ownership manifest is malformed");
            relative = lines[i].substr(second + 1);
        } else {
            relative = lines[i];
        }
        const std::string utf8 = wideToUtf8(relative);
        validateRelativePath(utf8);
        if (!owned.insert(normalizedPathKey(utf8)).second)
            throw std::runtime_error("installed ownership manifest contains a duplicate path");
    }
    return owned;
}

std::vector<uint8_t> readFileBytes(const fs::path &path);

std::set<std::string> packageOwnedPaths(const HostPackage &package) {
    std::set<std::string> owned;
    for (const auto &file : package.metadata.payloadFiles)
        owned.insert(normalizedPathKey(file.path));
    for (const auto &file : package.metadata.outerFiles)
        owned.insert(normalizedPathKey(file.path));
    owned.insert(normalizedPathKey(package.metadata.uninstallerRelativePath));
    owned.insert(normalizedPathKey(package.metadata.stateRelativePath));
    owned.insert(normalizedPathKey(package.metadata.installedManifestRelativePath));
    return owned;
}

std::optional<std::wstring> versionResourceString(const fs::path &path, std::wstring_view field) {
    DWORD ignored = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (bytes == 0U || bytes > 16U * 1024U * 1024U)
        return std::nullopt;
    std::vector<uint8_t> resource(bytes);
    if (!GetFileVersionInfoW(path.c_str(), 0, bytes, resource.data()))
        return std::nullopt;

    struct Translation {
        WORD language;
        WORD codePage;
    };

    Translation *translations = nullptr;
    UINT translationBytes = 0;
    if (!VerQueryValueW(resource.data(),
                        L"\\VarFileInfo\\Translation",
                        reinterpret_cast<void **>(&translations),
                        &translationBytes) ||
        !translations || translationBytes < sizeof(Translation)) {
        return std::nullopt;
    }
    wchar_t query[256]{};
    if (swprintf_s(query,
                   L"\\StringFileInfo\\%04x%04x\\%.*s",
                   translations[0].language,
                   translations[0].codePage,
                   static_cast<int>(field.size()),
                   field.data()) < 0) {
        return std::nullopt;
    }
    wchar_t *value = nullptr;
    UINT valueChars = 0;
    if (!VerQueryValueW(resource.data(), query, reinterpret_cast<void **>(&value), &valueChars) ||
        !value || valueChars <= 1U) {
        return std::nullopt;
    }
    return std::wstring(value, valueChars - 1U);
}

bool containsWideBytes(const std::vector<uint8_t> &bytes, std::wstring_view text) {
    if (text.empty() || text.size() > std::numeric_limits<size_t>::max() / sizeof(wchar_t))
        return false;
    const auto *begin = reinterpret_cast<const uint8_t *>(text.data());
    const size_t length = text.size() * sizeof(wchar_t);
    return std::search(bytes.begin(), bytes.end(), begin, begin + length) != bytes.end();
}

bool isRecognizedLegacyUninstaller(const fs::path &path, const HostPackage &incomingPackage) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U) {
        return false;
    }
    const auto originalName = versionResourceString(path, L"OriginalFilename");
    const auto description = versionResourceString(path, L"FileDescription");
    if (!originalName || lowerWide(*originalName) != L"uninstall.exe" || !description ||
        description->size() < 12U ||
        lowerWide(description->substr(description->size() - 12U)) != L" uninstaller") {
        return false;
    }
    std::error_code sizeError;
    const uintmax_t size = fs::file_size(path, sizeError);
    if (sizeError || size == 0U || size > 512ULL * 1024ULL * 1024ULL)
        return false;
    const std::vector<uint8_t> bytes = readFileBytes(path);
    return containsWideBytes(bytes, utf8ToWide(incomingPackage.metadata.identifier)) &&
           containsWideBytes(bytes,
                             utf8ToWide(incomingPackage.metadata.installedManifestRelativePath));
}

std::set<std::string> loadUpgradeOwnership(const HostPackage &incomingPackage,
                                           const fs::path &installRoot,
                                           Logger &logger) {
    std::set<std::string> owned =
        loadOwnershipManifest(installRoot, incomingPackage.metadata.installedManifestRelativePath);
    if (!owned.empty() || !fs::is_directory(installRoot))
        return owned;
    const fs::path uninstaller =
        safeJoin(installRoot, incomingPackage.metadata.uninstallerRelativePath);
    if (!fs::is_regular_file(uninstaller))
        return owned;
    try {
        const HostPackage previous = loadHostPackage(uninstaller);
        if (previous.metadata.identifier != incomingPackage.metadata.identifier)
            throw std::runtime_error("existing maintenance package identifier does not match");
        logger.warning(L"Migrating a verified Viper installation with a missing ownership "
                       L"manifest");
        return packageOwnedPaths(previous);
    } catch (const std::exception &error) {
        if (!isRecognizedLegacyUninstaller(uninstaller, incomingPackage)) {
            logger.warning(L"Could not establish legacy installer ownership: " +
                           utf8ToWide(error.what()));
            return owned;
        }
    }
    logger.warning(L"Migrating generated legacy installation at " + installRoot.wstring());
    return packageOwnedPaths(incomingPackage);
}

void copyUnownedFiles(const fs::path &oldRoot,
                      const fs::path &newRoot,
                      const std::set<std::string> &owned,
                      Logger &logger) {
    if (!fs::exists(oldRoot))
        return;
    std::error_code error;
    for (fs::recursive_directory_iterator
             it(oldRoot, fs::directory_options::skip_permission_denied, error),
         end;
         it != end;
         it.increment(error)) {
        cancellationPoint(logger);
        if (error)
            throw std::runtime_error("cannot enumerate existing installation content");
        const DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            throw std::runtime_error("existing installation contains a reparse point");
        }
        const fs::path relativePath = fs::relative(it->path(), oldRoot, error);
        if (error)
            throw std::runtime_error("cannot resolve existing installation content");
        const std::string relative = slashPath(wideToUtf8(relativePath.generic_wstring()));
        validateRelativePath(relative);
        const fs::path destination = safeJoin(newRoot, relative);
        if (it->is_directory(error)) {
            if (error)
                throw std::runtime_error("cannot inspect existing installation directory");
            continue;
        }
        if (!it->is_regular_file(error) || error)
            throw std::runtime_error("existing installation contains an unsupported file type");
        if (owned.find(normalizedPathKey(relative)) != owned.end())
            continue;
        if (fs::exists(destination)) {
            throw std::runtime_error("unowned file conflicts with the new Viper payload: " +
                                     relative);
        }
        fs::create_directories(destination.parent_path());
        fs::copy_file(it->path(), destination, fs::copy_options::none, error);
        if (error)
            throw std::runtime_error("cannot preserve unowned installation file: " + relative);
        logger.info(L"Preserved unowned file: " + utf8ToWide(relative));
    }
}

std::wstring stateText(const HostPackage &package, const InstallationPlan &plan) {
    std::wostringstream out;
    out << kStateHeader << L"\r\n"
        << L"identifier\t" << utf8ToWide(package.metadata.identifier) << L"\r\n"
        << L"version\t" << utf8ToWide(package.metadata.version) << L"\r\n"
        << L"scope\t" << (plan.scope == InstallScope::User ? L"user" : L"machine") << L"\r\n"
        << L"components\t" << joinComponents(plan.components) << L"\r\n"
        << L"add-to-path\t" << (plan.addToPath ? L'1' : L'0') << L"\r\n"
        << L"associations\t" << (plan.registerAssociations ? L'1' : L'0') << L"\r\n"
        << L"shortcuts\t" << (plan.createShortcuts ? L'1' : L'0') << L"\r\n";
    return out.str();
}

std::wstring manifestText(const HostPackage &package,
                          const std::vector<SelectedFile> &installedFiles,
                          std::string_view stateHash,
                          uint64_t stateSize) {
    std::vector<SelectedFile> files = installedFiles;
    files.push_back({package.metadata.stateRelativePath, std::string(stateHash), stateSize});
    std::sort(files.begin(), files.end(), [](const SelectedFile &left, const SelectedFile &right) {
        return normalizedPathKey(left.path) < normalizedPathKey(right.path);
    });
    std::wostringstream out;
    out << kManifestHeader << L"\r\n";
    for (const SelectedFile &file : files)
        out << utf8ToWide(file.sha256) << L'\t' << file.sizeBytes << L'\t'
            << utf8ToWide(slashPath(file.path)) << L"\r\n";
    out << L'-' << L'\t' << 0 << L'\t'
        << utf8ToWide(slashPath(package.metadata.installedManifestRelativePath)) << L"\r\n";
    return out.str();
}

std::vector<uint8_t> maintenanceBytes(const HostPackage &package) {
    if (!package.metadata.outerFiles.empty()) {
        const auto &record = package.metadata.outerFiles.front();
        const auto found = package.outerFileBytes.find(record.overlayPath);
        if (found == package.outerFileBytes.end())
            throw std::runtime_error("setup package lacks its maintenance executable");
        return found->second;
    }
    return package.executableBytes;
}

std::vector<SelectedFile> stageSelectedTree(const HostPackage &package,
                                            const InstallationPlan &plan,
                                            const fs::path &newRoot,
                                            Logger &logger) {
    viper::pkg::ZipReader payload(package.payloadZip.data(), package.payloadZip.size());
    std::vector<SelectedFile> installed;
    for (const auto &record : package.metadata.payloadFiles) {
        cancellationPoint(logger);
        if (!componentEnabled(record.componentId, plan.components))
            continue;
        const viper::pkg::ZipEntry *entry = payload.find(record.path);
        if (!entry)
            throw std::runtime_error("selected payload entry is missing");
        std::vector<uint8_t> bytes = payload.extract(*entry);
        if (bytes.size() != record.sizeBytes ||
            viper::pkg::sha256Hex(bytes.data(), bytes.size()) != record.sha256) {
            throw std::runtime_error("selected payload entry failed SHA-256 verification");
        }
        const fs::path destination = safeJoin(newRoot, record.path);
        writeBytesAtomic(destination, bytes);
        installed.push_back({record.path, record.sha256, record.sizeBytes});
    }

    const std::vector<uint8_t> uninstaller = maintenanceBytes(package);
    cancellationPoint(logger);
    const std::string uninstallerHash =
        viper::pkg::sha256Hex(uninstaller.data(), uninstaller.size());
    writeBytesAtomic(safeJoin(newRoot, package.metadata.uninstallerRelativePath), uninstaller);
    installed.push_back(
        {package.metadata.uninstallerRelativePath, uninstallerHash, uninstaller.size()});

    const std::wstring state = stateText(package, plan);
    const std::string stateUtf8 = wideToUtf8(state);
    const std::string stateHash = viper::pkg::sha256Hex(
        reinterpret_cast<const uint8_t *>(stateUtf8.data()), stateUtf8.size());
    writeTextAtomic(safeJoin(newRoot, package.metadata.stateRelativePath), state);
    const std::wstring manifest = manifestText(package, installed, stateHash, stateUtf8.size());
    writeTextAtomic(safeJoin(newRoot, package.metadata.installedManifestRelativePath), manifest);
    logger.info(L"Selected payload staged and verified");
    return installed;
}

enum class JournalState {
    None,
    Prepared,
    OldMoved,
    NewActive,
    MetadataCommitted,
    RollbackFilesRestored,
    Committed
};

std::wstring journalName(JournalState state) {
    switch (state) {
        case JournalState::Prepared:
            return L"prepared";
        case JournalState::OldMoved:
            return L"old-moved";
        case JournalState::NewActive:
            return L"new-active";
        case JournalState::MetadataCommitted:
            return L"metadata-committed";
        case JournalState::RollbackFilesRestored:
            return L"rollback-files-restored";
        case JournalState::Committed:
            return L"committed";
        case JournalState::None:
        default:
            return L"none";
    }
}

JournalState parseJournal(std::wstring_view value) {
    if (value.find(L"state=prepared") != std::wstring_view::npos)
        return JournalState::Prepared;
    if (value.find(L"state=old-moved") != std::wstring_view::npos)
        return JournalState::OldMoved;
    if (value.find(L"state=new-active") != std::wstring_view::npos)
        return JournalState::NewActive;
    if (value.find(L"state=metadata-committed") != std::wstring_view::npos)
        return JournalState::MetadataCommitted;
    if (value.find(L"state=rollback-files-restored") != std::wstring_view::npos)
        return JournalState::RollbackFilesRestored;
    if (value.find(L"state=committed") != std::wstring_view::npos)
        return JournalState::Committed;
    return JournalState::None;
}

struct TransactionPaths {
    fs::path directory;
    fs::path newRoot;
    fs::path oldRoot;
    fs::path journal;
    fs::path pathBackup;
    fs::path appliedShortcuts;
};

TransactionPaths transactionPaths(const InstallationPlan &plan, std::string_view identifier) {
    const fs::path directory = plan.installRoot.parent_path() /
                               (L"." + plan.installRoot.filename().wstring() +
                                L".viper-transaction-" + hashHex(fnv1a64(utf8ToWide(identifier))));
    return {directory,
            directory / L"new",
            directory / L"old",
            directory / L"journal.txt",
            directory / L"path-backup.txt",
            directory / L"applied-shortcuts.txt"};
}

void writeJournal(const TransactionPaths &paths, JournalState state) {
    writeTextAtomic(paths.journal, L"schema=2\r\nstate=" + journalName(state) + L"\r\n");
}

void removeTreeChecked(const fs::path &path) {
    if (!fs::exists(path))
        return;
    const DWORD rootAttributes = GetFileAttributesW(path.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES ||
        (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        throw std::runtime_error("refusing to remove a reparse-point transaction tree");
    }
    std::error_code error;
    for (fs::recursive_directory_iterator
             it(path, fs::directory_options::skip_permission_denied, error),
         end;
         it != end;
         it.increment(error)) {
        if (error)
            throw std::runtime_error("cannot inspect an installer tree before removal");
        const DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            throw std::runtime_error("refusing to remove a tree containing a reparse point");
        }
    }
    fs::remove_all(path, error);
    if (error || fs::exists(path))
        throw std::runtime_error("cannot remove installer transaction path: " +
                                 wideToUtf8(path.wstring()));
}

void moveDirectory(const fs::path &source, const fs::path &destination) {
    constexpr ULONGLONG kRetryWindowMilliseconds = 30000U;
    DWORD delayMilliseconds = 25U;
    DWORD error = ERROR_SUCCESS;
    const ULONGLONG deadline = GetTickCount64() + kRetryWindowMilliseconds;
    do {
        if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
            return;
        error = GetLastError();
        if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION &&
            error != ERROR_LOCK_VIOLATION && error != ERROR_BUSY) {
            break;
        }
        if (GetTickCount64() >= deadline)
            break;
        Sleep(delayMilliseconds);
        delayMilliseconds = std::min<DWORD>(delayMilliseconds * 2U, 500U);
    } while (true);
    throw std::runtime_error("cannot atomically move installation directory after retrying: " +
                             wideToUtf8(formatWindowsError(error)));
}

void recoverTransaction(const HostPackage &package,
                        const InstallationPlan &plan,
                        const TransactionPaths &paths,
                        Logger &logger);

void maybeInjectFailure(std::string_view stage) {
#if defined(VIPER_INSTALLER_ENABLE_TEST_HOOKS)
    const wchar_t *value = _wgetenv(L"VIPER_INSTALLER_TEST_CANCEL_AT");
    if (value && wideToUtf8(value) == stage)
        throw InstallerError(kExitUserCancelled,
                             "injected installer cancellation at " + std::string(stage));
    value = _wgetenv(L"VIPER_INSTALLER_TEST_FAIL_AT");
    if (value && wideToUtf8(value) == stage)
        throw std::runtime_error("injected installer failure at " + std::string(stage));
    value = _wgetenv(L"VIPER_INSTALLER_TEST_CRASH_AT");
    if (value && wideToUtf8(value) == stage)
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(kExitFatalError));
    value = _wgetenv(L"VIPER_INSTALLER_TEST_PAUSE_AT");
    if (value && wideToUtf8(value) == stage) {
        DWORD milliseconds = 3000U;
        if (const wchar_t *duration = _wgetenv(L"VIPER_INSTALLER_TEST_PAUSE_MS")) {
            wchar_t *end = nullptr;
            errno = 0;
            const unsigned long parsed = std::wcstoul(duration, &end, 10);
            if (errno == 0 && end && *end == L'\0' && parsed >= 1U && parsed <= 30000U)
                milliseconds = static_cast<DWORD>(parsed);
        }
        Sleep(milliseconds);
    }
#else
    static_cast<void>(stage);
#endif
}

uint64_t testLimitedFreeBytes(uint64_t available) {
#if defined(VIPER_INSTALLER_ENABLE_TEST_HOOKS)
    const wchar_t *value = _wgetenv(L"VIPER_INSTALLER_TEST_FREE_BYTES");
    if (value && *value) {
        wchar_t *end = nullptr;
        errno = 0;
        const unsigned long long parsed = std::wcstoull(value, &end, 10);
        if (errno == 0 && end && *end == L'\0')
            return std::min<uint64_t>(available, parsed);
    }
#endif
    return available;
}

std::vector<std::wstring> splitPathValue(std::wstring_view value) {
    std::vector<std::wstring> entries;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t end = value.find(L';', start);
        entries.emplace_back(value.substr(
            start, end == std::wstring_view::npos ? value.size() - start : end - start));
        if (end == std::wstring_view::npos)
            break;
        start = end + 1;
    }
    return entries;
}

std::wstring trimPathEntry(std::wstring value) {
    while (!value.empty() && std::iswspace(value.front()))
        value.erase(value.begin());
    while (!value.empty() && std::iswspace(value.back()))
        value.pop_back();
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
        value = value.substr(1, value.size() - 2);
    while (value.size() > 3 && (value.back() == L'\\' || value.back() == L'/'))
        value.pop_back();
    return lowerWide(value);
}

void broadcastEnvironment() {
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST,
                        WM_SETTINGCHANGE,
                        0,
                        reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG,
                        5000,
                        &result);
}

std::wstring updatePath(InstallScope scope,
                        std::wstring_view removeEntry,
                        std::wstring_view addEntry) {
    RegKey environment =
        openKey(rootKey(scope),
                scope == InstallScope::User ? kUserEnvironment : kMachineEnvironment,
                KEY_QUERY_VALUE | KEY_SET_VALUE,
                true);
    const std::wstring original = queryRegistryString(environment.get(), L"Path").value_or(L"");
    std::vector<std::wstring> entries = splitPathValue(original);
    const std::wstring removeKey = trimPathEntry(std::wstring(removeEntry));
    const std::wstring addKey = trimPathEntry(std::wstring(addEntry));
    entries.erase(std::remove_if(entries.begin(),
                                 entries.end(),
                                 [&](const std::wstring &entry) {
                                     const std::wstring key = trimPathEntry(entry);
                                     return key.empty() ||
                                            (!removeKey.empty() && key == removeKey) ||
                                            (!addKey.empty() && key == addKey);
                                 }),
                  entries.end());
    if (!addEntry.empty())
        entries.emplace_back(addEntry);
    std::wstring updated;
    for (const std::wstring &entry : entries) {
        if (!updated.empty())
            updated.push_back(L';');
        updated += entry;
    }
    setRegistryString(environment.get(), L"Path", updated, REG_EXPAND_SZ);
    broadcastEnvironment();
    return original;
}

std::wstring bytesToHex(std::string_view bytes) {
    static constexpr wchar_t kHex[] = L"0123456789abcdef";
    std::wstring result;
    result.reserve(bytes.size() * 2U);
    for (const unsigned char byte : bytes) {
        result.push_back(kHex[byte >> 4U]);
        result.push_back(kHex[byte & 0x0FU]);
    }
    return result;
}

unsigned hexNibble(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9')
        return static_cast<unsigned>(ch - L'0');
    if (ch >= L'a' && ch <= L'f')
        return 10U + static_cast<unsigned>(ch - L'a');
    throw std::runtime_error("invalid installer transaction hexadecimal data");
}

std::string hexToBytes(std::wstring_view text) {
    if ((text.size() & 1U) != 0)
        throw std::runtime_error("invalid installer transaction hexadecimal length");
    std::string result;
    result.reserve(text.size() / 2U);
    for (size_t i = 0; i < text.size(); i += 2U) {
        result.push_back(static_cast<char>((hexNibble(text[i]) << 4U) | hexNibble(text[i + 1U])));
    }
    return result;
}

struct PathBackup {
    bool present{false};
    DWORD type{REG_EXPAND_SZ};
    std::wstring value;
};

PathBackup readCurrentPath(InstallScope scope) {
    RegKey environment =
        openKey(rootKey(scope),
                scope == InstallScope::User ? kUserEnvironment : kMachineEnvironment,
                KEY_QUERY_VALUE,
                true);
    DWORD type = 0;
    DWORD bytes = 0;
    LONG result = RegQueryValueExW(environment.get(), L"Path", nullptr, &type, nullptr, &bytes);
    if (result == ERROR_FILE_NOT_FOUND)
        return {};
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes > 32U * 1024U * 1024U) {
        throw std::runtime_error("cannot snapshot the environment PATH value");
    }
    std::vector<wchar_t> value(static_cast<size_t>(bytes) / sizeof(wchar_t) + 1U, L'\0');
    result = RegQueryValueExW(
        environment.get(), L"Path", nullptr, &type, reinterpret_cast<BYTE *>(value.data()), &bytes);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("cannot read the environment PATH value for rollback");
    value.back() = L'\0';
    return {true, type, std::wstring(value.data())};
}

void writePathBackup(const TransactionPaths &paths, InstallScope scope) {
    const PathBackup backup = readCurrentPath(scope);
    std::wostringstream text;
    text << L"VIPER-PATH-BACKUP\t1\r\n"
         << L"present\t" << (backup.present ? L'1' : L'0') << L"\r\n"
         << L"type\t" << backup.type << L"\r\n"
         << L"value\t" << bytesToHex(wideToUtf8(backup.value)) << L"\r\n";
    writeTextAtomic(paths.pathBackup, text.str());
}

PathBackup readPathBackup(const TransactionPaths &paths) {
    const std::wstring text = readTextFileWide(paths.pathBackup);
    if (text.rfind(L"VIPER-PATH-BACKUP\t1\r\n", 0) != 0)
        throw std::runtime_error("installer transaction PATH backup is missing or invalid");
    PathBackup result;
    bool sawPresent = false;
    bool sawType = false;
    bool sawValue = false;
    for (const std::wstring &line : splitLines(text)) {
        const size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos)
            continue;
        const std::wstring key = line.substr(0, tab);
        const std::wstring value = line.substr(tab + 1U);
        if (key == L"present") {
            if (sawPresent || (value != L"0" && value != L"1"))
                throw std::runtime_error("invalid installer transaction PATH presence");
            result.present = value == L"1";
            sawPresent = true;
        } else if (key == L"type") {
            if (sawType ||
                (value != std::to_wstring(REG_SZ) && value != std::to_wstring(REG_EXPAND_SZ))) {
                throw std::runtime_error("invalid installer transaction PATH type");
            }
            result.type = value == std::to_wstring(REG_SZ) ? REG_SZ : REG_EXPAND_SZ;
            sawType = true;
        } else if (key == L"value") {
            if (sawValue)
                throw std::runtime_error("duplicate installer transaction PATH value");
            result.value = utf8ToWide(hexToBytes(value));
            sawValue = true;
        }
    }
    if (!sawPresent || !sawType || !sawValue)
        throw std::runtime_error("incomplete installer transaction PATH backup");
    return result;
}

void restorePathBackup(const TransactionPaths &paths, InstallScope scope) {
    const PathBackup backup = readPathBackup(paths);
    RegKey environment =
        openKey(rootKey(scope),
                scope == InstallScope::User ? kUserEnvironment : kMachineEnvironment,
                KEY_SET_VALUE,
                true);
    if (backup.present) {
        setRegistryString(environment.get(), L"Path", backup.value, backup.type);
    } else {
        const LONG result = RegDeleteValueW(environment.get(), L"Path");
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
            throw std::runtime_error("cannot restore the absent environment PATH value");
    }
    broadcastEnvironment();
}

void initializeAppliedShortcuts(const TransactionPaths &paths) {
    writeTextAtomic(paths.appliedShortcuts, L"VIPER-APPLIED-SHORTCUTS\t1\r\n");
}

void recordAppliedShortcut(const TransactionPaths &paths, const fs::path &path) {
    UniqueHandle file(CreateFileW(paths.appliedShortcuts.c_str(),
                                  FILE_APPEND_DATA,
                                  FILE_SHARE_READ,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr));
    if (!file)
        throw std::runtime_error("cannot open the shortcut rollback journal");
    const std::wstring line = bytesToHex(wideToUtf8(path.wstring())) + L"\r\n";
    const std::string bytes = wideToUtf8(line);
    DWORD written = 0;
    if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != bytes.size() || !FlushFileBuffers(file.get())) {
        throw std::runtime_error("cannot update the shortcut rollback journal");
    }
}

std::vector<fs::path> readAppliedShortcuts(const TransactionPaths &paths) {
    const std::wstring text = readTextFileWide(paths.appliedShortcuts);
    if (text.rfind(L"VIPER-APPLIED-SHORTCUTS\t1\r\n", 0) != 0)
        throw std::runtime_error("shortcut rollback journal is missing or invalid");
    std::vector<fs::path> result;
    const std::vector<std::wstring> lines = splitLines(text);
    for (size_t i = 1; i < lines.size(); ++i) {
        if (!lines[i].empty())
            result.emplace_back(utf8ToWide(hexToBytes(lines[i])));
    }
    return result;
}

void unregisterAssociations(const HostPackage &package, InstallScope scope) {
    for (const auto &association : package.metadata.associations) {
        const std::wstring progIdKey = std::wstring(kClassesBase) + utf8ToWide(association.progId);
        if (RegKey key = openKey(rootKey(scope), progIdKey, KEY_READ, false)) {
            const auto owner = queryRegistryString(key.get(), L"ViperOwner");
            if (owner && wideToUtf8(*owner) == package.metadata.identifier) {
                const std::wstring extensionKey = std::wstring(kClassesBase) +
                                                  utf8ToWide(association.extension) +
                                                  L"\\OpenWithProgids";
                bool openWithEmpty = false;
                {
                    if (RegKey openWith = openKey(
                            rootKey(scope), extensionKey, KEY_QUERY_VALUE | KEY_SET_VALUE, false)) {
                        const LONG removed =
                            RegDeleteValueW(openWith.get(), utf8ToWide(association.progId).c_str());
                        if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND)
                            throw std::runtime_error("cannot remove a Viper Open-With entry");
                        DWORD subkeys = 0;
                        DWORD values = 0;
                        if (RegQueryInfoKeyW(openWith.get(),
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             &subkeys,
                                             nullptr,
                                             nullptr,
                                             &values,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr) == ERROR_SUCCESS) {
                            openWithEmpty = subkeys == 0 && values == 0;
                        }
                    }
                }
                if (openWithEmpty) {
                    RegDeleteKeyW(rootKey(scope), extensionKey.c_str());
                    const std::wstring extensionBase =
                        std::wstring(kClassesBase) + utf8ToWide(association.extension);
                    bool extensionEmpty = false;
                    if (RegKey extension =
                            openKey(rootKey(scope), extensionBase, KEY_READ, false)) {
                        DWORD subkeys = 0;
                        DWORD values = 0;
                        if (RegQueryInfoKeyW(extension.get(),
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             &subkeys,
                                             nullptr,
                                             nullptr,
                                             &values,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr) == ERROR_SUCCESS) {
                            extensionEmpty = subkeys == 0 && values == 0;
                        }
                    }
                    if (extensionEmpty)
                        RegDeleteKeyW(rootKey(scope), extensionBase.c_str());
                }
                const LONG removed = RegDeleteTreeW(rootKey(scope), progIdKey.c_str());
                if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND)
                    throw std::runtime_error("cannot remove a Viper file-association ProgID");
            }
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void registerAssociations(const HostPackage &package,
                          const InstallationPlan &plan,
                          Logger &logger) {
    unregisterAssociations(package, plan.scope);
    if (!plan.registerAssociations)
        return;
    const fs::path executable = safeJoin(plan.installRoot, package.metadata.associationExecutable);
    if (!fs::is_regular_file(executable)) {
        logger.warning(L"File associations were skipped because ViperIDE is not selected");
        return;
    }
    for (const auto &association : package.metadata.associations) {
        cancellationPoint(logger);
        const std::wstring extensionBase =
            std::wstring(kClassesBase) + utf8ToWide(association.extension);
        const std::wstring progId = utf8ToWide(association.progId);
        const std::wstring progIdBase = std::wstring(kClassesBase) + progId;
        if (RegKey existing = openKey(rootKey(plan.scope), progIdBase, KEY_READ, false)) {
            const auto owner = queryRegistryString(existing.get(), L"ViperOwner");
            if (!owner || wideToUtf8(*owner) != package.metadata.identifier) {
                logger.warning(L"Skipped unowned file-association ProgID collision: " + progId);
                continue;
            }
        }
        RegKey prog = openKey(rootKey(plan.scope), progIdBase, KEY_SET_VALUE, true);
        setRegistryString(prog.get(), L"ViperOwner", utf8ToWide(package.metadata.identifier));
        setRegistryString(prog.get(), {}, utf8ToWide(association.description));
        RegKey openWith =
            openKey(rootKey(plan.scope), extensionBase + L"\\OpenWithProgids", KEY_SET_VALUE, true);
        const LONG noneResult =
            RegSetValueExW(openWith.get(), progId.c_str(), 0, REG_NONE, nullptr, 0);
        if (noneResult != ERROR_SUCCESS)
            throw std::runtime_error("cannot register Viper Open-With association");
        RegKey icon =
            openKey(rootKey(plan.scope), progIdBase + L"\\DefaultIcon", KEY_SET_VALUE, true);
        const fs::path iconPath =
            package.metadata.displayIconRelativePath.empty()
                ? executable
                : safeJoin(plan.installRoot, package.metadata.displayIconRelativePath);
        setRegistryString(icon.get(), {}, iconPath.wstring());
        RegKey command = openKey(
            rootKey(plan.scope), progIdBase + L"\\shell\\open\\command", KEY_SET_VALUE, true);
        std::wstring commandText = quoteCommandLineArgument(executable.wstring());
        if (!association.arguments.empty())
            commandText += L" " + utf8ToWide(association.arguments);
        commandText += L" \"%1\"";
        setRegistryString(command.get(), {}, commandText);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

fs::path windowsDirectory() {
    std::wstring value(32768, L'\0');
    const UINT length = GetWindowsDirectoryW(value.data(), static_cast<UINT>(value.size()));
    if (length == 0 || length >= value.size())
        throw std::runtime_error("cannot resolve the Windows directory for a shortcut");
    value.resize(length);
    return fs::path(value);
}

fs::path resolveShortcutPath(const InstallationPlan &plan,
                             std::string_view root,
                             std::string_view relative) {
    fs::path base;
    if (root == "install")
        base = plan.installRoot;
    else if (root == "windows")
        base = windowsDirectory();
    else if (root == "profile")
        base = knownFolder(FOLDERID_Profile);
    else
        throw std::runtime_error("unsupported shortcut path root");
    return relative.empty() ? base : safeJoin(base, relative);
}

std::wstring shortcutArguments(const viper::pkg::WindowsInstallerShortcutMetadata &metadata,
                               const InstallationPlan &plan) {
    if (metadata.argumentPath.empty())
        return {};
    const fs::path argument = safeJoin(plan.installRoot, metadata.argumentPath);
    return utf8ToWide(metadata.argumentPrefix) + L" " +
           quoteCommandLineArgument(argument.wstring());
}

bool shellLinkMatches(const fs::path &path,
                      const viper::pkg::WindowsInstallerShortcutMetadata &metadata,
                      const InstallationPlan &plan) {
    const HRESULT apartment =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE)
        return false;
    const bool uninitialize = SUCCEEDED(apartment);

    struct ApartmentGuard {
        bool active;

        ~ApartmentGuard() {
            if (active)
                CoUninitialize();
        }
    } guard{uninitialize};

    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(
            CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put())))) {
        return false;
    }
    ComPtr<IPersistFile> persist;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(persist.put()))) ||
        FAILED(persist->Load(path.c_str(), STGM_READ))) {
        return false;
    }
    std::array<wchar_t, 32768> target{};
    std::array<wchar_t, 32768> working{};
    std::array<wchar_t, 32768> arguments{};
    std::array<wchar_t, 32768> icon{};
    WIN32_FIND_DATAW findData{};
    int iconIndex = 0;
    if (FAILED(link->GetPath(
            target.data(), static_cast<int>(target.size()), &findData, SLGP_RAWPATH)) ||
        FAILED(link->GetWorkingDirectory(working.data(), static_cast<int>(working.size()))) ||
        FAILED(link->GetArguments(arguments.data(), static_cast<int>(arguments.size()))) ||
        FAILED(link->GetIconLocation(icon.data(), static_cast<int>(icon.size()), &iconIndex))) {
        return false;
    }
    auto samePath = [](std::wstring_view left, const fs::path &right) {
        return lowerWide(fs::path(left).lexically_normal().wstring()) ==
               lowerWide(right.lexically_normal().wstring());
    };
    if (!samePath(target.data(),
                  resolveShortcutPath(plan, metadata.targetRoot, metadata.targetPath)) ||
        !samePath(working.data(),
                  resolveShortcutPath(plan, metadata.workingRoot, metadata.workingPath)) ||
        std::wstring(arguments.data()) != shortcutArguments(metadata, plan)) {
        return false;
    }
    if (!metadata.iconPath.empty() &&
        (!samePath(icon.data(), resolveShortcutPath(plan, metadata.iconRoot, metadata.iconPath)) ||
         iconIndex != metadata.iconIndex)) {
        return false;
    }
    return true;
}

void createShellLink(const viper::pkg::WindowsInstallerShortcutMetadata &metadata,
                     const InstallationPlan &plan,
                     const fs::path &destination) {
    const HRESULT apartment =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE)
        throw std::runtime_error("cannot initialize COM for a Start menu shortcut");
    const bool uninitialize = SUCCEEDED(apartment);

    struct ApartmentGuard {
        bool active;

        ~ApartmentGuard() {
            if (active)
                CoUninitialize();
        }
    } guard{uninitialize};

    ComPtr<IShellLinkW> link;
    HRESULT result =
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put()));
    if (FAILED(result))
        throw std::runtime_error("cannot create a Windows Shell Link object");
    const fs::path target = resolveShortcutPath(plan, metadata.targetRoot, metadata.targetPath);
    const fs::path working = resolveShortcutPath(plan, metadata.workingRoot, metadata.workingPath);
    result = link->SetPath(target.c_str());
    if (SUCCEEDED(result))
        result = link->SetWorkingDirectory(working.c_str());
    if (SUCCEEDED(result))
        result = link->SetDescription(utf8ToWide(metadata.description).c_str());
    if (SUCCEEDED(result) && !metadata.argumentPath.empty()) {
        const std::wstring arguments = shortcutArguments(metadata, plan);
        result = link->SetArguments(arguments.c_str());
    }
    if (SUCCEEDED(result) && !metadata.iconPath.empty()) {
        const fs::path icon = resolveShortcutPath(plan, metadata.iconRoot, metadata.iconPath);
        result = link->SetIconLocation(icon.c_str(), metadata.iconIndex);
    }
    if (FAILED(result))
        throw std::runtime_error("cannot configure a destination-aware Windows shortcut");

    ComPtr<IPersistFile> persist;
    result = link->QueryInterface(IID_PPV_ARGS(persist.put()));
    if (FAILED(result))
        throw std::runtime_error("cannot persist a Windows shortcut");
    fs::create_directories(destination.parent_path());
    const fs::path temporary = destination.wstring() + L".tmp-" +
                               std::to_wstring(GetCurrentProcessId()) + L"-" +
                               hashHex(GetTickCount64()) + L".lnk";
    result = persist->Save(temporary.c_str(), TRUE);
    if (FAILED(result)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("cannot save a Windows shortcut");
    }
    if (!MoveFileExW(temporary.c_str(),
                     destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("cannot commit a Windows shortcut: " +
                                 wideToUtf8(formatWindowsError(error)));
    }
}

void removeShortcuts(const InstalledRecord &record);

std::vector<fs::path> installShortcuts(const HostPackage &package,
                                       const InstallationPlan &plan,
                                       const InstalledRecord &existing,
                                       Logger &logger,
                                       const TransactionPaths *transaction = nullptr) {
    InstalledRecord recognized = existing;
    for (const auto &shortcut : package.metadata.shortcuts) {
        fs::path root;
        if (shortcut.root == "desktop") {
            root = knownFolder(plan.scope == InstallScope::User ? FOLDERID_Desktop
                                                                : FOLDERID_PublicDesktop);
        } else {
            root = knownFolder(plan.scope == InstallScope::User ? FOLDERID_Programs
                                                                : FOLDERID_CommonPrograms);
            root /= utf8ToWide(package.metadata.defaultInstallDir);
        }
        const fs::path destination = safeJoin(root, shortcut.relativePath);
        const bool recorded = std::any_of(
            recognized.shortcuts.begin(), recognized.shortcuts.end(), [&](const fs::path &old) {
                return lowerWide(old.wstring()) == lowerWide(destination.wstring());
            });
        if (!recorded && fs::is_regular_file(destination) &&
            shellLinkMatches(destination, shortcut, plan)) {
            recognized.shortcuts.push_back(destination);
            logger.warning(L"Recovered ownership of a matching Viper shortcut: " +
                           destination.wstring());
        }
    }
    // Remove the complete owned set through the same checked cleanup path used
    // by uninstall.  Deleting the files inline used to leave an empty Start
    // Menu product directory whenever Modify disabled shortcuts.
    removeShortcuts(recognized);
    if (!plan.createShortcuts)
        return {};
    std::vector<fs::path> installed;
    for (const auto &shortcut : package.metadata.shortcuts) {
        cancellationPoint(logger);
        if (!componentEnabled(shortcut.componentId, plan.components))
            continue;
        fs::path root;
        if (shortcut.root == "desktop") {
            root = knownFolder(plan.scope == InstallScope::User ? FOLDERID_Desktop
                                                                : FOLDERID_PublicDesktop);
        } else {
            root = knownFolder(plan.scope == InstallScope::User ? FOLDERID_Programs
                                                                : FOLDERID_CommonPrograms);
            root /= utf8ToWide(package.metadata.defaultInstallDir);
        }
        const fs::path destination = safeJoin(root, shortcut.relativePath);
        if (fs::exists(destination) &&
            std::find_if(
                existing.shortcuts.begin(), existing.shortcuts.end(), [&](const fs::path &old) {
                    return lowerWide(old.wstring()) == lowerWide(destination.wstring());
                }) == existing.shortcuts.end()) {
            logger.warning(L"Skipped unowned shortcut collision: " + destination.wstring());
            continue;
        }
        if (transaction)
            recordAppliedShortcut(*transaction, destination);
        createShellLink(shortcut, plan, destination);
        installed.push_back(destination);
    }
    return installed;
}

void removeShortcuts(const InstalledRecord &record) {
    for (const fs::path &path : record.shortcuts) {
        std::error_code error;
        fs::remove(path, error);
        if (error || fs::exists(path))
            throw std::runtime_error("cannot remove an installed Viper shortcut");
        fs::path parent = path.parent_path();
        if (!parent.empty()) {
            error.clear();
            fs::remove(parent, error);
            if (error == std::errc::directory_not_empty) {
                error.clear();
            }
            if (error)
                throw std::runtime_error("cannot clean the Viper shortcut directory");
        }
    }
}

std::wstring quotedExecutableCommand(const fs::path &path, std::wstring_view argument) {
    std::wstring result = quoteCommandLineArgument(path.wstring());
    if (!argument.empty())
        result += L" " + std::wstring(argument);
    return result;
}

void registerArp(const HostPackage &package,
                 const InstallationPlan &plan,
                 const std::vector<fs::path> &shortcuts,
                 std::wstring_view pathEntry,
                 const Logger &logger) {
    RegKey key = openKey(rootKey(plan.scope),
                         uninstallSubkey(package.metadata.identifier),
                         KEY_QUERY_VALUE | KEY_SET_VALUE,
                         true);
    const fs::path icon =
        package.metadata.displayIconRelativePath.empty()
            ? plan.installRoot / utf8ToWide(package.metadata.executableName)
            : safeJoin(plan.installRoot, package.metadata.displayIconRelativePath);
    setRegistryString(key.get(), L"DisplayName", utf8ToWide(package.metadata.displayName));
    setRegistryString(key.get(), L"DisplayVersion", utf8ToWide(package.metadata.version));
    setRegistryString(key.get(), L"Publisher", utf8ToWide(package.metadata.publisher));
    setRegistryString(key.get(), L"InstallLocation", plan.installRoot.wstring());
    setRegistryString(key.get(), L"DisplayIcon", icon.wstring());
    setRegistryString(key.get(),
                      L"UninstallString",
                      quotedExecutableCommand(plan.cacheExecutable, L"/uninstall"));
    setRegistryString(key.get(),
                      L"QuietUninstallString",
                      quotedExecutableCommand(plan.cacheExecutable, L"/uninstall /quiet"));
    setRegistryString(
        key.get(), L"ModifyPath", quotedExecutableCommand(plan.cacheExecutable, L"/modify"));
    setRegistryString(
        key.get(), L"RepairPath", quotedExecutableCommand(plan.cacheExecutable, L"/repair"));
    setRegistryDword(key.get(), L"NoModify", 0);
    setRegistryDword(key.get(), L"NoRepair", 0);
    setRegistryDword(key.get(), L"WindowsInstaller", 0);
    setRegistryDword(key.get(),
                     L"EstimatedSize",
                     static_cast<DWORD>(std::min<uint64_t>((plan.selectedSizeBytes + 1023U) / 1024U,
                                                           std::numeric_limits<DWORD>::max())));
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::wostringstream date;
    date << std::setfill(L'0') << std::setw(4) << now.wYear << std::setw(2) << now.wMonth
         << std::setw(2) << now.wDay;
    setRegistryString(key.get(), L"InstallDate", date.str());
    if (!package.metadata.homepage.empty()) {
        const std::wstring homepage = utf8ToWide(package.metadata.homepage);
        setRegistryString(key.get(), L"URLInfoAbout", homepage);
        setRegistryString(key.get(),
                          L"HelpLink",
                          package.metadata.documentationUrl.empty()
                              ? homepage
                              : utf8ToWide(package.metadata.documentationUrl));
    }
    if (!package.metadata.updateManifestUrl.empty())
        setRegistryString(
            key.get(), L"URLUpdateInfo", utf8ToWide(package.metadata.updateManifestUrl));
    setRegistryString(key.get(), L"Comments", utf8ToWide(package.metadata.description));
    setRegistryString(key.get(), L"Contact", utf8ToWide(package.metadata.contact));
    setRegistryString(
        key.get(), L"ViperPackageIdentifier", utf8ToWide(package.metadata.identifier));
    setRegistryString(key.get(), L"ViperArchitecture", utf8ToWide(package.metadata.architecture));
    setRegistryString(key.get(), L"ViperChannel", utf8ToWide(package.metadata.channel));
    setRegistryString(key.get(), L"ViperCommit", utf8ToWide(package.metadata.commit));
    setRegistryString(key.get(), L"ViperPackageSha256", utf8ToWide(package.executableSha256));
    setRegistryString(key.get(), L"ViperLastInstallerLog", logger.path().wstring());
    setRegistryString(key.get(), L"ViperComponents", joinComponents(plan.components));
    setRegistryString(key.get(), L"ViperMaintenanceCache", plan.cacheExecutable.wstring());
    setRegistryString(key.get(), L"ViperPathEntry", pathEntry);
    std::wstring shortcutText;
    for (const fs::path &shortcut : shortcuts)
        shortcutText += shortcut.wstring() + L"\r\n";
    setRegistryString(key.get(), L"ViperShortcutPaths", shortcutText);
    setRegistryDword(key.get(), L"ViperInstallSchema", 2);
    setRegistryDword(key.get(), L"ViperSettingsVersion", 1);
    setRegistryDword(key.get(), L"ViperAddToPath", plan.addToPath ? 1U : 0U);
    setRegistryDword(key.get(), L"ViperAssociations", plan.registerAssociations ? 1U : 0U);
    setRegistryDword(key.get(), L"ViperCreateShortcuts", plan.createShortcuts ? 1U : 0U);
}

void removeArp(const HostPackage &package, InstallScope scope) {
    const LONG result =
        RegDeleteTreeW(rootKey(scope), uninstallSubkey(package.metadata.identifier).c_str());
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
        throw std::runtime_error("cannot remove Add/Remove Programs registration");
}

std::vector<uint8_t> readFileBytes(const fs::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return {};
    const std::streamoff size = input.tellg();
    if (size < 0 ||
        static_cast<uint64_t>(size) > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::runtime_error("maintenance cache file is too large");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char *>(bytes.data()),
                                      static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("cannot read the maintenance cache file");
    }
    return bytes;
}

void ensureMaintenanceCache(const fs::path &path, const std::vector<uint8_t> &maintenance) {
    if (fs::is_regular_file(path)) {
        const std::vector<uint8_t> old = readFileBytes(path);
        if (viper::pkg::sha256Hex(old.data(), old.size()) ==
            viper::pkg::sha256Hex(maintenance.data(), maintenance.size())) {
            return;
        }
    }
    writeBytesAtomic(path, maintenance);
}

void applyMetadata(const HostPackage &package,
                   const InstallationPlan &plan,
                   const TransactionPaths &paths,
                   Logger &logger) {
    const std::vector<uint8_t> maintenance = maintenanceBytes(package);
    ensureMaintenanceCache(plan.cacheExecutable, maintenance);

    std::wstring pathEntry;
    if (plan.addToPath && !package.metadata.pathRelativePath.empty())
        pathEntry = safeJoin(plan.installRoot, package.metadata.pathRelativePath).wstring();
    updatePath(plan.scope, plan.existing.pathEntry, pathEntry);
    registerAssociations(package, plan, logger);
    const std::vector<fs::path> shortcuts =
        installShortcuts(package, plan, plan.existing, logger, &paths);
    registerArp(package, plan, shortcuts, pathEntry, logger);
    logger.info(L"Windows integration metadata committed");
}

void removeMetadata(const HostPackage &package, const InstallationPlan &plan, Logger &logger) {
    removeShortcuts(plan.existing);
    unregisterAssociations(package, plan.scope);
    updatePath(plan.scope, plan.existing.pathEntry, {});
    removeArp(package, plan.scope);
    logger.info(L"Windows integration metadata removed");
}

fs::path temporaryDirectory() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (length == 0)
            throw std::runtime_error("cannot locate the Windows temporary directory");
        if (length < buffer.size())
            return fs::path(std::wstring(buffer.data(), length));
        buffer.resize(static_cast<size_t>(length) + 1U);
    }
}

void writeHandleBytes(HANDLE handle, const std::vector<uint8_t> &bytes) {
    size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD chunk =
            static_cast<DWORD>((std::min)(bytes.size() - offset, static_cast<size_t>(MAXDWORD)));
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            throw std::runtime_error("cannot write the detached cleanup helper");
        }
        offset += written;
    }
    if (!FlushFileBuffers(handle))
        throw std::runtime_error("cannot flush the detached cleanup helper");
}

bool launchDetachedCleanup(const HostPackage &package,
                           const InstallationPlan &plan,
                           Logger &logger) {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid)))
        throw std::runtime_error("cannot allocate a unique cleanup helper name");
    wchar_t guidText[40]{};
    if (StringFromGUID2(guid, guidText, static_cast<int>(std::size(guidText))) == 0)
        throw std::runtime_error("cannot format the cleanup helper name");
    std::wstring directoryName = L"ViperCleanup-" + std::wstring(guidText);
    directoryName.erase(std::remove_if(directoryName.begin(),
                                       directoryName.end(),
                                       [](wchar_t ch) { return ch == L'{' || ch == L'}'; }),
                        directoryName.end());
    const fs::path helperDirectory = temporaryDirectory() / directoryName;
    const fs::path helperPath = helperDirectory / L"cleanup.exe";
    if (!CreateDirectoryW(helperDirectory.c_str(), nullptr))
        throw std::runtime_error("cannot create the detached cleanup directory");

    UniqueHandle helper(CreateFileW(helperPath.c_str(),
                                    GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_TEMPORARY,
                                    nullptr));
    if (!helper) {
        RemoveDirectoryW(helperDirectory.c_str());
        throw std::runtime_error("cannot create the detached cleanup helper");
    }

    try {
        writeHandleBytes(helper.get(), package.cleanupBytes);
        helper.reset();
        helper.reset(CreateFileW(helperPath.c_str(),
                                 GENERIC_READ | DELETE | SYNCHRONIZE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL,
                                 nullptr));
        if (!helper)
            throw std::runtime_error("cannot reopen the detached cleanup helper");
        std::wstring command =
            quoteCommandLineArgument(helperPath.wstring()) + L" /parent " +
            std::to_wstring(GetCurrentProcessId()) + L" /delete " +
            quoteCommandLineArgument(plan.cacheExecutable.wstring()) + L" /rmdir " +
            quoteCommandLineArgument(plan.cacheExecutable.parent_path().wstring()) +
            L" /rmdir-if-empty " +
            quoteCommandLineArgument(plan.cacheExecutable.parent_path().parent_path().wstring()) +
            L" /rmdir-if-empty " +
            quoteCommandLineArgument(
                plan.cacheExecutable.parent_path().parent_path().parent_path().wstring()) +
            L" /rmdir " + quoteCommandLineArgument(helperDirectory.wstring());
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(helperPath.c_str(),
                            mutableCommand.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            CREATE_SUSPENDED | CREATE_NO_WINDOW,
                            nullptr,
                            helperDirectory.parent_path().c_str(),
                            &startup,
                            &process)) {
            const DWORD error = GetLastError();
            throw std::runtime_error("cannot start the detached cleanup helper: " +
                                     wideToUtf8(formatWindowsError(error)));
        }
        UniqueHandle processHandle(process.hProcess);
        UniqueHandle threadHandle(process.hThread);
        helper.reset();
        if (ResumeThread(threadHandle.get()) == static_cast<DWORD>(-1)) {
            TerminateProcess(processHandle.get(), ERROR_INVALID_FUNCTION);
            throw std::runtime_error("cannot resume the detached cleanup helper");
        }
        bool unlinked = false;
        DWORD helperExit = STILL_ACTIVE;
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            if (GetFileAttributesW(helperPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                    unlinked = true;
                    break;
                }
            }
            if (WaitForSingleObject(processHandle.get(), 0) == WAIT_OBJECT_0) {
                GetExitCodeProcess(processHandle.get(), &helperExit);
                break;
            }
            Sleep(20);
        }
        if (!unlinked) {
            TerminateProcess(processHandle.get(), ERROR_ACCESS_DENIED);
            WaitForSingleObject(processHandle.get(), 5000);
            throw std::runtime_error("detached cleanup helper could not self-delete (exit " +
                                     std::to_string(helperExit) + ")");
        }
        logger.info(L"Detached maintenance-cache cleanup was started");
        return true;
    } catch (...) {
        helper.reset();
        DeleteFileW(helperPath.c_str());
        RemoveDirectoryW(helperDirectory.c_str());
        throw;
    }
}

bool cleanupCacheAfterUninstall(const HostPackage &package,
                                const InstallationPlan &plan,
                                Logger &logger) {
    try {
        return launchDetachedCleanup(package, plan, logger);
    } catch (const std::exception &error) {
        MoveFileExW(plan.cacheExecutable.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        logger.warning(L"Detached cleanup failed; maintenance cache cleanup was scheduled "
                       L"for reboot: " +
                       utf8ToWide(error.what()));
        return false;
    }
}

bool pathIsWithin(const fs::path &root, const fs::path &candidate) {
    std::wstring rootText = lowerWide(root.lexically_normal().wstring());
    const std::wstring candidateText = lowerWide(candidate.lexically_normal().wstring());
    while (rootText.size() > 3U && (rootText.back() == L'\\' || rootText.back() == L'/')) {
        rootText.pop_back();
    }
    if (candidateText == rootText)
        return true;
    rootText.push_back(L'\\');
    return candidateText.size() > rootText.size() &&
           candidateText.compare(0, rootText.size(), rootText) == 0;
}

int launchMaintenanceHandoff(const HostPackage &package,
                             const HostOptions &options,
                             const InstallationPlan &plan,
                             Logger &logger) {
    if (!fs::is_regular_file(plan.cacheExecutable))
        throw std::runtime_error("the verified maintenance cache is missing");
    const HostPackage cached = loadHostPackage(plan.cacheExecutable);
    if (cached.metadata.identifier != package.metadata.identifier ||
        cached.metadata.version != package.metadata.version ||
        viper::pkg::sha256Hex(cached.executableBytes.data(), cached.executableBytes.size()) !=
            viper::pkg::sha256Hex(package.executableBytes.data(), package.executableBytes.size())) {
        throw std::runtime_error("the maintenance cache does not match the installed package");
    }

    std::vector<std::wstring> arguments = {operationSwitch(plan.operation),
                                           L"/scope",
                                           plan.scope == InstallScope::User ? L"user" : L"machine",
                                           L"/installDir",
                                           plan.installRoot.wstring(),
                                           L"/uninstall-worker",
                                           L"/handoff-parent",
                                           std::to_wstring(GetCurrentProcessId()),
                                           L"/log",
                                           logger.path().wstring()};
    if (options.uiLevel == UiLevel::Quiet)
        arguments.push_back(L"/quiet");
    else if (options.uiLevel == UiLevel::Passive)
        arguments.push_back(L"/passive");
    if (options.allowDowngrade)
        arguments.push_back(L"/allowDowngrade");
    if (options.noRestart)
        arguments.push_back(L"/norestart");
    if (options.closeApplications)
        arguments.push_back(L"/closeApplications");
    if (options.addToPath)
        arguments.push_back(*options.addToPath ? L"/addToPath" : L"/noPath");
    if (options.registerAssociations) {
        arguments.push_back(*options.registerAssociations ? L"/associations" : L"/noAssociations");
    }
    if (options.createShortcuts)
        arguments.push_back(*options.createShortcuts ? L"/shortcuts" : L"/noShortcuts");
    if (!plan.components.empty()) {
        arguments.push_back(L"/components");
        arguments.push_back(joinComponents(plan.components));
    }
    if (options.launchIDE)
        arguments.push_back(L"/launch-ide");
    if (options.launchPrompt)
        arguments.push_back(L"/launch-prompt");
    if (options.openQuickstart)
        arguments.push_back(L"/open-quickstart");
    if (options.openSamples)
        arguments.push_back(L"/open-samples");

    std::wstring command = quoteCommandLineArgument(plan.cacheExecutable.wstring());
    for (const std::wstring &argument : arguments)
        command += L" " + quoteCommandLineArgument(argument);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(plan.cacheExecutable.c_str(),
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        0,
                        nullptr,
                        plan.cacheExecutable.parent_path().c_str(),
                        &startup,
                        &process)) {
        const DWORD error = GetLastError();
        throw std::runtime_error("cannot hand maintenance off to the verified cache: " +
                                 wideToUtf8(formatWindowsError(error)));
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    logger.info(L"Maintenance operation handed off to the verified cache");
    return kExitSuccess;
}

void waitForHandoffParent(DWORD processId) {
    if (processId == 0)
        return;
    UniqueHandle parent(OpenProcess(SYNCHRONIZE, FALSE, processId));
    if (!parent) {
        if (GetLastError() == ERROR_INVALID_PARAMETER)
            return;
        throw std::runtime_error("cannot synchronize the maintenance handoff");
    }
    const DWORD wait = WaitForSingleObject(parent.get(), 60U * 1000U);
    if (wait != WAIT_OBJECT_0)
        throw std::runtime_error("the originating maintenance process did not exit");
}

std::optional<HostPackage> loadInstalledPackage(const fs::path &root,
                                                const HostPackage &fallbackPackage,
                                                Logger &logger) {
    std::vector<fs::path> candidates = {
        safeJoin(root, fallbackPackage.metadata.uninstallerRelativePath), root / L"uninstall.exe"};
    for (const fs::path &candidate : candidates) {
        if (!fs::is_regular_file(candidate))
            continue;
        try {
            HostPackage package = loadHostPackage(candidate);
            if (package.metadata.identifier == fallbackPackage.metadata.identifier)
                return package;
            logger.warning(L"Ignored a transaction uninstaller with a mismatched package id");
        } catch (const std::exception &error) {
            logger.warning(L"Could not read a transaction uninstaller: " +
                           utf8ToWide(error.what()));
        }
    }
    return std::nullopt;
}

std::set<std::string> installedComponents(const HostPackage &package, const fs::path &root) {
    const std::wstring state = readTextFileWide(safeJoin(root, package.metadata.stateRelativePath));
    for (const std::wstring &line : splitLines(state)) {
        constexpr std::wstring_view kPrefix = L"components\t";
        if (line.rfind(kPrefix, 0) == 0)
            return parseComponentList(std::wstring_view(line).substr(kPrefix.size()));
    }
    std::set<std::string> selected;
    for (const auto &component : package.metadata.components) {
        if (component.required || component.defaultSelected)
            selected.insert(lowerAscii(component.id));
    }
    return selected;
}

InstallationPlan restorationPlan(const HostPackage &package, const InstallationPlan &currentPlan) {
    InstallationPlan restored;
    restored.operation = Operation::Repair;
    restored.scope = currentPlan.scope;
    restored.installRoot = currentPlan.installRoot;
    restored.cacheExecutable = currentPlan.cacheExecutable;
    restored.components = installedComponents(package, restored.installRoot);
    restored.addToPath = package.metadata.addToPath;
    restored.registerAssociations = package.metadata.registerFileAssociations;
    restored.createShortcuts = package.metadata.createShortcuts;
    const std::wstring state =
        readTextFileWide(safeJoin(restored.installRoot, package.metadata.stateRelativePath));
    for (const std::wstring &line : splitLines(state)) {
        const size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos)
            continue;
        const std::wstring key = line.substr(0, tab);
        const std::wstring value = line.substr(tab + 1U);
        if (value != L"0" && value != L"1")
            continue;
        if (key == L"add-to-path")
            restored.addToPath = value == L"1";
        else if (key == L"associations")
            restored.registerAssociations = value == L"1";
        else if (key == L"shortcuts")
            restored.createShortcuts = value == L"1";
    }
    for (const auto &file : package.metadata.payloadFiles) {
        if (componentEnabled(file.componentId, restored.components))
            restored.selectedSizeBytes += file.sizeBytes;
    }
    for (const auto &file : package.metadata.outerFiles) {
        if (componentEnabled(file.componentId, restored.components))
            restored.selectedSizeBytes += file.sizeBytes;
    }
    if (package.metadata.packageMode == "maintenance")
        restored.selectedSizeBytes += package.executableBytes.size();
    return restored;
}

void removeCacheFile(const fs::path &cacheExecutable) {
    std::error_code error;
    fs::remove(cacheExecutable, error);
    fs::remove(cacheExecutable.parent_path(), error);
    fs::remove(cacheExecutable.parent_path().parent_path(), error);
    fs::remove(cacheExecutable.parent_path().parent_path().parent_path(), error);
}

void restoreMetadataAfterRollback(const HostPackage &newPackage,
                                  const std::optional<HostPackage> &oldPackage,
                                  const InstallationPlan &plan,
                                  const TransactionPaths &paths,
                                  Logger &logger) {
    for (const fs::path &shortcut : readAppliedShortcuts(paths)) {
        std::error_code error;
        fs::remove(shortcut, error);
    }
    const InstalledRecord current = readInstalledRecord(newPackage.metadata.identifier, plan.scope);
    removeShortcuts(current);
    unregisterAssociations(newPackage, plan.scope);
    removeArp(newPackage, plan.scope);
    restorePathBackup(paths, plan.scope);

    if (!oldPackage) {
        removeCacheFile(plan.cacheExecutable);
        logger.warning(
            L"Rolled back Windows integration metadata for an interrupted first install");
        return;
    }

    InstallationPlan restored = restorationPlan(*oldPackage, plan);
    ensureMaintenanceCache(restored.cacheExecutable, maintenanceBytes(*oldPackage));
    registerAssociations(*oldPackage, restored, logger);
    InstalledRecord noExisting;
    const std::vector<fs::path> shortcuts =
        installShortcuts(*oldPackage, restored, noExisting, logger);
    std::wstring pathEntry;
    if (restored.addToPath && !oldPackage->metadata.pathRelativePath.empty()) {
        pathEntry = safeJoin(restored.installRoot, oldPackage->metadata.pathRelativePath).wstring();
    }
    registerArp(*oldPackage, restored, shortcuts, pathEntry, logger);
    logger.warning(L"Restored the previous package's Windows integration metadata");
}

void recoverTransaction(const HostPackage &package,
                        const InstallationPlan &plan,
                        const TransactionPaths &paths,
                        Logger &logger) {
    if (!fs::exists(paths.directory))
        return;
    const JournalState state = parseJournal(readTextFileWide(paths.journal));
    logger.warning(L"Recovering interrupted installer transaction in state " + journalName(state));
    if (state == JournalState::Committed) {
        if (fs::exists(paths.oldRoot))
            removeTreeChecked(paths.oldRoot);
        if (fs::exists(paths.directory))
            removeTreeChecked(paths.directory);
        removeRecoveryMarker(plan);
        return;
    }
    if (state == JournalState::None || state == JournalState::Prepared) {
        removeTreeChecked(paths.directory);
        removeRecoveryMarker(plan);
        return;
    }
    if (state == JournalState::RollbackFilesRestored) {
        std::optional<HostPackage> restoredPackage;
        if (fs::exists(plan.installRoot))
            restoredPackage = loadInstalledPackage(plan.installRoot, package, logger);
        restoreMetadataAfterRollback(package, restoredPackage, plan, paths, logger);
        removeTreeChecked(paths.directory);
        removeRecoveryMarker(plan);
        return;
    }

    std::optional<HostPackage> oldPackage;
    const bool hadOldRoot = fs::exists(paths.oldRoot);
    if (hadOldRoot)
        oldPackage = loadInstalledPackage(paths.oldRoot, package, logger);
    if (hadOldRoot && !oldPackage)
        throw std::runtime_error(
            "cannot recover the prior installation metadata; transaction retained");
    if (state == JournalState::OldMoved) {
        if (!fs::exists(plan.installRoot) && fs::exists(paths.oldRoot))
            moveDirectory(paths.oldRoot, plan.installRoot);
        removeTreeChecked(paths.directory);
        removeRecoveryMarker(plan);
        return;
    }

    std::optional<HostPackage> newPackage;
    if (fs::exists(plan.installRoot))
        newPackage = loadInstalledPackage(plan.installRoot, package, logger);
    if (fs::exists(plan.installRoot))
        removeTreeChecked(plan.installRoot);
    if (fs::exists(paths.oldRoot))
        moveDirectory(paths.oldRoot, plan.installRoot);
    writeJournal(paths, JournalState::RollbackFilesRestored);
    restoreMetadataAfterRollback(
        newPackage ? *newPackage : package, oldPackage, plan, paths, logger);
    removeTreeChecked(paths.directory);
    removeRecoveryMarker(plan);
}

int performInstallLike(const HostPackage &package,
                       const HostOptions &options,
                       const InstallationPlan &plan,
                       Logger &logger) {
    ensureParentWritable(plan.installRoot);
    const TransactionPaths paths = transactionPaths(plan, package.metadata.identifier);
    recoverTransaction(package, plan, paths, logger);
    RestartManagerSession restart;
    bool committed = false;
    try {
        writeRecoveryMarker(plan, package.metadata.identifier);
        fs::create_directories(paths.newRoot);
        writePathBackup(paths, plan.scope);
        initializeAppliedShortcuts(paths);
        writeJournal(paths, JournalState::Prepared);
        stageSelectedTree(package, plan, paths.newRoot, logger);
        cancellationPoint(logger);
        const std::set<std::string> oldOwned =
            loadUpgradeOwnership(package, plan.installRoot, logger);
        copyUnownedFiles(plan.installRoot, paths.newRoot, oldOwned, logger);
        cancellationPoint(logger);
        maybeInjectFailure("after-stage");

        handleFilesInUse(restart, package, plan, options, logger);
        cancellationPoint(logger);
        const bool hadOldRoot = fs::exists(plan.installRoot);
        if (hadOldRoot)
            moveDirectory(plan.installRoot, paths.oldRoot);
        writeJournal(paths, JournalState::OldMoved);
        maybeInjectFailure("after-old-move");
        moveDirectory(paths.newRoot, plan.installRoot);
        writeJournal(paths, JournalState::NewActive);
        maybeInjectFailure("after-new-move");
        cancellationPoint(logger);
        applyMetadata(package, plan, paths, logger);
        writeJournal(paths, JournalState::MetadataCommitted);
        maybeInjectFailure("after-registry");
        writeJournal(paths, JournalState::Committed);
        committed = true;
        if (fs::exists(paths.oldRoot))
            removeTreeChecked(paths.oldRoot);
        removeTreeChecked(paths.directory);
        removeRecoveryMarker(plan);
        restart.restartApplications(!options.noRestart);
        logger.info(L"Transactional installation committed");
        return kExitSuccess;
    } catch (...) {
        const std::exception_ptr failure = std::current_exception();
        try {
            recoverTransaction(package, plan, paths, logger);
            restart.restartApplications(!options.noRestart);
            if (committed) {
                logger.warning(L"Recovered cleanup after the installation commit point");
                return kExitSuccess;
            }
        } catch (const std::exception &recoveryError) {
            logger.error(L"Transaction rollback failed; the recovery journal was retained: " +
                         utf8ToWide(recoveryError.what()));
        }
        std::rethrow_exception(failure);
    }
}

int performUninstall(const HostPackage &package,
                     const HostOptions &options,
                     const InstallationPlan &plan,
                     Logger &logger) {
    const TransactionPaths paths = transactionPaths(plan, package.metadata.identifier);
    recoverTransaction(package, plan, paths, logger);
    RestartManagerSession restart;
    bool committed = false;
    bool hasUnowned = false;
    try {
        writeRecoveryMarker(plan, package.metadata.identifier);
        fs::create_directories(paths.newRoot);
        writePathBackup(paths, plan.scope);
        initializeAppliedShortcuts(paths);
        writeJournal(paths, JournalState::Prepared);
        const std::set<std::string> owned =
            loadOwnershipManifest(plan.installRoot, package.metadata.installedManifestRelativePath);
        if (owned.empty())
            throw std::runtime_error("ownership manifest is missing; refusing an unsafe uninstall");
        copyUnownedFiles(plan.installRoot, paths.newRoot, owned, logger);
        cancellationPoint(logger);

        handleFilesInUse(restart, package, plan, options, logger);
        cancellationPoint(logger);
        moveDirectory(plan.installRoot, paths.oldRoot);
        writeJournal(paths, JournalState::OldMoved);
        maybeInjectFailure("after-old-move");
        hasUnowned =
            fs::recursive_directory_iterator(paths.newRoot) != fs::recursive_directory_iterator{};
        if (hasUnowned)
            moveDirectory(paths.newRoot, plan.installRoot);
        writeJournal(paths, JournalState::NewActive);
        maybeInjectFailure("after-new-move");
        cancellationPoint(logger);
        removeMetadata(package, plan, logger);
        writeJournal(paths, JournalState::MetadataCommitted);
        maybeInjectFailure("after-registry");
        writeJournal(paths, JournalState::Committed);
        committed = true;
        removeTreeChecked(paths.oldRoot);
        removeTreeChecked(paths.directory);
        removeRecoveryMarker(plan);
        const bool cleanupComplete = cleanupCacheAfterUninstall(package, plan, logger);
        restart.restartApplications(!options.noRestart);
        if (!hasUnowned && fs::exists(plan.installRoot))
            removeTreeChecked(plan.installRoot);
        logger.info(L"Transactional uninstall committed without owned residue");
        return cleanupComplete ? kExitSuccess : kExitRebootRequired;
    } catch (...) {
        const std::exception_ptr failure = std::current_exception();
        try {
            recoverTransaction(package, plan, paths, logger);
            restart.restartApplications(!options.noRestart);
            if (committed) {
                const bool cleanupComplete = cleanupCacheAfterUninstall(package, plan, logger);
                logger.warning(L"Recovered cleanup after the uninstall commit point");
                return cleanupComplete ? kExitSuccess : kExitRebootRequired;
            }
        } catch (const std::exception &recoveryError) {
            logger.error(L"Uninstall rollback failed; the recovery journal was retained: " +
                         utf8ToWide(recoveryError.what()));
        }
        std::rethrow_exception(failure);
    }
}

void launchPostInstallActions(const HostPackage &package,
                              const HostOptions &options,
                              const InstallationPlan &plan,
                              Logger &logger) {
    auto open = [&](const fs::path &path, const wchar_t *parameters = nullptr) {
        if (path.empty() || !fs::exists(path)) {
            logger.warning(L"Requested post-install item is unavailable: " + path.wstring());
            return;
        }
        const HINSTANCE result = ShellExecuteW(
            nullptr, L"open", path.c_str(), parameters, plan.installRoot.c_str(), SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32)
            logger.warning(L"Windows could not open the requested post-install item");
    };

    if (options.launchIDE) {
        const std::string relative = package.metadata.productKind == "toolchain" &&
                                             !package.metadata.associationExecutable.empty()
                                         ? package.metadata.associationExecutable
                                         : package.metadata.executableName;
        open(safeJoin(plan.installRoot, relative));
    }
    if (options.launchPrompt) {
        wchar_t systemDirectory[32768]{};
        const UINT length =
            GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
        if (length > 0 && length < std::size(systemDirectory)) {
            const fs::path script = plan.installRoot / L"bin" / L"viper-dev.cmd";
            const std::wstring parameters = L"/k " + quoteCommandLineArgument(script.wstring());
            open(fs::path(systemDirectory) / L"cmd.exe", parameters.c_str());
        }
    }
    if (options.openQuickstart) {
        const std::array<fs::path, 3> candidates = {
            plan.installRoot / L"share" / L"viper" / L"README.windows-prerequisites.txt",
            plan.installRoot / L"share" / L"doc" / L"viper" / L"README.md",
            plan.installRoot / L"README.md"};
        const auto found =
            std::find_if(candidates.begin(), candidates.end(), [](const fs::path &path) {
                return fs::is_regular_file(path);
            });
        open(found == candidates.end() ? fs::path{} : *found);
    }
    if (options.openSamples)
        open(plan.installRoot / L"share" / L"viper" / L"samples");
}

} // namespace

int runLifecycle(HINSTANCE instance,
                 const HostPackage &package,
                 const HostOptions &requestedOptions,
                 Logger &logger) {
    HostOptions options = requestedOptions;
    preflightWindowsVersion(package, logger);
    waitForHandoffParent(options.handoffParentId);
    const std::optional<InstalledRecord> recoveryRecord =
        readRecoveryRecord(package, options, logger);
    InstallationPlan plan = makePlan(package, options, recoveryRecord ? &*recoveryRecord : nullptr);
    if (recoveryRecord) {
        if (plan.scope == InstallScope::Machine && !isProcessElevated()) {
            const int elevated = relaunchElevated(package, options, plan, logger);
            if ((elevated == kExitSuccess || elevated == kExitRebootRequired) &&
                options.uiLevel == UiLevel::Full && plan.operation != Operation::Uninstall) {
                showInstallerFinish(instance, package, plan.installRoot, plan.components, options);
                launchPostInstallActions(package, options, plan, logger);
            }
            return elevated;
        }
        LifecycleMutex recoveryMutex(plan, package.metadata.identifier);
        if (!recoveryMutex.acquired())
            return kExitAnotherInstallRunning;
        recoverTransaction(
            package, plan, transactionPaths(plan, package.metadata.identifier), logger);
        const InstalledRecord recovered =
            readInstalledRecord(package.metadata.identifier, plan.scope);
        if (!recovered.present && plan.operation == Operation::Uninstall) {
            logger.info(L"Interrupted uninstall recovery completed the requested removal");
            return kExitSuccess;
        }
        plan = makePlan(package, options);
        logger.info(L"Interrupted installer transaction recovery completed");
    }

    if (options.uiLevel == UiLevel::Full && !options.elevatedWorker && !options.uninstallWorker) {
        if (!options.addToPath)
            options.addToPath = plan.addToPath;
        if (!options.registerAssociations)
            options.registerAssociations = plan.registerAssociations;
        if (!options.createShortcuts)
            options.createShortcuts = plan.createShortcuts;
        if (!configureInstallerWizard(instance,
                                      package,
                                      plan.installRoot,
                                      plan.scope,
                                      plan.components,
                                      plan.existing.present,
                                      options)) {
            return kExitUserCancelled;
        }
        plan = makePlan(package, options);
    }

    logger.info(L"Operation: " + operationSwitch(plan.operation));
    logger.info(L"Scope: " +
                std::wstring(plan.scope == InstallScope::User ? L"current user" : L"all users"));
    logger.info(L"Destination: " + plan.installRoot.wstring());
    const fs::path runningExecutable = currentExecutablePath();
    if (!options.uninstallWorker && package.metadata.packageMode == "maintenance" &&
        lowerWide(runningExecutable.wstring()) != lowerWide(plan.cacheExecutable.wstring()) &&
        pathIsWithin(plan.installRoot, runningExecutable)) {
        return launchMaintenanceHandoff(package, options, plan, logger);
    }
    if (plan.scope == InstallScope::Machine && !isProcessElevated()) {
        const int elevated = relaunchElevated(package, options, plan, logger);
        if ((elevated == kExitSuccess || elevated == kExitRebootRequired) &&
            options.uiLevel == UiLevel::Full && plan.operation != Operation::Uninstall) {
            showInstallerFinish(instance, package, plan.installRoot, plan.components, options);
            launchPostInstallActions(package, options, plan, logger);
        }
        return elevated;
    }
    LifecycleMutex mutex(plan, package.metadata.identifier);
    if (!mutex.acquired()) {
        logger.error(L"Another Viper lifecycle operation is already active");
        return kExitAnotherInstallRunning;
    }
    preflightVersion(package, options, plan);
    preflightDisk(package, plan);
    const int result =
        runInstallerProgress(instance, package, plan.operation, options.uiLevel, logger, [&] {
            if (plan.operation == Operation::Uninstall)
                return performUninstall(package, options, plan, logger);
            return performInstallLike(package, options, plan, logger);
        });
    if ((result == kExitSuccess || result == kExitRebootRequired) &&
        plan.operation != Operation::Uninstall) {
        if (options.uiLevel == UiLevel::Full && !options.elevatedWorker && !options.uninstallWorker)
            showInstallerFinish(instance, package, plan.installRoot, plan.components, options);
        if (!options.elevatedWorker)
            launchPostInstallActions(package, options, plan, logger);
    }
    return result;
}

} // namespace viper::installer
