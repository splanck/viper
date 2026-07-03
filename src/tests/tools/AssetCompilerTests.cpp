//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include "tools/common/asset/AssetCompiler.hpp"
#include "tools/common/asset/VpaWriter.hpp"
#include "tools/common/project_loader.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

using il::tools::common::ProjectConfig;

namespace {

fs::path makeTempRoot(const std::string &name) {
    fs::path root = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    return root;
}

void writeText(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

TEST(AssetCompiler, RejectsSourcePathTraversal) {
    fs::path root = makeTempRoot("viper_asset_compiler_escape_project");
    fs::path outside = root.parent_path() / "viper_asset_compiler_outside.txt";
    writeText(outside, "outside");

    ProjectConfig config;
    config.name = "escape";
    config.rootDir = root.string();
    config.embedAssets.push_back({"../viper_asset_compiler_outside.txt"});

    std::string err;
    auto bundle = viper::asset::compileAssets(config, root.string(), err);
    EXPECT_TRUE(!bundle.has_value());
    EXPECT_TRUE(err.find("must not contain") != std::string::npos ||
                err.find("escapes") != std::string::npos);

    std::error_code ec;
    fs::remove(outside, ec);
    fs::remove_all(root, ec);
}

TEST(AssetCompiler, SanitizesPackOutputName) {
    fs::path root = makeTempRoot("viper_asset_compiler_pack_project");
    fs::path output = root / "out";
    writeText(root / "assets" / "file.txt", "payload");
    fs::create_directories(output);

    ProjectConfig config;
    config.name = "My App";
    config.rootDir = root.string();
    config.packGroups.push_back({"Data Pack", {"assets"}, false});

    std::string err;
    auto bundle = viper::asset::compileAssets(config, output.string(), err);
    ASSERT_TRUE(bundle.has_value());
    ASSERT_EQ(bundle->packFilePaths.size(), static_cast<size_t>(1));
    EXPECT_EQ(fs::path(bundle->packFilePaths[0]).filename().string(), "my_app-data_pack.vpa");
    EXPECT_TRUE(fs::exists(bundle->packFilePaths[0]));

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(AssetCompiler, RidgeboundRequiredAssetsArePresent) {
#if defined(VIPER_SOURCE_DIR)
    fs::path output = makeTempRoot("viper_ridgebound_asset_compiler");
    auto project = il::tools::common::resolveProject(
        (fs::path(VIPER_SOURCE_DIR) / "examples" / "games" / "ridgebound").string());
    ASSERT_TRUE(project);

    std::string err;
    auto bundle = viper::asset::compileAssets(project.value(), output.string(), err);
    EXPECT_TRUE(bundle.has_value());
    EXPECT_EQ(err, std::string());

    std::error_code ec;
    fs::remove_all(output, ec);
#endif
}

TEST(VpaWriter, RejectsDuplicateEntries) {
    const uint8_t data[] = {'x'};
    viper::asset::VpaWriter writer;
    writer.addEntry("assets/a.txt", data, sizeof(data), false);
    EXPECT_THROWS(writer.addEntry("assets/a.txt", data, sizeof(data), false),
                  std::invalid_argument);
}

TEST(VpaWriter, RejectsUnsafeNames) {
    const uint8_t data[] = {'x'};
    viper::asset::VpaWriter writer;
    EXPECT_THROWS(writer.addEntry("../escape.txt", data, sizeof(data), false),
                  std::invalid_argument);
    EXPECT_THROWS(writer.addEntry("assets\\a.txt", data, sizeof(data), false),
                  std::invalid_argument);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
