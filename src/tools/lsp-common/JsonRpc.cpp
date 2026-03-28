//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/JsonRpc.cpp
// Purpose: JSON-RPC 2.0 message parsing and construction.
// Key invariants:
//   - All responses include "jsonrpc": "2.0"
//   - Notifications have no "id" field in the output
//   - Error responses have {code, message, data?} in the "error" field
// Ownership/Lifetime:
//   - All functions return owned strings
// Links: tools/lsp-common/JsonRpc.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/JsonRpc.hpp"

namespace viper::server {

bool parseRequest(const JsonValue &msg, JsonRpcRequest &out) {
    if (msg.type() != JsonType::Object)
        return false;

    // Method is required
    const auto *method = msg.get("method");
    if (!method || method->type() != JsonType::String)
        return false;
    out.method = method->asString();

    // Params is optional (default to null)
    const auto *params = msg.get("params");
    out.params = params ? *params : JsonValue();

    // ID is optional (null = notification)
    const auto *id = msg.get("id");
    out.id = id ? *id : JsonValue();

    return true;
}

std::string buildResponse(const JsonValue &id, const JsonValue &result) {
    auto response = JsonValue::object({
        {"jsonrpc", JsonValue("2.0")},
        {"id", id},
        {"result", result},
    });
    return response.toCompactString();
}

std::string buildError(const JsonValue &id,
                       int code,
                       const std::string &message,
                       const JsonValue &data) {
    JsonValue::ObjectType errObj;
    errObj.emplace_back("code", JsonValue(static_cast<int64_t>(code)));
    errObj.emplace_back("message", JsonValue(message));
    if (!data.isNull())
        errObj.emplace_back("data", data);

    auto response = JsonValue::object({
        {"jsonrpc", JsonValue("2.0")},
        {"id", id},
        {"error", JsonValue(std::move(errObj))},
    });
    return response.toCompactString();
}

std::string buildNotification(const std::string &method, const JsonValue &params) {
    auto notification = JsonValue::object({
        {"jsonrpc", JsonValue("2.0")},
        {"method", JsonValue(method)},
        {"params", params},
    });
    return notification.toCompactString();
}

} // namespace viper::server
