// File: tests/runtime/RTInputLineTests.cpp
// Purpose: Ensure rt_input_line handles lines longer than the initial buffer and EOF-terminated
// lines. Key invariants: rt_input_line returns full line content for >1023 chars, with or without
// trailing newline. Ownership: Uses runtime library. Links: docs/runtime-abi.md
#include "rt_internal.h"
#include <cassert>
#include <cstring>
#include <string>
#include <unistd.h>

static void feed_and_check(size_t len, bool with_newline)
{
    std::string input(len, 'x');
    int fds[2];
    assert(pipe(fds) == 0);
    if (with_newline)
    {
        std::string data = input + "\n";
        (void)write(fds[1], data.data(), data.size());
    }
    else
    {
        (void)write(fds[1], input.data(), input.size());
    }
    close(fds[1]);
    dup2(fds[0], 0);
    close(fds[0]);
    rt_string s = rt_input_line();
    assert(s);
    assert(s->size == (int64_t)input.size());
    assert(std::memcmp(s->data, input.data(), input.size()) == 0);
}

int main()
{
    feed_and_check(1500, true);
    feed_and_check(1500, false);
    return 0;
}
