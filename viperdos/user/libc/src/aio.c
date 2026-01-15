//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/aio.c
// Purpose: Asynchronous I/O functions for ViperDOS libc.
// Key invariants: Executes synchronously (no true async); immediate completion.
// Ownership/Lifetime: Library; state stored in aiocb structs.
// Links: user/libc/include/aio.h
//
//===----------------------------------------------------------------------===//

/**
 * @file aio.c
 * @brief Asynchronous I/O functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX asynchronous I/O functions:
 *
 * - aio_read/aio_write: Async read/write (sync fallback)
 * - lio_listio: Process a list of I/O requests
 * - aio_error/aio_return: Get operation status/result
 * - aio_cancel: Cancel pending operations
 * - aio_suspend: Wait for operations to complete
 * - aio_fsync: Async file synchronization
 *
 * ViperDOS implements these as synchronous operations. Each aio_*
 * call completes immediately using pread/pwrite, and the aiocb
 * state is set to __AIO_COMPLETE. True asynchronous I/O with
 * worker threads is not implemented.
 */

#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/*
 * Perform synchronous read as fallback for async read.
 * In a real implementation, this would submit to a worker thread.
 */
int aio_read(struct aiocb *aiocbp)
{
    if (aiocbp == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    /* Perform synchronous read */
    ssize_t result =
        pread(aiocbp->aio_fildes, (void *)aiocbp->aio_buf, aiocbp->aio_nbytes, aiocbp->aio_offset);

    if (result < 0)
    {
        aiocbp->__aio_error = errno;
        aiocbp->__aio_return = -1;
        aiocbp->__aio_state = __AIO_ERROR;
    }
    else
    {
        aiocbp->__aio_error = 0;
        aiocbp->__aio_return = result;
        aiocbp->__aio_state = __AIO_COMPLETE;
    }

    return 0;
}

/*
 * Perform synchronous write as fallback for async write.
 */
int aio_write(struct aiocb *aiocbp)
{
    if (aiocbp == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    /* Perform synchronous write */
    ssize_t result = pwrite(
        aiocbp->aio_fildes, (const void *)aiocbp->aio_buf, aiocbp->aio_nbytes, aiocbp->aio_offset);

    if (result < 0)
    {
        aiocbp->__aio_error = errno;
        aiocbp->__aio_return = -1;
        aiocbp->__aio_state = __AIO_ERROR;
    }
    else
    {
        aiocbp->__aio_error = 0;
        aiocbp->__aio_return = result;
        aiocbp->__aio_state = __AIO_COMPLETE;
    }

    return 0;
}

/*
 * Process a list of I/O requests.
 */
int lio_listio(int mode, struct aiocb *const list[], int nent, struct sigevent *sig)
{
    (void)sig; /* Signal notification not supported */

    if (list == NULL || nent <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    int errors = 0;

    for (int i = 0; i < nent; i++)
    {
        struct aiocb *aio = list[i];
        if (aio == NULL)
            continue;

        int result = 0;
        switch (aio->aio_lio_opcode)
        {
            case LIO_READ:
                result = aio_read(aio);
                break;
            case LIO_WRITE:
                result = aio_write(aio);
                break;
            case LIO_NOP:
                aio->__aio_error = 0;
                aio->__aio_return = 0;
                aio->__aio_state = __AIO_COMPLETE;
                break;
            default:
                aio->__aio_error = EINVAL;
                aio->__aio_return = -1;
                aio->__aio_state = __AIO_ERROR;
                errors++;
                continue;
        }

        if (result < 0)
        {
            errors++;
        }
    }

    if (mode == LIO_WAIT)
    {
        /* Already completed synchronously */
    }

    if (errors > 0)
    {
        errno = EIO;
        return -1;
    }

    return 0;
}

/*
 * Get the error status of an I/O request.
 */
int aio_error(const struct aiocb *aiocbp)
{
    if (aiocbp == NULL)
    {
        return EINVAL;
    }

    switch (aiocbp->__aio_state)
    {
        case __AIO_PENDING:
            return EINPROGRESS;
        case __AIO_COMPLETE:
            return 0;
        case __AIO_CANCELED:
            return ECANCELED;
        case __AIO_ERROR:
        default:
            return aiocbp->__aio_error;
    }
}

/*
 * Get the return value of a completed I/O request.
 */
ssize_t aio_return(struct aiocb *aiocbp)
{
    if (aiocbp == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    /* Return value should only be retrieved once */
    ssize_t result = aiocbp->__aio_return;

    /* Reset state */
    aiocbp->__aio_state = __AIO_PENDING;
    aiocbp->__aio_error = 0;
    aiocbp->__aio_return = 0;

    return result;
}

/*
 * Cancel an I/O request.
 * Since we execute synchronously, requests are always complete.
 */
int aio_cancel(int fd, struct aiocb *aiocbp)
{
    (void)fd;

    if (aiocbp == NULL)
    {
        /* Cancel all requests on fd - not supported */
        return AIO_ALLDONE;
    }

    /* In our synchronous implementation, always complete */
    return AIO_ALLDONE;
}

/*
 * Suspend until one or more requests complete.
 * Since we execute synchronously, this always returns immediately.
 */
int aio_suspend(const struct aiocb *const list[], int nent, const struct timespec *timeout)
{
    (void)timeout;

    if (list == NULL || nent <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    /* All operations are synchronous, so already complete */
    for (int i = 0; i < nent; i++)
    {
        if (list[i] != NULL && list[i]->__aio_state == __AIO_COMPLETE)
        {
            return 0;
        }
    }

    /* If nothing is complete, return success anyway
     * (synchronous mode) */
    return 0;
}

/*
 * Asynchronous file synchronization.
 */
int aio_fsync(int op, struct aiocb *aiocbp)
{
    if (aiocbp == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    /* Perform synchronous fsync */
    int result;
    if (op == O_DSYNC)
    {
        result = fdatasync(aiocbp->aio_fildes);
    }
    else
    {
        result = fsync(aiocbp->aio_fildes);
    }

    if (result < 0)
    {
        aiocbp->__aio_error = errno;
        aiocbp->__aio_return = -1;
        aiocbp->__aio_state = __AIO_ERROR;
    }
    else
    {
        aiocbp->__aio_error = 0;
        aiocbp->__aio_return = 0;
        aiocbp->__aio_state = __AIO_COMPLETE;
    }

    return 0;
}
