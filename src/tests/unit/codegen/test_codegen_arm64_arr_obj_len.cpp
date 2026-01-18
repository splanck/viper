//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_arr_obj_len.cpp
// Purpose: Verify arm64 lowers calls to rt_arr_obj_* and returns length.
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/viper/cmd_codegen_arm64.hpp"

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

TEST(Arm64CLI, ArrObj_Len_Run)
{
    const std::string in = outPath("arm64_arr_obj_len.il");
    const std::string il = "il 0.1\n"
                           "extern @rt_arr_obj_new(i64) -> ptr\n"
                           "extern @rt_arr_obj_len(ptr) -> i64\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  %a = call @rt_arr_obj_new(3)\n"
                           "  %n = call @rt_arr_obj_len(%a)\n"
                           "  %ok = icmp_eq %n, 3\n"
                           "  cbr %ok, ^yes, ^no\n"
                           "yes:\n"
                           "  ret 0\n"
                           "no:\n"
                           "  ret 1\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
