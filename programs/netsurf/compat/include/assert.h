#ifndef NSBK_ASSERT_H
#define NSBK_ASSERT_H
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
void nsbk_assert_fail(const char *expression, const char *file, unsigned int line);
#define assert(expression) ((expression) ? (void)0 : nsbk_assert_fail(#expression, __FILE__, __LINE__))
#endif
#endif
