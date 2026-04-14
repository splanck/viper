//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/LinkerSupport.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace viper::codegen::common;

namespace {

void setEnvVar(const char *name, const std::string &value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void unsetEnvVar(const char *name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

struct ScopedEnvVar {
    explicit ScopedEnvVar(const char *envName) : name(envName) {
        if (const char *existing = std::getenv(name)) {
            hadOldValue = true;
            oldValue = existing;
        }
    }

    ~ScopedEnvVar() {
        if (hadOldValue)
            setEnvVar(name, oldValue);
        else
            unsetEnvVar(name);
    }

    const char *name;
    bool hadOldValue = false;
    std::string oldValue;
};

#if defined(_WIN32)
std::string archiveFileName(std::string_view base) {
    return std::string(base) + ".lib";
}
#else
std::string archiveFileName(std::string_view base) {
    return "lib" + std::string(base) + ".a";
}
#endif

void writeArchive(const std::filesystem::path &path, const char *contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

bool containsComponent(const LinkContext &ctx, viper::codegen::RtComponent component) {
    return std::find(ctx.requiredComponents.begin(), ctx.requiredComponents.end(), component) !=
           ctx.requiredComponents.end();
}

} // namespace

TEST(LinkerSupport, InstalledLibDirViaEnvVar) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_linker_support_env";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);

    {
        std::ofstream out(tmpRoot / archiveFileName("viper_rt_base"), std::ios::binary);
        out << "ar";
    }
    {
        std::ofstream out(tmpRoot / archiveFileName("vipergfx"), std::ios::binary);
        out << "gfx";
    }

    const char *var = "VIPER_LIB_PATH";
    ScopedEnvVar scopedEnv(var);
    setEnvVar(var, tmpRoot.string());

    const auto found = findInstalledLibDir();
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->lexically_normal(), tmpRoot.lexically_normal());
    EXPECT_EQ(runtimeArchivePath({}, "viper_rt_base").lexically_normal(),
              (tmpRoot / archiveFileName("viper_rt_base")).lexically_normal());
    EXPECT_EQ(supportLibraryPath({}, "vipergfx").lexically_normal(),
              (tmpRoot / archiveFileName("vipergfx")).lexically_normal());

    fs::remove_all(tmpRoot);
}

TEST(LinkerSupport, InstalledLayoutPreferredOverBuildTree) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_linker_support_prefer_installed";
    const fs::path installedDir = tmpRoot / "installed";
    const fs::path buildDir = tmpRoot / "build";

    fs::remove_all(tmpRoot);
    fs::create_directories(installedDir);
    fs::create_directories(buildDir);

    writeArchive(installedDir / archiveFileName("viper_rt_base"), "installed-runtime");
    writeArchive(installedDir / archiveFileName("vipergfx"), "installed-gfx");

#if defined(_WIN32)
    writeArchive(buildDir / "src/runtime/Debug" / archiveFileName("viper_rt_base"), "build-runtime");
    writeArchive(buildDir / "lib/Debug" / archiveFileName("vipergfx"), "build-gfx");
#else
    writeArchive(buildDir / "src/runtime" / archiveFileName("viper_rt_base"), "build-runtime");
    writeArchive(buildDir / "lib" / archiveFileName("vipergfx"), "build-gfx");
#endif

    const char *var = "VIPER_LIB_PATH";
    ScopedEnvVar scopedEnv(var);
    setEnvVar(var, installedDir.string());

    EXPECT_EQ(runtimeArchivePath(buildDir, "viper_rt_base").lexically_normal(),
              (installedDir / archiveFileName("viper_rt_base")).lexically_normal());
    EXPECT_EQ(supportLibraryPath(buildDir, "vipergfx").lexically_normal(),
              (installedDir / archiveFileName("vipergfx")).lexically_normal());

    fs::remove_all(tmpRoot);
}

TEST(LinkerSupport, ArchiveClosureAddsTextForBaseStringIntern) {
    LinkContext ctx;
    std::ostringstream out;
    std::ostringstream err;
    ASSERT_EQ(0, prepareLinkContextFromSymbols({"rt_string_intern"}, ctx, out, err));
    ASSERT_TRUE(err.str().empty());

    EXPECT_TRUE(containsComponent(ctx, viper::codegen::RtComponent::Base));
    EXPECT_TRUE(containsComponent(ctx, viper::codegen::RtComponent::Text));
}

TEST(LinkerSupport, ArchiveClosureAddsIoFsForBaseChannelHelpers) {
    LinkContext ctx;
    std::ostringstream out;
    std::ostringstream err;
    ASSERT_EQ(0, prepareLinkContextFromSymbols({"rt_eof_ch"}, ctx, out, err));
    ASSERT_TRUE(err.str().empty());

    EXPECT_TRUE(containsComponent(ctx, viper::codegen::RtComponent::Base));
    EXPECT_TRUE(containsComponent(ctx, viper::codegen::RtComponent::IoFs));
}

TEST(LinkerSupport, ArchiveClosureAddsTextAndIoFsForGraphicsRuntimeDeps) {
    LinkContext ctx;
    std::ostringstream out;
    std::ostringstream err;
    ASSERT_EQ(0, prepareLinkContextFromSymbols({"rt_action_load", "rt_pixels_load_png"},
                                               ctx,
                                               out,
                                               err));
    ASSERT_TRUE(err.str().empty());

    EXPECT_TRUE(containsComponent(ctx, viper::codegen::RtComponent::Graphics));
    EXPECT_TRUE(containsComponent(ctx, viper::codegen::RtComponent::Text));
    EXPECT_TRUE(containsComponent(ctx, viper::codegen::RtComponent::IoFs));
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
