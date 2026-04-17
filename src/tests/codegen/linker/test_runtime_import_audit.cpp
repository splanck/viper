//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/codegen/linker/test_runtime_import_audit.cpp
// Purpose: Audit the built runtime/support archives for unresolved imports that
//          are not covered by the host native-link dynamic import policy.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/LinkerSupport.hpp"
#include "codegen/common/RuntimeComponents.hpp"
#include "codegen/common/linker/ArchiveReader.hpp"
#include "codegen/common/linker/DynamicSymbolPolicy.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/PlatformImportPlanner.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace viper::codegen;
using namespace viper::codegen::common;
using namespace viper::codegen::linker;

namespace {

using SymbolOriginMap = std::unordered_map<std::string, std::unordered_set<std::string>>;

void addObjectSymbols(const ObjFile &obj,
                      const std::string &origin,
                      std::unordered_set<std::string> &defined,
                      std::unordered_set<std::string> &undefined,
                      SymbolOriginMap &undefinedOrigins) {
    for (size_t i = 1; i < obj.symbols.size(); ++i) {
        const auto &sym = obj.symbols[i];
        if (sym.name.empty())
            continue;
        if (sym.binding == ObjSymbol::Undefined) {
            undefined.insert(sym.name);
            undefinedOrigins[sym.name].insert(origin);
            continue;
        }
        if (sym.binding == ObjSymbol::Local)
            continue;
        defined.insert(sym.name);
    }
}

std::vector<std::filesystem::path> collectAuditArchives(const std::filesystem::path &buildDir) {
    std::vector<std::filesystem::path> paths;
    std::unordered_set<std::string> seen;

    auto appendIfExists = [&](const std::filesystem::path &path) {
        if (!fileExists(path))
            return;
        const std::string normalized = path.lexically_normal().string();
        if (seen.insert(normalized).second)
            paths.push_back(path);
    };

    for (size_t i = 0; i < static_cast<size_t>(RtComponent::Count); ++i) {
        const auto comp = static_cast<RtComponent>(i);
        appendIfExists(runtimeArchivePath(buildDir, archiveNameForComponent(comp)));
    }

    appendIfExists(supportLibraryPath(buildDir, "vipergui"));
    appendIfExists(supportLibraryPath(buildDir, "vipergfx"));
    appendIfExists(supportLibraryPath(buildDir, "viperaud"));
    return paths;
}

bool usesDebugWindowsRuntimeArchives(const std::vector<std::filesystem::path> &archives) {
    for (const auto &path : archives) {
        std::string lower = path.lexically_normal().string();
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower.find("\\debug\\") != std::string::npos ||
            lower.find("/debug/") != std::string::npos ||
            lower.rfind("msvcrtd.lib") != std::string::npos ||
            lower.rfind("ucrtd.lib") != std::string::npos ||
            lower.rfind("vcruntimed.lib") != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(LinkerRuntimeImportAudit, HostRuntimeArchivesUseKnownDynamicImports) {
    const auto buildDir = findBuildDir();
    ASSERT_TRUE(buildDir.has_value());

    const std::vector<std::filesystem::path> archives = collectAuditArchives(*buildDir);
    ASSERT_FALSE(archives.empty());

    std::unordered_set<std::string> defined;
    std::unordered_set<std::string> undefined;
    SymbolOriginMap undefinedOrigins;

    for (const auto &archivePath : archives) {
        Archive archive;
        std::ostringstream err;
        ASSERT_TRUE(readArchive(archivePath.string(), archive, err));
        ASSERT_TRUE(err.str().empty());

        for (const auto &member : archive.members) {
            const auto memberBytes = extractMember(archive, member);
            ASSERT_FALSE(memberBytes.empty());

            ObjFile obj;
            std::ostringstream objErr;
            ASSERT_TRUE(readObjFile(memberBytes.data(),
                                    memberBytes.size(),
                                    archive.path + "(" + member.name + ")",
                                    obj,
                                    objErr));
            ASSERT_TRUE(objErr.str().empty());
            addObjectSymbols(obj,
                             archive.path + "(" + member.name + ")",
                             defined,
                             undefined,
                             undefinedOrigins);
        }
    }

    std::vector<std::string> unresolved;
    unresolved.reserve(undefined.size());
    for (const auto &sym : undefined) {
        if (defined.count(sym) == 0)
            unresolved.push_back(sym);
    }
    std::sort(unresolved.begin(), unresolved.end());
    unresolved.erase(std::unique(unresolved.begin(), unresolved.end()), unresolved.end());

    const LinkPlatform platform = detectLinkPlatform();
    std::unordered_set<std::string> dynamicSyms;
    std::vector<std::string> unknown;

    for (const auto &sym : unresolved) {
        const bool allowSynthetic =
            platform == LinkPlatform::Windows && isWindowsLinkerHelperSymbol(sym);
        const bool allowDynamic = allowSynthetic || isKnownDynamicSymbol(sym, platform);
        if (allowDynamic) {
            dynamicSyms.insert(sym);
            continue;
        }
        unknown.push_back(sym);
    }

    if (!unknown.empty()) {
        std::ostringstream msg;
        msg << "Unclassified runtime imports:\n";
        for (const auto &sym : unknown) {
            msg << "  " << sym;
            auto it = undefinedOrigins.find(sym);
            if (it != undefinedOrigins.end() && !it->second.empty()) {
                std::vector<std::string> origins(it->second.begin(), it->second.end());
                std::sort(origins.begin(), origins.end());
                msg << " <- ";
                for (size_t i = 0; i < origins.size(); ++i) {
                    if (i != 0)
                        msg << ", ";
                    msg << origins[i];
                }
            }
            msg << "\n";
        }
        std::cerr << msg.str() << "\n";
        ASSERT_TRUE(unknown.empty());
    }

    std::ostringstream planErr;
    switch (platform) {
        case LinkPlatform::Linux: {
            LinuxImportPlan plan;
            ASSERT_TRUE(planLinuxImports(dynamicSyms, plan, planErr));
            break;
        }
        case LinkPlatform::macOS: {
            MacImportPlan plan;
            ASSERT_TRUE(planMacImports(dynamicSyms, plan, planErr));
            break;
        }
        case LinkPlatform::Windows: {
            WindowsImportPlan plan;
            ASSERT_TRUE(generateWindowsImports(detectLinkArch(),
                                               dynamicSyms,
                                               usesDebugWindowsRuntimeArchives(archives),
                                               plan,
                                               planErr));
            break;
        }
    }

    EXPECT_TRUE(planErr.str().empty());
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
