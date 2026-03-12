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

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace viper::server
{

// --- Platform init ---

void platformInitStdio()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

// --- McpTransport ---

McpTransport::McpTransport(FILE *in, FILE *out) : in_(in), out_(out) {}

bool McpTransport::readMessage(RawMessage &out)
{
    std::string line;
    int c;
    while ((c = std::fgetc(in_)) != EOF)
    {
        if (c == '\n')
        {
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
    }
    // Handle last line without trailing newline
    if (!line.empty())
    {
        if (line.back() == '\r')
            line.pop_back();
        out.content = std::move(line);
        return true;
    }
    return false;
}

void McpTransport::writeMessage(const std::string &json)
{
    std::fwrite(json.data(), 1, json.size(), out_);
    std::fputc('\n', out_);
    std::fflush(out_);
}

// --- LspTransport ---

LspTransport::LspTransport(FILE *in, FILE *out) : in_(in), out_(out) {}

bool LspTransport::readLine(std::string &line)
{
    line.clear();
    int c;
    while ((c = std::fgetc(in_)) != EOF)
    {
        if (c == '\n')
        {
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        }
        line += static_cast<char>(c);
    }
    return !line.empty();
}

bool LspTransport::readMessage(RawMessage &out)
{
    // Read headers until empty line
    int contentLength = -1;
    std::string line;

    while (readLine(line))
    {
        if (line.empty())
            break; // End of headers

        // Parse Content-Length header (case-insensitive prefix match)
        const char *prefix = "Content-Length:";
        size_t prefixLen = std::strlen(prefix);
        if (line.size() > prefixLen)
        {
            // Case-insensitive comparison of the prefix
            bool match = true;
            for (size_t i = 0; i < prefixLen; ++i)
            {
                if (std::tolower(static_cast<unsigned char>(line[i])) !=
                    std::tolower(static_cast<unsigned char>(prefix[i])))
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                // Skip whitespace after colon
                size_t valStart = prefixLen;
                while (valStart < line.size() && line[valStart] == ' ')
                    ++valStart;
                contentLength = std::atoi(line.c_str() + valStart);
            }
        }
    }

    if (contentLength <= 0)
        return false;

    // Read exactly contentLength bytes
    out.content.resize(static_cast<size_t>(contentLength));
    size_t bytesRead = std::fread(&out.content[0], 1, static_cast<size_t>(contentLength), in_);
    if (bytesRead != static_cast<size_t>(contentLength))
        return false;

    return true;
}

void LspTransport::writeMessage(const std::string &json)
{
    char header[64];
    int headerLen =
        std::snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", json.size());
    std::fwrite(header, 1, static_cast<size_t>(headerLen), out_);
    std::fwrite(json.data(), 1, json.size(), out_);
    std::fflush(out_);
}

} // namespace viper::server
