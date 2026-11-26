/* nlibc - spawn.h
 * posix_spawn: cria um processo novo executando um programa (é o que o
 * cproc usa). As ações de arquivo suportadas são dup2 para a entrada (0) e
 * a saída (1) padrão, e close. */
#ifndef _SPAWN_H
#define _SPAWN_H
#include <sys/types.h>
#define POSIX_SPAWN_MAX_ACOES 8
typedef struct { int tipo, fd, novo; } __acao_spawn;
typedef struct { int n; __acao_spawn acoes[POSIX_SPAWN_MAX_ACOES]; } posix_spawn_file_actions_t;
typedef struct { int nada; } posix_spawnattr_t;
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *a);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *a);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *a, int fd, int novo);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *a, int fd);
int posix_spawn(pid_t *pid, const char *caminho, const posix_spawn_file_actions_t *acoes,
                const posix_spawnattr_t *atrib, char *const argv[], char *const envp[]);
int posix_spawnp(pid_t *pid, const char *arquivo, const posix_spawn_file_actions_t *acoes,
                 const posix_spawnattr_t *atrib, char *const argv[], char *const envp[]);
#endif
