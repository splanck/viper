//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_source_loader_limits.cpp
// Purpose: Verify that loadSourceBuffer rejects files exceeding the 256 MB
//          limit with a clean error diagnostic instead of OOM-crashing.
// Key invariants: The loader must not attempt to read oversized files.
// Ownership/Lifetime: Creates and removes a temporary sparse file.
// Links: src/tools/common/source_loader.cpp
//
//===----------------------------------------------------------------------===//

#include "tests/common/PosixCompat.h"

#include "support/source_manager.hpp"
#include "tools/common/source_loader.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// ── Test: loadSourceBuffer rejects files > 256 MB ───────────────────────────
static void test_source_too_large()
{
#if !defined(_WIN32)
    const char *path = "/tmp/viper_large_source_test.vpr";

    // Create a sparse file > 256 MB using ftruncate (no actual disk usage)
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert(fd >= 0);
    off_t target_size = 257LL * 1024 * 1024; // 257 MB
    int rc = ftruncate(fd, target_size);
    assert(rc == 0);
    close(fd);

    // Verify the file reports the expected size
    struct stat st;
    stat(path, &st);
    assert(st.st_size == target_size);

    // loadSourceBuffer should reject it with "too large"
    il::support::SourceManager sm;
    auto result = il::tools::common::loadSourceBuffer(path, sm);
    assert(!result.hasValue());
    const auto &diag = result.error();
    std::string msg = diag.message;
    assert(msg.find("too large") != std::string::npos);

    // Also test loadSourceFile
    auto result2 = il::tools::common::loadSourceFile(path);
    assert(!result2.hasValue());
    const auto &diag2 = result2.error();
    std::string msg2 = diag2.message;
    assert(msg2.find("too large") != std::string::npos);

    remove(path);
#else
    printf("  SKIP: sparse file test not supported on Windows\n");
#endif
}

int main()
{
    SKIP_TEST_NO_FORK();

    test_source_too_large();
    printf("  PASS: loadSourceBuffer rejects file > 256 MB\n");

    printf("All source-loader-limits tests passed.\n");
    return 0;
}
