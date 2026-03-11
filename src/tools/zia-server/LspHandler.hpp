//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/zia-server/LspHandler.hpp
// Purpose: LSP protocol handler — capability negotiation and request dispatch.
// Key invariants:
//   - Follows LSP 3.17 specification
//   - Manages document state via DocumentStore
//   - Publishes diagnostics asynchronously via Transport notifications
// Ownership/Lifetime:
//   - LspHandler borrows CompilerBridge and Transport (must outlive handler)
//   - Owns the DocumentStore
// Links: tools/zia-server/CompilerBridge.hpp, tools/zia-server/DocumentStore.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tools/zia-server/DocumentStore.hpp"
#include "tools/zia-server/Json.hpp"
#include "tools/zia-server/JsonRpc.hpp"

#include <string>

namespace viper::server
{

class CompilerBridge;
class Transport;

/// @brief LSP protocol handler.
///
/// Handles the LSP lifecycle (initialize, shutdown, exit) and request/notification
/// dispatch for editor features (completion, hover, diagnostics, document symbols).
///
/// ## Capabilities
///
/// - textDocumentSync: Full (1) — client sends complete text on change
/// - completionProvider: trigger on "."
/// - hoverProvider: true
/// - documentSymbolProvider: true
///
/// ## Diagnostic Publishing
///
/// Diagnostics are published via notifications on didOpen/didChange.
/// The handler uses the Transport reference to send publishDiagnostics
/// notifications outside of the normal request/response flow.
class LspHandler
{
  public:
    /// @brief Construct a handler that dispatches to the given bridge.
    /// @param bridge The compiler bridge (must outlive this handler).
    /// @param transport The transport for sending notifications (must outlive this handler).
    LspHandler(CompilerBridge &bridge, Transport &transport);

    /// @brief Handle a JSON-RPC request and return a response string.
    /// @param req The parsed JSON-RPC request.
    /// @return JSON-RPC response string, or empty string for notifications.
    std::string handleRequest(const JsonRpcRequest &req);

  private:
    CompilerBridge &bridge_;
    Transport &transport_;
    DocumentStore store_;
    bool shutdownRequested_{false};

    // Lifecycle
    std::string handleInitialize(const JsonRpcRequest &req);
    std::string handleShutdown(const JsonRpcRequest &req);

    // Document sync notifications
    void handleDidOpen(const JsonRpcRequest &req);
    void handleDidChange(const JsonRpcRequest &req);
    void handleDidClose(const JsonRpcRequest &req);

    // Requests
    std::string handleCompletion(const JsonRpcRequest &req);
    std::string handleHover(const JsonRpcRequest &req);
    std::string handleDocumentSymbol(const JsonRpcRequest &req);

    // Helpers
    void publishDiagnostics(const std::string &uri);

    /// @brief Map CompletionInfo kind → LSP CompletionItemKind.
    static int completionKindToLsp(int kind);

    /// @brief Map SymbolInfo kind string → LSP SymbolKind.
    static int symbolKindToLsp(const std::string &kind);
};

} // namespace viper::server
