//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_obj_field_gep.cpp
// Purpose: Verify GEP + load/store on object memory via rt_obj_new_i64.
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

TEST(Arm64CLI, ObjField_Gep_LoadStore_Run)
{
    const std::string in = outPath("arm64_obj_field_gep.il");
    const std::string il = "il 0.1\n"
                           "extern @rt_obj_new_i64(i64, i64) -> ptr\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  %p = call @rt_obj_new_i64(0, 16)\n"
                           "  %f = gep %p, 8\n"
                           "  store i64, %f, 7\n"
                           "  %v = load i64, %f\n"
                           "  ret %v\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    ASSERT_EQ(rc, 7);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
