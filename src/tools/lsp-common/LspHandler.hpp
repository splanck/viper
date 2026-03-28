//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/LspHandler.hpp
// Purpose: LSP protocol handler — capability negotiation and request dispatch.
// Key invariants:
//   - Follows LSP 3.17 specification
//   - Manages document state via DocumentStore
//   - Publishes diagnostics asynchronously via Transport notifications
//   - Parameterized via ICompilerBridge and ServerConfig for language reuse
// Ownership/Lifetime:
//   - LspHandler borrows ICompilerBridge and Transport (must outlive handler)
//   - Owns the DocumentStore
// Links: tools/lsp-common/ICompilerBridge.hpp, tools/lsp-common/DocumentStore.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tools/lsp-common/DocumentStore.hpp"
#include "tools/lsp-common/ICompilerBridge.hpp"
#include "tools/lsp-common/Json.hpp"
#include "tools/lsp-common/JsonRpc.hpp"

#include <string>

namespace viper::server {

class Transport;

/// @brief LSP protocol handler.
///
/// Handles the LSP lifecycle (initialize, shutdown, exit) and request/notification
/// dispatch for editor features (completion, hover, diagnostics, document symbols).
///
/// Parameterized via ICompilerBridge (language-specific compiler) and ServerConfig
/// (language-specific strings: server name, diagnostic source, etc.).
class LspHandler {
  public:
    /// @brief Construct a handler that dispatches to the given bridge.
    /// @param bridge The compiler bridge (must outlive this handler).
    /// @param transport The transport for sending notifications (must outlive this handler).
    /// @param config Server configuration for language-specific strings.
    LspHandler(ICompilerBridge &bridge, Transport &transport, const ServerConfig &config);

    /// @brief Handle a JSON-RPC request and return a response string.
    /// @param req The parsed JSON-RPC request.
    /// @return JSON-RPC response string, or empty string for notifications.
    std::string handleRequest(const JsonRpcRequest &req);

  private:
    ICompilerBridge &bridge_;
    Transport &transport_;
    ServerConfig config_;
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
