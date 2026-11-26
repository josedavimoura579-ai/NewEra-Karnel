#!/bin/sh
# ===========================================================================
#  NEFS - ferramentas/testes/testa-cpp.sh
#  Confere o nefs-cpp contra o cpp do GCC, de dois jeitos:
#   1. macros.c (os exemplos do padrão C): a saída, token a token
#   2. com o cproc-qbe e o qbe no PATH: cada arquivo C do NovaEra (núcleo,
#      servidores, comandos, libc, ferramentas e os portes já extraídos em
#      build/portes) vira assembly pelos dois caminhos; tem que dar igual
#  Precisa do cpp (GCC) e do python3.
# ===========================================================================
set -eu
cd "$(dirname "$0")/../.."
SAIDA="build/testes-nefs-cpp"
mkdir -p "$SAIDA"
${HOSTCC:-cc} -O2 -o "$SAIDA/nefs-cpp" ferramentas/nefs-cpp.c

"$SAIDA/nefs-cpp" -undef -P ferramentas/testes/macros.c > "$SAIDA/macros.nefs"
cpp -undef -P ferramentas/testes/macros.c > "$SAIDA/macros.gcc" 2>/dev/null
echo "macros.c: $(python3 ferramentas/testes/compara_tokens.py "$SAIDA/macros.gcc" "$SAIDA/macros.nefs")"

command -v cproc-qbe >/dev/null && command -v qbe >/dev/null || {
    echo "(sem cproc-qbe e qbe no PATH: o teste 2 fica de fora)"; exit 0; }

# Os mesmos argumentos que o cproc passa ao pré-processador no NovaEra.
ARGS=$(sed -n '/preprocesscmd/,/};/p' portes/cproc/config.h | grep -o '"[^"]*"' | tr -d '"' | tail -n +2 | tr '\n' ' ')
iguais=0; diferentes=0
for f in kernel.c usuario/*.c usuario/bin/*.c libc/src/*.c ferramentas/*.c \
         $(find build/portes -name '*.c' 2>/dev/null); do
    case "$f" in
        kernel.c)      FL="-I. -Ibuild" ;;
        build/portes/*) FL="-nostdinc -Ilibc/include -I$(dirname "$f")" ;;
        *)             FL="-nostdinc -Ilibc/include -I. -Ibuild -Iusuario" ;;
    esac
    n="$(echo "$f" | tr / _)"
    # shellcheck disable=SC2086
    cpp $ARGS $FL "$f" 2>/dev/null | cproc-qbe -t x86_64-sysv 2>/dev/null | qbe -t amd64_sysv > "$SAIDA/$n.gcc.s" 2>/dev/null || true
    # shellcheck disable=SC2086
    "$SAIDA/nefs-cpp" $ARGS $FL "$f" | cproc-qbe -t x86_64-sysv 2>/dev/null | qbe -t amd64_sysv > "$SAIDA/$n.nefs.s" 2>/dev/null || true
    [ -s "$SAIDA/$n.gcc.s" ] || continue        # o cproc não compila este (ex.: ferramenta de teste)
    if cmp -s "$SAIDA/$n.gcc.s" "$SAIDA/$n.nefs.s"; then iguais=$((iguais + 1))
    else diferentes=$((diferentes + 1)); echo "DIFERENTE: $f"
    fi
done
echo "assembly com nefs-cpp x com o cpp do GCC: $iguais iguais, $diferentes diferentes"
[ "$diferentes" -eq 0 ]
