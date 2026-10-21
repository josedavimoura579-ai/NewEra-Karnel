#!/bin/sh
# ===========================================================================
#  NovaEra - portes/baixar.sh
#  Baixa as fontes dos programas de fora que rodam no NovaEra ("portes"),
#  confere o SHA-256 e guarda em portes/fontes/. Depois, o build.sh
#  compila cada porte com a libc do NovaEra e o põe no initrd.
#
#    portes/baixar.sh          baixa todos
#    portes/baixar.sh qbe      baixa só o QBE
#
#  Sem internet, copie o arquivo para portes/fontes/ à mão.
# ===========================================================================
set -eu
cd "$(dirname "$0")"
mkdir -p fontes

for receita in */receita; do
    porte="$(dirname "$receita")"
    [ $# -eq 0 ] || case " $* " in *" $porte "*) ;; *) continue ;; esac
    (
        . "./$receita"
        arquivo="fontes/$(basename "$URL")"
        if [ ! -f "$arquivo" ]; then
            echo "Baixando $NOME $VERSAO: $URL"
            curl -fsSL -o "$arquivo.parcial" "$URL"
            mv "$arquivo.parcial" "$arquivo"
        fi
        if [ "$(sha256sum "$arquivo" | cut -d' ' -f1)" != "$SHA256" ]; then
            echo "ERRO: $arquivo não confere com o SHA-256 da receita"; rm -f "$arquivo"; exit 1
        fi
        echo "  $NOME $VERSAO: ok ($arquivo)"
    )
done
