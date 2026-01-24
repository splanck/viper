/*
 * ViperDOS C Library - mqueue.h
 * POSIX message queues
 */

#ifndef _MQUEUE_H
#define _MQUEUE_H

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Message queue descriptor */
typedef int mqd_t;

/* Invalid message queue descriptor */
#define MQD_INVALID ((mqd_t) - 1)

/* Message queue attributes */
struct mq_attr {
    long mq_flags;   /* Message queue flags (O_NONBLOCK) */
    long mq_maxmsg;  /* Maximum number of messages */
    long mq_msgsize; /* Maximum message size */
    long mq_curmsgs; /* Number of messages currently queued */
};

/*
 * Open a message queue.
 * Returns message queue descriptor on success, (mqd_t)-1 on error.
 */
mqd_t mq_open(const char *name, int oflag, ...);

/*
 * Close a message queue.
 * Returns 0 on success, -1 on error.
 */
int mq_close(mqd_t mqdes);

/*
 * Remove a message queue.
 * Returns 0 on success, -1 on error.
 */
int mq_unlink(const char *name);

/*
 * Send a message to a queue.
 * Returns 0 on success, -1 on error.
 */
int mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio);

/*
 * Send a message with timeout.
 * Returns 0 on success, -1 on error or timeout.
 */
int mq_timedsend(mqd_t mqdes,
                 const char *msg_ptr,
                 size_t msg_len,
                 unsigned int msg_prio,
                 const struct timespec *abs_timeout);

/*
 * Receive a message from a queue.
 * Returns message length on success, -1 on error.
 */
ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio);

/*
 * Receive a message with timeout.
 * Returns message length on success, -1 on error or timeout.
 */
ssize_t mq_timedreceive(mqd_t mqdes,
                        char *msg_ptr,
                        size_t msg_len,
                        unsigned int *msg_prio,
                        const struct timespec *abs_timeout);

/*
 * Get message queue attributes.
 * Returns 0 on success, -1 on error.
 */
int mq_getattr(mqd_t mqdes, struct mq_attr *attr);

/*
 * Set message queue attributes.
 * Returns 0 on success, -1 on error.
 */
int mq_setattr(mqd_t mqdes, const struct mq_attr *newattr, struct mq_attr *oldattr);

/*
 * Register for notification when a message arrives.
 * Returns 0 on success, -1 on error.
 */
int mq_notify(mqd_t mqdes, const struct sigevent *sevp);

#ifdef __cplusplus
}
#endif

#endif /* _MQUEUE_H */
