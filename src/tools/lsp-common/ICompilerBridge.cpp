//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/ICompilerBridge.cpp
// Purpose: Default implementations for runtime query methods on ICompilerBridge.
// Key invariants:
//   - Runtime queries use the shared RuntimeClasses singleton registry
//   - These defaults work for any language server (Zia, BASIC, etc.)
// Ownership/Lifetime:
//   - All returned data is fully owned
// Links: tools/lsp-common/ICompilerBridge.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/ICompilerBridge.hpp"

#include "il/runtime/classes/RuntimeClasses.hpp"

#include <algorithm>
#include <cctype>

namespace viper::server {

/// @brief Return an ASCII-lowercased copy of @p s (used for case-insensitive search).
static std::string toLowerStr(const std::string &s) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

std::vector<RuntimeClassSummary> ICompilerBridge::runtimeClasses() {
    const auto &catalog = il::runtime::runtimeClassCatalog();
    std::vector<RuntimeClassSummary> result;
    result.reserve(catalog.size());
    for (const auto &cls : catalog) {
        result.push_back({cls.qname,
                          static_cast<int>(cls.properties.size()),
                          static_cast<int>(cls.methods.size())});
    }
    return result;
}

void ICompilerBridge::updateDocument(const std::string & /*path*/,
                                     const std::string & /*source*/) {}

void ICompilerBridge::removeDocument(const std::string & /*path*/) {}

bool ICompilerBridge::supportsDefinition() const {
    return false;
}

bool ICompilerBridge::supportsReferences() const {
    return false;
}

bool ICompilerBridge::supportsRename() const {
    return false;
}

bool ICompilerBridge::supportsSignatureHelp() const {
    return false;
}

bool ICompilerBridge::supportsWorkspaceSymbols() const {
    return false;
}

bool ICompilerBridge::supportsSemanticTokens() const {
    return false;
}

std::optional<LocationInfo> ICompilerBridge::definition(const std::string & /*source*/,
                                                        int /*line*/,
                                                        int /*col*/,
                                                        const std::string & /*path*/) {
    return std::nullopt;
}

std::vector<LocationInfo> ICompilerBridge::references(const std::string & /*source*/,
                                                      int /*line*/,
                                                      int /*col*/,
                                                      const std::string & /*path*/) {
    return {};
}

RenameResult ICompilerBridge::rename(const std::string & /*source*/,
                                     int /*line*/,
                                     int /*col*/,
                                     const std::string & /*path*/,
                                     const std::string & /*newName*/) {
    return {};
}

SignatureHelpInfo ICompilerBridge::signatureHelp(const std::string & /*source*/,
                                                 int /*line*/,
                                                 int /*col*/,
                                                 const std::string & /*path*/) {
    return {};
}

std::vector<SymbolInfo> ICompilerBridge::workspaceSymbols(const std::string & /*query*/) {
    return {};
}

std::vector<SemanticTokenInfo> ICompilerBridge::semanticTokens(const std::string & /*source*/,
                                                               const std::string & /*path*/) {
    return {};
}

std::vector<RuntimeMemberInfo> ICompilerBridge::runtimeMembers(const std::string &className) {
    const auto *cls = il::runtime::findRuntimeClassByQName(className);
    if (!cls)
        return {};

    std::vector<RuntimeMemberInfo> result;
    for (const auto &prop : cls->properties)
        result.push_back({prop.name, "property", prop.type ? prop.type : ""});
    for (const auto &method : cls->methods)
        result.push_back({method.name, "method", method.signature ? method.signature : ""});
    return result;
}

std::vector<RuntimeMemberInfo> ICompilerBridge::runtimeSearch(const std::string &keyword) {
    std::string lowerKw = toLowerStr(keyword);
    if (lowerKw.empty())
        return {};
    const auto &catalog = il::runtime::runtimeClassCatalog();
    std::vector<RuntimeMemberInfo> result;

    for (const auto &cls : catalog) {
        std::string lowerName = toLowerStr(cls.qname);
        if (lowerName.find(lowerKw) != std::string::npos)
            result.push_back({cls.qname, "class", ""});

        for (const auto &method : cls.methods) {
            std::string lowerMethod = toLowerStr(method.name);
            if (lowerMethod.find(lowerKw) != std::string::npos) {
                result.push_back({std::string(cls.qname) + "." + method.name,
                                  "method",
                                  method.signature ? method.signature : ""});
            }
        }

        for (const auto &prop : cls.properties) {
            std::string lowerProp = toLowerStr(prop.name);
            if (lowerProp.find(lowerKw) != std::string::npos) {
                result.push_back({std::string(cls.qname) + "." + prop.name,
                                  "property",
                                  prop.type ? prop.type : ""});
            }
        }
    }
    return result;
}

} // namespace viper::server
