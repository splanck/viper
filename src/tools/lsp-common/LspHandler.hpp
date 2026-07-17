//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

namespace zanna::server {

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

    /// @brief True after a successful `shutdown` request.
    [[nodiscard]] bool shutdownRequested() const {
        return shutdownRequested_;
    }

  private:
    ICompilerBridge &bridge_;
    Transport &transport_;
    ServerConfig config_;
    DocumentStore store_;
    bool initializeResponded_{false};
    bool clientInitialized_{false};
    bool shutdownRequested_{false};

    // Lifecycle
    /// @brief Handle `initialize`: advertise server capabilities to the client.
    std::string handleInitialize(const JsonRpcRequest &req);
    /// @brief Handle `shutdown`: mark the server for exit and acknowledge.
    std::string handleShutdown(const JsonRpcRequest &req);

    // Document sync notifications
    /// @brief Handle `textDocument/didOpen`: store the document and publish diagnostics.
    void handleDidOpen(const JsonRpcRequest &req);
    /// @brief Handle `textDocument/didChange`: update the document and re-diagnose.
    void handleDidChange(const JsonRpcRequest &req);
    /// @brief Handle `textDocument/didClose`: drop the document from the store.
    void handleDidClose(const JsonRpcRequest &req);

    // Requests
    /// @brief Handle `textDocument/completion`: return completion items at a position.
    std::string handleCompletion(const JsonRpcRequest &req);
    /// @brief Handle `textDocument/hover`: return hover text for the symbol at a position.
    std::string handleHover(const JsonRpcRequest &req);
    /// @brief Handle `textDocument/documentSymbol`: list the document's symbols.
    std::string handleDocumentSymbol(const JsonRpcRequest &req);
    /// @brief Handle `textDocument/definition`: return the target symbol location.
    std::string handleDefinition(const JsonRpcRequest &req);
    /// @brief Handle `textDocument/references`: return known references for a symbol.
    std::string handleReferences(const JsonRpcRequest &req);
    /// @brief Handle `textDocument/rename`: return a workspace edit for semantic rename.
    std::string handleRename(const JsonRpcRequest &req);
    /// @brief Handle `textDocument/signatureHelp`: return call signature help.
    std::string handleSignatureHelp(const JsonRpcRequest &req);
    /// @brief Handle `workspace/symbol`: return indexed workspace symbols.
    std::string handleWorkspaceSymbol(const JsonRpcRequest &req);
    /// @brief Handle `textDocument/semanticTokens/full`: return full semantic tokens.
    std::string handleSemanticTokensFull(const JsonRpcRequest &req);

    // Helpers
    /// @brief Compile the document for @p uri and publish its diagnostics notification.
    void publishDiagnostics(const std::string &uri);

    /// @brief Send a best-effort `window/logMessage` notification to the client.
    /// @param type LSP MessageType value: 1 error, 2 warning, 3 info, 4 log.
    /// @param message Human-readable text for the editor log.
    void logMessage(int type, const std::string &message);

    /// @brief Map CompletionInfo kind → LSP CompletionItemKind.
    static int completionKindToLsp(int kind);

    /// @brief Map SymbolInfo kind string → LSP SymbolKind.
    static int symbolKindToLsp(const std::string &kind);
};

} // namespace zanna::server
