/*
 * exemplos/compartilhada.c - dois programas conversando pela MESMA memória.
 *
 *   cproc -o /tmp/compartilhada /usr/share/nefs/exemplos/compartilhada.c
 *   /tmp/compartilhada
 *
 * O programa cria um pedaço de memória compartilhada ("/quadro") e roda
 * uma cópia de si mesmo, o "escritor". O escritor escreve recados ali; o
 * original, o "leitor", espera cada um (uma variável de condição, também
 * dentro da memória compartilhada) e mostra. Nenhuma mensagem é enviada:
 * os dois enxergam as mesmas páginas da memória.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>

typedef struct {
    pthread_mutex_t trava;
    pthread_cond_t  novo;           /* "tem recado novo" */
    int             numero;         /* quantos recados já foram escritos */
    char            recado[64];
} quadro_t;

static quadro_t *abrir(int criar)
{
    int fd = shm_open("/quadro", criar ? O_CREAT | O_RDWR : O_RDWR, 0600);
    if (fd < 0) return NULL;
    if (criar) ftruncate(fd, sizeof(quadro_t));
    quadro_t *q = mmap(NULL, sizeof(quadro_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return q == MAP_FAILED ? NULL : q;
}

static int escritor(void)
{
    quadro_t *q = abrir(0);
    if (!q) return 1;
    const char *recados[] = { "ola!", "estou noutro programa", "mas escrevo na mesma memoria",
                              "sem mandar mensagens", "tchau" };
    for (int i = 0; i < 5; i++) {
        usleep(400000);
        pthread_mutex_lock(&q->trava);
        strcpy(q->recado, recados[i]);
        q->numero++;
        pthread_cond_signal(&q->novo);   /* acorda o leitor (no outro programa) */
        pthread_mutex_unlock(&q->trava);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "escritor")) return escritor();

    shm_unlink("/quadro");
    quadro_t *q = abrir(1);
    if (!q) { printf("Nao consegui criar a memoria compartilhada.\n"); return 1; }
    pthread_mutex_init(&q->trava, NULL);
    pthread_cond_init(&q->novo, NULL);

    char *args[] = { argv[0], "escritor", NULL };
    pid_t pid;
    if (posix_spawn(&pid, argv[0], NULL, NULL, args, NULL) != 0) { printf("Nao consegui rodar o escritor.\n"); return 1; }
    printf("Leitor (processo %d) esperando o escritor (processo %d)...\n", getpid(), pid);

    pthread_mutex_lock(&q->trava);
    for (int lidos = 0; lidos < 5; lidos++) {
        while (q->numero == lidos) pthread_cond_wait(&q->novo, &q->trava);
        printf("  recado %d: %s\n", q->numero, q->recado);
    }
    pthread_mutex_unlock(&q->trava);
    waitpid(pid, NULL, 0);
    shm_unlink("/quadro");
    printf("Pronto.\n");
    return 0;
}
