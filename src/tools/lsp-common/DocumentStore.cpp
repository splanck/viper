//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/DocumentStore.cpp
// Purpose: Implementation of in-memory document storage for LSP.
// Key invariants:
//   - URI keys are stored verbatim (no normalization beyond uriToPath)
//   - uriToPath handles file:// prefix and %XX URL decoding
// Ownership/Lifetime:
//   - All data fully owned
// Links: tools/lsp-common/DocumentStore.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/DocumentStore.hpp"

#include <cctype>

namespace viper::server
{

void DocumentStore::open(const std::string &uri, int version, std::string content)
{
    docs_[uri] = {version, std::move(content)};
}

void DocumentStore::update(const std::string &uri, int version, std::string content)
{
    docs_[uri] = {version, std::move(content)};
}

void DocumentStore::close(const std::string &uri)
{
    docs_.erase(uri);
}

const std::string *DocumentStore::getContent(const std::string &uri) const
{
    auto it = docs_.find(uri);
    if (it == docs_.end())
        return nullptr;
    return &it->second.content;
}

bool DocumentStore::isOpen(const std::string &uri) const
{
    return docs_.find(uri) != docs_.end();
}

std::string DocumentStore::uriToPath(const std::string &uri)
{
    std::string path;
    std::string_view sv = uri;

    // Strip file:// prefix
    if (sv.substr(0, 7) == "file://")
    {
        sv.remove_prefix(7);
        // On Unix: file:///path → /path (skip the authority component)
        // On Windows: file:///C:/path → C:/path
        if (sv.size() >= 3 && sv[0] == '/' && sv[2] == ':')
        {
            // Windows drive letter: /C:/path → C:/path
            sv.remove_prefix(1);
        }
    }

    // URL-decode %XX sequences
    path.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ++i)
    {
        if (sv[i] == '%' && i + 2 < sv.size())
        {
            char hi = sv[i + 1];
            char lo = sv[i + 2];
            auto hexVal = [](char c) -> int
            {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            int h = hexVal(hi);
            int l = hexVal(lo);
            if (h >= 0 && l >= 0)
            {
                path += static_cast<char>((h << 4) | l);
                i += 2;
                continue;
            }
        }
        path += sv[i];
    }

    return path;
}

} // namespace viper::server
