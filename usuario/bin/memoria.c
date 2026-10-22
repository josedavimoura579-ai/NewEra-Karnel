/*
 * usuario/bin/memoria.c - testa o heap (SYS_SBRK) e a pilha que cresce
 * sob demanda. Os programas antigos do NovaEra tinham só 64 KiB no total.
 *   memoria [MiB]   (padrão: 8)
 */
#include "ulib.h"

/* Cada chamada usa ~4 KiB de pilha: 200 chamadas passam de 800 KiB. */
static u32 desce(u32 n)
{
    u8 bloco[4000];
    for (u32 i = 0; i < sizeof(bloco); i++) bloco[i] = (u8)(n + i);
    if (n == 0) return bloco[123];
    return desce(n - 1) + bloco[n % sizeof(bloco)];
}

int main(int argc, char **argv)
{
    u32 mib = 8;
    if (argc > 1) {
        mib = 0;
        for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++) mib = mib * 10 + (u32)(*p - '0');
    }

    long ini = sys_sbrk((u64)mib << 20);
    if (ini < 0) {
        escreve("sbrk falhou: sem memoria\n", COR(COR_VERMELHO_CLARO));
        return 1;
    }
    u8 *heap = (u8 *)ini;
    u64 tam = (u64)mib << 20;
    for (u64 i = 0; i < tam; i += 4096) heap[i] = (u8)(i >> 12);   /* toca cada página */
    u32 soma = 0;
    for (u64 i = 0; i < tam; i += 4096) soma += heap[i];

    escreve("Heap: ", COR(COR_VERDE_CLARO));
    escreve_num(mib, COR(COR_BRANCO));
    escreve(" MiB alocados e conferidos (soma ", COR(COR_VERDE_CLARO));
    escreve_num(soma, COR(COR_BRANCO));
    escreve(")\n", COR(COR_VERDE_CLARO));

    u32 r = desce(200);
    escreve("Pilha: 200 chamadas de 4 KiB (resultado ", COR(COR_VERDE_CLARO));
    escreve_num(r, COR(COR_BRANCO));
    escreve(")\n", COR(COR_VERDE_CLARO));
    return 0;
}
