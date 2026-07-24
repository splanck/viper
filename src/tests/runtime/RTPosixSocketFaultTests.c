//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTPosixSocketFaultTests.c
// Purpose: Fault-injection coverage for POSIX socket timing failures.
//
// Key invariants:
//   - A failed deadline clock read preserves EINTR after interrupted poll().
//   - A finite readiness wait never becomes an unbounded poll on clock failure.
//
// Ownership/Lifetime:
//   - Standalone test with no real descriptors or retained state.
//
// Links: src/runtime/network/rt_socket_platform_posix.c
//
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <time.h>

static int g_clock_calls;
static int g_poll_calls;
static int g_last_poll_timeout;

static int mock_clock_gettime(clockid_t clock_id, struct timespec *time_value);
static int mock_poll(struct pollfd *descriptors, nfds_t count, int timeout);

#define clock_gettime mock_clock_gettime
#define poll mock_poll
#include "../../runtime/network/rt_socket_platform_posix.c"
#undef poll
#undef clock_gettime

static int mock_clock_gettime(clockid_t clock_id, struct timespec *time_value) {
    assert(clock_id == CLOCK_MONOTONIC);
    g_clock_calls++;
    if (g_clock_calls == 1) {
        time_value->tv_sec = 10;
        time_value->tv_nsec = 0;
        return 0;
    }
    errno = EIO;
    return -1;
}

static int mock_poll(struct pollfd *descriptors, nfds_t count, int timeout) {
    assert(descriptors != NULL);
    assert(count == 1);
    g_poll_calls++;
    g_last_poll_timeout = timeout;
    errno = EINTR;
    return -1;
}

int main(void) {
    g_clock_calls = 0;
    g_poll_calls = 0;
    g_last_poll_timeout = -1;

    errno = 0;
    assert(wait_socket(42, 250, false) == -1);
    assert(errno == EINTR);
    assert(g_clock_calls == 2);
    assert(g_poll_calls == 1);
    assert(g_last_poll_timeout == 250);
    return 0;
}
