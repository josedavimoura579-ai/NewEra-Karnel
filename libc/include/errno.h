/* nlibc - errno.h: os mesmos números do Linux, para facilitar portar programas */
#ifndef _ERRNO_H
#define _ERRNO_H
extern int errno;
#define EPERM   1
#define ENOENT  2
#define ESRCH   3
#define EINTR   4
#define EIO     5
#define E2BIG   7
#define ENOEXEC 8
#define EBADF   9
#define ECHILD  10
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define EEXIST  17
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define EMFILE  24
#define ENOSPC  28
#define ESPIPE  29
#define EPIPE   32
#define EDOM    33
#define ERANGE  34
#define ENOSYS  38
#define ENOTEMPTY 39
#endif
