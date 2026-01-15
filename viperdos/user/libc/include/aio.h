/*
 * ViperDOS C Library - aio.h
 * Asynchronous I/O
 */

#ifndef _AIO_H
#define _AIO_H

#include <signal.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Asynchronous I/O control block
     */
    struct aiocb
    {
        int aio_fildes;               /* File descriptor */
        off_t aio_offset;             /* File offset */
        volatile void *aio_buf;       /* Buffer location */
        size_t aio_nbytes;            /* Number of bytes */
        int aio_reqprio;              /* Request priority offset */
        struct sigevent aio_sigevent; /* Signal event */
        int aio_lio_opcode;           /* List I/O operation code */

        /* Implementation-specific fields */
        int __aio_error;      /* Error code */
        ssize_t __aio_return; /* Return value */
        int __aio_state;      /* Operation state */
    };

/*
 * lio_listio() operation codes
 */
#define LIO_READ 0  /* Read operation */
#define LIO_WRITE 1 /* Write operation */
#define LIO_NOP 2   /* No operation */

/*
 * lio_listio() modes
 */
#define LIO_WAIT 0   /* Wait for completion */
#define LIO_NOWAIT 1 /* Do not wait */

/*
 * aio_cancel() return values
 */
#define AIO_CANCELED 0    /* Request was canceled */
#define AIO_NOTCANCELED 1 /* Request was not canceled */
#define AIO_ALLDONE 2     /* Request already completed */

/*
 * Internal states
 */
#define __AIO_PENDING 0  /* Operation pending */
#define __AIO_COMPLETE 1 /* Operation complete */
#define __AIO_CANCELED 2 /* Operation canceled */
#define __AIO_ERROR 3    /* Operation failed */

    /*
     * Submit an asynchronous read request.
     * Returns 0 on success, -1 on error.
     */
    int aio_read(struct aiocb *aiocbp);

    /*
     * Submit an asynchronous write request.
     * Returns 0 on success, -1 on error.
     */
    int aio_write(struct aiocb *aiocbp);

    /*
     * Submit a list of I/O requests.
     * Returns 0 on success, -1 on error.
     */
    int lio_listio(int mode, struct aiocb *const list[], int nent, struct sigevent *sig);

    /*
     * Get the error status of an asynchronous I/O operation.
     * Returns the error status (0 if complete, EINPROGRESS if pending).
     */
    int aio_error(const struct aiocb *aiocbp);

    /*
     * Get the return status of an asynchronous I/O operation.
     * Returns the bytes transferred, or -1 on error.
     */
    ssize_t aio_return(struct aiocb *aiocbp);

    /*
     * Cancel an asynchronous I/O request.
     * Returns AIO_CANCELED, AIO_NOTCANCELED, or AIO_ALLDONE.
     */
    int aio_cancel(int fd, struct aiocb *aiocbp);

    /*
     * Wait for asynchronous I/O request completion.
     * Returns 0 on success, -1 on error or timeout.
     */
    int aio_suspend(const struct aiocb *const list[], int nent, const struct timespec *timeout);

    /*
     * Asynchronous file synchronization.
     * Returns 0 on success, -1 on error.
     */
    int aio_fsync(int op, struct aiocb *aiocbp);

#ifdef __cplusplus
}
#endif

#endif /* _AIO_H */
