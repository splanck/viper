#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Declaration for assert handler */
void __assert_fail(const char *expr, const char *file, int line, const char *func);

#ifdef NDEBUG
/* Release build - assert does nothing */
#define assert(expr) ((void)0)
#else
/* Debug build - assert checks expression */
#define assert(expr) ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

/* Static assert (C11) */
#ifndef __cplusplus
#define static_assert _Static_assert
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ASSERT_H */
