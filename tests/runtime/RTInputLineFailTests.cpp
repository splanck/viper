// File: tests/runtime/RTInputLineFailTests.cpp
// Purpose: Verify rt_input_line returns NULL when buffer expansion fails.
// Key invariants: Function aborts reading on realloc failure and reports trap.
// Ownership: Uses runtime library; stubs realloc and trap for simulation.
// Links: docs/runtime-abi.md
#include "rt.hpp"
#include <cassert>
#include <cstring>
#include <string>
#include <unistd.h>

static const char *g_msg = nullptr;

// Stub realloc to simulate allocation failure.
extern "C" void *realloc(void *ptr, size_t size)
{
    (void)ptr;
    (void)size;
    return NULL;
}

// Stub vm_trap to capture message without aborting.
extern "C" void vm_trap(const char *msg)
{
    g_msg = msg;
}

int main()
{
    std::string input(1500, 'x');
    int fds[2];
    assert(pipe(fds) == 0);
    (void)write(fds[1], input.data(), input.size());
    close(fds[1]);
    dup2(fds[0], 0);
    close(fds[0]);

    rt_string s = rt_input_line();
    assert(!s);
    assert(g_msg && std::strcmp(g_msg, "out of memory") == 0);
    return 0;
}
