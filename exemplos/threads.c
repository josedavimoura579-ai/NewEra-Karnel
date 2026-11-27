/*
 * exemplos/threads.c - três threads trabalhando ao mesmo tempo.
 *
 *   cproc -o /tmp/threads /usr/share/nefs/exemplos/threads.c
 *   /tmp/threads
 *
 * Cada thread procura números primos numa faixa diferente. As três
 * mexem no mesmo contador: o mutex garante que uma não atrapalhe a outra
 * (tire o lock e o unlock e veja o total errar).
 */
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static int total;                         /* compartilhado pelas três */

typedef struct { int de, ate, achados; } faixa_t;

static int primo(int n)
{
    if (n < 2) return 0;
    for (int d = 2; d * d <= n; d++) if (n % d == 0) return 0;
    return 1;
}

static void *procurar(void *arg)
{
    faixa_t *f = arg;
    for (int n = f->de; n < f->ate; n++) {
        if (!primo(n)) continue;
        f->achados++;
        pthread_mutex_lock(&trava);       /* uma de cada vez aqui dentro */
        total++;
        pthread_mutex_unlock(&trava);
    }
    printf("  a thread de %d a %d achou %d primos\n", f->de, f->ate, f->achados);
    return NULL;
}

int main(void)
{
    faixa_t faixas[3] = { { 0, 100000, 0 }, { 100000, 200000, 0 }, { 200000, 300000, 0 } };
    pthread_t t[3];
    printf("Três threads procurando primos ao mesmo tempo...\n");
    for (int i = 0; i < 3; i++) pthread_create(&t[i], NULL, procurar, &faixas[i]);
    for (int i = 0; i < 3; i++) pthread_join(t[i], NULL);      /* espera as três */
    printf("Total: %d primos abaixo de 300000 (o certo é 25997).\n", total);
    return 0;
}
