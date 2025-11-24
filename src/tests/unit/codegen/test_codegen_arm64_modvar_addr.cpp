//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_modvar_addr.cpp
// Purpose: Verify call to rt_modvar_addr_i64 and pointer-based load/store.
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/ilc/cmd_codegen_arm64.hpp"

using namespace viper::tools::ilc;

static std::string outPath(const std::string &name)
{
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/arm64"};
    fs::create_directories(dir);
    return (dir / name).string();
}

static void writeFile(const std::string &path, const std::string &text)
{
    std::ofstream ofs(path);
    ASSERT_TRUE(static_cast<bool>(ofs));
    ofs << text;
}

static std::string readFile(const std::string &path)
{
    std::ifstream ifs(path);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

TEST(Arm64CLI, ModvarAddr_LoadStore)
{
    const std::string in = outPath("arm64_modvar_addr.il");
    const std::string out = outPath("arm64_modvar_addr.s");
    const std::string il =
        "il 0.1\n"
        "extern @rt_modvar_addr_i64(str) -> ptr\n"
        "global const str @.Lname = \"counter\"\n"
        "func @f() -> i64 {\n"
        "entry:\n"
        "  %n = const_str @.Lname\n"
        "  %p = call @rt_modvar_addr_i64(%n)\n"
        "  %v = load i64, %p\n"
        "  %v1 = add %v, 1\n"
        "  store i64, %p, %v1\n"
        "  ret %v1\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect a call and base-relative load/store
    EXPECT_NE(asmText.find("bl rt_modvar_addr_i64"), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("[x"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
