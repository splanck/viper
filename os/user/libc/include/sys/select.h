#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include "../time.h"
#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Maximum number of file descriptors in an fd_set */
#define FD_SETSIZE 1024

/* Bits per word */
#define __NFDBITS (8 * sizeof(unsigned long))

    /* fd_set type - bit array for file descriptors */
    typedef struct
    {
        unsigned long fds_bits[FD_SETSIZE / __NFDBITS];
    } fd_set;

/* fd_set manipulation macros */
#define __FD_ELT(d) ((d) / __NFDBITS)
#define __FD_MASK(d) (1UL << ((d) % __NFDBITS))

#define FD_ZERO(set)                                                                               \
    do                                                                                             \
    {                                                                                              \
        unsigned int __i;                                                                          \
        fd_set *__arr = (set);                                                                     \
        for (__i = 0; __i < FD_SETSIZE / __NFDBITS; __i++)                                         \
            __arr->fds_bits[__i] = 0;                                                              \
    } while (0)

#define FD_SET(d, set) ((void)((set)->fds_bits[__FD_ELT(d)] |= __FD_MASK(d)))

#define FD_CLR(d, set) ((void)((set)->fds_bits[__FD_ELT(d)] &= ~__FD_MASK(d)))

#define FD_ISSET(d, set) (((set)->fds_bits[__FD_ELT(d)] & __FD_MASK(d)) != 0)

/* Copy fd_set */
#define FD_COPY(src, dst)                                                                          \
    do                                                                                             \
    {                                                                                              \
        unsigned int __i;                                                                          \
        for (__i = 0; __i < FD_SETSIZE / __NFDBITS; __i++)                                         \
            (dst)->fds_bits[__i] = (src)->fds_bits[__i];                                           \
    } while (0)

    /*
     * select - Synchronous I/O multiplexing
     * nfds: highest fd + 1
     * readfds: fds to check for read readiness
     * writefds: fds to check for write readiness
     * exceptfds: fds to check for exceptional conditions
     * timeout: max wait time (NULL for infinite)
     * Returns: number of ready fds, 0 on timeout, -1 on error
     */
    int select(
        int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

    /*
     * pselect - select with nanosecond timeout and signal mask
     */
    int pselect(int nfds,
                fd_set *readfds,
                fd_set *writefds,
                fd_set *exceptfds,
                const struct timespec *timeout,
                const void *sigmask);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SELECT_H */
