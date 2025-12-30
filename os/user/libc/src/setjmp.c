#include "../include/setjmp.h"
#include "../include/signal.h"

/*
 * sigsetjmp - setjmp with optional signal mask save
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
