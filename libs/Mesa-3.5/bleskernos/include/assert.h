#ifndef BK_MESA_ASSERT_H
#define BK_MESA_ASSERT_H
void abort(void) __attribute__((noreturn));
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : abort())
#endif
#endif
