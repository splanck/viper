//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include <string>

namespace viper::server {

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

    std::string handleInitialize(const JsonRpcRequest &req);
    std::string handleToolsList(const JsonRpcRequest &req);
    std::string handleToolsCall(const JsonRpcRequest &req);

    // Tool dispatch helpers — each returns the "content" array for the MCP response
    JsonValue callCheck(const JsonValue &args);
    JsonValue callCompile(const JsonValue &args);
    JsonValue callCompletions(const JsonValue &args);
    JsonValue callHover(const JsonValue &args);
    JsonValue callSymbols(const JsonValue &args);
    JsonValue callDumpIL(const JsonValue &args);
    JsonValue callDumpAst(const JsonValue &args);
    JsonValue callDumpTokens(const JsonValue &args);
    JsonValue callRuntimeClasses(const JsonValue &args);
    JsonValue callRuntimeMembers(const JsonValue &args);
    JsonValue callRuntimeSearch(const JsonValue &args);

    /// @brief Build the tool definitions for tools/list response.
    JsonValue buildToolDefinitions() const;
};

} // namespace viper::server
