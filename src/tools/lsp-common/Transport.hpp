//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/Transport.hpp
// Purpose: Message transport layer for MCP and LSP protocols over stdio.
// Key invariants:
//   - McpTransport: newline-delimited JSON (one JSON object per line)
//   - LspTransport: Content-Length framed messages per LSP spec
//   - Both read from FILE* for binary-safe I/O on Windows
// Ownership/Lifetime:
//   - Transports do not own the FILE* handles (stdin/stdout are global)
// Links: tools/lsp-common/Json.hpp, tools/lsp-common/JsonRpc.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdio>
#include <string>

namespace viper::server
{

/// @brief A raw message read from or written to the transport.
struct RawMessage
{
    std::string content; ///< JSON string content
};

/// @brief Abstract base for reading/writing protocol messages over stdio.
class Transport
{
  public:
    virtual ~Transport() = default;

    /// @brief Read the next message from the transport.
    /// @param out Populated with the message content on success.
    /// @return true if a message was read, false on EOF or error.
    virtual bool readMessage(RawMessage &out) = 0;

    /// @brief Write a message to the transport.
    /// @param json The JSON string to send.
    virtual void writeMessage(const std::string &json) = 0;
};

/// @brief MCP transport: newline-delimited JSON over stdin/stdout.
///
/// Each message is a single JSON object on one line, terminated by newline.
/// Handles both \n and \r\n line endings on read.
class McpTransport : public Transport
{
  public:
    McpTransport(FILE *in, FILE *out);
    bool readMessage(RawMessage &out) override;
    void writeMessage(const std::string &json) override;

  private:
    FILE *in_;
    FILE *out_;
};

/// @brief LSP transport: Content-Length framed messages over stdin/stdout.
///
/// Messages are framed with HTTP-style headers:
///   Content-Length: <length>\r\n
///   \r\n
///   <json-body>
class LspTransport : public Transport
{
  public:
    LspTransport(FILE *in, FILE *out);
    bool readMessage(RawMessage &out) override;
    void writeMessage(const std::string &json) override;

  private:
    FILE *in_;
    FILE *out_;

    /// @brief Read a single line terminated by \r\n from the input.
    /// @return The line without the trailing \r\n, or empty string on EOF.
    bool readLine(std::string &line);
};

/// @brief Set stdin/stdout to binary mode on Windows (no-op on Unix).
void platformInitStdio();

} // namespace viper::server
