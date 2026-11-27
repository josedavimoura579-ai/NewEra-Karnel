/* nlibc - pthread.h: threads POSIX (pthread_create, mutex, condições).
 * Cada thread é uma tarefa do núcleo no mesmo espaço de memória
 * (SYS_THREAD_CRIAR); as esperas usam o futex (SYS_FUTEX_*).
 * A libc protege o malloc e o stdio; o errno ainda é um só para todas. */
#ifndef _PTHREAD_H
#define _PTHREAD_H
#include <sys/types.h>
#include <time.h>

typedef struct __pthread *pthread_t;
typedef struct { size_t pilha; int separada; } pthread_attr_t;
typedef struct { int estado; } pthread_mutex_t;            /* 0 livre, 1 travado, 2 com espera */
typedef struct { int tipo; } pthread_mutexattr_t;
typedef struct { int seq; } pthread_cond_t;
typedef struct { int nada; } pthread_condattr_t;
typedef int pthread_once_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_COND_INITIALIZER  { 0 }
#define PTHREAD_ONCE_INIT         0
#define PTHREAD_CREATE_JOINABLE   0
#define PTHREAD_CREATE_DETACHED   1
#define PTHREAD_MUTEX_NORMAL      0
#define PTHREAD_MUTEX_DEFAULT     0

int  pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *arg);
int  pthread_join(pthread_t t, void **ret);
int  pthread_detach(pthread_t t);
_Noreturn void pthread_exit(void *ret);
pthread_t pthread_self(void);
int  pthread_equal(pthread_t a, pthread_t b);

int  pthread_attr_init(pthread_attr_t *a);
int  pthread_attr_destroy(pthread_attr_t *a);
int  pthread_attr_setstacksize(pthread_attr_t *a, size_t tam);
int  pthread_attr_setdetachstate(pthread_attr_t *a, int s);

int  pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a);
int  pthread_mutex_destroy(pthread_mutex_t *m);
int  pthread_mutex_lock(pthread_mutex_t *m);
int  pthread_mutex_trylock(pthread_mutex_t *m);
int  pthread_mutex_unlock(pthread_mutex_t *m);
int  pthread_mutexattr_init(pthread_mutexattr_t *a);
int  pthread_mutexattr_destroy(pthread_mutexattr_t *a);

int  pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a);
int  pthread_cond_destroy(pthread_cond_t *c);
int  pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int  pthread_cond_signal(pthread_cond_t *c);
int  pthread_cond_broadcast(pthread_cond_t *c);

int  pthread_once(pthread_once_t *o, void (*f)(void));
#endif
