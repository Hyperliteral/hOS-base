#include <fenv.h>

#ifndef FE_ALL_EXCEPT
#define FE_ALL_EXCEPT (FE_DIVBYZERO | FE_INEXACT | FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW)
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

int feclearexcept(int excepts)
{
    excepts &= FE_ALL_EXCEPT;

    /* Clear x87 status word exceptions */
    struct {
        unsigned short cw, unused1;
        unsigned short sw, unused2;
        unsigned short tag, unused3;
        unsigned int ip, cs, oo, ds;
    } fenv;

    __asm__ __volatile__("fnstenv %0" : "=m"(fenv));
    fenv.sw &= ~excepts;
    __asm__ __volatile__("fldenv %0" : : "m"(fenv));

    /* Clear SSE MXCSR exceptions */
    unsigned int mxcsr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));
    mxcsr &= ~excepts;
    __asm__ __volatile__("ldmxcsr %0" : : "m"(mxcsr));

    return 0;
}

int fetestexcept(int excepts)
{
    excepts &= FE_ALL_EXCEPT;
    unsigned short sw;
    unsigned int mxcsr;

    __asm__ __volatile__("fnstsw %0" : "=am"(sw));
    __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));

    return (sw | mxcsr) & excepts;
}

int fegetexceptflag(fexcept_t *flagp, int excepts)
{
    if (!flagp) return -1;
    *flagp = fetestexcept(excepts);
    return 0;
}

int fesetexceptflag(const fexcept_t *flagp, int excepts)
{
    if (!flagp) return -1;
    excepts &= FE_ALL_EXCEPT;

    /* Set x87 exception flags */
    struct {
        unsigned short cw, unused1;
        unsigned short sw, unused2;
        unsigned short tag, unused3;
        unsigned int ip, cs, oo, ds;
    } fenv;

    __asm__ __volatile__("fnstenv %0" : "=m"(fenv));
    fenv.sw = (fenv.sw & ~excepts) | (*flagp & excepts);
    __asm__ __volatile__("fldenv %0" : : "m"(fenv));

    /* Set SSE MXCSR exception flags */
    unsigned int mxcsr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));
    mxcsr = (mxcsr & ~excepts) | (*flagp & excepts);
    __asm__ __volatile__("ldmxcsr %0" : : "m"(mxcsr));

    return 0;
}

int feraiseexcept(int excepts)
{
    excepts &= FE_ALL_EXCEPT;
    fexcept_t flags = excepts;
    return fesetexceptflag(&flags, excepts);
}

int fegetround(void)
{
    unsigned int mxcsr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));
    /* MXCSR bits 13..14 correspond to x87 CW bits 10..11 */
    return (mxcsr >> 3) & 0x0c00;
}

int fesetround(int round)
{
    if (round & ~0x0c00) return -1;

    /* Set x87 Control Word rounding mode */
    unsigned short cw;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    cw = (cw & ~0x0c00) | round;
    __asm__ __volatile__("fldcw %0" : : "m"(cw));

    /* Set SSE MXCSR rounding mode */
    unsigned int mxcsr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));
    mxcsr = (mxcsr & ~0x6000) | (round << 3);
    __asm__ __volatile__("ldmxcsr %0" : : "m"(mxcsr));

    return 0;
}

int fegetenv(fenv_t *envp)
{
    if (!envp) return -1;
    __asm__ __volatile__("fnstenv %0" : "=m"(*envp));
    return 0;
}

int feholdexcept(fenv_t *envp)
{
    fegetenv(envp);
    feclearexcept(FE_ALL_EXCEPT);
    return 0;
}

int fesetenv(const fenv_t *envp)
{
    if (!envp) return -1;
    __asm__ __volatile__("fldenv %0" : : "m"(*envp));
    return 0;
}

int feupdateenv(const fenv_t *envp)
{
    int excepts = fetestexcept(FE_ALL_EXCEPT);
    fesetenv(envp);
    feraiseexcept(excepts);
    return 0;
}

#else /* Generic software stub fallback */

static int __soft_excepts = 0;
static int __soft_round = 0;

int feclearexcept(int excepts) { __soft_excepts &= ~excepts; return 0; }
int fetestexcept(int excepts) { return __soft_excepts & excepts; }
int fegetexceptflag(fexcept_t *flagp, int excepts) { if (flagp) *flagp = fetestexcept(excepts); return 0; }
int fesetexceptflag(const fexcept_t *flagp, int excepts) { if (flagp) __soft_excepts = (__soft_excepts & ~excepts) | (*flagp & excepts); return 0; }
int feraiseexcept(int excepts) { __soft_excepts |= excepts; return 0; }
int fegetround(void) { return __soft_round; }
int fesetround(int round) { __soft_round = round; return 0; }
int fegetenv(fenv_t *envp) { (void)envp; return 0; }
int feholdexcept(fenv_t *envp) { (void)envp; feclearexcept(FE_ALL_EXCEPT); return 0; }
int fesetenv(const fenv_t *envp) { (void)envp; return 0; }
int feupdateenv(const fenv_t *envp) { (void)envp; return 0; }

#endif