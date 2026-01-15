//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/msg.c
// Purpose: System V message queue functions for ViperDOS libc.
// Key invariants: In-memory queues (16 max); no blocking support.
// Ownership/Lifetime: Library; global message queue table.
// Links: user/libc/include/sys/msg.h
//
//===----------------------------------------------------------------------===//

/**
 * @file msg.c
 * @brief System V message queue functions for ViperDOS libc.
 *
 * @details
 * This file implements System V message queue functions:
 *
 * - msgget: Get or create a message queue
 * - msgsnd: Send a message to a queue
 * - msgrcv: Receive a message from a queue
 * - msgctl: Message queue control operations
 *
 * ViperDOS provides an in-memory implementation with up to 16
 * message queues. Messages are stored as linked list nodes.
 * Blocking operations return EAGAIN/ENOMSG instead of blocking.
 * Message type filtering (msgtyp parameter) is fully supported.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>

/* Internal message structure */
struct msg_node
{
    long mtype;
    size_t msize;
    struct msg_node *next;
    char mtext[]; /* Flexible array member */
};

/* Internal message queue structure */
struct msg_queue
{
    int in_use;
    key_t key;
    struct msqid_ds ds;
    struct msg_node *head;
    struct msg_node *tail;
};

/* Global message queue table */
#define MAX_MSG_QUEUES 16
static struct msg_queue msg_queues[MAX_MSG_QUEUES];
static int msg_initialized = 0;

static void init_msg_queues(void)
{
    if (!msg_initialized)
    {
        memset(msg_queues, 0, sizeof(msg_queues));
        msg_initialized = 1;
    }
}

static int find_by_key(key_t key)
{
    for (int i = 0; i < MAX_MSG_QUEUES; i++)
    {
        if (msg_queues[i].in_use && msg_queues[i].key == key)
        {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_MSG_QUEUES; i++)
    {
        if (!msg_queues[i].in_use)
        {
            return i;
        }
    }
    return -1;
}

int msgget(key_t key, int msgflg)
{
    init_msg_queues();

    /* Check for IPC_PRIVATE or existing key */
    if (key != IPC_PRIVATE)
    {
        int existing = find_by_key(key);
        if (existing >= 0)
        {
            if (msgflg & IPC_CREAT && msgflg & IPC_EXCL)
            {
                errno = EEXIST;
                return -1;
            }
            return existing;
        }

        if (!(msgflg & IPC_CREAT))
        {
            errno = ENOENT;
            return -1;
        }
    }

    /* Create new message queue */
    int slot = find_free_slot();
    if (slot < 0)
    {
        errno = ENOSPC;
        return -1;
    }

    struct msg_queue *mq = &msg_queues[slot];
    mq->in_use = 1;
    mq->key = key;
    mq->ds.msg_perm.mode = msgflg & 0777;
    mq->ds.msg_perm.uid = 0; /* Would be getuid() */
    mq->ds.msg_perm.gid = 0; /* Would be getgid() */
    mq->ds.msg_stime = 0;
    mq->ds.msg_rtime = 0;
    mq->ds.msg_ctime = 0; /* Would be time(NULL) */
    mq->ds.msg_cbytes = 0;
    mq->ds.msg_qnum = 0;
    mq->ds.msg_qbytes = MSGMNB;
    mq->ds.msg_lspid = 0;
    mq->ds.msg_lrpid = 0;
    mq->head = NULL;
    mq->tail = NULL;

    return slot;
}

int msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg)
{
    init_msg_queues();

    if (msqid < 0 || msqid >= MAX_MSG_QUEUES || !msg_queues[msqid].in_use)
    {
        errno = EINVAL;
        return -1;
    }

    if (msgp == NULL)
    {
        errno = EFAULT;
        return -1;
    }

    if (msgsz > MSGMAX)
    {
        errno = EINVAL;
        return -1;
    }

    const struct msgbuf *msg = (const struct msgbuf *)msgp;
    if (msg->mtype <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    struct msg_queue *mq = &msg_queues[msqid];

    /* Check if queue is full */
    if (mq->ds.msg_cbytes + msgsz > mq->ds.msg_qbytes)
    {
        if (msgflg & IPC_NOWAIT)
        {
            errno = EAGAIN;
            return -1;
        }
        /* Would block - not implemented in single-process */
        errno = EAGAIN;
        return -1;
    }

    /* Allocate message node */
    struct msg_node *node = (struct msg_node *)malloc(sizeof(struct msg_node) + msgsz);
    if (node == NULL)
    {
        errno = ENOMEM;
        return -1;
    }

    node->mtype = msg->mtype;
    node->msize = msgsz;
    node->next = NULL;
    memcpy(node->mtext, msg->mtext, msgsz);

    /* Add to queue */
    if (mq->tail == NULL)
    {
        mq->head = node;
        mq->tail = node;
    }
    else
    {
        mq->tail->next = node;
        mq->tail = node;
    }

    mq->ds.msg_cbytes += msgsz;
    mq->ds.msg_qnum++;
    mq->ds.msg_lspid = 0; /* Would be getpid() */
    /* mq->ds.msg_stime = time(NULL); */

    return 0;
}

ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg)
{
    init_msg_queues();

    if (msqid < 0 || msqid >= MAX_MSG_QUEUES || !msg_queues[msqid].in_use)
    {
        errno = EINVAL;
        return -1;
    }

    if (msgp == NULL)
    {
        errno = EFAULT;
        return -1;
    }

    struct msg_queue *mq = &msg_queues[msqid];
    struct msgbuf *msg = (struct msgbuf *)msgp;

    /* Find matching message */
    struct msg_node *prev = NULL;
    struct msg_node *node = mq->head;
    struct msg_node *found = NULL;
    struct msg_node *found_prev = NULL;

    while (node != NULL)
    {
        int match = 0;

        if (msgtyp == 0)
        {
            /* First message in queue */
            match = 1;
        }
        else if (msgtyp > 0)
        {
            /* First message of type msgtyp */
            if (node->mtype == msgtyp)
            {
                match = 1;
            }
        }
        else
        {
            /* First message with type <= |msgtyp| */
            if (node->mtype <= -msgtyp)
            {
                if (found == NULL || node->mtype < found->mtype)
                {
                    found = node;
                    found_prev = prev;
                }
            }
        }

        if (match && msgtyp >= 0)
        {
            found = node;
            found_prev = prev;
            break;
        }

        prev = node;
        node = node->next;
    }

    if (found == NULL)
    {
        if (msgflg & IPC_NOWAIT)
        {
            errno = ENOMSG;
            return -1;
        }
        /* Would block - not implemented */
        errno = ENOMSG;
        return -1;
    }

    /* Check message size */
    size_t actual_size = found->msize;
    if (actual_size > msgsz)
    {
        if (!(msgflg & MSG_NOERROR))
        {
            errno = E2BIG;
            return -1;
        }
        actual_size = msgsz;
    }

    /* Copy message */
    msg->mtype = found->mtype;
    memcpy(msg->mtext, found->mtext, actual_size);

    /* Remove from queue (unless MSG_COPY) */
    if (!(msgflg & MSG_COPY))
    {
        if (found_prev == NULL)
        {
            mq->head = found->next;
        }
        else
        {
            found_prev->next = found->next;
        }
        if (found == mq->tail)
        {
            mq->tail = found_prev;
        }

        mq->ds.msg_cbytes -= found->msize;
        mq->ds.msg_qnum--;
        free(found);
    }

    mq->ds.msg_lrpid = 0; /* Would be getpid() */
    /* mq->ds.msg_rtime = time(NULL); */

    return (ssize_t)actual_size;
}

int msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
    init_msg_queues();

    if (cmd != IPC_INFO && cmd != MSG_INFO)
    {
        if (msqid < 0 || msqid >= MAX_MSG_QUEUES || !msg_queues[msqid].in_use)
        {
            errno = EINVAL;
            return -1;
        }
    }

    struct msg_queue *mq = &msg_queues[msqid];

    switch (cmd)
    {
        case IPC_RMID:
        {
            /* Free all messages */
            struct msg_node *node = mq->head;
            while (node != NULL)
            {
                struct msg_node *next = node->next;
                free(node);
                node = next;
            }
            mq->in_use = 0;
            mq->head = NULL;
            mq->tail = NULL;
            return 0;
        }

        case IPC_STAT:
            if (buf == NULL)
            {
                errno = EFAULT;
                return -1;
            }
            memcpy(buf, &mq->ds, sizeof(struct msqid_ds));
            return 0;

        case IPC_SET:
            if (buf == NULL)
            {
                errno = EFAULT;
                return -1;
            }
            mq->ds.msg_perm.uid = buf->msg_perm.uid;
            mq->ds.msg_perm.gid = buf->msg_perm.gid;
            mq->ds.msg_perm.mode = buf->msg_perm.mode & 0777;
            if (buf->msg_qbytes > 0 && buf->msg_qbytes <= MSGMNB)
            {
                mq->ds.msg_qbytes = buf->msg_qbytes;
            }
            /* mq->ds.msg_ctime = time(NULL); */
            return 0;

        case IPC_INFO:
        case MSG_INFO:
        {
            struct msginfo *info = (struct msginfo *)buf;
            if (info == NULL)
            {
                errno = EFAULT;
                return -1;
            }
            info->msgpool = MSGPOOL;
            info->msgmap = MSGMAP;
            info->msgmax = MSGMAX;
            info->msgmnb = MSGMNB;
            info->msgmni = MSGMNI;
            info->msgssz = MSGSSZ;
            info->msgtql = MSGTQL;
            info->msgseg = MSGSEG;
            return MAX_MSG_QUEUES - 1;
        }

        default:
            errno = EINVAL;
            return -1;
    }
}
