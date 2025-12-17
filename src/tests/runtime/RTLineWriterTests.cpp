//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTLineWriterTests.cpp
// Purpose: Comprehensive tests for Viper.IO.LineWriter text file writing.
//
//===----------------------------------------------------------------------===//

#include "rt_linewriter.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static const char *test_file = "/tmp/viper_linewriter_test.txt";

static rt_string make_string(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static void cleanup_test_file()
{
    remove(test_file);
}

static char *read_file_contents(size_t *out_len)
{
    FILE *fp = fopen(test_file, "rb");
    if (!fp)
    {
        *out_len = 0;
        return nullptr;
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)len + 1);
    size_t read = fread(buf, 1, (size_t)len, fp);
    buf[read] = '\0';
    fclose(fp);

    *out_len = read;
    return buf;
}

static void test_open_close()
{
    cleanup_test_file();

    rt_string path = make_string(test_file);
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    rt_linewriter_close(lw);
    // Should be able to close twice without issue
    rt_linewriter_close(lw);

    cleanup_test_file();
}

static void test_write_string()
{
    cleanup_test_file();

    rt_string path = make_string(test_file);
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    rt_string text = make_string("Hello");
    rt_linewriter_write(lw, text);

    rt_string text2 = make_string(" World");
    rt_linewriter_write(lw, text2);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 11);
    assert(strncmp(contents, "Hello World", 11) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_write_ln()
{
    cleanup_test_file();

    rt_string path = make_string(test_file);
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    rt_string line1 = make_string("Line 1");
    rt_linewriter_write_ln(lw, line1);

    rt_string line2 = make_string("Line 2");
    rt_linewriter_write_ln(lw, line2);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);

    // Platform-specific newline
#ifdef _WIN32
    assert(strncmp(contents, "Line 1\r\nLine 2\r\n", len) == 0);
#else
    assert(strncmp(contents, "Line 1\nLine 2\n", len) == 0);
#endif
    free(contents);

    cleanup_test_file();
}

static void test_write_char()
{
    cleanup_test_file();

    rt_string path = make_string(test_file);
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    rt_linewriter_write_char(lw, 'A');
    rt_linewriter_write_char(lw, 'B');
    rt_linewriter_write_char(lw, 'C');
    rt_linewriter_write_char(lw, '1');
    rt_linewriter_write_char(lw, '2');
    rt_linewriter_write_char(lw, '3');

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 6);
    assert(strncmp(contents, "ABC123", 6) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_append_mode()
{
    cleanup_test_file();

    // Create initial file
    {
        rt_string path = make_string(test_file);
        void *lw = rt_linewriter_open(path);
        rt_string text = make_string("First");
        rt_linewriter_write(lw, text);
        rt_linewriter_close(lw);
    }

    // Append to file
    {
        rt_string path = make_string(test_file);
        void *lw = rt_linewriter_append(path);
        rt_string text = make_string("Second");
        rt_linewriter_write(lw, text);
        rt_linewriter_close(lw);
    }

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 11);
    assert(strncmp(contents, "FirstSecond", 11) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_custom_newline()
{
    cleanup_test_file();

    rt_string path = make_string(test_file);
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    // Set custom newline (Windows-style)
    rt_string crlf = make_string("\r\n");
    rt_linewriter_set_newline(lw, crlf);

    // Verify newline was set
    rt_string nl = rt_linewriter_newline(lw);
    assert(rt_len(nl) == 2);
    assert(strncmp(rt_string_cstr(nl), "\r\n", 2) == 0);

    // Write lines with custom newline
    rt_string line1 = make_string("Line 1");
    rt_linewriter_write_ln(lw, line1);

    rt_string line2 = make_string("Line 2");
    rt_linewriter_write_ln(lw, line2);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 16); // "Line 1\r\nLine 2\r\n"
    assert(strncmp(contents, "Line 1\r\nLine 2\r\n", 16) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_unix_newline()
{
    cleanup_test_file();

    rt_string path = make_string(test_file);
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    // Set Unix-style newline
    rt_string lf = make_string("\n");
    rt_linewriter_set_newline(lw, lf);

    rt_string line1 = make_string("Line 1");
    rt_linewriter_write_ln(lw, line1);

    rt_string line2 = make_string("Line 2");
    rt_linewriter_write_ln(lw, line2);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 14); // "Line 1\nLine 2\n"
    assert(strncmp(contents, "Line 1\nLine 2\n", 14) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_flush()
{
    cleanup_test_file();

    rt_string path = make_string(test_file);
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    rt_string text = make_string("Flushed");
    rt_linewriter_write(lw, text);

    // Flush should not crash
    rt_linewriter_flush(lw);

    rt_linewriter_close(lw);

    // Verify byte was written
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 7);
    assert(strncmp(contents, "Flushed", 7) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_write_ln_empty()
{
    cleanup_test_file();

    rt_string path = make_string(test_file);
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    // Set Unix newline for predictable test
    rt_string lf = make_string("\n");
    rt_linewriter_set_newline(lw, lf);

    // Write empty line (just newline)
    rt_string empty = make_string("");
    rt_linewriter_write_ln(lw, empty);

    rt_string line = make_string("Content");
    rt_linewriter_write_ln(lw, line);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 9); // "\nContent\n"
    assert(strncmp(contents, "\nContent\n", 9) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_overwrite_existing()
{
    cleanup_test_file();

    // Create initial file with content
    {
        rt_string path = make_string(test_file);
        void *lw = rt_linewriter_open(path);
        rt_string text = make_string("This is a long initial content");
        rt_linewriter_write(lw, text);
        rt_linewriter_close(lw);
    }

    // Overwrite with shorter content
    {
        rt_string path = make_string(test_file);
        void *lw = rt_linewriter_open(path);
        rt_string text = make_string("Short");
        rt_linewriter_write(lw, text);
        rt_linewriter_close(lw);
    }

    // Verify contents (should be only "Short")
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 5);
    assert(strncmp(contents, "Short", 5) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_mixed_write_methods()
{
    cleanup_test_file();

    rt_string path = make_string(test_file);
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    // Set Unix newline
    rt_string lf = make_string("\n");
    rt_linewriter_set_newline(lw, lf);

    // Mix different write methods
    rt_string hello = make_string("Hello");
    rt_linewriter_write(lw, hello);
    rt_linewriter_write_char(lw, ',');
    rt_linewriter_write_char(lw, ' ');
    rt_string world = make_string("World");
    rt_linewriter_write_ln(lw, world);
    rt_string bye = make_string("Goodbye");
    rt_linewriter_write_ln(lw, bye);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    // "Hello, World\nGoodbye\n" = 21 chars
    assert(len == 21);
    assert(strncmp(contents, "Hello, World\nGoodbye\n", 21) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_null_handling()
{
    // Null operations should not crash
    rt_linewriter_close(nullptr);
    rt_linewriter_flush(nullptr);

    // Null writer get_NewLine returns default
    rt_string nl = rt_linewriter_newline(nullptr);
    assert(nl != nullptr);
    assert(rt_len(nl) > 0);
}

int main()
{
    test_open_close();
    test_write_string();
    test_write_ln();
    test_write_char();
    test_append_mode();
    test_custom_newline();
    test_unix_newline();
    test_flush();
    test_write_ln_empty();
    test_overwrite_existing();
    test_mixed_write_methods();
    test_null_handling();

    cleanup_test_file();
    return 0;
}
