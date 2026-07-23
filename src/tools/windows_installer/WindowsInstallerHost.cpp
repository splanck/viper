//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/windows_installer/WindowsInstallerHost.cpp
// Purpose: Implement native installer package discovery, UTF conversion,
//          command-line parsing, deterministic inspection, and session logging.
//
// Key invariants:
//   - The ZIP overlay is derived from its EOCD/central-directory relationship;
//     untrusted bytes are never located by filename scanning alone.
//   - Metadata and payload entries are extracted with CRC/DEFLATE validation.
//   - Options are case-insensitive but reject unknown or conflicting spellings.
//   - Logs use unique files, UTC timestamps, and sanitized single-line messages.
//
// Ownership/Lifetime:
//   - Archive readers borrow buffers only within loadHostPackage.
//
// Links: WindowsInstallerHost.hpp, ZipReader.hpp, WindowsInstallerMetadata.hpp
//
//===----------------------------------------------------------------------===//

#include "WindowsInstallerHost.hpp"

#include "PkgHash.hpp"
#include "ZipReader.hpp"

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace zanna::installer {
namespace {

constexpr uint64_t kMaximumInstallerExecutableBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;

uint16_t readLe16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8U);
}

uint32_t readLe32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8U) |
           (static_cast<uint32_t>(p[2]) << 16U) | (static_cast<uint32_t>(p[3]) << 24U);
}

std::wstring lowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) -> wchar_t {
        return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch + (L'a' - L'A')) : ch;
    });
    return value;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
    });
    return value;
}

bool isAsciiAlnum(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

std::vector<uint8_t> readWholeFile(const fs::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("cannot open installer executable");
    const std::streamoff end = input.tellg();
    if (end < 0 || static_cast<uint64_t>(end) > kMaximumInstallerExecutableBytes ||
        static_cast<uint64_t>(end) > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::runtime_error("installer executable is too large");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() && !input.read(reinterpret_cast<char *>(bytes.data()), end))
        throw std::runtime_error("cannot read installer executable");
    if (input.peek() != std::ifstream::traits_type::eof())
        throw std::runtime_error("installer executable changed while it was being read");
    return bytes;
}

struct OverlayRange {
    size_t offset{0};
    size_t length{0};
};

OverlayRange locateZipOverlay(const std::vector<uint8_t> &bytes) {
    constexpr uint32_t kEocdSignature = 0x06054B50U;
    constexpr uint32_t kCentralSignature = 0x02014B50U;
    if (bytes.size() < 22)
        throw std::runtime_error("installer has no ZIP overlay");

    const size_t minimum = bytes.size() > (0xFFFFU + 22U + 1024U * 1024U)
                               ? bytes.size() - (0xFFFFU + 22U + 1024U * 1024U)
                               : 0;
    for (size_t pos = bytes.size() - 22;; --pos) {
        if (readLe32(bytes.data() + pos) == kEocdSignature) {
            const uint16_t commentLength = readLe16(bytes.data() + pos + 20);
            const size_t eocdEnd = pos + 22U + commentLength;
            if (eocdEnd == bytes.size()) {
                const uint16_t diskNumber = readLe16(bytes.data() + pos + 4U);
                const uint16_t centralDisk = readLe16(bytes.data() + pos + 6U);
                const uint16_t entriesOnDisk = readLe16(bytes.data() + pos + 8U);
                const uint16_t totalEntries = readLe16(bytes.data() + pos + 10U);
                const uint32_t centralSize = readLe32(bytes.data() + pos + 12);
                const uint32_t centralOffset = readLe32(bytes.data() + pos + 16);
                const uint64_t prefixDistance = static_cast<uint64_t>(centralSize) + centralOffset;
                if (diskNumber == 0U && centralDisk == 0U && entriesOnDisk != 0U &&
                    entriesOnDisk == totalEntries && totalEntries != UINT16_MAX &&
                    centralSize != 0U && centralOffset != UINT32_MAX && centralSize != UINT32_MAX &&
                    prefixDistance <= pos) {
                    const size_t start = pos - static_cast<size_t>(prefixDistance);
                    const uint64_t centralAbsolute = static_cast<uint64_t>(start) + centralOffset;
                    if (centralAbsolute + centralSize == pos && centralAbsolute + 4U <= pos &&
                        readLe32(bytes.data() + static_cast<size_t>(centralAbsolute)) ==
                            kCentralSignature) {
                        return {start, eocdEnd - start};
                    }
                }
            }
        }
        if (pos == minimum)
            break;
    }
    throw std::runtime_error("installer ZIP end record is missing or invalid");
}

std::string bytesToString(const std::vector<uint8_t> &bytes) {
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

void requirePeArchitecture(const std::vector<uint8_t> &bytes,
                           std::string_view architecture,
                           std::string_view label) {
    if (bytes.size() < 64U)
        throw std::runtime_error(std::string(label) + " is too small for a PE header");
    const uint32_t peOffset = readLe32(bytes.data() + 60U);
    if (peOffset > bytes.size() - 6U || readLe32(bytes.data() + peOffset) != 0x00004550U)
        throw std::runtime_error(std::string(label) + " has an invalid PE header");
    const uint16_t machine = readLe16(bytes.data() + peOffset + 4U);
    const uint16_t expected = architecture == "arm64" ? 0xAA64U : 0x8664U;
    if (machine != expected)
        throw std::runtime_error(std::string(label) + " architecture does not match metadata");
}

void requireOuterInventory(const zanna::pkg::ZipReader &outer,
                           const zanna::pkg::WindowsInstallerMetadata &metadata) {
    std::set<std::string> required = {
        "meta/installer-v2.txt", metadata.payloadEntry, metadata.cleanupEntry};
    std::set<std::string> allowed = required;
    if (!metadata.licenseEntry.empty())
        allowed.insert(metadata.licenseEntry);
    if (!metadata.readmeEntry.empty())
        allowed.insert(metadata.readmeEntry);
    for (const auto &file : metadata.outerFiles) {
        required.insert(file.overlayPath);
        allowed.insert(file.overlayPath);
    }
    std::set<std::string> allowedDirectories = {"app/"};
    for (const std::string &path : allowed) {
        size_t separator = path.find('/');
        while (separator != std::string::npos) {
            allowedDirectories.insert(path.substr(0, separator + 1U));
            separator = path.find('/', separator + 1U);
        }
    }
    std::set<std::string> found;
    for (const zanna::pkg::ZipEntry &entry : outer.entries()) {
        if (!entry.name.empty() && entry.name.back() == '/') {
            if (allowedDirectories.find(entry.name) == allowedDirectories.end())
                throw std::runtime_error("installer contains an unowned outer directory");
            continue;
        }
        if (allowed.find(entry.name) == allowed.end())
            throw std::runtime_error("installer contains an unowned outer entry");
        found.insert(entry.name);
    }
    for (const std::string &path : required) {
        if (found.find(path) == found.end())
            throw std::runtime_error("installer outer inventory is incomplete");
    }
}

std::string jsonEscape(std::string_view value) {
    std::ostringstream out;
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (ch < 0x20U) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned>(ch) << std::dec;
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }
    return out.str();
}

bool startsWith(std::wstring_view value, std::wstring_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::wstring optionValue(std::wstring_view original,
                         std::wstring_view lower,
                         std::wstring_view name) {
    const std::wstring equals = std::wstring(name) + L"=";
    const std::wstring colon = std::wstring(name) + L":";
    if (startsWith(lower, equals) || startsWith(lower, colon))
        return std::wstring(original.substr(name.size() + 1));
    return {};
}

std::set<std::string> parseComponents(std::wstring_view text) {
    std::set<std::string> result;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(L',', start);
        std::wstring item(text.substr(
            start, comma == std::wstring_view::npos ? text.size() - start : comma - start));
        item = lowerWide(item);
        if (item.empty())
            throw std::runtime_error("/components contains an empty component id");
        const std::string utf8 = wideToUtf8(item);
        for (unsigned char ch : utf8) {
            if (!(isAsciiAlnum(ch) || ch == '-' || ch == '_' || ch == '.'))
                throw std::runtime_error("/components contains an invalid component id");
        }
        if (!result.insert(utf8).second)
            throw std::runtime_error("/components contains a duplicate component id");
        if (comma == std::wstring_view::npos)
            break;
        start = comma + 1;
    }
    return result;
}

ComponentPreset parseComponentPreset(std::wstring value) {
    value = lowerWide(std::move(value));
    if (value == L"minimal")
        return ComponentPreset::Minimal;
    if (value == L"typical" || value == L"recommended")
        return ComponentPreset::Typical;
    if (value == L"sdk" || value == L"developer")
        return ComponentPreset::SDK;
    if (value == L"complete" || value == L"full")
        return ComponentPreset::Complete;
    throw std::runtime_error("/type must be minimal, typical, sdk, or complete");
}

std::wstring sanitizeLogLine(std::wstring_view message) {
    std::wstring result;
    result.reserve(message.size());
    for (wchar_t ch : message) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t')
            result.push_back(L' ');
        else if (ch >= 0x20)
            result.push_back(ch);
    }
    constexpr size_t kMaximumLine = 8192;
    if (result.size() > kMaximumLine) {
        result.resize(kMaximumLine);
        result += L" [truncated]";
    }
    return result;
}

} // namespace

std::wstring utf8ToWide(std::string_view text) {
    if (text.empty())
        return {};
    if (text.size() > static_cast<size_t>(INT_MAX))
        throw std::runtime_error("package metadata is too large for Windows text conversion");
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0)
        throw std::runtime_error("package metadata is not valid UTF-8");
    std::wstring out(static_cast<size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            text.data(),
                            static_cast<int>(text.size()),
                            out.data(),
                            required) != required) {
        throw std::runtime_error("cannot decode package metadata as UTF-8");
    }
    return out;
}

std::string wideToUtf8(std::wstring_view text) {
    if (text.empty())
        return {};
    if (text.size() > static_cast<size_t>(INT_MAX))
        throw std::runtime_error("Windows text is too large for UTF-8 conversion");
    const int required = WideCharToMultiByte(CP_UTF8,
                                             WC_ERR_INVALID_CHARS,
                                             text.data(),
                                             static_cast<int>(text.size()),
                                             nullptr,
                                             0,
                                             nullptr,
                                             nullptr);
    if (required <= 0)
        throw std::runtime_error("Windows text contains invalid UTF-16");
    std::string out(static_cast<size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            WC_ERR_INVALID_CHARS,
                            text.data(),
                            static_cast<int>(text.size()),
                            out.data(),
                            required,
                            nullptr,
                            nullptr) != required) {
        throw std::runtime_error("cannot encode Windows text as UTF-8");
    }
    return out;
}

std::wstring formatWindowsError(DWORD error) {
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<wchar_t *>(&buffer),
        0,
        nullptr);
    std::wstring result = length && buffer ? std::wstring(buffer, length)
                                           : L"Windows error " + std::to_wstring(error);
    if (buffer)
        LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' ||
                               result.back() == L' ' || result.back() == L'.')) {
        result.pop_back();
    }
    return result;
}

std::wstring quoteCommandLineArgument(std::wstring_view argument) {
    if (argument.empty())
        return L"\"\"";
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
        return std::wstring(argument);
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'\"') {
            if (slashes > (std::numeric_limits<size_t>::max() - 1U) / 2U)
                throw std::runtime_error("Windows command-line argument is too large");
            out.append(slashes * 2U + 1U, L'\\');
            out.push_back(L'\"');
            slashes = 0;
        } else {
            out.append(slashes, L'\\');
            slashes = 0;
            out.push_back(ch);
        }
    }
    if (slashes > std::numeric_limits<size_t>::max() / 2U)
        throw std::runtime_error("Windows command-line argument is too large");
    out.append(slashes * 2U, L'\\');
    out.push_back(L'\"');
    return out;
}

fs::path currentExecutablePath() {
    std::wstring buffer(512, L'\0');
    while (buffer.size() <= 32768) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            throw std::runtime_error("cannot determine installer executable path");
        if (length < buffer.size()) {
            buffer.resize(length);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2U);
    }
    throw std::runtime_error("installer executable path exceeds the Windows limit");
}

fs::path defaultLogPath(std::string_view identifier) {
    wchar_t tempPath[32768]{};
    const DWORD count = GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
    if (count == 0 || count >= std::size(tempPath))
        throw std::runtime_error("cannot locate the temporary directory for the installer log");
    SYSTEMTIME now{};
    GetSystemTime(&now);
    const DWORD pid = GetCurrentProcessId();
    std::wostringstream leaf;
    leaf << L"ZannaInstaller-" << utf8ToWide(identifier) << L'-' << std::setfill(L'0')
         << std::setw(4) << now.wYear << std::setw(2) << now.wMonth << std::setw(2) << now.wDay
         << L'T' << std::setw(2) << now.wHour << std::setw(2) << now.wMinute << std::setw(2)
         << now.wSecond << L'Z' << L'-' << pid << L".log";
    return fs::path(tempPath) / leaf.str();
}

Logger::~Logger() {
    if (handle_ != INVALID_HANDLE_VALUE)
        CloseHandle(handle_);
}

Logger::Logger(Logger &&other) noexcept
    : handle_(other.handle_), path_(std::move(other.path_)),
      progressCallback_(std::move(other.progressCallback_)),
      cancellationCallback_(std::move(other.cancellationCallback_)) {
    other.handle_ = INVALID_HANDLE_VALUE;
}

Logger &Logger::operator=(Logger &&other) noexcept {
    if (this == &other)
        return *this;
    if (handle_ != INVALID_HANDLE_VALUE)
        CloseHandle(handle_);
    handle_ = other.handle_;
    path_ = std::move(other.path_);
    progressCallback_ = std::move(other.progressCallback_);
    cancellationCallback_ = std::move(other.cancellationCallback_);
    other.handle_ = INVALID_HANDLE_VALUE;
    return *this;
}

void Logger::open(const fs::path &path) {
    if (handle_ != INVALID_HANDLE_VALUE)
        CloseHandle(handle_);
    path_ = path;
    if (!path.parent_path().empty())
        fs::create_directories(path.parent_path());
    handle_ = CreateFileW(path.c_str(),
                          FILE_APPEND_DATA,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr,
                          OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL,
                          nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
        throw std::runtime_error("cannot create the installer log");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle_, &size) || size.QuadPart < 0) {
        const DWORD error = GetLastError();
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        throw std::runtime_error("cannot inspect the installer log: " +
                                 wideToUtf8(formatWindowsError(error)));
    }
    if (size.QuadPart == 0) {
        static constexpr uint8_t kUtf8Bom[] = {0xEF, 0xBB, 0xBF};
        DWORD written = 0;
        if (!WriteFile(handle_, kUtf8Bom, sizeof(kUtf8Bom), &written, nullptr) ||
            written != sizeof(kUtf8Bom)) {
            const DWORD error = GetLastError();
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("cannot initialize the installer log: " +
                                     wideToUtf8(formatWindowsError(error)));
        }
    }
}

void Logger::write(std::wstring_view level, std::wstring_view message) {
    if (handle_ != INVALID_HANDLE_VALUE) {
        SYSTEMTIME now{};
        GetSystemTime(&now);
        std::wostringstream line;
        line << std::setfill(L'0') << std::setw(4) << now.wYear << L'-' << std::setw(2)
             << now.wMonth << L'-' << std::setw(2) << now.wDay << L'T' << std::setw(2) << now.wHour
             << L':' << std::setw(2) << now.wMinute << L':' << std::setw(2) << now.wSecond << L'.'
             << std::setw(3) << now.wMilliseconds << L"Z [" << level << L"] "
             << sanitizeLogLine(message) << L"\r\n";
        const std::string utf8 = wideToUtf8(line.str());
        DWORD written = 0;
        if (utf8.size() <= MAXDWORD &&
            WriteFile(handle_, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) &&
            written == utf8.size()) {
            (void)FlushFileBuffers(handle_);
        }
    }
    if (progressCallback_ && level != L"ERROR") {
        try {
            progressCallback_(message);
        } catch (...) {
            // Logging and lifecycle progress must survive a failed presentation callback.
        }
    }
}

void Logger::info(std::wstring_view message) {
    write(L"INFO", message);
}

void Logger::warning(std::wstring_view message) {
    write(L"WARN", message);
}

void Logger::error(std::wstring_view message) {
    write(L"ERROR", message);
}

void Logger::setProgressCallback(std::function<void(std::wstring_view)> callback) {
    progressCallback_ = std::move(callback);
}

void Logger::setCancellationCallback(std::function<bool()> callback) {
    cancellationCallback_ = std::move(callback);
}

bool Logger::cancellationRequested() const {
    return cancellationCallback_ && cancellationCallback_();
}

HostOptions parseCommandLine(int argc, wchar_t **argv) {
    HostOptions result;
    std::optional<Operation> explicitOperation;
    bool uiSpecified = false;
    bool destinationSpecified = false;
    bool logSpecified = false;
    bool outputSpecified = false;
    bool scopeSpecified = false;
    bool presetSpecified = false;
    bool helpRequested = false;
    auto setOperation = [&](Operation operation) {
        if (explicitOperation && *explicitOperation != operation)
            throw std::runtime_error("installer lifecycle operations are mutually exclusive");
        explicitOperation = operation;
        result.operation = operation;
    };
    auto setUiLevel = [&](UiLevel level) {
        if (uiSpecified && result.uiLevel != level)
            throw std::runtime_error("/quiet and /passive cannot be combined");
        uiSpecified = true;
        result.uiLevel = level;
    };
    auto setIntegration = [](std::optional<bool> &field, bool value, std::string_view name) {
        if (field && *field != value)
            throw std::runtime_error("conflicting installer integration options for " +
                                     std::string(name));
        field = value;
    };
    auto setScope = [&](InstallScope scope) {
        if (scopeSpecified && result.scope != scope)
            throw std::runtime_error("conflicting /scope values");
        scopeSpecified = true;
        result.scope = scope;
    };
    auto setPreset = [&](ComponentPreset preset) {
        if (presetSpecified && result.componentPreset != preset)
            throw std::runtime_error("conflicting /type or /preset values");
        presetSpecified = true;
        result.componentPreset = preset;
    };
    for (int i = 1; i < argc; ++i) {
        const std::wstring original(argv[i]);
        const std::wstring arg = lowerWide(original);
        if (arg == L"/?" || arg == L"/help" || arg == L"--help" || arg == L"-h") {
            helpRequested = true;
        } else if (arg == L"/quiet" || arg == L"/silent") {
            setUiLevel(UiLevel::Quiet);
        } else if (arg == L"/passive") {
            setUiLevel(UiLevel::Passive);
        } else if (arg == L"/install") {
            setOperation(Operation::Install);
        } else if (arg == L"/modify") {
            setOperation(Operation::Modify);
        } else if (arg == L"/repair") {
            setOperation(Operation::Repair);
        } else if (arg == L"/uninstall") {
            setOperation(Operation::Uninstall);
        } else if (arg == L"/inspect") {
            setOperation(Operation::Inspect);
        } else if (arg == L"/checkforupdates" || arg == L"/check-updates") {
            setOperation(Operation::CheckUpdates);
        } else if (arg == L"/selftest" || arg == L"/self-test") {
            setOperation(Operation::SelfTest);
        } else if (arg == L"/allowdowngrade") {
            result.allowDowngrade = true;
        } else if (arg == L"/norestart") {
            result.noRestart = true;
        } else if (arg == L"/closeapplications" || arg == L"/closeapps") {
            result.closeApplications = true;
        } else if (arg == L"/addtopath" || arg == L"/path") {
            setIntegration(result.addToPath, true, "PATH");
        } else if (arg == L"/nopath" || arg == L"/no-path") {
            setIntegration(result.addToPath, false, "PATH");
        } else if (arg == L"/associations") {
            setIntegration(result.registerAssociations, true, "file associations");
        } else if (arg == L"/noassociations" || arg == L"/no-associations") {
            setIntegration(result.registerAssociations, false, "file associations");
        } else if (arg == L"/shortcuts") {
            setIntegration(result.createShortcuts, true, "shortcuts");
        } else if (arg == L"/noshortcuts" || arg == L"/no-shortcuts") {
            setIntegration(result.createShortcuts, false, "shortcuts");
        } else if (arg == L"/elevated-worker") {
            result.elevatedWorker = true;
        } else if (arg == L"/uninstall-worker") {
            result.uninstallWorker = true;
        } else if (arg == L"/launch-ide") {
            result.launchIDE = true;
        } else if (arg == L"/launch-prompt") {
            result.launchPrompt = true;
        } else if (arg == L"/open-quickstart") {
            result.openQuickstart = true;
        } else if (arg == L"/open-samples") {
            result.openSamples = true;
        } else if (const std::wstring scopeValue = optionValue(original, arg, L"/scope");
                   !scopeValue.empty()) {
            const std::wstring lower = lowerWide(scopeValue);
            if (lower == L"user" || lower == L"currentuser")
                setScope(InstallScope::User);
            else if (lower == L"machine" || lower == L"allusers")
                setScope(InstallScope::Machine);
            else
                throw std::runtime_error("/scope must be user or machine");
        } else if (const std::wstring installDirValue = optionValue(original, arg, L"/installdir");
                   !installDirValue.empty()) {
            if (destinationSpecified)
                throw std::runtime_error("/installDir was specified more than once");
            destinationSpecified = true;
            result.destination = installDirValue;
        } else if (const std::wstring logValue = optionValue(original, arg, L"/log");
                   !logValue.empty()) {
            if (logSpecified)
                throw std::runtime_error("/log was specified more than once");
            logSpecified = true;
            result.logPath = logValue;
        } else if (const std::wstring outputValue = optionValue(original, arg, L"/output");
                   !outputValue.empty()) {
            if (outputSpecified)
                throw std::runtime_error("/output was specified more than once");
            outputSpecified = true;
            result.outputPath = outputValue;
        } else if (const std::wstring componentsValue = optionValue(original, arg, L"/components");
                   !componentsValue.empty()) {
            result.selectedComponents = parseComponents(componentsValue);
            if (result.componentsSpecified)
                throw std::runtime_error("/components was specified more than once");
            result.componentsSpecified = true;
        } else if (const std::wstring typeValue = optionValue(original, arg, L"/type");
                   !typeValue.empty()) {
            setPreset(parseComponentPreset(typeValue));
        } else if (const std::wstring presetValue = optionValue(original, arg, L"/preset");
                   !presetValue.empty()) {
            setPreset(parseComponentPreset(presetValue));
        } else if (arg == L"/scope" || arg == L"/installdir" || arg == L"/log" ||
                   arg == L"/output" || arg == L"/components" || arg == L"/type" ||
                   arg == L"/preset" || arg == L"/handoff-parent") {
            if (++i >= argc)
                throw std::runtime_error("installer option is missing its value");
            const std::wstring separatedValue(argv[i]);
            if (arg == L"/scope") {
                const std::wstring lower = lowerWide(separatedValue);
                if (lower == L"user" || lower == L"currentuser")
                    setScope(InstallScope::User);
                else if (lower == L"machine" || lower == L"allusers")
                    setScope(InstallScope::Machine);
                else
                    throw std::runtime_error("/scope must be user or machine");
            } else if (arg == L"/installdir") {
                if (destinationSpecified)
                    throw std::runtime_error("/installDir was specified more than once");
                destinationSpecified = true;
                result.destination = separatedValue;
            } else if (arg == L"/log") {
                if (logSpecified)
                    throw std::runtime_error("/log was specified more than once");
                logSpecified = true;
                result.logPath = separatedValue;
            } else if (arg == L"/output") {
                if (outputSpecified)
                    throw std::runtime_error("/output was specified more than once");
                outputSpecified = true;
                result.outputPath = separatedValue;
            } else if (arg == L"/handoff-parent") {
                wchar_t *end = nullptr;
                errno = 0;
                const unsigned long value = std::wcstoul(separatedValue.c_str(), &end, 10);
                if (errno != 0 || !end || *end != L'\0' || value == 0 || value > MAXDWORD)
                    throw std::runtime_error("invalid internal handoff process identifier");
                result.handoffParentId = static_cast<DWORD>(value);
            } else if (arg == L"/type" || arg == L"/preset") {
                setPreset(parseComponentPreset(separatedValue));
            } else {
                if (result.componentsSpecified)
                    throw std::runtime_error("/components was specified more than once");
                result.selectedComponents = parseComponents(separatedValue);
                result.componentsSpecified = true;
            }
        } else {
            throw std::runtime_error("unknown installer option: " + wideToUtf8(original));
        }
    }
    if (result.componentsSpecified && result.componentPreset != ComponentPreset::Unspecified)
        throw std::runtime_error("/components cannot be combined with /type or /preset");
    if (helpRequested)
        result.operation = Operation::Help;
    if (outputSpecified && result.outputPath.empty())
        throw std::runtime_error("/output requires a non-empty path");
    if (outputSpecified && result.operation != Operation::Inspect &&
        result.operation != Operation::CheckUpdates) {
        throw std::runtime_error("/output is valid only with /inspect or /checkForUpdates");
    }
    if (result.uiLevel == UiLevel::Quiet && result.operation == Operation::Help)
        result.uiLevel = UiLevel::Full;
    return result;
}

std::wstring commandLineHelp() {
    return LR"HELP(Zanna Tools Installer

Usage:
  setup.exe [/install|/modify|/repair|/uninstall|/inspect|/checkForUpdates] [options]

Options:
  /quiet, /silent          No user interface
  /passive                 Progress interface without prompts
  /scope user|machine      Install for the current user or all users
  /installDir <path>       Validated custom installation directory
  /components <ids>        Comma-separated component identifiers
  /type <preset>           minimal, typical (recommended), sdk, or complete
  /addToPath, /noPath      Enable or disable the owned PATH entry
  /associations            Register safe Open With entries
  /noAssociations          Do not register Open With entries
  /shortcuts, /noShortcuts Enable or disable packaged shortcuts
  /allowDowngrade          Explicitly permit installing an older version
  /closeApplications       Ask Restart Manager to close eligible applications
  /log <path>              Write the redacted UTF-8 session log to this path
  /output <path>           Atomically write inspection or update JSON to this path
  /norestart               Never restart applications or the computer
  /inspect                 Print verified package metadata as JSON
  /checkForUpdates         Check the pinned, signed update manifest (when configured)
  /?                       Show this help

Exit codes:
  0 success; 87 invalid arguments; 1602 cancelled; 1603 fatal error;
  1618 another lifecycle operation is active; 1638 newer version installed;
  3010 success with restart required.
)HELP";
}

HostPackage loadHostPackage(const fs::path &executablePath) {
    HostPackage package;
    package.executablePath = executablePath;
    package.executableBytes = readWholeFile(executablePath);
    package.executableSha256 =
        zanna::pkg::sha256Hex(package.executableBytes.data(), package.executableBytes.size());
    const OverlayRange overlay = locateZipOverlay(package.executableBytes);
    package.overlayOffset = overlay.offset;
    package.overlayLength = overlay.length;
    zanna::pkg::ZipReader outer(package.executableBytes.data() + overlay.offset, overlay.length);
    const zanna::pkg::ZipEntry *metadataEntry = outer.find("meta/installer-v2.txt");
    if (!metadataEntry)
        throw std::runtime_error("installer metadata entry is missing");
    const std::vector<uint8_t> metadataBytes = outer.extract(*metadataEntry);
    package.metadata = zanna::pkg::parseWindowsInstallerMetadata(bytesToString(metadataBytes));
    requirePeArchitecture(
        package.executableBytes, package.metadata.architecture, "installer executable");
    requireOuterInventory(outer, package.metadata);
    const zanna::pkg::ZipEntry *payloadEntry = outer.find(package.metadata.payloadEntry);
    if (!payloadEntry)
        throw std::runtime_error("installer payload entry is missing");
    package.payloadZip = outer.extract(*payloadEntry);
    const zanna::pkg::ZipEntry *cleanupEntry = outer.find(package.metadata.cleanupEntry);
    if (!cleanupEntry)
        throw std::runtime_error("installer detached cleanup helper is missing");
    package.cleanupBytes = outer.extract(*cleanupEntry);
    if (zanna::pkg::sha256Hex(package.cleanupBytes.data(), package.cleanupBytes.size()) !=
        package.metadata.cleanupSha256) {
        throw std::runtime_error("installer detached cleanup helper SHA-256 verification failed");
    }
    requirePeArchitecture(
        package.cleanupBytes, package.metadata.architecture, "installer cleanup helper");

    zanna::pkg::ZipReader payload(package.payloadZip.data(), package.payloadZip.size());
    if (payload.entries().size() != package.metadata.payloadFiles.size())
        throw std::runtime_error("installer payload entry count does not match metadata");
    for (const auto &file : package.metadata.payloadFiles) {
        const zanna::pkg::ZipEntry *entry = payload.find(file.path);
        if (!entry || entry->uncompressedSize != file.sizeBytes)
            throw std::runtime_error("installer payload inventory does not match metadata");
        const std::vector<uint8_t> bytes = payload.extract(*entry);
        if (zanna::pkg::sha256Hex(bytes.data(), bytes.size()) != file.sha256)
            throw std::runtime_error("installer payload SHA-256 verification failed");
    }

    if (const zanna::pkg::ZipEntry *license = outer.find(package.metadata.licenseEntry))
        package.licenseText = bytesToString(outer.extract(*license));
    if (const zanna::pkg::ZipEntry *readme = outer.find(package.metadata.readmeEntry))
        package.readmeText = bytesToString(outer.extract(*readme));
    for (const auto &file : package.metadata.outerFiles) {
        const zanna::pkg::ZipEntry *entry = outer.find(file.overlayPath);
        if (!entry || entry->uncompressedSize != file.sizeBytes)
            throw std::runtime_error(
                "installer outer-file payload is missing or has the wrong size");
        std::vector<uint8_t> bytes = outer.extract(*entry);
        if (zanna::pkg::sha256Hex(bytes.data(), bytes.size()) != file.sha256)
            throw std::runtime_error("installer outer-file SHA-256 verification failed");
        if (file.path == package.metadata.uninstallerRelativePath)
            requirePeArchitecture(bytes, package.metadata.architecture, "maintenance executable");
        package.outerFileBytes.emplace(file.overlayPath, std::move(bytes));
    }
    return package;
}

std::wstring inspectPackageJson(const HostPackage &package) {
    const auto &m = package.metadata;
    std::ostringstream out;
    out << "{\n"
        << "  \"schema_version\": " << m.schemaVersion << ",\n"
        << "  \"mode\": \"" << jsonEscape(m.packageMode) << "\",\n"
        << "  \"kind\": \"" << jsonEscape(m.productKind) << "\",\n"
        << "  \"identifier\": \"" << jsonEscape(m.identifier) << "\",\n"
        << "  \"display_name\": \"" << jsonEscape(m.displayName) << "\",\n"
        << "  \"version\": \"" << jsonEscape(m.version) << "\",\n"
        << "  \"architecture\": \"" << jsonEscape(m.architecture) << "\",\n"
        << "  \"channel\": \"" << jsonEscape(m.channel) << "\",\n"
        << "  \"default_scope\": \"" << jsonEscape(m.defaultScope) << "\",\n"
        << "  \"default_install_dir\": \"" << jsonEscape(m.defaultInstallDir) << "\",\n"
        << "  \"minimum_windows_version\": \"" << jsonEscape(m.minimumWindowsVersion) << "\",\n"
        << "  \"source_commit\": \"" << jsonEscape(m.commit) << "\",\n"
        << "  \"package_sha256\": \"" << jsonEscape(package.executableSha256) << "\",\n"
        << "  \"signed_update_discovery\": " << (!m.updateManifestUrl.empty() ? "true" : "false")
        << ",\n"
        << "  \"payload_files\": " << m.payloadFiles.size() << ",\n"
        << "  \"installed_size\": " << m.installedSizeBytes << ",\n"
        << "  \"components\": [";
    for (size_t i = 0; i < m.components.size(); ++i) {
        if (i)
            out << ", ";
        out << "\"" << jsonEscape(m.components[i].id) << "\"";
    }
    out << "]\n}\n";
    return utf8ToWide(out.str());
}

} // namespace zanna::installer
