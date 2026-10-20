/* Freestanding helpers: no libc, WASI imports, allocation or host services.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "string.h"
void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}
void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    if (d < s) return memcpy(dest, src, n);
    while (n) { n--; d[n] = s[n]; }
    return dest;
}
void *memset(void *dest, int value, size_t n) {
    unsigned char *d = dest;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)value;
    return dest;
}
size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
char *strcpy(char *dest, const char *src) {
    size_t i = 0;
    do { dest[i] = src[i]; } while (src[i++]);
    return dest;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int difference = (unsigned char)a[i] - (unsigned char)b[i];
        if (difference || !a[i]) return difference;
    }
    return 0;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
