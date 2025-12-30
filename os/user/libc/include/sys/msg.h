/*
 * ViperOS C Library - sys/msg.h
 * System V message queue operations
 */

#ifndef _SYS_MSG_H
#define _SYS_MSG_H

#include <sys/types.h>
#include <sys/ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Message queue types */
typedef unsigned long msgqnum_t;  /* Number of messages in queue */
typedef unsigned long msglen_t;   /* Message length */

/* msgsnd/msgrcv flags */
#define MSG_NOERROR     010000  /* Truncate message if too long */
#define MSG_EXCEPT      020000  /* Receive any msg except of specified type */
#define MSG_COPY        040000  /* Copy (don't remove) a message */

/* msgctl() commands - shared with IPC */
#ifndef IPC_RMID
#define IPC_RMID        0       /* Remove identifier */
#define IPC_SET         1       /* Set ipc_perm options */
#define IPC_STAT        2       /* Get msqid_ds structure */
#define IPC_INFO        3       /* Get system info */
#endif

#define MSG_INFO        12      /* Get msginfo struct */
#define MSG_STAT        11      /* Get msqid_ds (special) */

/* System limits (implementation-defined) */
#define MSGMNI          16      /* Max number of message queues */
#define MSGMAX          8192    /* Max size of a single message */
#define MSGMNB          16384   /* Max bytes in a queue */
#define MSGTQL          128     /* Max messages in system */
#define MSGPOOL         1024    /* Message pool size (KB) */
#define MSGMAP          128     /* # of entries in message map */
#define MSGSSZ          16      /* Message segment size */
#define MSGSEG          2048    /* Max # of message segments */

/* Message queue ID data structure */
struct msqid_ds {
    struct ipc_perm msg_perm;   /* Operation permission structure */
    time_t          msg_stime;  /* Time of last msgsnd() */
    time_t          msg_rtime;  /* Time of last msgrcv() */
    time_t          msg_ctime;  /* Time of last change */
    unsigned long   msg_cbytes; /* Current # of bytes in queue */
    msgqnum_t       msg_qnum;   /* Current # of messages in queue */
    msglen_t        msg_qbytes; /* Max # of bytes allowed in queue */
    pid_t           msg_lspid;  /* PID of last msgsnd() */
    pid_t           msg_lrpid;  /* PID of last msgrcv() */
};

/* Message info structure for MSG_INFO */
struct msginfo {
    int msgpool;    /* Size of message pool (KB) */
    int msgmap;     /* # of entries in message map */
    int msgmax;     /* Max size of a single message */
    int msgmnb;     /* Max bytes in a queue */
    int msgmni;     /* Max # of message queues */
    int msgssz;     /* Message segment size */
    int msgtql;     /* Max # of messages in system */
    unsigned short msgseg; /* Max # of message segments */
};

/* Template for message buffer */
struct msgbuf {
    long mtype;     /* Message type (must be > 0) */
    char mtext[1];  /* Message data (variable length) */
};

/*
 * Get a message queue identifier.
 * Returns message queue ID on success, -1 on error.
 */
int msgget(key_t key, int msgflg);

/*
 * Send a message to a message queue.
 * Returns 0 on success, -1 on error.
 */
int msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);

/*
 * Receive a message from a message queue.
 * Returns the number of bytes received, -1 on error.
 */
ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);

/*
 * Message queue control operations.
 * Returns 0 on success for most operations, -1 on error.
 */
int msgctl(int msqid, int cmd, struct msqid_ds *buf);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_MSG_H */
