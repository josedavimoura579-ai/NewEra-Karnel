#!/usr/bin/env python3
"""ferramentas/fonte/gerar.py - transforma uma fonte de console PSF (do
pacote console-setup) em usuario/fonte8x16.h, a fonte do terminal gráfico.

  python3 ferramentas/fonte/gerar.py /usr/share/consolefonts/Lat15-Fixed16.psf.gz

A Lat15-Fixed16 vem das fontes "Fixed" do X11, que são de domínio público.
O arquivo gerado tem os desenhos (16 bytes por letra, um byte por linha,
o bit mais alto é o ponto da esquerda) e a tabela Unicode -> desenho.
"""
import gzip, sys, os

arq = sys.argv[1]
dados = gzip.open(arq).read() if arq.endswith('.gz') else open(arq, 'rb').read()
if dados[:2] == b'\x36\x04':                       # PSF1
    modo, altura = dados[2], dados[3]
    n = 512 if modo & 1 else 256
    tem_tabela = bool(modo & 2)
    pos = 4
    glifos = [dados[pos + i * altura: pos + (i + 1) * altura] for i in range(n)]
    pos += n * altura
    mapa = {}
    if tem_tabela:
        for g in range(n):
            while True:
                u = dados[pos] | dados[pos + 1] << 8
                pos += 2
                if u == 0xFFFF: break
                if u == 0xFFFE: continue
                mapa.setdefault(u, g)
else:
    sys.exit('só PSF1')
assert altura == 16, altura
pares = sorted((u, g) for u, g in mapa.items() if u < 0x10000)
saida = os.path.join(os.path.dirname(__file__), '..', '..', 'usuario', 'fonte8x16.h')
with open(saida, 'w') as f:
    f.write('/* usuario/fonte8x16.h - GERADO por ferramentas/fonte/gerar.py a partir de\n')
    f.write(' * %s (as fontes "Fixed" do X11: domínio público).\n' % os.path.basename(arq))
    f.write(' * Não edite: rode o gerador de novo. */\n')
    f.write('#ifndef FONTE8X16_H\n#define FONTE8X16_H\n\n')
    f.write('#define FONTE_LARGURA 8u\n#define FONTE_ALTURA 16u\n#define FONTE_GLIFOS %du\n\n' % n)
    f.write('static const unsigned char fonte_glifos[FONTE_GLIFOS][16] = {\n')
    for g in glifos:
        f.write('    {' + ','.join('0x%02x' % b for b in g) + '},\n')
    f.write('};\n\n/* Unicode -> número do desenho, em ordem (para a busca binária) */\n')
    f.write('#define FONTE_MAPA %du\n' % len(pares))
    f.write('static const unsigned short fonte_mapa[FONTE_MAPA][2] = {\n')
    for i in range(0, len(pares), 6):
        f.write('    ' + ' '.join('{0x%04x,%d},' % p for p in pares[i:i + 6]) + '\n')
    f.write('};\n\n#endif\n')
print('fonte8x16.h: %d desenhos, %d códigos Unicode' % (n, len(pares)))
