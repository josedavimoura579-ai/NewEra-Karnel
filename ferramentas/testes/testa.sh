#!/bin/sh
# ===========================================================================
#  NEFS - ferramentas/testes/testa.sh
#  Confere se o nefs-as gera os MESMOS bytes, relocações e símbolos que o
#  GNU as. Precisa do binutils (as) e do python3.
#
#    sh ferramentas/testes/testa.sh              os .s do NovaEra e o teste
#    sh ferramentas/testes/testa.sh a.s b.s ...  outros arquivos
#
#  Para testar com todo o C do NovaEra, compile antes com o compilador do
#  NEFS (CC=cproc ./build.sh): o assembly fica em build/, e o teste o usa.
# ===========================================================================
set -eu
cd "$(dirname "$0")/../.."
SAIDA="build/testes-nefs-as"
mkdir -p "$SAIDA"
${HOSTCC:-cc} -O2 -o "$SAIDA/nefs-as" ferramentas/nefs-as.c

if [ $# -eq 0 ]; then
    set -- ferramentas/testes/instrucoes.s boot.s cpu.s usuario/crt0.s usuario/sys.s libc/crt0.s
    for f in $(find build -name '*.o.s' 2>/dev/null); do set -- "$@" "$f"; done
fi

iguais=0; diferentes=0
for f in "$@"; do
    nome="$(echo "$f" | tr / _)"
    as --64 -o "$SAIDA/$nome.gnu.o" "$f"
    "$SAIDA/nefs-as" -o "$SAIDA/$nome.nefs.o" "$f"
    if python3 ferramentas/testes/compara_o.py "$SAIDA/$nome.gnu.o" "$SAIDA/$nome.nefs.o" > "$SAIDA/$nome.dif"; then
        iguais=$((iguais + 1))
    else
        diferentes=$((diferentes + 1))
        echo "DIFERENTE: $f"
        head -5 "$SAIDA/$nome.dif"
    fi
done
echo "nefs-as x GNU as: $iguais iguais, $diferentes diferentes"
[ "$diferentes" -eq 0 ]
