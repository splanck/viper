//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/JsonRpc.hpp
// Purpose: JSON-RPC 2.0 request/response types and helpers.
// Key invariants:
//   - Conforms to JSON-RPC 2.0 specification
//   - Request IDs can be string, int, or null (notification)
//   - Standard error codes from -32700 to -32603
// Ownership/Lifetime:
//   - All types are value types (owned data)
// Links: tools/lsp-common/Json.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tools/lsp-common/Json.hpp"

#include <string>

namespace viper::server
{

/// @brief Standard JSON-RPC 2.0 error codes.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;

/// @brief A parsed JSON-RPC 2.0 request or notification.
struct JsonRpcRequest
{
    std::string method; ///< Method name (e.g., "initialize", "tools/call")
    JsonValue params;   ///< Parameters (may be null, object, or array)
    JsonValue id;       ///< Request ID (string, int, or null for notifications)

    /// @brief True if this is a notification (no id, no response expected).
    [[nodiscard]] bool isNotification() const
    {
        return id.isNull();
    }
};

/// @brief Parse a JSON-RPC 2.0 message from a JSON value.
/// @param msg The parsed JSON message.
/// @param out Populated with the parsed request.
/// @return true if the message is a valid JSON-RPC 2.0 request/notification.
bool parseRequest(const JsonValue &msg, JsonRpcRequest &out);

/// @brief Build a JSON-RPC 2.0 success response.
/// @param id The request ID to echo back.
/// @param result The result payload.
/// @return Compact JSON string for the response.
std::string buildResponse(const JsonValue &id, const JsonValue &result);

/// @brief Build a JSON-RPC 2.0 error response.
/// @param id The request ID to echo back (may be null).
/// @param code Error code (use kParseError, kMethodNotFound, etc.).
/// @param message Human-readable error message.
/// @param data Optional additional error data.
/// @return Compact JSON string for the error response.
std::string buildError(const JsonValue &id,
                       int code,
                       const std::string &message,
                       const JsonValue &data = JsonValue());

/// @brief Build a JSON-RPC 2.0 notification (no id, no response expected).
/// @param method The notification method name.
/// @param params The notification parameters.
/// @return Compact JSON string for the notification.
std::string buildNotification(const std::string &method, const JsonValue &params);

} // namespace viper::server
