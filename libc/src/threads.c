/*
 * nlibc - libc/src/threads.c
 * pthread_create, pthread_join, os mutex e as variáveis de condição.
 *
 * Uma thread nova ganha uma pilha do malloc e começa em __comeca, que chama
 * a função pedida e depois pthread_exit. Ao sair, o núcleo zera t->viva e
 * acorda quem dorme nesse endereço (o futex): é assim que o join acorda.
 *
 * O mutex (o do Ulrich Drepper, "Futexes Are Tricky"): 0 = livre,
 * 1 = travado, 2 = travado e alguém dormindo. Travar e destravar sem
 * disputa nem entram no núcleo: só uma instrução atômica.
 */
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <errno.h>
#include "interno.h"

#define PILHA_PADRAO (64 * 1024)

struct __pthread {
    int    viva;                   /* 1 até ela terminar (o núcleo zera) */
    u32    id;                     /* o id da tarefa no núcleo */
    void *(*f)(void *);
    void  *arg;
    void  *ret;
    void  *pilha;
    int    separada;
};

static struct __pthread *tabela[PROCESSOS_MAX];   /* id da tarefa -> thread */
static struct __pthread principal;
int __threads_ativas;                              /* 1 depois da 1ª pthread_create */
static int trava_tabela;

static int le(int *p) { return atomico_soma(p, 0); }   /* lê sem o compilador guardar */

/* ------------------------------- O mutex --------------------------------- */

void __trava(int *m)
{
    int c = atomico_cmpxchg(m, 0, 1);
    if (c == 0) return;                            /* estava livre: é meu */
    if (c != 2) c = atomico_troca(m, 2);
    while (c != 0) {
        sys_futex_esperar(m, 2);                   /* dorme enquanto valer 2 */
        c = atomico_troca(m, 2);
    }
}

void __destrava(int *m)
{
    if (atomico_soma(m, -1) != 1) {                /* havia alguém esperando */
        atomico_troca(m, 0);
        sys_futex_acordar(m, 1);
    }
}

/* O stdio usa uma trava que a mesma thread pode pegar de novo (o printf
 * chama o fputc). Só depois que o programa cria uma thread. */
static int stdio_trava;
static u32 stdio_dono, stdio_vezes;

void __stdio_travar(void)
{
    if (!__threads_ativas) return;
    u32 eu = (u32)sys_pid();
    if (stdio_vezes && stdio_dono == eu) { stdio_vezes++; return; }
    __trava(&stdio_trava);
    stdio_dono = eu;
    stdio_vezes = 1;
}

void __stdio_destravar(void)
{
    if (!__threads_ativas || !stdio_vezes) return;
    if (--stdio_vezes == 0) { stdio_dono = 0; __destrava(&stdio_trava); }
}

/* ------------------------------- Threads --------------------------------- */

/* As pilhas das threads separadas (detach) que já acabaram. */
static void recolher(void)
{
    __trava(&trava_tabela);
    for (u32 i = 0; i < PROCESSOS_MAX; i++) {
        struct __pthread *t = tabela[i];
        if (t && t != &principal && t->separada && !le(&t->viva)) {
            tabela[i] = 0;
            free(t->pilha);
            free(t);
        }
    }
    __destrava(&trava_tabela);
}

static void __comeca(struct __pthread *t)
{
    tabela[t->id = (u32)sys_pid()] = t;
    pthread_exit(t->f(t->arg));
}

int pthread_create(pthread_t *saida, const pthread_attr_t *a, void *(*f)(void *), void *arg)
{
    if (!__threads_ativas) {                       /* a principal entra na tabela */
        principal.viva = 1;
        principal.id = (u32)sys_pid();
        tabela[principal.id] = &principal;
        __threads_ativas = 1;
    }
    recolher();
    size_t tam = a && a->pilha >= 16384 ? a->pilha : PILHA_PADRAO;
    struct __pthread *t = calloc(1, sizeof *t);
    char *pilha = t ? malloc(tam) : NULL;
    if (!pilha) { free(t); return EAGAIN; }
    t->viva = 1;
    t->f = f;
    t->arg = arg;
    t->pilha = pilha;
    t->separada = a && a->separada;
    /* o topo: alinhado em 16, com um "endereço de volta" 0 (como depois de um call) */
    u64 topo = ((u64)(pilha + tam) & ~15ull) - 8;
    *(u64 *)topo = 0;
    thread_t c;
    c.entrada = (u64)__comeca;
    c.pilha = topo;
    c.arg = (u64)t;
    c.tid_endereco = (u64)&t->viva;
    long id = sys_thread_criar(&c);
    if (id < 0) { free(pilha); free(t); return EAGAIN; }
    t->id = (u32)id;
    tabela[id] = t;
    *saida = t;
    return 0;
}

pthread_t pthread_self(void)
{
    u32 id = (u32)sys_pid();
    if (id < PROCESSOS_MAX && tabela[id]) return tabela[id];
    principal.viva = 1;
    principal.id = id;
    return &principal;                             /* (antes de qualquer thread) */
}

int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

_Noreturn void pthread_exit(void *ret)
{
    pthread_t eu = pthread_self();
    eu->ret = ret;
    if (eu == &principal) __stdio_esvaziar_todos();
    sys_thread_sair(0);
    for (;;) ;
}

int pthread_join(pthread_t t, void **ret)
{
    if (!t || t == pthread_self()) return EINVAL;
    if (t->separada) return EINVAL;
    while (le(&t->viva)) sys_futex_esperar(&t->viva, 1);
    if (ret) *ret = t->ret;
    if (t != &principal) {
        __trava(&trava_tabela);
        if (t->id < PROCESSOS_MAX && tabela[t->id] == t) tabela[t->id] = 0;
        __destrava(&trava_tabela);
        free(t->pilha);
        free(t);
    }
    return 0;
}

int pthread_detach(pthread_t t)
{
    if (!t) return EINVAL;
    t->separada = 1;
    return 0;
}

int pthread_attr_init(pthread_attr_t *a)              { a->pilha = PILHA_PADRAO; a->separada = 0; return 0; }
int pthread_attr_destroy(pthread_attr_t *a)           { (void)a; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *a, size_t tam) { a->pilha = tam; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int s)    { a->separada = s == PTHREAD_CREATE_DETACHED; return 0; }

int sched_yield(void) { sys_ceder(); return 0; }

/* --------------------------------- Mutex --------------------------------- */

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)a; m->estado = 0; return 0; }
int pthread_mutex_destroy(pthread_mutex_t *m)  { return le(&m->estado) ? EBUSY : 0; }
int pthread_mutex_lock(pthread_mutex_t *m)     { __trava(&m->estado); return 0; }
int pthread_mutex_unlock(pthread_mutex_t *m)   { __destrava(&m->estado); return 0; }
int pthread_mutex_trylock(pthread_mutex_t *m)  { return atomico_cmpxchg(&m->estado, 0, 1) == 0 ? 0 : EBUSY; }
int pthread_mutexattr_init(pthread_mutexattr_t *a)    { a->tipo = 0; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }

/* ------------------------- Variáveis de condição -------------------------
 * Um contador: quem espera anota o número e dorme enquanto ele não mudar;
 * signal e broadcast somam 1 e acordam. */

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)a; c->seq = 0; return 0; }
int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    int seq = le(&c->seq);
    __destrava(&m->estado);
    sys_futex_esperar(&c->seq, seq);
    /* volta com o mutex, marcado "com espera" (pode haver outros acordados) */
    while (atomico_troca(&m->estado, 2) != 0) sys_futex_esperar(&m->estado, 2);
    return 0;
}

int pthread_cond_signal(pthread_cond_t *c)    { atomico_soma(&c->seq, 1); sys_futex_acordar(&c->seq, 1); return 0; }
int pthread_cond_broadcast(pthread_cond_t *c) { atomico_soma(&c->seq, 1); sys_futex_acordar(&c->seq, 0x7FFFFFFF); return 0; }

int pthread_once(pthread_once_t *o, void (*f)(void))
{
    static int trava;
    if (le(o) == 2) return 0;
    __trava(&trava);
    if (*o == 0) { *o = 1; f(); atomico_troca(o, 2); }
    __destrava(&trava);
    return 0;
}
