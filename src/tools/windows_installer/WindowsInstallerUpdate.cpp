//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/windows_installer/WindowsInstallerUpdate.cpp
// Purpose: Implement bounded HTTPS update discovery with pinned RSA signatures.
//
// Key invariants:
//   - Network redirects, oversized responses, cross-origin links, and unsigned
//     manifests are rejected before any result reaches the user.
//   - The canonical signed bytes are reconstructed from strictly ordered fields.
//   - This module never downloads or launches an installer automatically.
//
// Ownership/Lifetime:
//   - RAII wrappers close WinHTTP and CNG handles on every path.
//
// Links: WindowsInstallerUpdate.hpp, WindowsInstallerMetadata.hpp, PkgHash.hpp
//
//===----------------------------------------------------------------------===//

#include "WindowsInstallerUpdate.hpp"
#include "WindowsInstallerResources.h"

#include "PkgHash.hpp"

#include <bcrypt.h>
#include <commctrl.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace zanna::installer {
namespace {

constexpr std::size_t kMaximumManifestBytes = 64U * 1024U;
constexpr int kOpenUpdate = 2401;

class InternetHandle {
  public:
    InternetHandle() = default;

    explicit InternetHandle(HINTERNET value) : value_(value) {}

    ~InternetHandle() {
        if (value_)
            WinHttpCloseHandle(value_);
    }

    InternetHandle(const InternetHandle &) = delete;
    InternetHandle &operator=(const InternetHandle &) = delete;

    InternetHandle(InternetHandle &&other) noexcept : value_(other.value_) {
        other.value_ = nullptr;
    }

    InternetHandle &operator=(InternetHandle &&other) noexcept {
        if (this != &other) {
            if (value_)
                WinHttpCloseHandle(value_);
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }

    HINTERNET get() const {
        return value_;
    }

    explicit operator bool() const {
        return value_ != nullptr;
    }

  private:
    HINTERNET value_{nullptr};
};

class AlgorithmHandle {
  public:
    ~AlgorithmHandle() {
        if (value_)
            BCryptCloseAlgorithmProvider(value_, 0);
    }

    BCRYPT_ALG_HANDLE *put() {
        return &value_;
    }

    BCRYPT_ALG_HANDLE get() const {
        return value_;
    }

  private:
    BCRYPT_ALG_HANDLE value_{nullptr};
};

class KeyHandle {
  public:
    ~KeyHandle() {
        if (value_)
            BCryptDestroyKey(value_);
    }

    BCRYPT_KEY_HANDLE *put() {
        return &value_;
    }

    BCRYPT_KEY_HANDLE get() const {
        return value_;
    }

  private:
    BCRYPT_KEY_HANDLE value_{nullptr};
};

struct ParsedUrl {
    INTERNET_SCHEME scheme{static_cast<INTERNET_SCHEME>(0)};
    std::wstring host;
    INTERNET_PORT port{0};
    std::wstring resource;
};

std::string jsonEscape(std::string_view value) {
    std::ostringstream out;
    for (unsigned char ch : value) {
        switch (ch) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
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
                    static constexpr char kHex[] = "0123456789abcdef";
                    out << "\\u00" << kHex[ch >> 4U] << kHex[ch & 0x0fU];
                } else {
                    out << static_cast<char>(ch);
                }
                break;
        }
    }
    return out.str();
}

std::vector<uint8_t> decodeHex(std::string_view value, std::string_view field) {
    if (value.empty() || value.size() % 2U != 0U)
        throw std::runtime_error("invalid " + std::string(field));
    auto nibble = [field](char ch) -> uint8_t {
        if (ch >= '0' && ch <= '9')
            return static_cast<uint8_t>(ch - '0');
        if (ch >= 'a' && ch <= 'f')
            return static_cast<uint8_t>(ch - 'a' + 10);
        throw std::runtime_error("invalid " + std::string(field));
    };
    std::vector<uint8_t> result(value.size() / 2U);
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] =
            static_cast<uint8_t>((nibble(value[i * 2U]) << 4U) | nibble(value[i * 2U + 1U]));
    return result;
}

void validateManifestValue(std::string_view value,
                           std::string_view field,
                           bool allowEmpty = false) {
    if ((!allowEmpty && value.empty()) || value.size() > 8192U ||
        std::any_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch < 0x20U || ch == 0x7fU;
        })) {
        throw std::runtime_error("invalid update manifest " + std::string(field));
    }
}

ParsedUrl parseHttpsUrl(std::string_view utf8, std::string_view field) {
    validateManifestValue(utf8, field);
    if (utf8.find('#') != std::string_view::npos)
        throw std::runtime_error("update " + std::string(field) + " must not contain a fragment");
    const std::wstring url = utf8ToWide(utf8);
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUserNameLength = static_cast<DWORD>(-1);
    components.dwPasswordLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS || components.dwHostNameLength == 0U ||
        components.dwUserNameLength != 0U || components.dwPasswordLength != 0U) {
        throw std::runtime_error("update " + std::string(field) + " must be an HTTPS URL");
    }
    ParsedUrl result;
    result.scheme = components.nScheme;
    result.host.assign(components.lpszHostName, components.dwHostNameLength);
    std::transform(result.host.begin(), result.host.end(), result.host.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(::towlower(ch));
    });
    result.port = components.nPort;
    if (components.dwUrlPathLength != 0U)
        result.resource.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength != 0U)
        result.resource.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    if (result.resource.empty())
        result.resource = L"/";
    return result;
}

bool sameOrigin(const ParsedUrl &left, const ParsedUrl &right) {
    return left.scheme == right.scheme && left.port == right.port && left.host == right.host;
}

std::string downloadManifest(std::string_view manifestUrl, std::string_view version) {
    const ParsedUrl url = parseHttpsUrl(manifestUrl, "manifest URL");
    const std::wstring agent = L"Zanna-Installer/" + utf8ToWide(version);
    InternetHandle session(
        WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0));
    if (!session) {
        session = InternetHandle(
            WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0));
    }
    if (!session)
        throw std::runtime_error("cannot initialize secure update networking");
    if (!WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 10000))
        throw std::runtime_error("cannot configure update network timeouts");
    InternetHandle connection(WinHttpConnect(session.get(), url.host.c_str(), url.port, 0));
    if (!connection)
        throw std::runtime_error("cannot connect to the update service");
    LPCWSTR accept[] = {L"text/plain", nullptr};
    InternetHandle request(WinHttpOpenRequest(connection.get(),
                                              L"GET",
                                              url.resource.c_str(),
                                              nullptr,
                                              WINHTTP_NO_REFERER,
                                              accept,
                                              WINHTTP_FLAG_SECURE));
    if (!request)
        throw std::runtime_error("cannot create the secure update request");
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.get(),
                          WINHTTP_OPTION_REDIRECT_POLICY,
                          &redirectPolicy,
                          sizeof(redirectPolicy))) {
        throw std::runtime_error("cannot disable update-service redirects");
    }
    if (!WinHttpSendRequest(request.get(),
                            L"Accept: text/plain\r\nCache-Control: no-cache\r\n",
                            static_cast<DWORD>(-1),
                            WINHTTP_NO_REQUEST_DATA,
                            0,
                            0,
                            0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error("the update service request failed");
    }
    DWORD status = 0;
    DWORD statusBytes = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(),
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &status,
                             &statusBytes,
                             WINHTTP_NO_HEADER_INDEX) ||
        status != 200U) {
        throw std::runtime_error("the update service returned HTTP status " +
                                 std::to_string(status));
    }
    DWORD contentLength = 0;
    DWORD lengthBytes = sizeof(contentLength);
    if (WinHttpQueryHeaders(request.get(),
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &contentLength,
                            &lengthBytes,
                            WINHTTP_NO_HEADER_INDEX) &&
        contentLength > kMaximumManifestBytes) {
        throw std::runtime_error("the update manifest exceeds the 64 KiB safety limit");
    }
    std::string body;
    if (contentLength != 0U)
        body.reserve(contentLength);
    std::array<char, 4096> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(
                request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read))
            throw std::runtime_error("cannot read the update manifest");
        if (read == 0U)
            break;
        if (read > kMaximumManifestBytes - body.size())
            throw std::runtime_error("the update manifest exceeds the 64 KiB safety limit");
        body.append(buffer.data(), read);
    }
    if (body.empty())
        throw std::runtime_error("the update service returned an empty manifest");
    return body;
}

std::string fieldValue(const std::string &line, std::string_view name, bool allowEmpty = false) {
    const std::string prefix = std::string(name) + "\t";
    if (line.rfind(prefix, 0) != 0 || line.find('\t', prefix.size()) != std::string::npos)
        throw std::runtime_error("malformed update manifest field " + std::string(name));
    std::string value = line.substr(prefix.size());
    validateManifestValue(value, name, allowEmpty);
    return value;
}

void verifySignature(const zanna::pkg::WindowsInstallerMetadata &metadata,
                     std::string_view canonical,
                     std::string_view signatureHex) {
    const std::vector<uint8_t> modulus = decodeHex(metadata.updateRsaModulus, "update RSA modulus");
    const std::vector<uint8_t> exponent =
        decodeHex(metadata.updateRsaExponent, "update RSA exponent");
    const std::vector<uint8_t> signature = decodeHex(signatureHex, "update manifest signature");
    if (signature.size() != modulus.size())
        throw std::runtime_error("update manifest signature size does not match its pinned key");
    const std::string hashHex = zanna::pkg::sha256Hex(
        reinterpret_cast<const uint8_t *>(canonical.data()), canonical.size());
    const std::vector<uint8_t> hash = decodeHex(hashHex, "update manifest digest");

    BCRYPT_RSAKEY_BLOB header{};
    header.Magic = BCRYPT_RSAPUBLIC_MAGIC;
    header.BitLength = static_cast<ULONG>(modulus.size() * 8U);
    header.cbPublicExp = static_cast<ULONG>(exponent.size());
    header.cbModulus = static_cast<ULONG>(modulus.size());
    std::vector<uint8_t> blob(sizeof(header) + exponent.size() + modulus.size());
    std::memcpy(blob.data(), &header, sizeof(header));
    std::memcpy(blob.data() + sizeof(header), exponent.data(), exponent.size());
    std::memcpy(blob.data() + sizeof(header) + exponent.size(), modulus.data(), modulus.size());

    AlgorithmHandle algorithm;
    if (BCryptOpenAlgorithmProvider(algorithm.put(), BCRYPT_RSA_ALGORITHM, nullptr, 0) != 0)
        throw std::runtime_error("cannot initialize update signature verification");
    KeyHandle key;
    if (BCryptImportKeyPair(algorithm.get(),
                            nullptr,
                            BCRYPT_RSAPUBLIC_BLOB,
                            key.put(),
                            blob.data(),
                            static_cast<ULONG>(blob.size()),
                            0) != 0) {
        throw std::runtime_error("cannot import the pinned update public key");
    }
    BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
    if (BCryptVerifySignature(key.get(),
                              &padding,
                              const_cast<PUCHAR>(hash.data()),
                              static_cast<ULONG>(hash.size()),
                              const_cast<PUCHAR>(signature.data()),
                              static_cast<ULONG>(signature.size()),
                              BCRYPT_PAD_PKCS1) != 0) {
        throw std::runtime_error("update manifest RSA-SHA256 signature verification failed");
    }
}

} // namespace

UpdateCheckResult verifyUpdateManifest(const HostPackage &package, std::string_view manifestText) {
    if (package.metadata.updateManifestUrl.empty() || package.metadata.updateRsaModulus.empty() ||
        package.metadata.updateRsaExponent.empty()) {
        return {UpdateStatus::Unconfigured,
                package.metadata.version,
                {},
                package.metadata.channel,
                package.metadata.architecture,
                {},
                {},
                {}};
    }
    if (manifestText.empty() || manifestText.size() > kMaximumManifestBytes ||
        manifestText.find('\0') != std::string_view::npos) {
        throw std::runtime_error("invalid update manifest size or encoding");
    }
    std::istringstream input{std::string(manifestText)};
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    if (lines.size() != 8U || lines[0] != "ZANNA-WINDOWS-UPDATE\t1")
        throw std::runtime_error("unsupported or malformed Zanna update manifest");

    UpdateCheckResult result;
    result.currentVersion = package.metadata.version;
    result.channel = fieldValue(lines[1], "channel");
    result.architecture = fieldValue(lines[2], "architecture");
    result.availableVersion = fieldValue(lines[3], "version");
    result.downloadUrl = fieldValue(lines[4], "download-url");
    result.downloadSha256 = fieldValue(lines[5], "sha256");
    result.releaseNotesUrl = fieldValue(lines[6], "release-notes-url", true);
    const std::string signature = fieldValue(lines[7], "signature");
    if (result.channel != package.metadata.channel)
        throw std::runtime_error("update manifest channel does not match this installer");
    if (result.architecture != package.metadata.architecture)
        throw std::runtime_error("update manifest architecture does not match this installer");
    const std::vector<uint8_t> digest = decodeHex(result.downloadSha256, "download SHA-256");
    if (digest.size() != 32U)
        throw std::runtime_error("update manifest download SHA-256 must contain 32 bytes");

    const ParsedUrl manifestUrl = parseHttpsUrl(package.metadata.updateManifestUrl, "manifest URL");
    const ParsedUrl downloadUrl = parseHttpsUrl(result.downloadUrl, "download URL");
    if (!sameOrigin(manifestUrl, downloadUrl))
        throw std::runtime_error("update download URL must use the pinned manifest origin");
    if (!result.releaseNotesUrl.empty()) {
        const ParsedUrl notesUrl = parseHttpsUrl(result.releaseNotesUrl, "release notes URL");
        if (!sameOrigin(manifestUrl, notesUrl))
            throw std::runtime_error(
                "update release-notes URL must use the pinned manifest origin");
    }

    std::string canonical;
    for (std::size_t index = 0; index < 7U; ++index) {
        canonical += lines[index];
        canonical.push_back('\n');
    }
    verifySignature(package.metadata, canonical, signature);
    result.status = compareInstallerVersions(result.availableVersion, result.currentVersion) > 0
                        ? UpdateStatus::Available
                        : UpdateStatus::Current;
    return result;
}

UpdateCheckResult checkForUpdates(const HostPackage &package) {
    if (package.metadata.updateManifestUrl.empty())
        return verifyUpdateManifest(package, {});
    return verifyUpdateManifest(
        package, downloadManifest(package.metadata.updateManifestUrl, package.metadata.version));
}

std::wstring updateResultJson(const UpdateCheckResult &result) {
    const char *status =
        result.status == UpdateStatus::Available
            ? "available"
            : (result.status == UpdateStatus::Current ? "current" : "unconfigured");
    std::ostringstream out;
    out << "{\n"
        << "  \"status\": \"" << status << "\",\n"
        << "  \"current_version\": \"" << jsonEscape(result.currentVersion) << "\",\n"
        << "  \"available_version\": \"" << jsonEscape(result.availableVersion) << "\",\n"
        << "  \"channel\": \"" << jsonEscape(result.channel) << "\",\n"
        << "  \"architecture\": \"" << jsonEscape(result.architecture) << "\",\n"
        << "  \"download_url\": \"" << jsonEscape(result.downloadUrl) << "\",\n"
        << "  \"sha256\": \"" << jsonEscape(result.downloadSha256) << "\"\n"
        << "}\n";
    return utf8ToWide(out.str());
}

void showUpdateResult(HINSTANCE instance,
                      const HostPackage &package,
                      const UpdateCheckResult &result) {
    std::wstring instruction;
    std::wstring content;
    std::array<TASKDIALOG_BUTTON, 2> buttons{};
    UINT buttonCount = 0;
    if (result.status == UpdateStatus::Unconfigured) {
        instruction = L"Update discovery is not configured";
        content = L"This development package has no pinned signed update service.";
    } else if (result.status == UpdateStatus::Current) {
        instruction = L"Zanna is up to date";
        content = L"Installed/package version: " + utf8ToWide(result.currentVersion) +
                  L"\r\nChannel: " + utf8ToWide(result.channel) + L"  |  " +
                  utf8ToWide(result.architecture);
    } else {
        instruction = L"A newer Zanna version is available";
        content = L"Current: " + utf8ToWide(result.currentVersion) + L"\r\nAvailable: " +
                  utf8ToWide(result.availableVersion) + L"\r\nChannel: " +
                  utf8ToWide(result.channel) + L"  |  " + utf8ToWide(result.architecture) +
                  L"\r\n\r\nThe release link was authenticated by the public key pinned in this "
                  L"installer.";
        buttons[buttonCount++] = {kOpenUpdate, L"Open authenticated release page"};
    }
    buttons[buttonCount++] = {IDCLOSE, L"Close"};
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hInstance = instance;
    const std::wstring title = utf8ToWide(package.metadata.displayName) + L" Update";
    config.pszWindowTitle = title.c_str();
    config.pszMainInstruction = instruction.c_str();
    config.pszContent = content.c_str();
    config.dwFlags = TDF_SIZE_TO_CONTENT | TDF_USE_COMMAND_LINKS | TDF_USE_HICON_MAIN;
    config.hMainIcon = static_cast<HICON>(LoadImageW(instance,
                                                     MAKEINTRESOURCEW(IDI_ZANNA_INSTALLER),
                                                     IMAGE_ICON,
                                                     0,
                                                     0,
                                                     LR_DEFAULTSIZE | LR_SHARED));
    config.cButtons = buttonCount;
    config.pButtons = buttons.data();
    config.nDefaultButton = result.status == UpdateStatus::Available ? kOpenUpdate : IDCLOSE;
    int selected = IDCLOSE;
    if (FAILED(TaskDialogIndirect(&config, &selected, nullptr, nullptr)))
        throw std::runtime_error("cannot display the Zanna update result");
    if (selected == kOpenUpdate) {
        const std::wstring url = utf8ToWide(
            result.releaseNotesUrl.empty() ? result.downloadUrl : result.releaseNotesUrl);
        const INT_PTR launched = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (launched <= 32)
            throw std::runtime_error("cannot open the authenticated Zanna release URL");
    }
}

} // namespace zanna::installer
