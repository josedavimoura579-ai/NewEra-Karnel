#!/bin/sh
# ===========================================================================
#  NovaEra - portes/baixar.sh
#  Baixa as fontes dos programas de fora que rodam no NovaEra ("portes"),
#  confere o SHA-256 (ou pega o commit fixo, se vierem do git) e guarda
#  em portes/fontes/. Depois, o build.sh
#  compila cada porte com a libc do NovaEra e o põe no initrd.
#
#    portes/baixar.sh          baixa todos
#    portes/baixar.sh qbe      baixa só o QBE
#    portes/baixar.sh cproc    baixa só o cproc (precisa do git)
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
        if [ -n "${GIT:-}" ]; then
            # do git: o commit fixo da receita garante o conteúdo
            arquivo="fontes/$NOME-$COMMIT.tar.gz"
            if [ ! -f "$arquivo" ]; then
                echo "Baixando $NOME $VERSAO: $GIT"
                [ -d "fontes/$NOME.git" ] || git clone -q "$GIT" "fontes/$NOME.git"
                git -C "fontes/$NOME.git" cat-file -e "$COMMIT^{commit}" 2>/dev/null ||
                    git -C "fontes/$NOME.git" fetch -q origin
                git -C "fontes/$NOME.git" archive --format=tar --prefix="$PASTA/" "$COMMIT" |
                    gzip -n > "$arquivo.parcial"
                mv "$arquivo.parcial" "$arquivo"
                rm -rf "fontes/$NOME.git"           # o .tar.gz basta
            fi
        else
            arquivo="fontes/$(basename "$URL")"
            if [ ! -f "$arquivo" ]; then
                echo "Baixando $NOME $VERSAO: $URL"
                curl -fsSL -o "$arquivo.parcial" "$URL"
                mv "$arquivo.parcial" "$arquivo"
            fi
            if [ "$(sha256sum "$arquivo" | cut -d' ' -f1)" != "$SHA256" ]; then
                echo "ERRO: $arquivo não confere com o SHA-256 da receita"; rm -f "$arquivo"; exit 1
            fi
        fi
        echo "  $NOME $VERSAO: ok ($arquivo)"
    )
done
