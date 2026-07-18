//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestPathExeDir.cpp
// Purpose: Unit tests for platform-aware Path.ExeDir() detection.
// Key invariants:
//   - Returned paths identify an existing directory and are never null.
//   - POSIX hosts return an absolute path rather than the legacy dot fallback.
//   - Platform-specific probing is selected through shared capability macros.
// Ownership/Lifetime:
//   - C-string results are owned and released by each test.
//   - Runtime strings retain and release their own storage.
// Links: src/runtime/io/rt_path.c, src/common/PlatformCapabilities.hpp
//
//===----------------------------------------------------------------------===//

#include "common/PlatformCapabilities.hpp"
#include "tests/TestHarness.hpp"

#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#if ZANNA_HOST_WINDOWS
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#endif

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

// VDOC-185: the dynamic-buffer implementation must return the REAL executable
// directory (an absolute path), not the "." truncation fallback.
TEST(PathExeDir, ReturnsAbsoluteNotDotFallback) {
    char *dir = rt_path_exe_dir_cstr();
    ASSERT_TRUE(dir != nullptr);
    // A valid resolved exe directory is absolute and never the bare "." that
    // the old fixed-buffer path returned on long/non-ASCII paths.
    EXPECT_TRUE(strcmp(dir, ".") != 0);
#if !ZANNA_HOST_WINDOWS
    EXPECT_EQ(dir[0], '/');
#endif
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
    return zanna_test::run_all_tests();
}
