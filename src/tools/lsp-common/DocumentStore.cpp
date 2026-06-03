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
#include <stdexcept>
#include <string_view>

namespace viper::server {

void DocumentStore::open(const std::string &uri, int version, std::string content) {
    docs_[uri] = {version, std::move(content)};
}

void DocumentStore::update(const std::string &uri, int version, std::string content) {
    docs_[uri] = {version, std::move(content)};
}

void DocumentStore::close(const std::string &uri) {
    docs_.erase(uri);
}

const std::string *DocumentStore::getContent(const std::string &uri) const {
    auto it = docs_.find(uri);
    if (it == docs_.end())
        return nullptr;
    return &it->second.content;
}

bool DocumentStore::isOpen(const std::string &uri) const {
    return docs_.find(uri) != docs_.end();
}

bool DocumentStore::tryUriToPath(const std::string &uri, std::string &outPath, std::string *err) {
    outPath.clear();
    std::string path;
    std::string_view sv = uri;

    // Strip file:// prefix and handle the URI authority component.
    if (sv.substr(0, 7) == "file://") {
        sv.remove_prefix(7);
        std::string_view authority;
        if (!sv.empty() && sv.front() != '/') {
            const std::size_t slash = sv.find('/');
            authority = slash == std::string_view::npos ? sv : sv.substr(0, slash);
            sv = slash == std::string_view::npos ? std::string_view{} : sv.substr(slash);
        }

        if (!authority.empty() && authority != "localhost") {
            path = "//";
            path.append(authority);
        } else if (sv.size() >= 3 && sv[0] == '/' && sv[2] == ':') {
            // Windows drive letter: /C:/path -> C:/path
            sv.remove_prefix(1);
        }
    }

    // URL-decode %XX sequences. Encoded separators are rejected so percent
    // decoding cannot create extra path components after URI parsing.
    path.reserve(path.size() + sv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '%') {
            if (i + 2 >= sv.size()) {
                if (err)
                    *err = "malformed percent escape in URI: " + uri;
                return false;
            }
            char hi = sv[i + 1];
            char lo = sv[i + 2];
            auto hexVal = [](char c) -> int {
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
            if (h < 0 || l < 0) {
                if (err)
                    *err = "malformed percent escape in URI: " + uri;
                return false;
            }
            const char decoded = static_cast<char>((h << 4) | l);
            if (decoded == '/' || decoded == '\\') {
                if (err)
                    *err = "URI must not percent-encode path separators: " + uri;
                return false;
            }
            path += decoded;
            i += 2;
            continue;
        }
        path += sv[i];
    }

    outPath = std::move(path);
    return true;
}

std::string DocumentStore::uriToPath(const std::string &uri) {
    std::string path;
    std::string err;
    if (!tryUriToPath(uri, path, &err))
        throw std::runtime_error(err);
    return path;
}

} // namespace viper::server
