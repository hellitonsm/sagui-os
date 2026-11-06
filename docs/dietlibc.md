# dietlibc no lugar do musl: analise para o SaguiOS

## Pergunta

O pedido anterior era adaptar o musl para compilar programas. A nova
pergunta e: **vale a pena usar o dietlibc no lugar do musl** para os
programas de usuario do SaguiOS?

Resposta curta: para o SaguiOS, **nenhuma das duas entra inteira**.
O que cabe e uma camada fina de adaptacao sobre a libsagui, com nomes
POSIX (`malloc`, `printf`, `open`...). Entre as duas, o dietlibc e a
camada mais barata de adaptar; o musl e a mais fiel ao POSIX.

## O que o SaguiOS exige de uma libc

- Programas em anel 3, freestanding (`-ffreestanding -nostdlib`,
  sem libc e sem PIC), ELF32 via `userland/programa.ld`.
- Syscalls proprias via `int 0x80` e `kernel/inc/abi.h` (nao Linux):
  `abrir/ler/escrever/fechar/executar/dormir/...` com nomes em
  portugues na `libsagui` (`aloca/imprimir/copia_texto/...`).
- Sem FPU (`-mgeneral-regs-only`), sem ponto flutuante, sem fork/vfork
  no sentido Linux (o shell cria pipes sem fork, D010).
- Um teste por feature, docs de decisao/erro, commits pequenos.

Nem musl nem dietlibc conhecem essa ABI. Os dois esperam syscalls
Linux (`syscall`/`int 0x80` com numeros diferentes, `fork`, `mmap` de
arquivo, threads...). Qualquer uso direto exige reescrever a camada
de syscall.

## dietlibc

- Biblioteca C pequena, focada em programas estaticos e embedded.
  Fonte em C com asm so no ponto de entrada/syscall.
- Estatico por padrao: compila junto ao programa, sem loader dinamico.
- Funciona sem MMU em muitos alvos, um so binario, pouco RAM.
- Cobre `stdio/stdlib/string/unistd` e ate rede, mas assume Linux:
  espera `fork/exec`, `pipe`, `mmap`, `/proc`, etc. O que falta e
  stubs fracos ou erro em tempo de execucao.
- Portavel a novos kernels pequenos justamente por ser pequena: a
  camada de syscall e enxuta e isolada, da para trocar por outra.

Para o SaguiOS, adaptar o dietlibc significa: trocar so o arquivo de
syscall por um que chama `int 0x80` com os numeros do `abi.h`, e
cortar o que usa `fork/mmap/proc/threads`. O resto (printf, malloc,
string) ja serve. Esforco estimado: baixo-medio.

## Musl

- Implementacao POSIX completa, grande (milhares de arquivos), com
  `printf` com locale, UTF-8, threads, `mmap`, DNS, etc.
- Estatico tambem e possivel, mas puxa muito mais codigo e exige
  `mmap` anonimo, `futex`, `clone`... que o SaguiOS nao tem.
- A shim que ja existe em `userland/musl/` nao e o musl de verdade:
  e uma camada fina sobre a libsagui (`malloc` sobre `aloca`,
  `printf` sobre `imprimir`, POSIX de arquivos sobre
  `abrir/ler/escrever`, sem FPU). Manter esse desenha ja entrega
  90% do uso pratico com 2% do tamanho.

Para o SaguiOS, adaptar o musl inteiro seria reescrever syscall +
threads + mmap. Esforco estimado: alto, para pouco ganho aqui.

## Tabela comparativa (neste OS)

|  | libsagui atual | dietlibc adaptado | musl adaptado |
| --- | --- | --- | --- |
| Tamanho no binario | menor | pequeno | grande |
| Nomes | portugues | POSIX | POSIX |
| Syscall Linux needed | nenhuma | pouca (corta fork/mmap) | muita |
| FPU/long double | nao | evita | puxa |
| threads/pthreads | nao | nao | puxa |
| Esforco de porte | zero (ja existe) | baixo-medio | alto |

## Recomendacao

1. **Manter o que ja funciona**: libsagui + shims POSIX finos (o que
   esta em `userland/musl/` como esqueleto). E o caminho mais barato.
2. **Se for trocar de nome**, trocar para dietlibc e preferivel ao
   musl inteiro: mesma API POSIX que os programas esperam, menos
   codigo morto, menos tentativa de puxar threads/FPU.
3. Nao puxar nem musl nem dietlibc inteiros sem antes trocar a
   camada de syscall. Sem isso, nenhum dos dois compila um
   `hello` que rode no SaguiOS.

## O que faria se o Eli pedir "só compilar com nomes POSIX"

- Renomear/adicionar a camada `userland/diet/` com os mesmos
  headers do `userland/musl/` e o mesmo `musl.c` (so trocar o nome
  na documentacao), ligar no Makefile como `DIET_OBJ`, e um teste
  `userland/testes/diet_nativo.c` + `tests/h43_diet.py`.
- Nada de baixar o dietlibc oficial para dentro do repo: o PRD
  proibe bibliotecas prontas e o ganho seria pouco.

Ver decisao D036 em `docs/decisoes.md`.
