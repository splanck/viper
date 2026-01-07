/*
 * ViperOS libc - fenv.h
 * Floating-point environment control
 */

#ifndef _FENV_H
#define _FENV_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Floating-point exception flags (AArch64 FPSR bits) */
#define FE_INVALID 0x01   /* Invalid operation */
#define FE_DIVBYZERO 0x02 /* Division by zero */
#define FE_OVERFLOW 0x04  /* Overflow */
#define FE_UNDERFLOW 0x08 /* Underflow */
#define FE_INEXACT 0x10   /* Inexact result */
#define FE_DENORMAL 0x80  /* Denormal operand (AArch64 specific) */

#define FE_ALL_EXCEPT (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT)

/* Rounding modes (AArch64 FPCR bits 22-23) */
#define FE_TONEAREST 0x00000000  /* Round to nearest (default) */
#define FE_UPWARD 0x00400000     /* Round toward +infinity */
#define FE_DOWNWARD 0x00800000   /* Round toward -infinity */
#define FE_TOWARDZERO 0x00C00000 /* Round toward zero */

    /* Floating-point environment type */
    typedef struct
    {
        unsigned int __fpcr; /* Floating-point control register */
        unsigned int __fpsr; /* Floating-point status register */
    } fenv_t;

    /* Floating-point exception type */
    typedef unsigned int fexcept_t;

    /* Default floating-point environment */
    extern const fenv_t __fe_dfl_env;
#define FE_DFL_ENV (&__fe_dfl_env)

    /*
     * Exception handling functions
     */

    /* Clear the specified floating-point exception flags */
    int feclearexcept(int excepts);

    /* Get the current exception flags */
    int fegetexceptflag(fexcept_t *flagp, int excepts);

    /* Raise the specified floating-point exceptions */
    int feraiseexcept(int excepts);

    /* Set the exception flags from the saved state */
    int fesetexceptflag(const fexcept_t *flagp, int excepts);

    /* Test the specified exception flags */
    int fetestexcept(int excepts);

    /*
     * Rounding mode functions
     */

    /* Get the current rounding mode */
    int fegetround(void);

    /* Set the rounding mode */
    int fesetround(int round);

    /*
     * Environment functions
     */

    /* Get the current floating-point environment */
    int fegetenv(fenv_t *envp);

    /* Establish a floating-point environment while saving exception state */
    int feholdexcept(fenv_t *envp);

    /* Set the floating-point environment */
    int fesetenv(const fenv_t *envp);

    /* Set environment and raise saved exceptions */
    int feupdateenv(const fenv_t *envp);

    /*
     * Non-standard extensions
     */

    /* Enable floating-point exception traps */
    int feenableexcept(int excepts);

    /* Disable floating-point exception traps */
    int fedisableexcept(int excepts);

    /* Get enabled exceptions */
    int fegetexcept(void);

#ifdef __cplusplus
}
#endif

#endif /* _FENV_H */
