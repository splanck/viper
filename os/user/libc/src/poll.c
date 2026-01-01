#include "../include/poll.h"
#include "../include/errno.h"
#include "../include/sys/select.h"
#include "../include/stdlib.h"
#include "../include/time.h"

/* Syscall helpers */
extern long __syscall0(long num);
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall4(long num, long arg0, long arg1, long arg2, long arg3);

/* Socket FD helpers (libc virtual socket FDs). */
extern int __viper_socket_is_fd(int fd);

/* Syscall numbers from include/viperos/syscall_nums.hpp */
#define SYS_SLEEP 0x31
#define SYS_POLL_CREATE 0x20
#define SYS_POLL_ADD 0x21
#define SYS_POLL_REMOVE 0x22
#define SYS_POLL_WAIT 0x23

/* Kernel poll pseudo-handles/event bits (match kernel/ipc/poll.hpp) */
#define VIPER_HANDLE_CONSOLE_INPUT 0xFFFF0001u
#define VIPER_HANDLE_NETWORK_RX 0xFFFF0002u
#define VIPER_POLL_CONSOLE_INPUT (1u << 3)
#define VIPER_POLL_NETWORK_RX (1u << 4)

struct viper_poll_event
{
    unsigned int handle;
    unsigned int events;
    unsigned int triggered;
};

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

static int poll_set_configure(long poll_set, int want_console, int want_net)
{
    static int configured_console = 0;
    static int configured_net = 0;

    if (want_console && !configured_console)
    {
        long rc = __syscall3(SYS_POLL_ADD, poll_set, (long)VIPER_HANDLE_CONSOLE_INPUT, VIPER_POLL_CONSOLE_INPUT);
        if (rc < 0)
            return (int)rc;
        configured_console = 1;
    }
    if (!want_console && configured_console)
    {
        (void)__syscall2(SYS_POLL_REMOVE, poll_set, (long)VIPER_HANDLE_CONSOLE_INPUT);
        configured_console = 0;
    }

    if (want_net && !configured_net)
    {
        long rc = __syscall3(SYS_POLL_ADD, poll_set, (long)VIPER_HANDLE_NETWORK_RX, VIPER_POLL_NETWORK_RX);
        if (rc < 0)
            return (int)rc;
        configured_net = 1;
    }
    if (!want_net && configured_net)
    {
        (void)__syscall2(SYS_POLL_REMOVE, poll_set, (long)VIPER_HANDLE_NETWORK_RX);
        configured_net = 0;
    }

    return 0;
}

/*
 * poll - Wait for events on file descriptors
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
    int want_net = 0;
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

        // Virtual sockets: map POLLIN to network RX pseudo-handle.
        if (__viper_socket_is_fd(fd))
        {
            if (ev & (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND))
            {
                want_net = 1;
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

    // If we have console/network fds, sample or wait using the kernel pollset.
    if (want_console || want_net)
    {
        long poll_set = get_poll_set_id();
        if (poll_set < 0)
        {
            errno = (int)(-poll_set);
            return -1;
        }

        int cfg = poll_set_configure(poll_set, want_console, want_net);
        if (cfg < 0)
        {
            errno = -cfg;
            return -1;
        }

        int wait_ms = any_ready ? 0 : timeout;

        struct viper_poll_event events[2];
        unsigned int n = 0;

        if (want_console)
        {
            events[n].handle = VIPER_HANDLE_CONSOLE_INPUT;
            events[n].events = VIPER_POLL_CONSOLE_INPUT;
            events[n].triggered = 0;
            n++;
        }
        if (want_net)
        {
            events[n].handle = VIPER_HANDLE_NETWORK_RX;
            events[n].events = VIPER_POLL_NETWORK_RX;
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
        int net_ready = 0;
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
                net_ready = 1;
            }
        }

        if (console_ready || net_ready)
        {
            for (nfds_t i = 0; i < nfds; i++)
            {
                int fd = fds[i].fd;
                short ev = fds[i].events;

                if (fd == 0 && console_ready && (ev & (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)))
                {
                    fds[i].revents |= POLLIN;
                }

                if (__viper_socket_is_fd(fd) && net_ready &&
                    (ev & (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)))
                {
                    fds[i].revents |= POLLIN;
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

/*
 * ppoll - poll with precise timeout and signal mask
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

/*
 * select - Synchronous I/O multiplexing
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

/*
 * pselect - select with precise timeout and signal mask
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
