//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/McpHandler.hpp
// Purpose: MCP protocol handler — lifecycle, tool listing, and tool dispatch.
// Key invariants:
//   - Follows MCP 2024-11-05 protocol specification
//   - All tool calls dispatch through the shared ICompilerBridge
//   - Notifications (no id) are acknowledged silently (no response)
//   - Language-specific strings come from ServerConfig
// Ownership/Lifetime:
//   - McpHandler borrows an ICompilerBridge (must outlive the handler)
// Links: tools/lsp-common/ICompilerBridge.hpp, tools/lsp-common/JsonRpc.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tools/lsp-common/ICompilerBridge.hpp"
#include "tools/lsp-common/Json.hpp"
#include "tools/lsp-common/JsonRpc.hpp"

#include <optional>
#include <string>

namespace zanna::server {

/// @brief MCP protocol handler.
///
/// Handles the MCP lifecycle methods (initialize, initialized) and dispatches
/// tools/list and tools/call requests to the ICompilerBridge.
///
/// ## MCP Lifecycle
///
/// 1. Client sends `initialize` → server returns capabilities + protocol version
/// 2. Client sends `initialized` (notification) → no response
/// 3. Client sends `tools/list` → server returns tool definitions
/// 4. Client sends `tools/call` → server dispatches to ICompilerBridge, returns result
class McpHandler {
  public:
    /// @brief Construct a handler that dispatches to the given bridge.
    /// @param bridge The compiler bridge (must outlive this handler).
    /// @param config Server configuration for language-specific strings.
    McpHandler(ICompilerBridge &bridge, const ServerConfig &config);

    /// @brief Handle a JSON-RPC request and return a response string.
    /// @param req The parsed JSON-RPC request.
    /// @return JSON-RPC response string, or empty string for notifications.
    std::string handleRequest(const JsonRpcRequest &req);

  private:
    ICompilerBridge &bridge_;
    ServerConfig config_;
    bool initializeResponded_{false};
    bool initialized_{false};

    /// @brief Handle `initialize`: return protocol version and server capabilities.
    std::string handleInitialize(const JsonRpcRequest &req);
    /// @brief Handle `tools/list`: return the available tool definitions.
    std::string handleToolsList(const JsonRpcRequest &req);
    /// @brief Handle `tools/call`: validate args and dispatch to the matching callXxx.
    std::string handleToolsCall(const JsonRpcRequest &req);

    /// @brief Result payload for one MCP tool call.
    /// @details `content` is always populated for compatibility with clients that
    ///          only display text blocks. `structuredContent` is populated when the
    ///          tool's natural result is JSON data that MCP-aware agents can consume
    ///          without reparsing a text string.
    struct ToolCallResult {
        JsonValue content;                          ///< MCP `content` array.
        std::optional<JsonValue> structuredContent; ///< Optional MCP structured payload.
    };

    // Tool dispatch helpers.
    ToolCallResult callCheck(const JsonValue &args);
    ToolCallResult callCompile(const JsonValue &args);
    ToolCallResult callCompletions(const JsonValue &args);
    ToolCallResult callHover(const JsonValue &args);
    ToolCallResult callSymbols(const JsonValue &args);
    ToolCallResult callDumpIL(const JsonValue &args);
    ToolCallResult callDumpAst(const JsonValue &args);
    ToolCallResult callDumpTokens(const JsonValue &args);
    ToolCallResult callRuntimeClasses(const JsonValue &args);
    ToolCallResult callRuntimeMembers(const JsonValue &args);
    ToolCallResult callRuntimeSearch(const JsonValue &args);

    /// @brief Build the tool definitions for tools/list response.
    JsonValue buildToolDefinitions() const;
};

} // namespace zanna::server
