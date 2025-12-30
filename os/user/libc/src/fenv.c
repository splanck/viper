/*
 * ViperOS libc - fenv.c
 * Floating-point environment control implementation (AArch64)
 */

#include "../include/fenv.h"

/* Default floating-point environment */
const fenv_t __fe_dfl_env = {
    .__fpcr = 0,
    .__fpsr = 0
};

/* Mask for rounding mode bits in FPCR */
#define FPCR_RMODE_MASK 0x00C00000

/* Mask for exception enable bits in FPCR (bits 8-12) */
#define FPCR_EXCEPT_MASK 0x00001F00

/* Map exception flags to FPCR enable bits */
static unsigned int except_to_enable(int excepts)
{
    unsigned int result = 0;
    if (excepts & FE_INVALID)   result |= (1 << 8);
    if (excepts & FE_DIVBYZERO) result |= (1 << 9);
    if (excepts & FE_OVERFLOW)  result |= (1 << 10);
    if (excepts & FE_UNDERFLOW) result |= (1 << 11);
    if (excepts & FE_INEXACT)   result |= (1 << 12);
    return result;
}

/* Map FPCR enable bits to exception flags */
static int enable_to_except(unsigned int enables)
{
    int result = 0;
    if (enables & (1 << 8))  result |= FE_INVALID;
    if (enables & (1 << 9))  result |= FE_DIVBYZERO;
    if (enables & (1 << 10)) result |= FE_OVERFLOW;
    if (enables & (1 << 11)) result |= FE_UNDERFLOW;
    if (enables & (1 << 12)) result |= FE_INEXACT;
    return result;
}

/* Read FPCR register */
static inline unsigned int read_fpcr(void)
{
    unsigned long fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    return (unsigned int)fpcr;
}

/* Write FPCR register */
static inline void write_fpcr(unsigned int fpcr)
{
    unsigned long val = fpcr;
    __asm__ volatile("msr fpcr, %0" : : "r"(val));
}

/* Read FPSR register */
static inline unsigned int read_fpsr(void)
{
    unsigned long fpsr;
    __asm__ volatile("mrs %0, fpsr" : "=r"(fpsr));
    return (unsigned int)fpsr;
}

/* Write FPSR register */
static inline void write_fpsr(unsigned int fpsr)
{
    unsigned long val = fpsr;
    __asm__ volatile("msr fpsr, %0" : : "r"(val));
}

/*
 * feclearexcept - Clear floating-point exception flags
 */
int feclearexcept(int excepts)
{
    unsigned int fpsr = read_fpsr();
    fpsr &= ~(excepts & FE_ALL_EXCEPT);
    write_fpsr(fpsr);
    return 0;
}

/*
 * fegetexceptflag - Get exception flags
 */
int fegetexceptflag(fexcept_t *flagp, int excepts)
{
    if (!flagp)
        return -1;

    unsigned int fpsr = read_fpsr();
    *flagp = fpsr & (excepts & FE_ALL_EXCEPT);
    return 0;
}

/*
 * feraiseexcept - Raise floating-point exceptions
 */
int feraiseexcept(int excepts)
{
    unsigned int fpsr = read_fpsr();
    fpsr |= (excepts & FE_ALL_EXCEPT);
    write_fpsr(fpsr);

    /* For trapped exceptions, the hardware will generate a trap */
    /* For non-trapped exceptions, just setting the flags is enough */
    return 0;
}

/*
 * fesetexceptflag - Set exception flags from saved state
 */
int fesetexceptflag(const fexcept_t *flagp, int excepts)
{
    if (!flagp)
        return -1;

    unsigned int fpsr = read_fpsr();
    fpsr &= ~(excepts & FE_ALL_EXCEPT);
    fpsr |= (*flagp & excepts & FE_ALL_EXCEPT);
    write_fpsr(fpsr);
    return 0;
}

/*
 * fetestexcept - Test exception flags
 */
int fetestexcept(int excepts)
{
    unsigned int fpsr = read_fpsr();
    return fpsr & (excepts & FE_ALL_EXCEPT);
}

/*
 * fegetround - Get current rounding mode
 */
int fegetround(void)
{
    unsigned int fpcr = read_fpcr();
    return fpcr & FPCR_RMODE_MASK;
}

/*
 * fesetround - Set rounding mode
 */
int fesetround(int round)
{
    /* Validate rounding mode */
    if ((round & ~FPCR_RMODE_MASK) != 0)
        return -1;

    unsigned int fpcr = read_fpcr();
    fpcr &= ~FPCR_RMODE_MASK;
    fpcr |= round;
    write_fpcr(fpcr);
    return 0;
}

/*
 * fegetenv - Get current floating-point environment
 */
int fegetenv(fenv_t *envp)
{
    if (!envp)
        return -1;

    envp->__fpcr = read_fpcr();
    envp->__fpsr = read_fpsr();
    return 0;
}

/*
 * feholdexcept - Save environment and clear exceptions
 */
int feholdexcept(fenv_t *envp)
{
    if (!envp)
        return -1;

    /* Save current environment */
    envp->__fpcr = read_fpcr();
    envp->__fpsr = read_fpsr();

    /* Clear exception flags */
    write_fpsr(0);

    /* Disable exception traps */
    unsigned int fpcr = envp->__fpcr;
    fpcr &= ~FPCR_EXCEPT_MASK;
    write_fpcr(fpcr);

    return 0;
}

/*
 * fesetenv - Set floating-point environment
 */
int fesetenv(const fenv_t *envp)
{
    if (!envp)
        return -1;

    if (envp == FE_DFL_ENV)
    {
        write_fpcr(0);
        write_fpsr(0);
    }
    else
    {
        write_fpcr(envp->__fpcr);
        write_fpsr(envp->__fpsr);
    }
    return 0;
}

/*
 * feupdateenv - Set environment and raise saved exceptions
 */
int feupdateenv(const fenv_t *envp)
{
    if (!envp)
        return -1;

    /* Get current exception flags */
    unsigned int fpsr = read_fpsr();
    int excepts = fpsr & FE_ALL_EXCEPT;

    /* Set environment */
    fesetenv(envp);

    /* Raise saved exceptions */
    if (excepts)
    {
        feraiseexcept(excepts);
    }

    return 0;
}

/*
 * feenableexcept - Enable exception traps
 */
int feenableexcept(int excepts)
{
    unsigned int fpcr = read_fpcr();
    int prev = enable_to_except(fpcr & FPCR_EXCEPT_MASK);

    fpcr |= except_to_enable(excepts & FE_ALL_EXCEPT);
    write_fpcr(fpcr);

    return prev;
}

/*
 * fedisableexcept - Disable exception traps
 */
int fedisableexcept(int excepts)
{
    unsigned int fpcr = read_fpcr();
    int prev = enable_to_except(fpcr & FPCR_EXCEPT_MASK);

    fpcr &= ~except_to_enable(excepts & FE_ALL_EXCEPT);
    write_fpcr(fpcr);

    return prev;
}

/*
 * fegetexcept - Get enabled exceptions
 */
int fegetexcept(void)
{
    unsigned int fpcr = read_fpcr();
    return enable_to_except(fpcr & FPCR_EXCEPT_MASK);
}
