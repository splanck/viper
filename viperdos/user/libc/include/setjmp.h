#ifndef _SETJMP_H
#define _SETJMP_H

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * jmp_buf layout for AArch64:
     * [0-9]   x19-x28 (callee-saved general purpose registers)
     * [10]    x29 (frame pointer)
     * [11]    x30 (link register / return address)
     * [12]    sp (stack pointer)
     * [13-20] d8-d15 (callee-saved floating point registers)
     */
    typedef unsigned long jmp_buf[21];

    /* sigjmp_buf includes signal mask storage */
    typedef struct
    {
        jmp_buf buf;
        int savemask;
        unsigned long sigmask;
    } sigjmp_buf[1];

    /*
     * setjmp - Save current execution context
     * Returns 0 when called directly, non-zero when returning via longjmp
     */
    int setjmp(jmp_buf env);

    /*
     * longjmp - Restore execution context saved by setjmp
     * Never returns; instead, causes setjmp to return with val (or 1 if val is 0)
     */
    void longjmp(jmp_buf env, int val) __attribute__((noreturn));

    /*
     * _setjmp / _longjmp - versions that don't save/restore signal mask
     * (On this implementation, same as setjmp/longjmp since we don't have threads)
     */
    int _setjmp(jmp_buf env);
    void _longjmp(jmp_buf env, int val) __attribute__((noreturn));

    /*
     * sigsetjmp / siglongjmp - versions with optional signal mask save/restore
     */
    int sigsetjmp(sigjmp_buf env, int savemask);
    void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* _SETJMP_H */
