//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_arr_obj_put_get.cpp
// Purpose: Verify rt_arr_obj_put/get with a freshly allocated object.
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
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

TEST(Arm64CLI, ArrObj_PutGet_NonNull_Run)
{
    const std::string in = outPath("arm64_arr_obj_put_get.il");
    const std::string il = "il 0.1\n"
                           "extern @rt_arr_obj_new(i64) -> ptr\n"
                           "extern @rt_arr_obj_put(ptr, i64, ptr) -> void\n"
                           "extern @rt_arr_obj_get(ptr, i64) -> ptr\n"
                           "extern @rt_obj_new_i64(i64, i64) -> ptr\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  %arr = call @rt_arr_obj_new(1)\n"
                           "  %obj = call @rt_obj_new_i64(0, 16)\n"
                           "  call @rt_arr_obj_put(%arr, 0, %obj)\n"
                           "  %got = call @rt_arr_obj_get(%arr, 0)\n"
                           "  %isnull = icmp.eq %got, 0\n"
                           "  %res = select %isnull, 0, 1\n"
                           "  ret %res\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    ASSERT_EQ(rc, 1);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
