//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestPathExeDir.cpp
// Purpose: Unit tests for Path.ExeDir() — platform exe directory detection.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

extern "C" {
#include "rt_string.h"
char *rt_path_exe_dir_cstr(void);
rt_string rt_path_exe_dir_str(void);
}

TEST(PathExeDir, ReturnsNonNull) {
    char *dir = rt_path_exe_dir_cstr();
    ASSERT_TRUE(dir != nullptr);
    EXPECT_TRUE(strlen(dir) > 0);
    free(dir);
}

TEST(PathExeDir, ReturnsDirectory) {
    char *dir = rt_path_exe_dir_cstr();
    ASSERT_TRUE(dir != nullptr);

    struct stat st;
    EXPECT_EQ(stat(dir, &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    free(dir);
}

TEST(PathExeDir, RuntimeStringWorks) {
    rt_string str = rt_path_exe_dir_str();
    ASSERT_TRUE(str != nullptr);

    const char *cstr = rt_string_cstr(str);
    ASSERT_TRUE(cstr != nullptr);
    EXPECT_TRUE(strlen(cstr) > 0);
}

int main() {
    return viper_test::run_all_tests();
}
