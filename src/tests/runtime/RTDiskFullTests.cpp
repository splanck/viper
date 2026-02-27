//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTDiskFullTests.cpp
// Purpose: Verify that LineWriter and BinFile trap cleanly on write/flush
//          failures (disk full, I/O error) instead of silently losing data.
// Key invariants: fputc, fwrite, and fflush return values must be checked.
// Ownership/Lifetime: Uses fork-based isolation for trap verification.
// Links: src/runtime/io/rt_linewriter.c, src/runtime/io/rt_binfile.c
//
//===----------------------------------------------------------------------===//

#include "tests/common/PosixCompat.h"

#include "rt_binfile.h"
#include "rt_internal.h"
#include "rt_linewriter.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if !defined(_WIN32)
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

// ── Test: LineWriter.WriteChar traps on fputc failure ───────────────────────
// Strategy: Open a LineWriter to a real file, then replace its internal FILE*
// with a read-only stream so fputc returns EOF.
static void test_linewriter_write_char_traps()
{
    // Create a valid LineWriter first
    const char *path = "/tmp/viper_diskfull_test_wc.txt";
    void *lw = rt_linewriter_open(make_string(path));
    assert(lw != nullptr);

    // Now replace the FILE* with a read-only stream to force fputc to fail.
    // The struct layout is: { FILE *fp; int8_t closed; rt_string newline; }
    // We open /dev/null in read-only mode — fputc to a read-only stream = EOF.
    FILE **fp_ptr = (FILE **)lw;
    FILE *orig_fp = *fp_ptr;
    fclose(orig_fp);

    FILE *readonly_fp = fopen("/dev/null", "r");
    assert(readonly_fp != nullptr);
    *fp_ptr = readonly_fp;

    g_trap_count = 0;
    g_last_trap.clear();
    rt_linewriter_write_char(lw, 'A');
    assert(g_trap_count == 1);
    assert(g_last_trap.find("WriteChar") != std::string::npos);
    assert(g_last_trap.find("write failed") != std::string::npos);

    // Restore and clean up
    fclose(readonly_fp);
    *fp_ptr = nullptr;
    remove(path);
}

// ── Test: LineWriter.Flush traps on fflush failure ──────────────────────────
// Strategy: Same approach — replace FILE* with a broken stream.
static void test_linewriter_flush_traps()
{
    const char *path = "/tmp/viper_diskfull_test_fl.txt";
    void *lw = rt_linewriter_open(make_string(path));
    assert(lw != nullptr);

    // Replace FILE* with a read-only stream — fflush on a read-only
    // stream that hasn't been read returns 0 on most platforms, so we
    // need a different approach. Write some data first, then close the
    // underlying fd to make fflush fail.
    FILE **fp_ptr = (FILE **)lw;
    FILE *fp = *fp_ptr;

    // Write something to dirty the buffer
    fputc('X', fp);

    // Close the underlying fd — next fflush will fail with EBADF
    int fd = fileno(fp);
    close(fd);

    g_trap_count = 0;
    g_last_trap.clear();
    rt_linewriter_flush(lw);
    assert(g_trap_count == 1);
    assert(g_last_trap.find("Flush") != std::string::npos);
    assert(g_last_trap.find("flush failed") != std::string::npos);

    // The FILE* is now broken — set to NULL so finalizer doesn't double-close
    *fp_ptr = nullptr;
    remove(path);
}

// ── Test: BinFile.Flush traps on fflush failure ─────────────────────────────
static void test_binfile_flush_traps()
{
    const char *path = "/tmp/viper_diskfull_test_bf.txt";
    void *bf = rt_binfile_open(make_string(path), make_string("w"));
    assert(bf != nullptr);

    // BinFile struct layout: { FILE *fp; int8_t eof; int8_t closed; }
    FILE **fp_ptr = (FILE **)bf;
    FILE *fp = *fp_ptr;

    // Write something to dirty the buffer, then break the fd
    fputc('Y', fp);
    int fd = fileno(fp);
    close(fd);

    g_trap_count = 0;
    g_last_trap.clear();
    rt_binfile_flush(bf);
    assert(g_trap_count == 1);
    assert(g_last_trap.find("Flush") != std::string::npos);
    assert(g_last_trap.find("flush failed") != std::string::npos);

    *fp_ptr = nullptr;
    remove(path);
}

int main()
{
    SKIP_TEST_NO_FORK();

    test_linewriter_write_char_traps();
    printf("  PASS: LineWriter.WriteChar traps on fputc failure\n");

    test_linewriter_flush_traps();
    printf("  PASS: LineWriter.Flush traps on fflush failure\n");

    test_binfile_flush_traps();
    printf("  PASS: BinFile.Flush traps on fflush failure\n");

    printf("All disk-full tests passed.\n");
    return 0;
}
