//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/LinuxRuntimeStubGen.cpp
// Purpose: Generate the Linux self-extracting AppImage runtime stub.
//
// Key invariants:
//   - The shell runtime is POSIX sh and requires only standard base system tools.
//   - Payload bytes are never interpreted by the shell; they are streamed to tar.
//   - The extracted entry point is executed with the caller's argv.
//
// Ownership/Lifetime:
//   - Pure byte generation and structural verification helpers.
//
// Links: LinuxRuntimeStubGen.hpp
//
//===----------------------------------------------------------------------===//

#include "LinuxRuntimeStubGen.hpp"
#include "PkgUtils.hpp"

#include <algorithm>
#include <sstream>

namespace viper::pkg {
namespace {

std::string shellSingleQuote(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (char c : value) {
        if (c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

void validateStubToken(const std::string &value, const char *fieldName) {
    validateSingleLineField(value, fieldName);
    const std::string clean = sanitizePackageRelativePath(value, fieldName);
    if (clean != value)
        throw std::runtime_error(std::string(fieldName) + " was not normalized: " + value);
}

std::vector<uint8_t>::const_iterator payloadMarkerIt(const std::vector<uint8_t> &data) {
    const std::string marker = std::string(kLinuxRuntimePayloadMarker) + "\n";
    return std::search(data.begin(),
                       data.end(),
                       reinterpret_cast<const uint8_t *>(marker.data()),
                       reinterpret_cast<const uint8_t *>(marker.data()) + marker.size());
}

} // namespace

std::vector<uint8_t> buildLinuxRuntimeStub(const LinuxRuntimeStubParams &params) {
    validateStubToken(params.cacheName, "AppImage cache name");
    validateStubToken(params.entryPath, "AppImage entry path");

    std::ostringstream script;
    script << "#!/bin/sh\n"
              "set -eu\n"
              "self=$0\n"
              "cache_name="
           << shellSingleQuote(params.cacheName)
           << "\n"
              "entry_path="
           << shellSingleQuote(params.entryPath)
           << "\n"
              "case \"$entry_path\" in /*|..|../*|*/../*|*/..) echo \"Unsafe AppImage entry "
                 "path: $entry_path\" >&2; exit 2 ;; esac\n"
              "cache_root=${XDG_CACHE_HOME:-${TMPDIR:-/tmp}}\n"
              "extract_dir=$cache_root/$cache_name\n"
              "payload_line=$(awk '/^"
           << kLinuxRuntimePayloadMarker
           << "$/ { print NR + 1; exit }' \"$self\")\n"
              "if [ -z \"$payload_line\" ]; then echo \"AppImage payload marker not found\" >&2; "
                 "exit 2; fi\n"
              "mkdir -p \"$extract_dir\"\n"
              "if [ ! -x \"$extract_dir/$entry_path\" ]; then\n"
              "    tmp_dir=$extract_dir.tmp.$$\n"
              "    rm -rf \"$tmp_dir\"\n"
              "    mkdir -p \"$tmp_dir\"\n"
              "    tail -n +$payload_line \"$self\" | (cd \"$tmp_dir\" && tar xzf -)\n"
              "    chmod +x \"$tmp_dir/$entry_path\" 2>/dev/null || true\n"
              "    rm -rf \"$extract_dir.old.$$\"\n"
              "    if [ -e \"$extract_dir\" ]; then mv \"$extract_dir\" \"$extract_dir.old.$$\"; "
                 "fi\n"
              "    mv \"$tmp_dir\" \"$extract_dir\"\n"
              "    rm -rf \"$extract_dir.old.$$\"\n"
              "fi\n"
              "exec \"$extract_dir/$entry_path\" \"$@\"\n"
           << kLinuxRuntimePayloadMarker << "\n";
    const std::string text = script.str();
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> buildLinuxAppImage(const LinuxRuntimeStubParams &params,
                                        const std::vector<uint8_t> &payloadTarGz) {
    if (payloadTarGz.size() < 2 || payloadTarGz[0] != 0x1F || payloadTarGz[1] != 0x8B)
        throw std::runtime_error("AppImage payload must be a gzip-compressed tar archive");
    std::vector<uint8_t> out = buildLinuxRuntimeStub(params);
    out.insert(out.end(), payloadTarGz.begin(), payloadTarGz.end());
    return out;
}

bool verifyLinuxAppImage(const std::vector<uint8_t> &data, std::string *err) {
    if (data.size() < 4 || data[0] != '#' || data[1] != '!') {
        if (err)
            *err = "AppImage: missing executable runtime stub\n";
        return false;
    }
    const auto marker = payloadMarkerIt(data);
    if (marker == data.end()) {
        if (err)
            *err = "AppImage: missing payload marker\n";
        return false;
    }
    const std::string markerText = std::string(kLinuxRuntimePayloadMarker) + "\n";
    const auto payload = marker + static_cast<std::ptrdiff_t>(markerText.size());
    if (payload == data.end() || static_cast<size_t>(data.end() - payload) < 2u ||
        payload[0] != 0x1F || payload[1] != 0x8B) {
        if (err)
            *err = "AppImage: appended payload is not gzip data\n";
        return false;
    }
    return true;
}

} // namespace viper::pkg
