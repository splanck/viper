//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTInputLineTests.cpp
// Purpose: Ensure rt_input_line handles lines longer than the initial buffer and EOF-terminated
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

static rt_string read_line(const std::string &data)
{
    int fds[2];
    assert(pipe(fds) == 0);
    if (!data.empty())
        (void)write(fds[1], data.data(), data.size());
    close(fds[1]);

    // Save original stdin fd before replacing
    int saved_stdin = dup(0);
    assert(saved_stdin >= 0);

    dup2(fds[0], 0);
    close(fds[0]);

    // Clear any buffered state and error flags on stdin
    clearerr(stdin);
    // Discard any buffered input by seeking (no-op on pipes but clears buffer)
    fflush(stdin);

    rt_string result = rt_input_line();

    // Restore original stdin
    dup2(saved_stdin, 0);
    close(saved_stdin);
    clearerr(stdin);

    return result;
}

static void feed_and_check(size_t len, bool with_newline)
{
    std::string input(len, 'x');
    std::string data = with_newline ? input + "\n" : input;
    rt_string s = read_line(data);
    assert(s);
    assert(rt_len(s) == (int64_t)input.size());
    assert(std::memcmp(s->data, input.data(), input.size()) == 0);
}

static void feed_crlf_and_check(size_t len)
{
    std::string input(len, 'x');
    std::string data = input + "\r\n";
    rt_string s = read_line(data);
    assert(s);
    assert(rt_len(s) == (int64_t)input.size());
    assert(std::memcmp(s->data, input.data(), input.size()) == 0);
    assert(std::memchr(s->data, '\r', input.size()) == nullptr);
}

static void feed_empty_newline_returns_empty_string()
{
    rt_string s = read_line("\n");
    assert(s);
    assert(rt_len(s) == 0);
    assert(s->data[0] == '\0');
}

int main()
{
    feed_and_check(1500, true);
    feed_and_check(1500, false);
    feed_crlf_and_check(16);
    feed_empty_newline_returns_empty_string();
    return 0;
}
