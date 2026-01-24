#ifndef _POLL_H
#define _POLL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Type for number of file descriptors */
typedef unsigned int nfds_t;

/* poll event structure */
struct pollfd {
    int fd;        /* File descriptor */
    short events;  /* Requested events */
    short revents; /* Returned events */
};

/* Event types to poll for */
#define POLLIN 0x0001   /* Data to read */
#define POLLPRI 0x0002  /* Urgent data to read */
#define POLLOUT 0x0004  /* Writing will not block */
#define POLLERR 0x0008  /* Error condition (output only) */
#define POLLHUP 0x0010  /* Hang up (output only) */
#define POLLNVAL 0x0020 /* Invalid request: fd not open (output only) */

/* Linux/POSIX extensions */
#define POLLRDNORM 0x0040 /* Normal data to read */
#define POLLRDBAND 0x0080 /* Priority band data to read */
#define POLLWRNORM 0x0100 /* Writing normal data will not block */
#define POLLWRBAND 0x0200 /* Writing priority data will not block */

/* Compatibility aliases */
#define POLLREMOVE 0x1000 /* Remove fd from poll set */
#define POLLRDHUP 0x2000  /* Peer closed or shut down writing */

/*
 * poll - Wait for events on file descriptors
 * fds: array of pollfd structures
 * nfds: number of elements in fds
 * timeout: timeout in milliseconds (-1 for infinite, 0 for immediate return)
 * Returns: number of fds with events, 0 on timeout, -1 on error
 */
int poll(struct pollfd *fds, nfds_t nfds, int timeout);

/*
 * ppoll - poll with timeout precision and signal mask
 */
struct timespec;
int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const void *sigmask);

#ifdef __cplusplus
}
#endif

#endif /* _POLL_H */
