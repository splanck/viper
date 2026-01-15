//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/mqueue.c
// Purpose: POSIX message queue functions for ViperDOS libc.
// Key invariants: In-memory queues (16 max); priority-ordered messages.
// Ownership/Lifetime: Library; global queue table; messages dynamically allocated.
// Links: user/libc/include/mqueue.h
//
//===----------------------------------------------------------------------===//

/**
 * @file mqueue.c
 * @brief POSIX message queue functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX message queue functions:
 *
 * - mq_open: Open or create a message queue
 * - mq_close: Close a message queue descriptor
 * - mq_unlink: Remove a message queue
 * - mq_send/mq_timedsend: Send a message to a queue
 * - mq_receive/mq_timedreceive: Receive a message from a queue
 * - mq_getattr/mq_setattr: Get/set queue attributes
 * - mq_notify: Register for message arrival notification (stub)
 *
 * Messages are stored in priority order (highest first). Each queue
 * has configurable maximum message count and message size. Blocking
 * operations return EAGAIN; true blocking is not implemented.
 */

#include <errno.h>
#include <mqueue.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Default limits */
#define MQ_MAX_QUEUES 16
#define MQ_DEFAULT_MAXMSG 10
#define MQ_DEFAULT_MSGSIZE 1024
#define MQ_MAX_NAME 32

/* Message structure */
struct mq_message
{
    unsigned int priority;
    size_t length;
    struct mq_message *next;
    char data[]; /* Flexible array member */
};

/* Message queue structure */
struct mq_queue
{
    int in_use;
    char name[MQ_MAX_NAME];
    struct mq_attr attr;
    struct mq_message *head;
    struct mq_message *tail;
    int refcount;
    int unlinked;
};

/* Global queue table */
static struct mq_queue mq_queues[MQ_MAX_QUEUES];
static int mq_initialized = 0;

static void init_mq(void)
{
    if (!mq_initialized)
    {
        memset(mq_queues, 0, sizeof(mq_queues));
        mq_initialized = 1;
    }
}

static struct mq_queue *find_by_name(const char *name)
{
    for (int i = 0; i < MQ_MAX_QUEUES; i++)
    {
        if (mq_queues[i].in_use && !mq_queues[i].unlinked && strcmp(mq_queues[i].name, name) == 0)
        {
            return &mq_queues[i];
        }
    }
    return NULL;
}

static int find_free_slot(void)
{
    for (int i = 0; i < MQ_MAX_QUEUES; i++)
    {
        if (!mq_queues[i].in_use)
        {
            return i;
        }
    }
    return -1;
}

mqd_t mq_open(const char *name, int oflag, ...)
{
    init_mq();

    if (name == NULL || name[0] != '/')
    {
        errno = EINVAL;
        return MQD_INVALID;
    }

    const char *short_name = name + 1;
    if (strlen(short_name) >= MQ_MAX_NAME)
    {
        errno = ENAMETOOLONG;
        return MQD_INVALID;
    }

    struct mq_queue *mq = find_by_name(short_name);

    if (mq != NULL)
    {
        /* Queue exists */
        if ((oflag & O_CREAT) && (oflag & O_EXCL))
        {
            errno = EEXIST;
            return MQD_INVALID;
        }
        mq->refcount++;
        return (mqd_t)(mq - mq_queues);
    }

    /* Queue doesn't exist */
    if (!(oflag & O_CREAT))
    {
        errno = ENOENT;
        return MQD_INVALID;
    }

    /* Create new queue */
    int slot = find_free_slot();
    if (slot < 0)
    {
        errno = EMFILE;
        return MQD_INVALID;
    }

    /* Parse optional arguments */
    va_list ap;
    va_start(ap, oflag);
    mode_t mode = va_arg(ap, mode_t);
    struct mq_attr *attr = va_arg(ap, struct mq_attr *);
    va_end(ap);

    (void)mode; /* Mode not used in this implementation */

    mq = &mq_queues[slot];
    mq->in_use = 1;
    strncpy(mq->name, short_name, MQ_MAX_NAME - 1);
    mq->name[MQ_MAX_NAME - 1] = '\0';

    if (attr != NULL)
    {
        mq->attr.mq_maxmsg = attr->mq_maxmsg > 0 ? attr->mq_maxmsg : MQ_DEFAULT_MAXMSG;
        mq->attr.mq_msgsize = attr->mq_msgsize > 0 ? attr->mq_msgsize : MQ_DEFAULT_MSGSIZE;
    }
    else
    {
        mq->attr.mq_maxmsg = MQ_DEFAULT_MAXMSG;
        mq->attr.mq_msgsize = MQ_DEFAULT_MSGSIZE;
    }

    mq->attr.mq_flags = oflag & O_NONBLOCK;
    mq->attr.mq_curmsgs = 0;
    mq->head = NULL;
    mq->tail = NULL;
    mq->refcount = 1;
    mq->unlinked = 0;

    return (mqd_t)slot;
}

int mq_close(mqd_t mqdes)
{
    init_mq();

    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !mq_queues[mqdes].in_use)
    {
        errno = EBADF;
        return -1;
    }

    struct mq_queue *mq = &mq_queues[mqdes];
    mq->refcount--;

    if (mq->refcount <= 0 && mq->unlinked)
    {
        /* Free all messages */
        struct mq_message *msg = mq->head;
        while (msg != NULL)
        {
            struct mq_message *next = msg->next;
            free(msg);
            msg = next;
        }
        mq->in_use = 0;
    }

    return 0;
}

int mq_unlink(const char *name)
{
    init_mq();

    if (name == NULL || name[0] != '/')
    {
        errno = EINVAL;
        return -1;
    }

    const char *short_name = name + 1;
    struct mq_queue *mq = find_by_name(short_name);

    if (mq == NULL)
    {
        errno = ENOENT;
        return -1;
    }

    mq->unlinked = 1;

    if (mq->refcount <= 0)
    {
        /* Free all messages */
        struct mq_message *msg = mq->head;
        while (msg != NULL)
        {
            struct mq_message *next = msg->next;
            free(msg);
            msg = next;
        }
        mq->in_use = 0;
    }

    return 0;
}

int mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio)
{
    return mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, NULL);
}

int mq_timedsend(mqd_t mqdes,
                 const char *msg_ptr,
                 size_t msg_len,
                 unsigned int msg_prio,
                 const struct timespec *abs_timeout)
{
    (void)abs_timeout; /* Timeout not implemented */

    init_mq();

    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !mq_queues[mqdes].in_use)
    {
        errno = EBADF;
        return -1;
    }

    struct mq_queue *mq = &mq_queues[mqdes];

    if (msg_len > (size_t)mq->attr.mq_msgsize)
    {
        errno = EMSGSIZE;
        return -1;
    }

    if (mq->attr.mq_curmsgs >= mq->attr.mq_maxmsg)
    {
        if (mq->attr.mq_flags & O_NONBLOCK)
        {
            errno = EAGAIN;
            return -1;
        }
        /* Would block - not implemented */
        errno = EAGAIN;
        return -1;
    }

    /* Allocate message */
    struct mq_message *msg = malloc(sizeof(struct mq_message) + msg_len);
    if (msg == NULL)
    {
        errno = ENOMEM;
        return -1;
    }

    msg->priority = msg_prio;
    msg->length = msg_len;
    msg->next = NULL;
    memcpy(msg->data, msg_ptr, msg_len);

    /* Insert in priority order (higher priority first) */
    if (mq->head == NULL)
    {
        mq->head = msg;
        mq->tail = msg;
    }
    else if (msg_prio > mq->head->priority)
    {
        msg->next = mq->head;
        mq->head = msg;
    }
    else
    {
        struct mq_message *prev = mq->head;
        while (prev->next != NULL && prev->next->priority >= msg_prio)
        {
            prev = prev->next;
        }
        msg->next = prev->next;
        prev->next = msg;
        if (msg->next == NULL)
        {
            mq->tail = msg;
        }
    }

    mq->attr.mq_curmsgs++;
    return 0;
}

ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio)
{
    return mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio, NULL);
}

ssize_t mq_timedreceive(mqd_t mqdes,
                        char *msg_ptr,
                        size_t msg_len,
                        unsigned int *msg_prio,
                        const struct timespec *abs_timeout)
{
    (void)abs_timeout; /* Timeout not implemented */

    init_mq();

    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !mq_queues[mqdes].in_use)
    {
        errno = EBADF;
        return -1;
    }

    struct mq_queue *mq = &mq_queues[mqdes];

    if (msg_len < (size_t)mq->attr.mq_msgsize)
    {
        errno = EMSGSIZE;
        return -1;
    }

    if (mq->head == NULL)
    {
        if (mq->attr.mq_flags & O_NONBLOCK)
        {
            errno = EAGAIN;
            return -1;
        }
        /* Would block - not implemented */
        errno = EAGAIN;
        return -1;
    }

    /* Remove highest priority message */
    struct mq_message *msg = mq->head;
    mq->head = msg->next;
    if (mq->head == NULL)
    {
        mq->tail = NULL;
    }

    /* Copy data */
    memcpy(msg_ptr, msg->data, msg->length);
    if (msg_prio != NULL)
    {
        *msg_prio = msg->priority;
    }

    ssize_t len = (ssize_t)msg->length;
    free(msg);

    mq->attr.mq_curmsgs--;
    return len;
}

int mq_getattr(mqd_t mqdes, struct mq_attr *attr)
{
    init_mq();

    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !mq_queues[mqdes].in_use)
    {
        errno = EBADF;
        return -1;
    }

    if (attr == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    memcpy(attr, &mq_queues[mqdes].attr, sizeof(struct mq_attr));
    return 0;
}

int mq_setattr(mqd_t mqdes, const struct mq_attr *newattr, struct mq_attr *oldattr)
{
    init_mq();

    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !mq_queues[mqdes].in_use)
    {
        errno = EBADF;
        return -1;
    }

    struct mq_queue *mq = &mq_queues[mqdes];

    if (oldattr != NULL)
    {
        memcpy(oldattr, &mq->attr, sizeof(struct mq_attr));
    }

    if (newattr != NULL)
    {
        /* Only mq_flags can be changed */
        mq->attr.mq_flags = newattr->mq_flags & O_NONBLOCK;
    }

    return 0;
}

int mq_notify(mqd_t mqdes, const struct sigevent *sevp)
{
    (void)sevp; /* Notification not implemented */

    init_mq();

    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !mq_queues[mqdes].in_use)
    {
        errno = EBADF;
        return -1;
    }

    /* Notification not supported in this implementation */
    errno = ENOSYS;
    return -1;
}
