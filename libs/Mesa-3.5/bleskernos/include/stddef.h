#ifndef BK_MESA_STDDEF_H
#define BK_MESA_STDDEF_H
typedef unsigned int size_t;
typedef int ptrdiff_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
