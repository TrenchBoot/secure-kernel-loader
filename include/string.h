#ifndef __STRINGS_H__
#define __STRINGS_H__

#if __STDC_HOSTED__

#include <string.h>	/* memcpy, memset, memcmp */

/* Used in tests only */
#define memcmp(l, r, n) __builtin_memcmp(l, r, n)

#else

/* Local declaration of bits of libc */

void *memset(void *s, int c, size_t n);

void *memcpy(void *dst, const void *src, size_t n);

size_t strlen(const char *s);

#endif /* __STDC_HOSTED__ */

/*
 * Use __builtin_???() wherever possible to allow the compiler to perform
 * optimisations (e.g. constant folding) where possible.  Calls to ???() will
 * be emitted as needed.
 */

#define memset(d, c, n) __builtin_memset(d, c, n)
#define memcpy(d, s, n) __builtin_memcpy(d, s, n)
#define strlen(s)       __builtin_strlen(s)

#endif /* __STRINGS_H__ */
