/* nlibc - string.h */
#include <string.h>
#include <stdlib.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x - *y;
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (; n; n--, p++)
        if (*p == (unsigned char)c) return (void *)p;
    return NULL;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}

int strcoll(const char *a, const char *b) { return strcmp(a, b); }

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++)) ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src)
{
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst + strlen(dst);
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *ultimo = NULL;
    for (;; s++) {
        if (*s == (char)c) ultimo = s;
        if (!*s) return (char *)ultimo;
    }
}

char *strstr(const char *s, const char *procura)
{
    size_t n = strlen(procura);
    if (!n) return (char *)s;
    for (; *s; s++)
        if (*s == *procura && !strncmp(s, procura, n)) return (char *)s;
    return NULL;
}

size_t strspn(const char *s, const char *aceita)
{
    size_t n = 0;
    while (s[n] && strchr(aceita, s[n])) n++;
    return n;
}

size_t strcspn(const char *s, const char *rejeita)
{
    size_t n = 0;
    while (s[n] && !strchr(rejeita, s[n])) n++;
    return n;
}

char *strpbrk(const char *s, const char *aceita)
{
    s += strcspn(s, aceita);
    return *s ? (char *)s : NULL;
}

char *strtok(char *s, const char *sep)
{
    static char *resto;
    if (!s) s = resto;
    if (!s) return NULL;
    s += strspn(s, sep);
    if (!*s) { resto = NULL; return NULL; }
    char *fim = s + strcspn(s, sep);
    if (*fim) *fim++ = '\0'; else fim = NULL;
    resto = fim;
    return s;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

char *strndup(const char *s, size_t n)
{
    size_t tam = strnlen(s, n);
    char *d = malloc(tam + 1);
    if (d) { memcpy(d, s, tam); d[tam] = '\0'; }
    return d;
}
