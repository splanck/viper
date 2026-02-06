//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTStreamTests.cpp
// Purpose: Validate unified Stream interface.
// Key invariants: Stream wraps BinFile/MemStream transparently.
// Links: docs/viperlib/io.md
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_stream.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Create a Bytes object from raw data.
static void *make_bytes(const uint8_t *data, size_t len)
{
    void *bytes = rt_bytes_new((int64_t)len);
    for (size_t i = 0; i < len; i++)
    {
        rt_bytes_set(bytes, (int64_t)i, data[i]);
    }
    return bytes;
}

/// @brief Create a Bytes object from a C string.
static void *make_bytes_str(const char *str)
{
    return make_bytes((const uint8_t *)str, strlen(str));
}

/// @brief Compare two Bytes objects for equality.
static bool bytes_equal(void *a, void *b)
{
    int64_t len_a = rt_bytes_len(a);
    int64_t len_b = rt_bytes_len(b);
    if (len_a != len_b)
        return false;

    for (int64_t i = 0; i < len_a; i++)
    {
        if (rt_bytes_get(a, i) != rt_bytes_get(b, i))
            return false;
    }
    return true;
}

//=============================================================================
// Memory Stream Tests
//=============================================================================

static void test_memory_stream_basic()
{
    printf("Testing Stream with memory backend:\n");

    // Test 1: Create and write
    {
        void *stream = rt_stream_open_memory();
        test_result("OpenMemory creates stream", stream != NULL);
        test_result("Type is MEMSTREAM", rt_stream_get_type(stream) == RT_STREAM_TYPE_MEMSTREAM);
        test_result("Initial pos is 0", rt_stream_get_pos(stream) == 0);
        test_result("Initial len is 0", rt_stream_get_len(stream) == 0);
    }

    // Test 2: Write and read back
    {
        void *stream = rt_stream_open_memory();
        void *data = make_bytes_str("Hello, Stream!");

        rt_stream_write(stream, data);
        test_result("Write advances pos", rt_stream_get_pos(stream) == 14);
        test_result("Write updates len", rt_stream_get_len(stream) == 14);

        // Seek back to start and read
        rt_stream_set_pos(stream, 0);
        test_result("SetPos works", rt_stream_get_pos(stream) == 0);

        void *read_data = rt_stream_read(stream, 14);
        test_result("Read returns correct data", bytes_equal(data, read_data));
    }

    // Test 3: Read byte by byte
    {
        void *stream = rt_stream_open_bytes(make_bytes_str("ABC"));

        test_result("ReadByte 1", rt_stream_read_byte(stream) == 'A');
        test_result("ReadByte 2", rt_stream_read_byte(stream) == 'B');
        test_result("ReadByte 3", rt_stream_read_byte(stream) == 'C');
        test_result("ReadByte EOF", rt_stream_read_byte(stream) == -1);
    }

    // Test 4: Write byte by byte
    {
        void *stream = rt_stream_open_memory();

        rt_stream_write_byte(stream, 'X');
        rt_stream_write_byte(stream, 'Y');
        rt_stream_write_byte(stream, 'Z');

        test_result("WriteByte updates len", rt_stream_get_len(stream) == 3);

        void *bytes = rt_stream_to_bytes(stream);
        test_result("ToBytes works", bytes != NULL);
        test_result("ToBytes correct length", rt_bytes_len(bytes) == 3);
        test_result("ToBytes correct data",
                    rt_bytes_get(bytes, 0) == 'X' && rt_bytes_get(bytes, 1) == 'Y' &&
                        rt_bytes_get(bytes, 2) == 'Z');
    }

    // Test 5: EOF detection
    {
        void *stream = rt_stream_open_bytes(make_bytes_str("AB"));

        test_result("Not EOF at start", !rt_stream_is_eof(stream));
        rt_stream_read(stream, 2);
        test_result("EOF after reading all", rt_stream_is_eof(stream));
    }

    // Test 6: ReadAll
    {
        void *stream = rt_stream_open_bytes(make_bytes_str("Hello World"));

        // Read first 6 bytes
        rt_stream_read(stream, 6);
        test_result("Partial read pos", rt_stream_get_pos(stream) == 6);

        // ReadAll gets remaining
        void *remaining = rt_stream_read_all(stream);
        test_result("ReadAll length", rt_bytes_len(remaining) == 5);
    }

    printf("\n");
}

//=============================================================================
// Conversion Tests
//=============================================================================

static void test_stream_conversion()
{
    printf("Testing Stream conversion methods:\n");

    // Test 1: AsMemStream
    {
        void *stream = rt_stream_open_memory();
        void *ms = rt_stream_as_memstream(stream);
        test_result("AsMemStream returns memstream", ms != NULL);

        void *bf = rt_stream_as_binfile(stream);
        test_result("AsBinFile returns NULL for memory", bf == NULL);
    }

    // Test 2: FromMemStream (wrap existing)
    {
        void *original = rt_stream_open_memory();
        rt_stream_write(original, make_bytes_str("Test"));

        void *ms = rt_stream_as_memstream(original);
        void *wrapped = rt_stream_from_memstream(ms);

        test_result("FromMemStream creates wrapper", wrapped != NULL);

        // Both streams should see the same data
        rt_stream_set_pos(wrapped, 0);
        void *data = rt_stream_read(wrapped, 4);
        test_result("Wrapped reads same data", rt_bytes_len(data) == 4);
    }

    printf("\n");
}

//=============================================================================
// Edge Cases
//=============================================================================

static void test_edge_cases()
{
    printf("Testing Stream edge cases:\n");

    // Test 1: Empty stream
    {
        void *stream = rt_stream_open_memory();
        test_result("Empty stream len", rt_stream_get_len(stream) == 0);
        test_result("Empty stream EOF", rt_stream_is_eof(stream));

        void *data = rt_stream_read_all(stream);
        test_result("ReadAll on empty returns empty bytes", rt_bytes_len(data) == 0);
    }

    // Test 2: Large data
    {
        const size_t size = 10000;
        void *bytes = rt_bytes_new((int64_t)size);
        for (size_t i = 0; i < size; i++)
        {
            rt_bytes_set(bytes, (int64_t)i, (int64_t)(i % 256));
        }

        void *stream = rt_stream_open_bytes(bytes);
        void *read_back = rt_stream_read_all(stream);

        test_result("Large data roundtrip", bytes_equal(bytes, read_back));
    }

    // Test 3: Seek beyond end
    {
        void *stream = rt_stream_open_bytes(make_bytes_str("ABC"));
        rt_stream_set_pos(stream, 100); // Beyond end
        test_result("Seek beyond end - EOF", rt_stream_is_eof(stream));
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Stream Tests ===\n\n");

    test_memory_stream_basic();
    test_stream_conversion();
    test_edge_cases();

    printf("All Stream tests passed!\n");
    return 0;
}
