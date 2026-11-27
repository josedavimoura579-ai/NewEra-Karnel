/*
 * usuario/sha256.h - SHA-256 (FIPS 180-4), para guardar as senhas.
 * Uma "impressão digital" de 32 bytes: a mesma entrada dá sempre o mesmo
 * resultado, e não há como voltar do resultado para a entrada.
 */
#ifndef SHA256_H
#define SHA256_H

typedef struct {
    u32 h[8];
    u8  bloco[64];
    u32 n;                       /* bytes no bloco */
    u64 total;
} sha256_t;

static const u32 SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline u32 sha_rot(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }

static inline void sha256_bloco(sha256_t *s)
{
    u32 w[64];
    for (u32 i = 0; i < 16; i++)
        w[i] = (u32)s->bloco[4 * i] << 24 | (u32)s->bloco[4 * i + 1] << 16 |
               (u32)s->bloco[4 * i + 2] << 8 | s->bloco[4 * i + 3];
    for (u32 i = 16; i < 64; i++) {
        u32 s0 = sha_rot(w[i - 15], 7) ^ sha_rot(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = sha_rot(w[i - 2], 17) ^ sha_rot(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    u32 e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (u32 i = 0; i < 64; i++) {
        u32 t1 = h + (sha_rot(e, 6) ^ sha_rot(e, 11) ^ sha_rot(e, 25)) + ((e & f) ^ (~e & g)) + SHA256_K[i] + w[i];
        u32 t2 = (sha_rot(a, 2) ^ sha_rot(a, 13) ^ sha_rot(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static inline void sha256_inicio(sha256_t *s)
{
    static const u32 H0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    for (u32 i = 0; i < 8; i++) s->h[i] = H0[i];
    s->n = 0;
    s->total = 0;
}

static inline void sha256_mais(sha256_t *s, const void *dados, u64 n)
{
    const u8 *p = dados;
    s->total += n;
    while (n--) {
        s->bloco[s->n++] = *p++;
        if (s->n == 64) { sha256_bloco(s); s->n = 0; }
    }
}

static inline void sha256_fim(sha256_t *s, u8 saida[32])
{
    u64 bits = s->total * 8;
    u8 um = 0x80, zero = 0;
    sha256_mais(s, &um, 1);
    while (s->n != 56) sha256_mais(s, &zero, 1);
    for (int i = 7; i >= 0; i--) { u8 b = (u8)(bits >> (8 * i)); sha256_mais(s, &b, 1); }
    for (u32 i = 0; i < 8; i++) {
        saida[4 * i]     = (u8)(s->h[i] >> 24);
        saida[4 * i + 1] = (u8)(s->h[i] >> 16);
        saida[4 * i + 2] = (u8)(s->h[i] >> 8);
        saida[4 * i + 3] = (u8)s->h[i];
    }
}
#endif
