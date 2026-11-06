/* nlibc - stdarg.h: quem sabe percorrer os argumentos é o compilador. */
#ifndef _STDARG_H
#define _STDARG_H
typedef __builtin_va_list va_list;
#define va_start(ap, ultimo) __builtin_va_start(ap, ultimo)
#define va_arg(ap, tipo)     __builtin_va_arg(ap, tipo)
#define va_end(ap)           __builtin_va_end(ap)
#define va_copy(dst, src)    __builtin_va_copy(dst, src)
#endif
