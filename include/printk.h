#ifndef __PRINTK_H__
#define __PRINTK_H__

#include <types.h>

#ifdef DEBUG

void print(const char *unused);
void print_p(const void *unused);
void print_u64(u64 p);
void hexdump(const void *unused, size_t unused2);

#else

static inline void print(const char *unused) { }
static inline void print_p(const void *unused) { }
static inline void print_u64(u64 p) { }
static inline void hexdump(const void *unused, size_t unused2) { }

#endif

#endif
