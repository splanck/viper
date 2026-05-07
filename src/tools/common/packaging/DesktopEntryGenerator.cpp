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

/// @brief Escape a value string for inclusion in a .desktop file field.
/// Backslash is the only character requiring escaping per the Desktop Entry Spec;
/// also validates that the value is single-line (no embedded newlines), since the
/// spec treats each line as a key=value pair.
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

/// @brief Escape a string for safe embedding in XML attribute values and text content.
/// Replaces the five predefined XML entities: & → &amp;, < → &lt;, > → &gt;,
/// " → &quot;, ' → &apos;.
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

/// @brief Validate all file associations before generating any output.
/// Delegates to validatePackageFileAssociations() which checks for duplicate
/// extensions, well-formed MIME type strings, and legal extension characters;
/// throws on any violation.
void validateAssociations(const std::vector<FileAssoc> &assocs) {
    validatePackageFileAssociations(assocs);
}

} // namespace

/// @brief Build a complete freedesktop.org .desktop file from the given parameters.
/// Validates file associations and normalizes the categories string before writing.
/// Appends "%f" to the Exec= line when file associations or acceptsFileArgument is set,
/// enabling the desktop environment to pass the opened file path to the application.
std::string generateDesktopEntry(const DesktopEntryParams &params) {
    validateAssociations(params.fileAssociations);
    const std::string categories = normalizeDesktopCategories(params.categories);
    std::ostringstream os;
    os << "[Desktop Entry]\n";
    os << "Type=Application\n";
    os << "Name=" << desktopEscape(params.name) << "\n";
    if (!params.comment.empty())
        os << "Comment=" << desktopEscape(params.comment) << "\n";
    validateSingleLineField(params.execPath, "desktop Exec path");
    os << "Exec=" << params.execPath;
    if (params.acceptsFileArgument || !params.fileAssociations.empty())
        os << " %f";
    os << "\n";
    if (!params.iconName.empty())
        os << "Icon=" << desktopEscape(params.iconName) << "\n";
    if (!categories.empty())
        os << "Categories=" << desktopEscape(categories) << "\n";
    if (!params.workingDir.empty())
        os << "Path=" << desktopEscape(params.workingDir) << "\n";
    if (params.noDisplay)
        os << "NoDisplay=true\n";
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

/// @brief Build a shared-mime-info XML document registering each file association.
/// Returns an empty string when `assocs` is empty to avoid writing a stub file.
/// Each association produces a <mime-type> element with a <comment> and a glob
/// pattern derived from the extension (leading dot is normalized if missing).
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
