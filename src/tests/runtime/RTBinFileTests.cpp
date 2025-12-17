//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTBinFileTests.cpp
// Purpose: Comprehensive tests for Viper.IO.BinFile binary file streams.
//
//===----------------------------------------------------------------------===//

#include "rt_binfile.h"
#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static const char *test_file = "/tmp/viper_binfile_test.bin";

static rt_string make_string(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static void cleanup_test_file()
{
    remove(test_file);
}

static void test_open_write_close()
{
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

static void test_write_and_read_bytes()
{
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

static void test_read_byte_write_byte()
{
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

static void test_seek_and_pos()
{
    cleanup_test_file();

    // Create a file with some content
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        for (int i = 0; i < 10; ++i)
        {
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

static void test_size()
{
    cleanup_test_file();

    // Create a file with 100 bytes
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        for (int i = 0; i < 100; ++i)
        {
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

static void test_eof()
{
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

static void test_append_mode()
{
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

static void test_read_write_mode()
{
    cleanup_test_file();

    // Create file first
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        for (int i = 0; i < 10; ++i)
        {
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

static void test_partial_read()
{
    cleanup_test_file();

    // Create a file with 10 bytes
    {
        rt_string path = make_string(test_file);
        rt_string mode = make_string("w");

        void *bf = rt_binfile_open(path, mode);
        for (int i = 0; i < 10; ++i)
        {
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
        for (int i = 0; i < 10; ++i)
        {
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

static void test_partial_write()
{
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

static void test_flush()
{
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

static void test_null_handling()
{
    // Null file operations should return safe defaults
    assert(rt_binfile_pos(nullptr) == -1);
    assert(rt_binfile_size(nullptr) == -1);
    assert(rt_binfile_eof(nullptr) == 1);

    // These should not crash
    rt_binfile_close(nullptr);
    rt_binfile_flush(nullptr);
}

int main()
{
    test_open_write_close();
    test_write_and_read_bytes();
    test_read_byte_write_byte();
    test_seek_and_pos();
    test_size();
    test_eof();
    test_append_mode();
    test_read_write_mode();
    test_partial_read();
    test_partial_write();
    test_flush();
    test_null_handling();

    cleanup_test_file();
    return 0;
}
