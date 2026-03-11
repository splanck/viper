//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/zia-server/McpHandler.hpp
// Purpose: MCP protocol handler — lifecycle, tool listing, and tool dispatch.
// Key invariants:
//   - Follows MCP 2024-11-05 protocol specification
//   - All tool calls dispatch through the shared CompilerBridge
//   - Notifications (no id) are acknowledged silently (no response)
// Ownership/Lifetime:
//   - McpHandler borrows a CompilerBridge (must outlive the handler)
// Links: tools/zia-server/CompilerBridge.hpp, tools/zia-server/JsonRpc.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tools/zia-server/Json.hpp"
#include "tools/zia-server/JsonRpc.hpp"

#include <string>

namespace viper::server
{

class CompilerBridge;

/// @brief MCP protocol handler.
///
/// Handles the MCP lifecycle methods (initialize, initialized) and dispatches
/// tools/list and tools/call requests to the CompilerBridge.
///
/// ## MCP Lifecycle
///
/// 1. Client sends `initialize` → server returns capabilities + protocol version
/// 2. Client sends `initialized` (notification) → no response
/// 3. Client sends `tools/list` → server returns tool definitions
/// 4. Client sends `tools/call` → server dispatches to CompilerBridge, returns result
class McpHandler
{
  public:
    /// @brief Construct a handler that dispatches to the given bridge.
    /// @param bridge The compiler bridge (must outlive this handler).
    explicit McpHandler(CompilerBridge &bridge);

    /// @brief Handle a JSON-RPC request and return a response string.
    /// @param req The parsed JSON-RPC request.
    /// @return JSON-RPC response string, or empty string for notifications.
    std::string handleRequest(const JsonRpcRequest &req);

  private:
    CompilerBridge &bridge_;

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
    static JsonValue buildToolDefinitions();
};

} // namespace viper::server
