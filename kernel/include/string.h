/* Freestanding string and memory functions. gcc may emit calls to mem* on its own. */
#ifndef DRAKE_STRING_H
#define DRAKE_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strncpy(char *dst, const char *src, size_t n);
size_t strlcpy(char *dst, const char *src, size_t size);
char *strchr(const char *s, int c);
int atoi(const char *s);
unsigned long strtoul(const char *s, char **end, int base);

#endif
