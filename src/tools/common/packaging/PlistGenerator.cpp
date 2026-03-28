//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PlistGenerator.cpp
// Purpose: Generate macOS Info.plist XML and PkgInfo for .app bundles.
//
// Key invariants:
//   - XML output uses explicit entity escaping for &, <, >, in values.
//   - Empty optional fields are omitted from output.
//
// Ownership/Lifetime:
//   - Pure functions, no state.
//
// Links: PlistGenerator.hpp
//
//===----------------------------------------------------------------------===//

#include "PlistGenerator.hpp"

#include <sstream>

namespace viper::pkg {

/// @brief Escape XML special characters in a string value.
static std::string xmlEscape(const std::string &s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            default:
                result += c;
                break;
        }
    }
    return result;
}

std::string generatePlist(const PlistParams &params) {
    std::ostringstream os;

    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\"\n"
       << "  \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
       << "<plist version=\"1.0\">\n"
       << "<dict>\n";

    // Required keys
    os << "  <key>CFBundleExecutable</key>\n"
       << "  <string>" << xmlEscape(params.executableName) << "</string>\n";

    os << "  <key>CFBundleIdentifier</key>\n"
       << "  <string>" << xmlEscape(params.bundleId) << "</string>\n";

    os << "  <key>CFBundleName</key>\n"
       << "  <string>" << xmlEscape(params.bundleName) << "</string>\n";

    os << "  <key>CFBundleVersion</key>\n"
       << "  <string>" << xmlEscape(params.version) << "</string>\n";

    os << "  <key>CFBundleShortVersionString</key>\n"
       << "  <string>" << xmlEscape(params.version) << "</string>\n";

    os << "  <key>CFBundlePackageType</key>\n"
       << "  <string>APPL</string>\n";

    os << "  <key>CFBundleSignature</key>\n"
       << "  <string>" << "??" << "??" << "</string>\n";

    // Icon
    if (!params.iconFile.empty()) {
        os << "  <key>CFBundleIconFile</key>\n"
           << "  <string>" << xmlEscape(params.iconFile) << "</string>\n";
    }

    // Minimum OS version
    std::string minOs = params.minOsVersion.empty() ? "10.13" : params.minOsVersion;
    os << "  <key>LSMinimumSystemVersion</key>\n"
       << "  <string>" << xmlEscape(minOs) << "</string>\n";

    // Retina support
    os << "  <key>NSHighResolutionCapable</key>\n"
       << "  <true/>\n";

    // File associations
    if (!params.fileAssociations.empty()) {
        os << "  <key>CFBundleDocumentTypes</key>\n"
           << "  <array>\n";
        for (const auto &fa : params.fileAssociations) {
            os << "    <dict>\n";
            // Extension (strip leading dot)
            std::string ext = fa.extension;
            if (!ext.empty() && ext[0] == '.')
                ext = ext.substr(1);
            os << "      <key>CFBundleTypeExtensions</key>\n"
               << "      <array><string>" << xmlEscape(ext) << "</string></array>\n";
            os << "      <key>CFBundleTypeName</key>\n"
               << "      <string>" << xmlEscape(fa.description) << "</string>\n";
            os << "      <key>CFBundleTypeRole</key>\n"
               << "      <string>Editor</string>\n";
            os << "    </dict>\n";
        }
        os << "  </array>\n";
    }

    // UTExportedTypeDeclarations for custom file types
    if (!params.fileAssociations.empty()) {
        os << "  <key>UTExportedTypeDeclarations</key>\n"
           << "  <array>\n";
        for (const auto &fa : params.fileAssociations) {
            std::string ext = fa.extension;
            if (!ext.empty() && ext[0] == '.')
                ext = ext.substr(1);
            // UTI: reverse-DNS identifier derived from bundle ID + extension
            std::string uti = params.bundleId + "." + ext;
            os << "    <dict>\n";
            os << "      <key>UTTypeIdentifier</key>\n"
               << "      <string>" << xmlEscape(uti) << "</string>\n";
            os << "      <key>UTTypeDescription</key>\n"
               << "      <string>" << xmlEscape(fa.description) << "</string>\n";
            os << "      <key>UTTypeConformsTo</key>\n"
               << "      <array><string>public.data</string></array>\n";
            os << "      <key>UTTypeTagSpecification</key>\n"
               << "      <dict>\n";
            os << "        <key>public.filename-extension</key>\n"
               << "        <array><string>" << xmlEscape(ext) << "</string></array>\n";
            if (!fa.mimeType.empty()) {
                os << "        <key>public.mime-type</key>\n"
                   << "        <string>" << xmlEscape(fa.mimeType) << "</string>\n";
            }
            os << "      </dict>\n";
            os << "    </dict>\n";
        }
        os << "  </array>\n";
    }

    os << "</dict>\n"
       << "</plist>\n";

    return os.str();
}

std::string generatePkgInfo() {
    return "APPL????";
}

} // namespace viper::pkg
