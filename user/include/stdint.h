/*
 * Exact-width integer types for DrakeOS. Shadows the compiler's <stdint.h> so that both
 * the i686-elf cross compiler (where uint32_t is `long unsigned int`) and host gcc -m32
 * (`unsigned int`) agree, which keeps printf format checking identical on both.
 */
#ifndef DRAKE_STDINT_H
#define DRAKE_STDINT_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;

#define INT32_MAX  0x7fffffff
#define UINT32_MAX 0xffffffffu

#endif
