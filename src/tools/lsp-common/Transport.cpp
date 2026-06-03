//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/Transport.cpp
// Purpose: MCP and LSP transport implementations over stdio.
// Key invariants:
//   - MCP: one JSON line per message, \n terminated
//   - LSP: Content-Length header + \r\n\r\n + body
//   - Binary mode on Windows to prevent CR/LF corruption
// Ownership/Lifetime:
//   - FILE* handles are not owned (stdin/stdout are global)
// Links: tools/lsp-common/Transport.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/Transport.hpp"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace viper::server {
namespace {

constexpr size_t kMaxProtocolMessageBytes = 16u * 1024u * 1024u;
constexpr size_t kMaxProtocolLineBytes = 64u * 1024u;
constexpr size_t kMaxProtocolHeaders = 64u;

/// @brief Write @p size bytes to @p out, throwing if the write is short.
/// @details No-op for zero-length input; otherwise a partial fwrite raises a
///          std::runtime_error so callers never silently emit a truncated message.
void checkedWrite(FILE *out, const char *data, size_t size) {
    if (size == 0)
        return;
    if (std::fwrite(data, 1, size, out) != size)
        throw std::runtime_error("protocol write failed");
}

} // namespace

// --- Platform init ---

void platformInitStdio() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

// --- McpTransport ---

McpTransport::McpTransport(FILE *in, FILE *out) : in_(in), out_(out) {}

bool McpTransport::readMessage(RawMessage &out) {
    std::string line;
    int c;
    while ((c = std::fgetc(in_)) != EOF) {
        if (c == '\n') {
            // Strip trailing \r if present
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            // Skip empty lines
            if (line.empty())
                continue;
            out.content = std::move(line);
            return true;
        }
        line += static_cast<char>(c);
        if (line.size() > kMaxProtocolMessageBytes)
            return false;
    }
    // Handle last line without trailing newline
    if (!line.empty()) {
        if (line.back() == '\r')
            line.pop_back();
        out.content = std::move(line);
        return true;
    }
    return false;
}

void McpTransport::writeMessage(const std::string &json) {
    checkedWrite(out_, json.data(), json.size());
    if (std::fputc('\n', out_) == EOF)
        throw std::runtime_error("protocol write failed");
    if (std::fflush(out_) != 0)
        throw std::runtime_error("protocol flush failed");
}

// --- LspTransport ---

LspTransport::LspTransport(FILE *in, FILE *out) : in_(in), out_(out) {}

bool LspTransport::lastReadFailedDueToError() const {
    return lastReadHadError_;
}

bool LspTransport::readLine(std::string &line) {
    line.clear();
    int c;
    while ((c = std::fgetc(in_)) != EOF) {
        if (c == '\n') {
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        }
        line += static_cast<char>(c);
        if (line.size() > kMaxProtocolLineBytes) {
            lastReadHadError_ = true;
            return false;
        }
    }
    return !line.empty();
}

bool LspTransport::readMessage(RawMessage &out) {
    lastReadHadError_ = false;

    // Read headers until empty line
    size_t contentLength = 0;
    bool haveContentLength = false;
    size_t headerCount = 0;
    std::string line;

    while (readLine(line)) {
        if (line.empty())
            break; // End of headers
        if (++headerCount > kMaxProtocolHeaders) {
            lastReadHadError_ = true;
            return false;
        }

        // Parse Content-Length header (case-insensitive prefix match)
        const char *prefix = "Content-Length:";
        size_t prefixLen = std::strlen(prefix);
        if (line.size() >= prefixLen) {
            // Case-insensitive comparison of the prefix
            bool match = true;
            for (size_t i = 0; i < prefixLen; ++i) {
                if (std::tolower(static_cast<unsigned char>(line[i])) !=
                    std::tolower(static_cast<unsigned char>(prefix[i]))) {
                    match = false;
                    break;
                }
            }
            if (match) {
                // Skip whitespace after colon
                size_t valStart = prefixLen;
                while (valStart < line.size() &&
                       (line[valStart] == ' ' || line[valStart] == '\t'))
                    ++valStart;
                size_t parsed = 0;
                const char *begin = line.data() + valStart;
                const char *end = line.data() + line.size();
                auto [ptr, ec] = std::from_chars(begin, end, parsed);
                if (ec != std::errc{} || ptr != end || parsed == 0 ||
                    parsed > kMaxProtocolMessageBytes) {
                    lastReadHadError_ = true;
                    return false;
                }
                contentLength = parsed;
                haveContentLength = true;
            }
        }
    }

    if (!haveContentLength) {
        if (headerCount != 0 || !line.empty())
            lastReadHadError_ = true;
        return false;
    }

    // Read exactly contentLength bytes
    out.content.resize(contentLength);
    size_t bytesRead = std::fread(&out.content[0], 1, contentLength, in_);
    if (bytesRead != contentLength) {
        lastReadHadError_ = true;
        return false;
    }

    return true;
}

void LspTransport::writeMessage(const std::string &json) {
    char header[64];
    int headerLen =
        std::snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", json.size());
    if (headerLen <= 0 || static_cast<size_t>(headerLen) >= sizeof(header))
        throw std::runtime_error("protocol header formatting failed");
    checkedWrite(out_, header, static_cast<size_t>(headerLen));
    checkedWrite(out_, json.data(), json.size());
    if (std::fflush(out_) != 0)
        throw std::runtime_error("protocol flush failed");
}

} // namespace viper::server
