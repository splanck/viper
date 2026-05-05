//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/DesktopEntryGenerator.cpp
// Purpose: Generate freedesktop.org .desktop files and MIME type XML.
//
// Key invariants:
//   - .desktop follows [Desktop Entry] group with Type=Application.
//   - MIME XML conforms to http://www.freedesktop.org/standards/shared-mime-info.
//
// Ownership/Lifetime:
//   - Pure string generation, no side effects.
//
// Links: DesktopEntryGenerator.hpp
//
//===----------------------------------------------------------------------===//

#include "DesktopEntryGenerator.hpp"
#include "PkgUtils.hpp"

#include <sstream>

namespace viper::pkg {

namespace {

std::string desktopEscape(const std::string &s) {
    validateSingleLineField(s, "desktop entry field");
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\')
            out += "\\\\";
        else
            out.push_back(c);
    }
    return out;
}

std::string xmlEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

void validateAssociations(const std::vector<FileAssoc> &assocs) {
    for (const auto &assoc : assocs)
        validateFileAssociation(assoc.extension, assoc.description, assoc.mimeType);
}

} // namespace

std::string generateDesktopEntry(const DesktopEntryParams &params) {
    validateAssociations(params.fileAssociations);
    std::ostringstream os;
    os << "[Desktop Entry]\n";
    os << "Type=Application\n";
    os << "Name=" << desktopEscape(params.name) << "\n";
    if (!params.comment.empty())
        os << "Comment=" << desktopEscape(params.comment) << "\n";
    validateSingleLineField(params.execPath, "desktop Exec path");
    os << "Exec=" << params.execPath << "\n";
    if (!params.iconName.empty())
        os << "Icon=" << desktopEscape(params.iconName) << "\n";
    if (!params.categories.empty())
        os << "Categories=" << desktopEscape(params.categories) << "\n";
    if (!params.workingDir.empty())
        os << "Path=" << desktopEscape(params.workingDir) << "\n";
    os << "Terminal=" << (params.terminal ? "true" : "false") << "\n";

    // MimeType field for file associations
    if (!params.fileAssociations.empty()) {
        os << "MimeType=";
        for (size_t i = 0; i < params.fileAssociations.size(); i++) {
            os << desktopEscape(params.fileAssociations[i].mimeType) << ";";
        }
        os << "\n";
    }

    return os.str();
}

std::string generateMimeTypeXml(const std::string &packageName,
                                const std::vector<FileAssoc> &assocs) {
    if (assocs.empty())
        return {};

    (void)packageName;
    validateAssociations(assocs);

    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    os << "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";

    for (const auto &a : assocs) {
        os << "  <mime-type type=\"" << xmlEscape(a.mimeType) << "\">\n";
        os << "    <comment>" << xmlEscape(a.description) << "</comment>\n";

        // Extension without leading dot for the glob pattern
        std::string ext = a.extension;
        if (!ext.empty() && ext[0] != '.')
            ext = "." + ext;
        os << "    <glob pattern=\"*" << xmlEscape(ext) << "\"/>\n";
        os << "  </mime-type>\n";
    }

    os << "</mime-info>\n";
    return os.str();
}

} // namespace viper::pkg
