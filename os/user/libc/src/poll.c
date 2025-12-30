#include "../include/poll.h"
#include "../include/sys/select.h"
#include "../include/time.h"
#include "../include/errno.h"

/* Syscall helpers */
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall4(long num, long arg0, long arg1, long arg2, long arg3);
extern long __syscall5(long num, long arg0, long arg1, long arg2, long arg3, long arg4);

/* Syscall numbers */
#define SYS_POLL   0xB1
#define SYS_SELECT 0xB2

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

    long result = __syscall3(SYS_POLL, (long)fds, nfds, timeout);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return (int)result;
}

/*
 * ppoll - poll with precise timeout and signal mask
 */
int ppoll(struct pollfd *fds, nfds_t nfds,
          const struct timespec *timeout_ts,
          const void *sigmask)
{
    int timeout_ms;

    /* Convert timespec to milliseconds */
    if (timeout_ts == (void *)0)
    {
        timeout_ms = -1; /* Infinite */
    }
    else
    {
        timeout_ms = (int)(timeout_ts->tv_sec * 1000 +
                          timeout_ts->tv_nsec / 1000000);
    }

    /* Ignore sigmask - not fully implemented */
    (void)sigmask;

    return poll(fds, nfds, timeout_ms);
}

/*
 * select - Synchronous I/O multiplexing
 */
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
    long timeout_ms;

    if (nfds < 0 || nfds > FD_SETSIZE)
    {
        errno = EINVAL;
        return -1;
    }

    /* Convert timeval to milliseconds */
    if (timeout == (void *)0)
    {
        timeout_ms = -1; /* Infinite */
    }
    else
    {
        timeout_ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
    }

    long result = __syscall5(SYS_SELECT, nfds, (long)readfds,
                             (long)writefds, (long)exceptfds, timeout_ms);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }

    /* Update timeout with remaining time (simplified - just set to zero) */
    if (timeout != (void *)0)
    {
        timeout->tv_sec = 0;
        timeout->tv_usec = 0;
    }

    return (int)result;
}

/*
 * pselect - select with precise timeout and signal mask
 */
int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout,
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
