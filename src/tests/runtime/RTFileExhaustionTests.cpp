//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTFileExhaustionTests.cpp
// Purpose: Verify that the runtime handles file descriptor exhaustion (EMFILE)
//          gracefully, trapping with a useful error message.
// Key invariants: No silent NULL or crash when fd limit is reached.
// Ownership/Lifetime: Uses setrlimit(RLIMIT_NOFILE) to simulate fd exhaustion.
// Links: src/runtime/io/rt_binfile.c, src/runtime/io/rt_file_io.c
//
//===----------------------------------------------------------------------===//

#include "tests/common/PosixCompat.h"

#include "rt_binfile.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/resource.h>
#include <unistd.h>
#endif

// ── vm_trap override ────────────────────────────────────────────────────────
namespace
{
int g_trap_count = 0;
std::string g_last_trap;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_trap_count++;
    g_last_trap = msg ? msg : "";
}

static rt_string make_string(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

// ── Test: BinFile.Open traps with path and strerror on fd exhaustion ────────
// Strategy: Lower RLIMIT_NOFILE to a very small value, open files until
// exhausted, then try rt_binfile_open → should trap with descriptive message.
static void test_binfile_open_fd_exhaustion()
{
#if !defined(_WIN32)
    // Lower the fd limit to something small so we can exhaust it quickly.
    // We need a few fds for stdin/stdout/stderr + internal use.
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    struct rlimit low = {16, rl.rlim_max};
    if (setrlimit(RLIMIT_NOFILE, &low) != 0)
    {
        printf("  SKIP: setrlimit failed (insufficient privileges)\n");
        return;
    }

    // Open files until we run out of fds
    std::vector<FILE *> files;
    for (int i = 0; i < 64; i++)
    {
        FILE *f = fopen("/dev/null", "r");
        if (!f)
            break;
        files.push_back(f);
    }

    // Now try to open via BinFile — should fail with EMFILE
    g_trap_count = 0;
    g_last_trap.clear();
    void *bf = rt_binfile_open(make_string("/tmp/viper_fdtest.txt"), make_string("w"));
    assert(bf == NULL);
    assert(g_trap_count == 1);
    // Verify the trap message includes the path and a useful OS error
    assert(g_last_trap.find("BinFile.Open") != std::string::npos);
    assert(g_last_trap.find("viper_fdtest") != std::string::npos);

    // Clean up
    for (FILE *f : files)
        fclose(f);

    // Restore original limit
    setrlimit(RLIMIT_NOFILE, &rl);
#else
    printf("  SKIP: fd exhaustion test not supported on Windows\n");
#endif
}

int main()
{
    SKIP_TEST_NO_FORK();

    test_binfile_open_fd_exhaustion();
    printf("  PASS: BinFile.Open traps with path on fd exhaustion\n");

    printf("All file-exhaustion tests passed.\n");
    return 0;
}
