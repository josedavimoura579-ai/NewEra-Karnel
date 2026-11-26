/* nlibc - stddef.h */
#ifndef _STDDEF_H
#define _STDDEF_H
typedef unsigned long size_t;
typedef long          ptrdiff_t;
typedef int           wchar_t;
typedef struct { long long a; double b; } max_align_t;
#define NULL ((void *)0)
#define offsetof(tipo, campo) ((size_t)&((tipo *)0)->campo)
#endif
