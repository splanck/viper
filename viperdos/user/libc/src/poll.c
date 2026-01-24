//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/poll.c
// Purpose: I/O multiplexing functions for ViperDOS libc.
// Key invariants: Maps poll() to kernel poll syscalls; handles socket routing.
// Ownership/Lifetime: Library; static poll set created on first use.
// Links: user/libc/include/poll.h, user/libc/include/sys/select.h
//
//===----------------------------------------------------------------------===//

/**
 * @file poll.c
 * @brief I/O multiplexing functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX I/O multiplexing:
 *
 * - poll: Wait for events on file descriptors
 * - ppoll: poll with precise timeout and signal mask
 * - select: BSD-style synchronous I/O multiplexing
 * - pselect: select with precise timeout and signal mask
 *
 * ViperDOS poll implementation:
 * - stdin (fd 0) maps to console input pseudo-handle
 * - Socket FDs route to kernel or netd based on backend
 * - Regular file FDs are treated as always ready
 * - Uses kernel poll syscalls (SYS_POLL_*)
 *
 * The poll set is created on first use and handles are added/removed
 * dynamically based on what the caller requests.
 */

#include "../include/poll.h"
#include "../include/errno.h"
#include "../include/stdlib.h"
#include "../include/sys/select.h"
#include "../include/time.h"

/* Syscall helpers */
extern long __syscall0(long num);
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall4(long num, long arg0, long arg1, long arg2, long arg3);
extern long __syscall5(long num, long arg0, long arg1, long arg2, long arg3, long arg4);

/* Socket FD helpers (libc virtual socket FDs). */
extern int __viper_socket_is_fd(int fd);
extern int __viper_socket_get_backend(int fd, int *out_backend, int *out_socket_id);

/* netd backend helpers (libc netd_backend.cpp). */
extern unsigned int __viper_netd_poll_handle(void);
extern int __viper_netd_socket_status(int socket_id,
                                      unsigned int *out_flags,
                                      unsigned int *out_rx_available);

/* Syscall numbers from include/viperdos/syscall_nums.hpp */
#define SYS_SLEEP 0x31
#define SYS_POLL_CREATE 0x20
#define SYS_POLL_ADD 0x21
#define SYS_POLL_REMOVE 0x22
#define SYS_POLL_WAIT 0x23
#define SYS_CHANNEL_RECV 0x12
#define SYS_SHM_CLOSE 0x10C
#define SYS_CAP_REVOKE 0x71

/* Kernel poll pseudo-handles/event bits (match kernel/ipc/poll.hpp) */
#define VIPER_HANDLE_CONSOLE_INPUT 0xFFFF0001u
#define VIPER_HANDLE_NETWORK_RX 0xFFFF0002u
#define VIPER_POLL_CHANNEL_READ (1u << 0)
#define VIPER_POLL_CONSOLE_INPUT (1u << 3)
#define VIPER_POLL_NETWORK_RX (1u << 4)

/* libc socket backends (must match user/libc/src/socket.c). */
#define VIPER_SOCKET_BACKEND_KERNEL 1
#define VIPER_SOCKET_BACKEND_NETD 2

/* netd socket status flags (must match user/servers/netd/net_protocol.hpp). */
#define NETD_SOCK_READABLE (1u << 0)
#define NETD_SOCK_WRITABLE (1u << 1)
#define NETD_SOCK_EOF (1u << 2)

struct viper_poll_event
{
    unsigned int handle;
    unsigned int events;
    unsigned int triggered;
};

static void drain_event_channel(unsigned int ch)
{
    unsigned char buf[16];
    for (;;)
    {
        unsigned int handles[4];
        unsigned int handle_count = 4;
        long n = __syscall5(SYS_CHANNEL_RECV,
                            (long)ch,
                            (long)buf,
                            (long)sizeof(buf),
                            (long)handles,
                            (long)&handle_count);
        if (n == -300) /* VERR_WOULD_BLOCK */
        {
            break;
        }
        if (n < 0)
        {
            break;
        }
        for (unsigned int i = 0; i < handle_count; i++)
        {
            if (handles[i] == 0)
                continue;
            long cerr = __syscall1(SYS_SHM_CLOSE, (long)handles[i]);
            if (cerr != 0)
            {
                (void)__syscall1(SYS_CAP_REVOKE, (long)handles[i]);
            }
        }
    }
}

static long get_poll_set_id(void)
{
    static long poll_set = -1;
    if (poll_set >= 0)
        return poll_set;

    long id = __syscall0(SYS_POLL_CREATE);
    if (id < 0)
        return id;

    poll_set = id;
    return poll_set;
}

static int poll_set_configure(
    long poll_set, int want_console, int want_kernel_net, int want_netd, unsigned int netd_handle)
{
    static int configured_console = 0;
    static int configured_kernel_net = 0;
    static int configured_netd = 0;
    static unsigned int configured_netd_handle = 0;

    if (want_console && !configured_console)
    {
        long rc = __syscall3(
            SYS_POLL_ADD, poll_set, (long)VIPER_HANDLE_CONSOLE_INPUT, VIPER_POLL_CONSOLE_INPUT);
        if (rc < 0)
            return (int)rc;
        configured_console = 1;
    }
    if (!want_console && configured_console)
    {
        (void)__syscall2(SYS_POLL_REMOVE, poll_set, (long)VIPER_HANDLE_CONSOLE_INPUT);
        configured_console = 0;
    }

    if (want_kernel_net && !configured_kernel_net)
    {
        long rc = __syscall3(
            SYS_POLL_ADD, poll_set, (long)VIPER_HANDLE_NETWORK_RX, VIPER_POLL_NETWORK_RX);
        if (rc < 0)
            return (int)rc;
        configured_kernel_net = 1;
    }
    if (!want_kernel_net && configured_kernel_net)
    {
        (void)__syscall2(SYS_POLL_REMOVE, poll_set, (long)VIPER_HANDLE_NETWORK_RX);
        configured_kernel_net = 0;
    }

    if (want_netd)
    {
        if (!configured_netd || configured_netd_handle != netd_handle)
        {
            if (configured_netd)
            {
                (void)__syscall2(SYS_POLL_REMOVE, poll_set, (long)configured_netd_handle);
            }
            long rc =
                __syscall3(SYS_POLL_ADD, poll_set, (long)netd_handle, VIPER_POLL_CHANNEL_READ);
            if (rc < 0)
                return (int)rc;
            configured_netd = 1;
            configured_netd_handle = netd_handle;
        }
    }
    else if (configured_netd)
    {
        (void)__syscall2(SYS_POLL_REMOVE, poll_set, (long)configured_netd_handle);
        configured_netd = 0;
        configured_netd_handle = 0;
    }

    return 0;
}

/**
 * @brief Wait for events on multiple file descriptors.
 *
 * @details
 * Examines a set of file descriptors to see if some of them are ready
 * for I/O operations. The function blocks until one of the following:
 *
 * - One or more file descriptors become ready
 * - The call is interrupted by a signal
 * - The timeout expires
 *
 * Each pollfd structure specifies a file descriptor to monitor and the
 * events of interest (events field). On return, revents is filled with
 * the events that occurred.
 *
 * Event flags:
 * - POLLIN: Data available to read
 * - POLLOUT: Writing is possible
 * - POLLPRI: Priority data available
 * - POLLERR: Error condition (output only)
 * - POLLHUP: Hang up (output only)
 * - POLLNVAL: Invalid file descriptor (output only)
 *
 * ViperDOS implementation details:
 * - stdin (fd 0) maps to console input pseudo-handle
 * - Socket FDs route to kernel or netd based on backend
 * - Regular file FDs are treated as always ready
 *
 * @param fds Array of pollfd structures specifying fds and events.
 * @param nfds Number of elements in the fds array.
 * @param timeout Timeout in milliseconds (-1 for infinite, 0 for immediate).
 * @return Number of fds with events on success, 0 on timeout, -1 on error.
 *
 * @see ppoll, select, pselect
 */
int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (!fds && nfds > 0)
    {
        errno = EFAULT;
        return -1;
    }

    if (nfds == 0)
    {
        if (timeout > 0)
        {
            (void)__syscall1(SYS_SLEEP, timeout);
        }
        else if (timeout < 0)
        {
            // Poll forever with no fds: yield in a loop.
            for (;;)
            {
                (void)__syscall1(SYS_SLEEP, 1000);
            }
        }
        return 0;
    }

    int want_console = 0;
    int want_kernel_net = 0;
    int want_netd = 0;
    unsigned int netd_handle = 0xFFFFFFFFu;
    int any_ready = 0;

    // First pass: clear revents and handle "always ready" cases.
    for (nfds_t i = 0; i < nfds; i++)
    {
        fds[i].revents = 0;

        int fd = fds[i].fd;
        short ev = fds[i].events;

        if (fd < 0)
        {
            continue; // ignored
        }

        // stdin: map POLLIN to console input.
        if (fd == 0)
        {
            if (ev & (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND))
            {
                want_console = 1;
            }
            continue;
        }

        // Virtual sockets: kernel sockets use the kernel NETWORK_RX pseudo-handle,
        // netd sockets use a real event channel plus NET_SOCKET_STATUS checks.
        if (__viper_socket_is_fd(fd))
        {
            int backend = 0;
            int sock_id = 0;
            int b_rc = __viper_socket_get_backend(fd, &backend, &sock_id);
            if (b_rc < 0)
            {
                fds[i].revents |= POLLNVAL;
                any_ready = 1;
                continue;
            }

            if (backend == VIPER_SOCKET_BACKEND_NETD)
            {
                unsigned int flags = 0;
                unsigned int rx_avail = 0;
                int st_rc = -1;

                if (ev & (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND))
                {
                    st_rc = __viper_netd_socket_status(sock_id, &flags, &rx_avail);
                    if (st_rc == 0)
                    {
                        if (flags & NETD_SOCK_EOF)
                        {
                            fds[i].revents |= (POLLIN | POLLHUP);
                            any_ready = 1;
                        }
                        else if (flags & NETD_SOCK_READABLE)
                        {
                            fds[i].revents |= POLLIN;
                            any_ready = 1;
                        }
                        else
                        {
                            want_netd = 1;
                        }
                    }
                    else
                    {
                        fds[i].revents |= POLLERR;
                        any_ready = 1;
                    }
                }

                if (ev & (POLLOUT | POLLWRNORM | POLLWRBAND))
                {
                    if (st_rc == 0)
                    {
                        if (flags & NETD_SOCK_WRITABLE)
                        {
                            fds[i].revents |= POLLOUT;
                            any_ready = 1;
                        }
                    }
                    else
                    {
                        // Best-effort: treat as writable to preserve existing assumptions.
                        fds[i].revents |= POLLOUT;
                        any_ready = 1;
                    }
                }
                continue;
            }

            if (ev & (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND))
            {
                want_kernel_net = 1;
            }
            if (ev & (POLLOUT | POLLWRNORM | POLLWRBAND))
            {
                fds[i].revents |= POLLOUT;
                any_ready = 1;
            }
            continue;
        }

        // Default: treat non-socket fds as always-ready for read/write.
        short rw = (short)(ev & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
        if (rw)
        {
            fds[i].revents |= rw;
            any_ready = 1;
        }
    }

    if (want_netd)
    {
        netd_handle = __viper_netd_poll_handle();
        if (netd_handle == 0xFFFFFFFFu)
        {
            errno = ENOSYS;
            return -1;
        }
    }

    // If we have console/network handles, sample or wait using the kernel pollset.
    if (want_console || want_kernel_net || want_netd)
    {
        long poll_set = get_poll_set_id();
        if (poll_set < 0)
        {
            errno = (int)(-poll_set);
            return -1;
        }

        int cfg =
            poll_set_configure(poll_set, want_console, want_kernel_net, want_netd, netd_handle);
        if (cfg < 0)
        {
            errno = -cfg;
            return -1;
        }

        int wait_ms = any_ready ? 0 : timeout;

        struct viper_poll_event events[3];
        unsigned int n = 0;

        if (want_console)
        {
            events[n].handle = VIPER_HANDLE_CONSOLE_INPUT;
            events[n].events = VIPER_POLL_CONSOLE_INPUT;
            events[n].triggered = 0;
            n++;
        }
        if (want_kernel_net)
        {
            events[n].handle = VIPER_HANDLE_NETWORK_RX;
            events[n].events = VIPER_POLL_NETWORK_RX;
            events[n].triggered = 0;
            n++;
        }
        if (want_netd)
        {
            events[n].handle = netd_handle;
            events[n].events = VIPER_POLL_CHANNEL_READ;
            events[n].triggered = 0;
            n++;
        }

        long rc = __syscall4(SYS_POLL_WAIT, poll_set, (long)events, n, wait_ms);
        if (rc < 0)
        {
            errno = (int)(-rc);
            return -1;
        }

        int console_ready = 0;
        int kernel_net_ready = 0;
        int netd_ready = 0;
        for (long i = 0; i < rc; i++)
        {
            if (events[i].handle == VIPER_HANDLE_CONSOLE_INPUT &&
                (events[i].triggered & VIPER_POLL_CONSOLE_INPUT))
            {
                console_ready = 1;
            }
            if (events[i].handle == VIPER_HANDLE_NETWORK_RX &&
                (events[i].triggered & VIPER_POLL_NETWORK_RX))
            {
                kernel_net_ready = 1;
            }
            if (want_netd && events[i].handle == netd_handle &&
                (events[i].triggered & VIPER_POLL_CHANNEL_READ))
            {
                netd_ready = 1;
            }
        }

        if (netd_ready)
        {
            drain_event_channel(netd_handle);
        }

        if (console_ready || kernel_net_ready || netd_ready)
        {
            for (nfds_t i = 0; i < nfds; i++)
            {
                int fd = fds[i].fd;
                short ev = fds[i].events;

                if (fd == 0 && console_ready && (ev & (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)))
                {
                    fds[i].revents |= POLLIN;
                }

                if (__viper_socket_is_fd(fd) && (ev & (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)))
                {
                    int backend = 0;
                    int sock_id = 0;
                    if (__viper_socket_get_backend(fd, &backend, &sock_id) < 0)
                    {
                        fds[i].revents |= POLLNVAL;
                        continue;
                    }

                    if (backend == VIPER_SOCKET_BACKEND_NETD)
                    {
                        if (netd_ready)
                        {
                            unsigned int flags = 0;
                            unsigned int rx_avail = 0;
                            int st_rc = __viper_netd_socket_status(sock_id, &flags, &rx_avail);
                            if (st_rc == 0)
                            {
                                if (flags & NETD_SOCK_EOF)
                                {
                                    fds[i].revents |= (POLLIN | POLLHUP);
                                }
                                else if (flags & NETD_SOCK_READABLE)
                                {
                                    fds[i].revents |= POLLIN;
                                }
                            }
                        }
                    }
                    else
                    {
                        if (kernel_net_ready)
                        {
                            fds[i].revents |= POLLIN;
                        }
                    }
                }
            }
        }

        // If we blocked and got no events, it's a timeout.
        if (!any_ready && rc == 0)
        {
            return 0;
        }
    }

    // Count fds with any revents.
    int count = 0;
    for (nfds_t i = 0; i < nfds; i++)
    {
        if (fds[i].fd >= 0 && fds[i].revents != 0)
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief Wait for events with precise timeout and signal mask.
 *
 * @details
 * Like poll(), but with nanosecond timeout precision and the ability to
 * atomically change the signal mask during the wait. The original signal
 * mask is restored when the call returns.
 *
 * @note The signal mask is currently ignored in ViperDOS. The timeout
 * is converted to milliseconds since that's the kernel's resolution.
 *
 * @param fds Array of pollfd structures specifying fds and events.
 * @param nfds Number of elements in the fds array.
 * @param timeout_ts Timeout as timespec (NULL for infinite).
 * @param sigmask Signal mask to use during wait (ignored).
 * @return Number of fds with events on success, 0 on timeout, -1 on error.
 *
 * @see poll, pselect
 */
int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const void *sigmask)
{
    int timeout_ms;

    /* Convert timespec to milliseconds */
    if (timeout_ts == (void *)0)
    {
        timeout_ms = -1; /* Infinite */
    }
    else
    {
        timeout_ms = (int)(timeout_ts->tv_sec * 1000 + timeout_ts->tv_nsec / 1000000);
    }

    /* Ignore sigmask - not fully implemented */
    (void)sigmask;

    return poll(fds, nfds, timeout_ms);
}

/**
 * @brief Synchronous I/O multiplexing (BSD-style).
 *
 * @details
 * Examines file descriptor sets to see if some of them are ready for
 * reading, writing, or have exceptional conditions pending. This is
 * the older BSD interface; poll() is generally preferred for new code.
 *
 * The function modifies the fd_set arguments to indicate which file
 * descriptors are ready:
 * - readfds: Modified to contain only fds ready for reading
 * - writefds: Modified to contain only fds ready for writing
 * - exceptfds: Modified to contain only fds with exceptions (cleared in ViperDOS)
 *
 * The timeout argument specifies the maximum time to wait:
 * - NULL: Wait indefinitely
 * - Zero (0.0): Return immediately (poll)
 * - Positive: Wait up to the specified time
 *
 * ViperDOS converts select() calls to poll() internally.
 *
 * @param nfds Highest file descriptor number + 1.
 * @param readfds Set of fds to check for reading (modified on return).
 * @param writefds Set of fds to check for writing (modified on return).
 * @param exceptfds Set of fds to check for exceptions (cleared on return).
 * @param timeout Maximum time to wait (NULL for infinite).
 * @return Number of ready fds on success, 0 on timeout, -1 on error.
 *
 * @see pselect, poll, ppoll
 */
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    if (nfds < 0 || nfds > FD_SETSIZE)
    {
        errno = EINVAL;
        return -1;
    }

    // Ignore exceptfds for now (no exceptional conditions exposed).
    if (exceptfds)
    {
        FD_ZERO(exceptfds);
    }

    int timeout_ms;
    if (timeout == (void *)0)
    {
        timeout_ms = -1;
    }
    else
    {
        timeout_ms = (int)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
    }

    // Build a pollfd list for all requested read/write fds.
    // This is a minimal implementation intended for typical small nfds usage.
    struct pollfd stack_fds[64];
    struct pollfd *pfds = stack_fds;
    int pcount = 0;

    // Worst case could exceed stack_fds; fall back to heap allocation.
    // We cap to FD_SETSIZE, so allocation is bounded.
    if (nfds > (int)(sizeof(stack_fds) / sizeof(stack_fds[0])))
    {
        pfds = (struct pollfd *)malloc((unsigned long)nfds * sizeof(struct pollfd));
        if (!pfds)
        {
            errno = ENOMEM;
            return -1;
        }
    }

    for (int fd = 0; fd < nfds; fd++)
    {
        short ev = 0;
        if (readfds && FD_ISSET(fd, readfds))
            ev |= POLLIN;
        if (writefds && FD_ISSET(fd, writefds))
            ev |= POLLOUT;
        if (!ev)
            continue;

        pfds[pcount].fd = fd;
        pfds[pcount].events = ev;
        pfds[pcount].revents = 0;
        pcount++;
    }

    // select() with no fds behaves like a timed sleep.
    if (pcount == 0)
    {
        if (timeout_ms > 0)
        {
            (void)__syscall1(SYS_SLEEP, timeout_ms);
        }
        if (readfds)
            FD_ZERO(readfds);
        if (writefds)
            FD_ZERO(writefds);
        if (pfds != stack_fds)
            free(pfds);
        if (timeout)
        {
            timeout->tv_sec = 0;
            timeout->tv_usec = 0;
        }
        return 0;
    }

    int rc = poll(pfds, (nfds_t)pcount, timeout_ms);
    if (rc < 0)
    {
        if (pfds != stack_fds)
            free(pfds);
        return -1;
    }

    // Update fd_sets to only contain ready fds.
    if (readfds)
    {
        fd_set in = *readfds;
        FD_ZERO(readfds);
        for (int i = 0; i < pcount; i++)
        {
            if ((pfds[i].revents & POLLIN) && FD_ISSET(pfds[i].fd, &in))
            {
                FD_SET(pfds[i].fd, readfds);
            }
        }
    }

    if (writefds)
    {
        fd_set in = *writefds;
        FD_ZERO(writefds);
        for (int i = 0; i < pcount; i++)
        {
            if ((pfds[i].revents & POLLOUT) && FD_ISSET(pfds[i].fd, &in))
            {
                FD_SET(pfds[i].fd, writefds);
            }
        }
    }

    if (pfds != stack_fds)
        free(pfds);

    if (timeout != (void *)0)
    {
        timeout->tv_sec = 0;
        timeout->tv_usec = 0;
    }

    return rc;
}

/**
 * @brief Select with precise timeout and signal mask.
 *
 * @details
 * Like select(), but with nanosecond timeout precision and the ability
 * to atomically change the signal mask during the wait. This function
 * was introduced to fix race conditions with signals.
 *
 * Key differences from select():
 * - Timeout is specified as struct timespec (nanoseconds) instead of
 *   struct timeval (microseconds)
 * - Timeout is const (not modified on return)
 * - Signal mask can be atomically changed during the wait
 *
 * @note The signal mask is currently ignored in ViperDOS.
 *
 * @param nfds Highest file descriptor number + 1.
 * @param readfds Set of fds to check for reading (modified on return).
 * @param writefds Set of fds to check for writing (modified on return).
 * @param exceptfds Set of fds to check for exceptions (cleared on return).
 * @param timeout Maximum time to wait (NULL for infinite).
 * @param sigmask Signal mask to use during wait (ignored).
 * @return Number of ready fds on success, 0 on timeout, -1 on error.
 *
 * @see select, ppoll, poll
 */
int pselect(int nfds,
            fd_set *readfds,
            fd_set *writefds,
            fd_set *exceptfds,
            const struct timespec *timeout,
            const void *sigmask)
{
    struct timeval tv;
    struct timeval *tv_ptr = (void *)0;

    /* Ignore sigmask - not fully implemented */
    (void)sigmask;

    /* Convert timespec to timeval */
    if (timeout != (void *)0)
    {
        tv.tv_sec = timeout->tv_sec;
        tv.tv_usec = timeout->tv_nsec / 1000;
        tv_ptr = &tv;
    }

    return select(nfds, readfds, writefds, exceptfds, tv_ptr);
}
