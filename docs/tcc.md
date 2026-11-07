# tinycc — o compilador C completo dentro do SaguiOS

O TinyCC (0.9.28rc, `.local/tinycc`) é um compilador de C completo (padrão C99,
mais extensões GNU comuns) que roda **dentro** do SaguiOS: ele compila um fonte
`.c` e produz um executável ELF32 i386 que o próprio convidado executa. Não é
o compilador do subconjunto (`csagui`, ver `docs/asm-c.md`) nem o interpretador
Go/Lua — é o compilador C de verdade.

```text
tinycc -o /tmp/ola /tcc/hello-tcc.c
/tmp/ola
```

`/tcc/hello-tcc.c` é o exemplo empacotado na imagem. O teste `tests/h46_tinycc.py`
(caminho `/testes/tinycc`) faz exatamente isso no convidado: roda `tinycc -v`,
compila o exemplo, executa o resultado e confere a saída via pipe.

## Como é montado

- O tinycc é **compilado como qualquer outro programa do SaguiOS** (libsagui +
  shim C/POSIX de `userland/mlibc`, com `cc-latin1.py` no Windows ou `gcc
  -m32` no Linux) — ou seja, como um programa normal ligado com `programa.ld`
  e `_inicio`.
- Ele instala um **sysroot** em `/usr` dentro do ramdisk (ver alvo `sysroot`
  do Makefile):
  - `/usr/include/` — headers da libc nativa do SaguiOS (shim + libsagui) mais
    `gui.h` (o kit gráfico) e os headers do tcc que o SaguiOS não tem
    (`float.h`, `tccdefs.h`, `stdatomic.h`, `stdalign.h`, `stdnoreturn.h`,
    `tgmath.h`, `varargs.h`).
  - `/usr/lib/crt1.o` — ponto de entrada `_start` (ver `tools/tinycc-sagui/
    crt1-sagui.asm`): chama `main(argc, argv)` e encerra com o valor
    devolvido. É ao C que o crt1 serve; os programas do SaguiOS continuam
    usando o `_inicio` do libsagui, que chama `principal`.
  - `/usr/lib/crti.o` e `/usr/lib/crtn.o` — vazios; o SaguiOS não tem
    `.init`/`.fini`, só `.init_array`.
  - `/usr/lib/libc.a` — arquivo estático com a libc nativa (shim POSIX +
    libsagui, sem `gui.o`/`fonte8x16.o`), ligada por padrão pelos programas
    que o tinycc compila.
  - `/usr/lib/libsaguigui.a` — a biblioteca gráfica (`gui.o` + fonte 8x16),
    ligada sob demanda com `-lsaguigui`.
  - `/usr/lib/tcc/libtcc1.a` — helpers de 64 bits e atômicos exigidos por
    qualquer programa compilado (sempre ligado pelo tinycc).
- O ramdisk SGFS instala todo o `sysroot` em `/usr` e o binário em `/bin/tinycc`.

Reconstruir tudo: `make` (o sysroot e o binário são atualizados automaticamente).

## O que funciona

- C inteiro: tipos, `struct`, ponteiros, arrays, funções, recursão, `for`,
  `while`, `switch`, literais, strings, `sizeof`, `typedef`, `#define`,
  includes aninhados.
- Compilação **e execução** de programas no convidado (`/tmp/ola`).
- Compilação de qualquer fonte `.c` que use apenas inteiros, mais a libc nativa
  (`stdio`, `string`, `stdlib`, `unistd`, `fcntl`, `time`, `ctype`, `errno`,
  `assert`, `setjmp`, `inttypes` e os novos `signal`, `sys/mman`,
  `sys/ucontext`, `glob` — estes últimos para o próprio tinycc compilar).
- `setjmp`/`longjmp` de verdade (`userland/libsagui/setjmp.asm`).
- 64 bits: `long long`, literais `LL`, `int64_t` etc. (via `libtcc1.a`).
- Programas gráficos completos ligados com `-lsaguigui` (veja
  "Biblioteca gráfica" abaixo).

## O que não funciona (e por quê)

- **Ponto flutuante**: o kernel desliga a FPU (`CR0.EM`) e o libc não tem
  aritmética IEEE. Qualquer operação `double`/`float` no tinycc ou nos
  programas compilados por ele aborta com a mensagem "literais de ponto
  flutuante nao suportados no SaguiOS" (`userland/mlibc/float.c`,
  `tools/tinycc-sagui/float_proibido.c`). Use inteiros (o SaguiOS é um
  sistema de 32 bits sem FPU).
- `tcc -run` (compila e executa na memória): precisa mapear memória
  executável, que o kernel não oferece (`sys/mman.h` é stub).
- `tcc -ar`, `tcc -implib` (import library), `tcc -t<n>`: são variantes de
  outras plataformas.
- Linkar contra o mlibc upstream: os binários ligados com a libc upstream
  **não iniciam** no SaguiOS (ver "Observações sobre a libc upstream" abaixo).
  O tinycc e o sysroot usam a libc nativa, que é estável e testada.

## Biblioteca gráfica (libsaguigui)

Além de C inteiro, o tinycc compila e liga **programas gráficos completos**
para a área de trabalho do SaguiOS com o mesmo kit `gui.h` dos programas
nativos (`editor`, `ide`, …): janela, rótulo, botão, caixa de texto de uma
linha, área de texto, lista e diálogos.

A biblioteca é **independente da libc** (`libc.a`): o kit gráfico foi movido
para `build/sysroot/lib/libsaguigui.a` (`userland/libsagui/gui.o` +
`userland/fonte8x16.o`), e o header é instalado como `/usr/include/gui.h` no
sysroot. O `libc.a` ficou só com o núcleo (sem `gui.o`, sem `fonte8x16.o`).
O tinycc compila a biblioteca no hospedeiro e a embute na imagem junto com o
resto do sysroot:

```text
tinycc -o /tmp/exemplo-gui /tcc/exemplo-gui.c -lsaguigui
/tmp/exemplo-gui
```

O exemplo empacotado na imagem (`rootfs/tcc/exemplo-gui.c`) cria uma janela
240x140 com rótulo, caixa de texto (`caixa_t`) e botão (`botao_t`). Sem
argumento ele abre a janela e entra no loop de eventos; com `--autoteste`
exercita os componentes com eventos sintéticos (digita "oi" na caixa, aperta
e solta o botão) e imprime marcadores — é isso que o teste `/testes/gui_tinycc`
verifica no convidado. Os testes gráficos precisam da área de trabalho no ar
(`--vesa`, sem `--console`), porque o compositor só existe nesse modo; por
isso o `tests/h47_gui_tinycc.py` usa `imagem_de_teste("/testes/gui_tinycc", ["--vesa"])`.

O `gui.h` expõe (veja `userland/inc/gui.h`):

| O quê | API |
| --- | --- |
| Janela | `jan_abrir`, `jan_fechar`, `jan_mostrar`, `jan_mostrar_area` |
| Desenho | `g_retangulo`, `g_moldura`, `g_linha`, `g_texto`, `g_relevo`, `g_largura_texto` |
| Rótulo | `rotulo` |
| Botão | `botao_desenhar`, `botao_tratar` (clique = apertou e soltou dentro) |
| Caixa de texto | `caixa_desenhar`, `caixa_tratar`, `caixa_definir` |
| Área de texto | `area_iniciar`, `area_desenhar`, `area_tratar`, `area_definir` |
| Lista | `lista_desenhar`, `lista_tratar` |
| Diálogos | `confirmar`, `mensagem`, `pedir_texto` |
| Eventos | `proximo_evento` (`EV_CLIQUE`, `EV_SOLTOU`, `EV_MOVER`, `EV_TECLA`, `EV_FECHAR`) |

Um programa de verdade começa criando a janela e reage a `proximo_evento`;
veja o bloco `else` de `rootfs/tcc/exemplo-gui.c` para o esqueleto mínimo
(loop interativo com botão e caixa).

O `gui.h` usa os tipos do `abi.h` (`evento_t`, `retangulo_t`, `EV_*`) e
inclui `sagui.h` — ambos já estão em `/usr/include` no sysroot.

## Observações sobre a libc upstream

Durante o porte, descobriu-se que **nenhum** binário ligado com o mlibc
upstream inicia no SaguiOS (proteger o `upstream-teste` com `LIBC=upstream`
reproduz a falha: falta de página dentro de `interpreterMain`, lendo o vetor
auxiliar). Por isso o `tinycc` e o `upstream-teste` atual usam a libc
nativa. O upstream permanece no repositório como experimento (`make
LIBC=upstream` para o programa de teste), mas não é usado pelo `/bin/tinycc`
nem pelo sysroot.

## Detalhe de ABI encontrado e corrigido

O kernel monta a pilha inicial no layout ELF padrão (`[esp] = argc`; `[esp+4]`
é o vetor `argv` *inline*, terminado em `NULL`, seguido de `envp = NULL` e o
vetor auxiliar). O `_inicio` da libsagui ainda lia `[esp+4]` como valor em vez
do endereço do vetor (`lea`), entregando `argv = argv[0]` a todo programa com
argumentos. Corrigido em `userland/libsagui/inicio.asm`; depois da correção,
todos os programas com argumentos (`cat`, `cp`, `echo`, `ls` etc.) passaram a
funcionar, e `/testes/abiargv` confirma o ABI reexecutando a si mesmo com
argumentos. (O `tools/tinycc-sagui/crt1-sagui.asm` já usava o layout correto.)

## Comandos úteis dentro do SaguiOS

```text
tinycc -v                    # versão
tinycc -E -o - /tcc/hello-tcc.c   # só pré-processar
tinycc -c -o /tmp/ola.o /tcc/hello-tcc.c
tinycc -o /tmp/ola /tcc/hello-tcc.c
```

## Onde estão as peças

| O quê | Onde |
| --- | --- |
| Fonte do tinycc (pinado) | `.local/tinycc` via `tools/tinycc-sagui/setup.ps1` |
| Entrada do `_inicio` / `principal` (libc nativa) | `userland/libsagui/inicio.asm`, `userland/libsagui/setjmp.asm` |
| Entrada `_start` / `main` (sysroot) | `tools/tinycc-sagui/crt1-sagui.asm` |
| Scaffold vazio (crti/crtn) | `tools/tinycc-sagui/crti.S`, `crtn.S` |
| `main` → `principal` (o binário do tinycc, convenção SaguiOS) | `tools/tinycc-sagui/principal.c` |
| Stubs de ponto flutuante | `tools/tinycc-sagui/float_proibido.c` (no binário), `userland/mlibc/float.c` (na libc do sysroot) |
| Headers novos da libc | `userland/mlibc/inc/` (`assert.h`, `inttypes.h`, `setjmp.h`, `signal.h`, `glob.h`, `math.h`, `sys/mman.h`, `sys/ucontext.h`) |
| Funções novas da libc | `userland/mlibc/{error,stdio,stdlib,time,unistd}.c` (assert_fail, freopen, getenv, localtime, realpath, execvp, environ, __extendsfdf2) |
| Regras de build/sysroot | `Makefile` (alvo `sysroot`, `build/userland/bin/tinycc`, `build/sysroot/lib/libc.a`, `build/sysroot/lib/libsaguigui.a`) |
| Kit gráfico usado pelo tinycc | `userland/libsagui/gui.c` → `libsaguigui.a`; header `userland/inc/gui.h` → `/usr/include/gui.h` |
| Exemplo gráfico | `rootfs/tcc/exemplo-gui.c` → `/tcc/exemplo-gui.c` |
| Teste gráfico no convidado | `userland/testes/gui_tinycc.c` → `/testes/gui_tinycc` (via `tests/h47_gui_tinycc.py`, `--vesa`) |
| Exemplo | `rootfs/tcc/hello-tcc.c` |
| Teste no convidado | `userland/testes/tinycc.c` → `/testes/tinycc` |
| Teste de ABI | `userland/testes/abiargv.c` → `/testes/abiargv` |
