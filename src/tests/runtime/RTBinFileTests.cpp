//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTBinFileTests.cpp
// Purpose: Comprehensive tests for Zanna.IO.BinFile binary file streams.
//
//===----------------------------------------------------------------------===//

#include "rt_binfile.h"
#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"
#include "tests/common/PlatformSkip.h"

#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
        assert(g_last_trap != nullptr);                                                            \
    } while (0)

static const char *test_file = "/tmp/zanna_binfile_test.bin";

static rt_string make_string(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void cleanup_test_file() {
    remove(test_file);
}

static void test_open_write_close() {
    cleanup_test_file();

    rt_string path = make_string(test_file);
    rt_string mode = make_string("w");

    void *bf = rt_binfile_open(path, mode);
    assert(bf != nullptr);
    assert(rt_binfile_eof(bf) == 0);

    rt_binfile_close(bf);
    // Should be able to close twice without issue
    rt_binfile_close(bf);

    cleanup_test_file();
}

static void test_write_and_read_bytes() {
    cleanup_test_file();

    // Write some bytes
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        assert(bf != nullptr);

        void *bytes = rt_bytes_new(4);
        rt_bytes_set(bytes, 0, 0xCA);
        rt_bytes_set(bytes, 1, 0xFE);
        rt_bytes_set(bytes, 2, 0xBA);
        rt_bytes_set(bytes, 3, 0xBE);

        rt_binfile_write(bf, bytes, 0, 4);
        rt_binfile_close(bf);
    }

    // Read them back
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");

        void *bf = rt_binfile_open(path, mode);
        assert(bf != nullptr);

        void *bytes = rt_bytes_new(4);
        int64_t read = rt_binfile_read(bf, bytes, 0, 4);
        assert(read == 4);

        assert(rt_bytes_get(bytes, 0) == 0xCA);
        assert(rt_bytes_get(bytes, 1) == 0xFE);
        assert(rt_bytes_get(bytes, 2) == 0xBA);
        assert(rt_bytes_get(bytes, 3) == 0xBE);

        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_read_byte_write_byte() {
    cleanup_test_file();

    // Write bytes one at a time
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        assert(bf != nullptr);

        rt_binfile_write_byte(bf, 0x12);
        rt_binfile_write_byte(bf, 0x34);
        rt_binfile_write_byte(bf, 0x56);
        rt_binfile_write_byte(bf, 0x78);

        rt_binfile_close(bf);
    }

    // Read bytes one at a time
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");

        void *bf = rt_binfile_open(path, mode);
        assert(bf != nullptr);

        assert(rt_binfile_read_byte(bf) == 0x12);
        assert(rt_binfile_read_byte(bf) == 0x34);
        assert(rt_binfile_read_byte(bf) == 0x56);
        assert(rt_binfile_read_byte(bf) == 0x78);

        // Should return -1 at EOF
        assert(rt_binfile_read_byte(bf) == -1);
        assert(rt_binfile_eof(bf) == 1);

        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_write_byte_range_traps() {
    cleanup_test_file();

    rt_string path = make_string(test_file);
    rt_string mode = make_string("w");
    void *bf = rt_binfile_open(path, mode);
    assert(bf != nullptr);

    EXPECT_TRAP(rt_binfile_write_byte(bf, -1));
    EXPECT_TRAP(rt_binfile_write_byte(bf, 256));
    rt_binfile_write_byte(bf, 255);
    rt_binfile_close(bf);

    cleanup_test_file();
}

static void test_seek_and_pos() {
    cleanup_test_file();

    // Create a file with some content
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        for (int i = 0; i < 10; ++i) {
            rt_binfile_write_byte(bf, (int64_t)i);
        }
        rt_binfile_close(bf);
    }

    // Test seeking
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");

        void *bf = rt_binfile_open(path, mode);
        assert(bf != nullptr);

        // Position should start at 0
        assert(rt_binfile_pos(bf) == 0);

        // Seek to position 5 from start
        int64_t new_pos = rt_binfile_seek(bf, 5, 0);
        assert(new_pos == 5);
        assert(rt_binfile_pos(bf) == 5);

        // Read byte at position 5
        assert(rt_binfile_read_byte(bf) == 5);

        // Seek from current (+2)
        new_pos = rt_binfile_seek(bf, 2, 1);
        assert(new_pos == 8);
        assert(rt_binfile_read_byte(bf) == 8);

        // Seek from end (-2)
        new_pos = rt_binfile_seek(bf, -2, 2);
        assert(new_pos == 8);
        assert(rt_binfile_read_byte(bf) == 8);

        // Seek back to start
        rt_binfile_seek(bf, 0, 0);
        assert(rt_binfile_read_byte(bf) == 0);

        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_size() {
    cleanup_test_file();

    // Create a file with 100 bytes
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        for (int i = 0; i < 100; ++i) {
            rt_binfile_write_byte(bf, 0);
        }
        rt_binfile_close(bf);
    }

    // Check size
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");

        void *bf = rt_binfile_open(path, mode);
        assert(rt_binfile_size(bf) == 100);
        // Position should still be 0 after size query
        assert(rt_binfile_pos(bf) == 0);
        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_large_count_clamps_without_overflow() {
    cleanup_test_file();

    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");
        void *bf = rt_binfile_open(path, mode);
        for (int i = 0; i < 4; ++i)
            rt_binfile_write_byte(bf, 'A' + i);
        rt_binfile_close(bf);
    }

    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");
        void *bf = rt_binfile_open(path, mode);
        void *bytes = rt_bytes_new(4);
        int64_t read = rt_binfile_read(bf, bytes, 1, INT64_MAX);
        assert(read == 3);
        assert(rt_bytes_get(bytes, 1) == 'A');
        assert(rt_bytes_get(bytes, 3) == 'C');
        rt_binfile_close(bf);
    }

    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");
        void *bf = rt_binfile_open(path, mode);
        void *bytes = rt_bytes_new(4);
        rt_bytes_set(bytes, 0, 'W');
        rt_bytes_set(bytes, 1, 'X');
        rt_bytes_set(bytes, 2, 'Y');
        rt_bytes_set(bytes, 3, 'Z');
        rt_binfile_write(bf, bytes, 1, INT64_MAX);
        rt_binfile_close(bf);
    }

    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");
        void *bf = rt_binfile_open(path, mode);
        assert(rt_binfile_size(bf) == 3);
        assert(rt_binfile_read_byte(bf) == 'X');
        assert(rt_binfile_read_byte(bf) == 'Y');
        assert(rt_binfile_read_byte(bf) == 'Z');
        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_invalid_bytes_handle_traps() {
    cleanup_test_file();

    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");
        void *bf = rt_binfile_open(path, mode);
        assert(bf != nullptr);
        EXPECT_TRAP(rt_binfile_write(bf, nullptr, 0, 1));
        rt_binfile_close(bf);
    }

    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");
        void *bf = rt_binfile_open(path, mode);
        assert(bf != nullptr);
        EXPECT_TRAP(rt_binfile_read(bf, nullptr, 0, 1));
        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_write_invalid_bytes_data_traps_before_write() {
    cleanup_test_file();

    struct FakeBytes {
        int64_t len;
        uint8_t *data;
    };

    rt_string path = make_string(test_file);
    rt_string mode = make_string("w");
    void *bf = rt_binfile_open(path, mode);
    assert(bf != nullptr);

    FakeBytes *bad =
        static_cast<FakeBytes *>(rt_obj_new_i64(RT_BYTES_CLASS_ID, sizeof(FakeBytes)));
    bad->len = 1;
    bad->data = nullptr;

    EXPECT_TRAP(rt_binfile_write(bf, bad, 0, 1));
    rt_binfile_write_byte(bf, 0x55);
    rt_binfile_close(bf);

    mode = make_string("r");
    bf = rt_binfile_open(path, mode);
    assert(rt_binfile_read_byte(bf) == 0x55);
    assert(rt_binfile_read_byte(bf) == -1);
    rt_binfile_close(bf);

    if (rt_obj_release_check0(bad))
        rt_obj_free(bad);
    cleanup_test_file();
}

static void test_read_invalid_bytes_data_traps_before_read() {
    cleanup_test_file();

    struct FakeBytes {
        int64_t len;
        uint8_t *data;
    };

    rt_string path = make_string(test_file);
    rt_string mode = make_string("w");
    void *bf = rt_binfile_open(path, mode);
    assert(bf != nullptr);
    rt_binfile_write_byte(bf, 0x66);
    rt_binfile_close(bf);

    mode = make_string("r");
    bf = rt_binfile_open(path, mode);
    assert(bf != nullptr);

    FakeBytes *bad =
        static_cast<FakeBytes *>(rt_obj_new_i64(RT_BYTES_CLASS_ID, sizeof(FakeBytes)));
    bad->len = 1;
    bad->data = nullptr;

    EXPECT_TRAP(rt_binfile_read(bf, bad, 0, 1));
    assert(rt_binfile_pos(bf) == 0);
    assert(rt_binfile_read_byte(bf) == 0x66);
    rt_binfile_close(bf);

    if (rt_obj_release_check0(bad))
        rt_obj_free(bad);
    cleanup_test_file();
}

static void test_eof() {
    cleanup_test_file();

    // Create a file with 3 bytes
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        rt_binfile_write_byte(bf, 1);
        rt_binfile_write_byte(bf, 2);
        rt_binfile_write_byte(bf, 3);
        rt_binfile_close(bf);
    }

    // Read and check EOF
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");

        void *bf = rt_binfile_open(path, mode);
        assert(rt_binfile_eof(bf) == 0);

        rt_binfile_read_byte(bf);
        assert(rt_binfile_eof(bf) == 0);

        rt_binfile_read_byte(bf);
        assert(rt_binfile_eof(bf) == 0);

        rt_binfile_read_byte(bf);
        // Not EOF yet until we try to read past end
        assert(rt_binfile_eof(bf) == 0);

        // This read should set EOF
        int64_t result = rt_binfile_read_byte(bf);
        assert(result == -1);
        assert(rt_binfile_eof(bf) == 1);

        // Seeking should clear EOF
        rt_binfile_seek(bf, 0, 0);
        assert(rt_binfile_eof(bf) == 0);

        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_append_mode() {
    cleanup_test_file();

    // Create initial file
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        rt_binfile_write_byte(bf, 1);
        rt_binfile_write_byte(bf, 2);
        rt_binfile_close(bf);
    }

    // Append to file
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("a");

        void *bf = rt_binfile_open(path, mode);
        rt_binfile_write_byte(bf, 3);
        rt_binfile_write_byte(bf, 4);
        rt_binfile_close(bf);
    }

    // Verify contents
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");

        void *bf = rt_binfile_open(path, mode);
        assert(rt_binfile_size(bf) == 4);
        assert(rt_binfile_read_byte(bf) == 1);
        assert(rt_binfile_read_byte(bf) == 2);
        assert(rt_binfile_read_byte(bf) == 3);
        assert(rt_binfile_read_byte(bf) == 4);
        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_read_write_mode() {
    cleanup_test_file();

    // Create file first
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        for (int i = 0; i < 10; ++i) {
            rt_binfile_write_byte(bf, (int64_t)i);
        }
        rt_binfile_close(bf);
    }

    // Open for read/write
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("rw");

        void *bf = rt_binfile_open(path, mode);

        // Read first byte
        assert(rt_binfile_read_byte(bf) == 0);

        // Seek to position 5 and overwrite
        rt_binfile_seek(bf, 5, 0);
        rt_binfile_write_byte(bf, 99);

        // Seek back and verify
        rt_binfile_seek(bf, 5, 0);
        assert(rt_binfile_read_byte(bf) == 99);

        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_read_to_write_transition_without_user_seek() {
    cleanup_test_file();

    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");
        void *bf = rt_binfile_open(path, mode);
        rt_binfile_write_byte(bf, 'A');
        rt_binfile_write_byte(bf, 'B');
        rt_binfile_write_byte(bf, 'C');
        rt_binfile_write_byte(bf, 'D');
        rt_binfile_close(bf);
    }

    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("rw");
        void *bf = rt_binfile_open(path, mode);
        assert(rt_binfile_read_byte(bf) == 'A');
        rt_binfile_write_byte(bf, 'Z');
        rt_binfile_close(bf);
    }

    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");
        void *bf = rt_binfile_open(path, mode);
        assert(rt_binfile_read_byte(bf) == 'A');
        assert(rt_binfile_read_byte(bf) == 'Z');
        assert(rt_binfile_read_byte(bf) == 'C');
        assert(rt_binfile_read_byte(bf) == 'D');
        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_partial_read() {
    cleanup_test_file();

    // Create a file with 10 bytes
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        for (int i = 0; i < 10; ++i) {
            rt_binfile_write_byte(bf, (int64_t)(i + 1));
        }
        rt_binfile_close(bf);
    }

    // Read into middle of buffer
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");

        void *bf = rt_binfile_open(path, mode);
        void *bytes = rt_bytes_new(20);

        // Read 5 bytes starting at offset 10 in the buffer
        int64_t read = rt_binfile_read(bf, bytes, 10, 5);
        assert(read == 5);

        // First 10 bytes should be 0
        for (int i = 0; i < 10; ++i) {
            assert(rt_bytes_get(bytes, i) == 0);
        }

        // Next 5 should be 1-5
        assert(rt_bytes_get(bytes, 10) == 1);
        assert(rt_bytes_get(bytes, 11) == 2);
        assert(rt_bytes_get(bytes, 12) == 3);
        assert(rt_bytes_get(bytes, 13) == 4);
        assert(rt_bytes_get(bytes, 14) == 5);

        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_partial_write() {
    cleanup_test_file();

    // Write from middle of buffer
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        void *bytes = rt_bytes_new(10);

        // Set bytes 5-9 to interesting values
        rt_bytes_set(bytes, 5, 0xAA);
        rt_bytes_set(bytes, 6, 0xBB);
        rt_bytes_set(bytes, 7, 0xCC);
        rt_bytes_set(bytes, 8, 0xDD);
        rt_bytes_set(bytes, 9, 0xEE);

        // Write only bytes 5-9
        rt_binfile_write(bf, bytes, 5, 5);
        rt_binfile_close(bf);
    }

    // Verify
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("r");

        void *bf = rt_binfile_open(path, mode);
        assert(rt_binfile_size(bf) == 5);

        assert(rt_binfile_read_byte(bf) == 0xAA);
        assert(rt_binfile_read_byte(bf) == 0xBB);
        assert(rt_binfile_read_byte(bf) == 0xCC);
        assert(rt_binfile_read_byte(bf) == 0xDD);
        assert(rt_binfile_read_byte(bf) == 0xEE);

        rt_binfile_close(bf);
    }

    cleanup_test_file();
}

static void test_flush() {
    cleanup_test_file();

    rt_string path = make_string(test_file);
    rt_string mode = make_string("w");

    void *bf = rt_binfile_open(path, mode);
    rt_binfile_write_byte(bf, 42);

    // Flush should not crash
    rt_binfile_flush(bf);

    rt_binfile_close(bf);

    // Verify byte was written
    {
        rt_string rpath = make_string(test_file);
        rt_string rmode = make_string("r");
        void *rbf = rt_binfile_open(rpath, rmode);
        assert(rt_binfile_read_byte(rbf) == 42);
        rt_binfile_close(rbf);
    }

    cleanup_test_file();
}

static void test_null_handling() {
    // Null file operations should return safe defaults
    assert(rt_binfile_pos(nullptr) == -1);
    assert(rt_binfile_size(nullptr) == -1);
    assert(rt_binfile_eof(nullptr) == 1);

    // These should not crash
    rt_binfile_close(nullptr);
    rt_binfile_flush(nullptr);
}

static void test_open_rejects_embedded_nul_mode() {
    cleanup_test_file();

    rt_string path = make_string(test_file);
    const char mode_bytes[] = {'r', '\0', 'w'};
    rt_string mode = rt_string_from_bytes(mode_bytes, sizeof(mode_bytes));

    EXPECT_TRAP(rt_binfile_open(path, mode));
    rt_string_unref(mode);
    cleanup_test_file();
}

int main() {
#ifdef _WIN32
    // Skip on Windows: test uses /tmp paths not available on Windows
    ZANNA_PLATFORM_SKIP("POSIX temp paths not available on Windows");
#endif
    test_open_write_close();
    test_write_and_read_bytes();
    test_read_byte_write_byte();
    test_write_byte_range_traps();
    test_seek_and_pos();
    test_size();
    test_large_count_clamps_without_overflow();
    test_invalid_bytes_handle_traps();
    test_write_invalid_bytes_data_traps_before_write();
    test_read_invalid_bytes_data_traps_before_read();
    test_eof();
    test_append_mode();
    test_read_write_mode();
    test_read_to_write_transition_without_user_seek();
    test_partial_read();
    test_partial_write();
    test_flush();
    test_null_handling();
    test_open_rejects_embedded_nul_mode();

    cleanup_test_file();
    return 0;
}
