/* Small freestanding string API for the browser build. SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
