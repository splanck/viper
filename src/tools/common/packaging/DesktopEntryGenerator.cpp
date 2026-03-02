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

#include <sstream>

namespace viper::pkg {

std::string generateDesktopEntry(const DesktopEntryParams &params)
{
    std::ostringstream os;
    os << "[Desktop Entry]\n";
    os << "Type=Application\n";
    os << "Name=" << params.name << "\n";
    if (!params.comment.empty())
        os << "Comment=" << params.comment << "\n";
    os << "Exec=" << params.execPath << "\n";
    if (!params.iconName.empty())
        os << "Icon=" << params.iconName << "\n";
    if (!params.categories.empty())
        os << "Categories=" << params.categories << "\n";
    os << "Terminal=" << (params.terminal ? "true" : "false") << "\n";

    // MimeType field for file associations
    if (!params.fileAssociations.empty()) {
        os << "MimeType=";
        for (size_t i = 0; i < params.fileAssociations.size(); i++) {
            os << params.fileAssociations[i].mimeType << ";";
        }
        os << "\n";
    }

    return os.str();
}

std::string generateMimeTypeXml(const std::string &packageName,
                                const std::vector<FileAssoc> &assocs)
{
    if (assocs.empty())
        return {};

    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    os << "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";

    for (const auto &a : assocs) {
        os << "  <mime-type type=\"" << a.mimeType << "\">\n";
        os << "    <comment>" << a.description << "</comment>\n";

        // Extension without leading dot for the glob pattern
        std::string ext = a.extension;
        if (!ext.empty() && ext[0] != '.')
            ext = "." + ext;
        os << "    <glob pattern=\"*" << ext << "\"/>\n";
        os << "  </mime-type>\n";
    }

    os << "</mime-info>\n";
    return os.str();
}

} // namespace viper::pkg
