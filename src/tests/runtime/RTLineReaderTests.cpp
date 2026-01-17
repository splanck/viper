//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTLineReaderTests.cpp
// Purpose: Comprehensive tests for Viper.IO.LineReader text file reading.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_linereader.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static const char *test_file = "/tmp/viper_linereader_test.txt";

static rt_string make_string(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static void cleanup_test_file()
{
    remove(test_file);
}

static void write_raw_file(const char *data, size_t len)
{
    FILE *fp = fopen(test_file, "wb");
    assert(fp != nullptr);
    fwrite(data, 1, len, fp);
    fclose(fp);
}

static void test_open_close()
{
    cleanup_test_file();
    write_raw_file("test\n", 5);

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);
    assert(rt_linereader_eof(lr) == 0);

    rt_linereader_close(lr);
    // Should be able to close twice without issue
    rt_linereader_close(lr);

    cleanup_test_file();
}

static void test_read_lf_lines()
{
    cleanup_test_file();

    // Unix-style LF line endings
    const char *content = "line1\nline2\nline3\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string line1 = rt_linereader_read(lr);
    assert(rt_len(line1) == 5);
    assert(strncmp(rt_string_cstr(line1), "line1", 5) == 0);

    rt_string line2 = rt_linereader_read(lr);
    assert(rt_len(line2) == 5);
    assert(strncmp(rt_string_cstr(line2), "line2", 5) == 0);

    rt_string line3 = rt_linereader_read(lr);
    assert(rt_len(line3) == 5);
    assert(strncmp(rt_string_cstr(line3), "line3", 5) == 0);

    // Reading at EOF should return empty string and set eof
    rt_string empty = rt_linereader_read(lr);
    assert(rt_len(empty) == 0);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_crlf_lines()
{
    cleanup_test_file();

    // Windows-style CRLF line endings
    const char *content = "line1\r\nline2\r\nline3\r\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string line1 = rt_linereader_read(lr);
    assert(rt_len(line1) == 5);
    assert(strncmp(rt_string_cstr(line1), "line1", 5) == 0);

    rt_string line2 = rt_linereader_read(lr);
    assert(rt_len(line2) == 5);
    assert(strncmp(rt_string_cstr(line2), "line2", 5) == 0);

    rt_string line3 = rt_linereader_read(lr);
    assert(rt_len(line3) == 5);
    assert(strncmp(rt_string_cstr(line3), "line3", 5) == 0);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_cr_lines()
{
    cleanup_test_file();

    // Classic Mac CR line endings
    const char *content = "line1\rline2\rline3\r";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string line1 = rt_linereader_read(lr);
    assert(rt_len(line1) == 5);
    assert(strncmp(rt_string_cstr(line1), "line1", 5) == 0);

    rt_string line2 = rt_linereader_read(lr);
    assert(rt_len(line2) == 5);
    assert(strncmp(rt_string_cstr(line2), "line2", 5) == 0);

    rt_string line3 = rt_linereader_read(lr);
    assert(rt_len(line3) == 5);
    assert(strncmp(rt_string_cstr(line3), "line3", 5) == 0);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_mixed_endings()
{
    cleanup_test_file();

    // Mixed line endings
    const char *content = "lf\ncrlf\r\ncr\rend";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string l1 = rt_linereader_read(lr);
    assert(rt_len(l1) == 2);
    assert(strncmp(rt_string_cstr(l1), "lf", 2) == 0);

    rt_string l2 = rt_linereader_read(lr);
    assert(rt_len(l2) == 4);
    assert(strncmp(rt_string_cstr(l2), "crlf", 4) == 0);

    rt_string l3 = rt_linereader_read(lr);
    assert(rt_len(l3) == 2);
    assert(strncmp(rt_string_cstr(l3), "cr", 2) == 0);

    // Last line without newline
    rt_string l4 = rt_linereader_read(lr);
    assert(rt_len(l4) == 3);
    assert(strncmp(rt_string_cstr(l4), "end", 3) == 0);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_char()
{
    cleanup_test_file();

    const char *content = "ABC";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    assert(rt_linereader_read_char(lr) == 'A');
    assert(rt_linereader_read_char(lr) == 'B');
    assert(rt_linereader_read_char(lr) == 'C');

    // EOF should return -1
    assert(rt_linereader_read_char(lr) == -1);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_peek_char()
{
    cleanup_test_file();

    const char *content = "XYZ";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    // Peek should not consume
    assert(rt_linereader_peek_char(lr) == 'X');
    assert(rt_linereader_peek_char(lr) == 'X');

    // Read should consume
    assert(rt_linereader_read_char(lr) == 'X');

    // Peek next
    assert(rt_linereader_peek_char(lr) == 'Y');
    assert(rt_linereader_read_char(lr) == 'Y');

    assert(rt_linereader_peek_char(lr) == 'Z');
    assert(rt_linereader_read_char(lr) == 'Z');

    // Peek at EOF
    assert(rt_linereader_peek_char(lr) == -1);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_peek_then_read_line()
{
    cleanup_test_file();

    const char *content = "hello\nworld\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    // Peek first char
    assert(rt_linereader_peek_char(lr) == 'h');

    // Read full line (should still work with peeked char)
    rt_string line = rt_linereader_read(lr);
    assert(rt_len(line) == 5);
    assert(strncmp(rt_string_cstr(line), "hello", 5) == 0);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_all()
{
    cleanup_test_file();

    const char *content = "Hello, World!\nThis is a test.\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string all = rt_linereader_read_all(lr);
    assert(rt_len(all) == (int64_t)strlen(content));
    assert(strncmp(rt_string_cstr(all), content, strlen(content)) == 0);

    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_all_partial()
{
    cleanup_test_file();

    const char *content = "line1\nline2\nline3\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    // Read first line
    rt_string line1 = rt_linereader_read(lr);
    assert(rt_len(line1) == 5);

    // Read remaining with ReadAll
    rt_string rest = rt_linereader_read_all(lr);
    assert(rt_len(rest) == strlen("line2\nline3\n"));

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_all_with_peek()
{
    cleanup_test_file();

    const char *content = "ABCDEF";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    // Consume first char
    assert(rt_linereader_read_char(lr) == 'A');

    // Peek should see 'B'
    assert(rt_linereader_peek_char(lr) == 'B');

    // ReadAll should include the peeked char
    rt_string rest = rt_linereader_read_all(lr);
    assert(rt_len(rest) == 5);
    assert(strncmp(rt_string_cstr(rest), "BCDEF", 5) == 0);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_empty_file()
{
    cleanup_test_file();
    write_raw_file("", 0);

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    // Read should return empty and set EOF
    rt_string line = rt_linereader_read(lr);
    assert(rt_len(line) == 0);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_empty_lines()
{
    cleanup_test_file();

    // Three empty lines
    const char *content = "\n\n\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string l1 = rt_linereader_read(lr);
    assert(rt_len(l1) == 0);
    assert(rt_linereader_eof(lr) == 0);

    rt_string l2 = rt_linereader_read(lr);
    assert(rt_len(l2) == 0);
    assert(rt_linereader_eof(lr) == 0);

    rt_string l3 = rt_linereader_read(lr);
    assert(rt_len(l3) == 0);
    assert(rt_linereader_eof(lr) == 0);

    // Now we're at EOF
    rt_string l4 = rt_linereader_read(lr);
    assert(rt_len(l4) == 0);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_long_line()
{
    cleanup_test_file();

    // Create a line longer than initial buffer (256)
    char content[1000];
    for (int i = 0; i < 999; ++i)
    {
        content[i] = 'X';
    }
    content[999] = '\0';

    FILE *fp = fopen(test_file, "w");
    assert(fp != nullptr);
    fprintf(fp, "%s\n", content);
    fclose(fp);

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string line = rt_linereader_read(lr);
    assert(rt_len(line) == 999);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_null_handling()
{
    // Null operations should return safe defaults
    assert(rt_linereader_eof(nullptr) == 1);

    // This should not crash
    rt_linereader_close(nullptr);
}

int main()
{
#ifdef _WIN32
    // Skip on Windows: test uses /tmp paths not available on Windows
    printf("Test skipped: POSIX temp paths not available on Windows\n");
    return 0;
#endif
    test_open_close();
    test_read_lf_lines();
    test_read_crlf_lines();
    test_read_cr_lines();
    test_read_mixed_endings();
    test_read_char();
    test_peek_char();
    test_peek_then_read_line();
    test_read_all();
    test_read_all_partial();
    test_read_all_with_peek();
    test_empty_file();
    test_empty_lines();
    test_long_line();
    test_null_handling();

    cleanup_test_file();
    return 0;
}
