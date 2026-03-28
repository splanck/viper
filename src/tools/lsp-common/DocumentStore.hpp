//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/DocumentStore.hpp
// Purpose: In-memory document storage for LSP open files.
// Key invariants:
//   - URI → (version, content) mapping
//   - Thread-safe is NOT required (single-threaded server)
//   - Documents are only tracked while open (didOpen → didClose)
// Ownership/Lifetime:
//   - Owns document content strings
// Links: tools/lsp-common/LspHandler.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <unordered_map>
#include <utility>

namespace viper::server {

/// @brief In-memory store for open documents, keyed by URI.
///
/// Tracks the current content and version of each open file so that
/// LSP requests (completion, hover, etc.) can retrieve the latest text
/// without re-reading from disk.
class DocumentStore {
  public:
    /// @brief Open or update a document.
    /// @param uri Document URI (e.g., "file:///path/to/file.zia").
    /// @param version Version number from the client.
    /// @param content Full text content of the document.
    void open(const std::string &uri, int version, std::string content);

    /// @brief Update a document's content and version.
    void update(const std::string &uri, int version, std::string content);

    /// @brief Close a document (remove from store).
    void close(const std::string &uri);

    /// @brief Get the content of a document.
    /// @return Pointer to content string, or nullptr if not found.
    const std::string *getContent(const std::string &uri) const;

    /// @brief Check if a document is open.
    bool isOpen(const std::string &uri) const;

    /// @brief Extract a file path from a URI.
    /// @details Strips "file://" prefix and URL-decodes %XX sequences.
    static std::string uriToPath(const std::string &uri);

  private:
    struct Document {
        int version;
        std::string content;
    };

    std::unordered_map<std::string, Document> docs_;
};

} // namespace viper::server
