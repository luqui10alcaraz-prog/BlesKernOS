#ifndef TYPES_H
#define TYPES_H

/* Tipos de tamaño fijo para un kernel 32-bit freestanding */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint32_t size_t;
typedef int32_t intptr_t;
typedef uint32_t uintptr_t;

#define NULL ((void*)0)
#define true 1
#define false 0
typedef uint8_t bool;

/* Atributos GCC útiles para kernel */
#define PACKED   __attribute__((packed))
#define NORETURN __attribute__((noreturn))
#define UNUSED   __attribute__((unused))

#endif
