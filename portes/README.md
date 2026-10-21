# Portes: programas de fora rodando no NovaEra

Um **porte** é um programa de outro projeto, compilado do código original
**sem nenhuma mudança** para rodar no NovaEra, com a libc do NovaEra
(`libc/`). Se um porte precisa de algo que falta, quem muda é o NovaEra
(a libc ou o núcleo), não o programa.

| Porte | Versão | O que é | Vai para |
|---|---|---|---|
| `qbe` | 1.3 | o *backend* do compilador do NEFS: lê SSA, escreve assembly x86_64 | `/bin/qbe`, exemplos em `/usr/share/qbe` |

## Como funciona

Cada porte tem uma pasta com:

- `receita`: nome, versão, endereço, SHA-256 e a lista de arquivos `.c`
- arquivos que o NovaEra acrescenta (o `config.h` do QBE, que o Makefile
  original gera com `uname`)
- `exemplos/`, opcional: vão para o initrd

```
portes/baixar.sh     # baixa as fontes para portes/fontes/ e confere o SHA-256
./build.sh           # compila cada porte que já tem fontes e o põe no initrd
```

Sem as fontes, o `build.sh` avisa e constrói o sistema sem o porte.

## Experimente o QBE dentro do NovaEra

```
qbe /usr/share/qbe/ola.ssa
qbe -o /tmp/soma.s /usr/share/qbe/soma.ssa
cat /tmp/soma.s
```

O assembly gerado é idêntico, byte a byte, ao do QBE no Linux. Para
transformá-lo em programa ainda falta o assembler e o ligador do NEFS
(Fase 3, Etapa 5).
