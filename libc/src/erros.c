/* nlibc - errno, strerror, perror, assert */
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "interno.h"

int errno;

int __erro_c(long e)
{
    switch (e) {
    case ERRO_INVALIDO:      return EINVAL;
    case ERRO_SEM_PERMISSAO: return EPERM;
    case ERRO_PID:           return ECHILD;
    case ERRO_NAO_ACHADO:    return ENOENT;
    case ERRO_SEM_MEMORIA:   return ENOMEM;
    case ERRO_FORMATO:       return ENOEXEC;
    case ERRO_LOTADO:        return EAGAIN;
    }
    return EIO;
}

char *strsignal(int s)
{
    static char buf[24];
    snprintf(buf, sizeof buf, "sinal %d", s);
    return buf;
}

char *strerror(int e)
{
    switch (e) {
    case 0:       return "Sucesso";
    case EPERM:   return "Operacao nao permitida";
    case ENOENT:  return "Arquivo ou pasta nao encontrado";
    case EIO:     return "Erro de entrada/saida";
    case E2BIG:   return "Argumentos demais";
    case ENOEXEC: return "Nao e um programa executavel";
    case EBADF:   return "Descritor de arquivo invalido";
    case ECHILD:  return "Nenhum processo filho";
    case EAGAIN:  return "Recurso temporariamente indisponivel";
    case ENOMEM:  return "Sem memoria";
    case EACCES:  return "Permissao negada";
    case EEXIST:  return "O arquivo ja existe";
    case ENOTDIR: return "Nao e uma pasta";
    case EISDIR:  return "E uma pasta";
    case EINVAL:  return "Argumento invalido";
    case EMFILE:  return "Arquivos abertos demais";
    case ENOSPC:  return "Sem espaco";
    case ESPIPE:  return "Posicionamento invalido";
    case EPIPE:   return "Pipe sem leitor";
    case EDOM:    return "Fora do dominio";
    case ERANGE:  return "Resultado fora do intervalo";
    case ENOSYS:  return "Ainda nao existe no NovaEra";
    case ENOTEMPTY: return "A pasta nao esta vazia";
    }
    return "Erro desconhecido";
}

void perror(const char *s)
{
    if (s && *s) fprintf(stderr, "%s: ", s);
    fprintf(stderr, "%s\n", strerror(errno));
}

void __assert_falhou(const char *expr, const char *arquivo, int linha)
{
    fprintf(stderr, "%s:%d: assert falhou: %s\n", arquivo, linha, expr);
    abort();
}
