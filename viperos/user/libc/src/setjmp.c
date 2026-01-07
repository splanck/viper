//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/setjmp.c
// Purpose: Non-local jump functions with signal mask support.
// Key invariants: Signal mask save/restore on sigsetjmp/siglongjmp.
// Ownership/Lifetime: Library; wraps assembly setjmp/longjmp.
// Links: user/libc/include/setjmp.h
//
//===----------------------------------------------------------------------===//

/**
 * @file setjmp.c
 * @brief Non-local jump functions with signal mask support.
 *
 * @details
 * This file implements signal-aware non-local jumps:
 *
 * - sigsetjmp: Save execution context with optional signal mask
 * - siglongjmp: Restore execution context with optional signal mask
 *
 * The basic setjmp/longjmp are implemented in assembly (crt0.c).
 * These wrappers add optional signal mask preservation for POSIX
 * compliance. The sigjmp_buf stores both the jump buffer and
 * the signal mask if savemask is non-zero.
 */

#include "../include/setjmp.h"
#include "../include/signal.h"

/**
 * @brief Save execution context with optional signal mask.
 *
 * @param env Buffer to store execution context and signal mask.
 * @param savemask If non-zero, save the current signal mask.
 * @return 0 on initial call, non-zero when returning from siglongjmp.
 */
int sigsetjmp(sigjmp_buf env, int savemask)
{
    env->savemask = savemask;
    if (savemask)
    {
        /* Save current signal mask */
        sigprocmask(SIG_BLOCK, (void *)0, (sigset_t *)&env->sigmask);
    }
    return setjmp(env->buf);
}

/*
 * siglongjmp - longjmp with optional signal mask restore
 */
void siglongjmp(sigjmp_buf env, int val)
{
    if (env->savemask)
    {
        /* Restore signal mask */
        sigset_t mask = (sigset_t)env->sigmask;
        sigprocmask(SIG_SETMASK, &mask, (void *)0);
    }
    longjmp(env->buf, val);
}
