# Jogos em C compilados dentro do SaguiOS

Desde a versão 0.3 do `csagui`, o SaguiOS compila programas em C pequenos o
bastante para rodar **dentro do próprio sistema**, sem nenhuma ferramenta do
hospedeiro — e com entrada de teclado no terminal. São três jogos em
`/jogos` (console) e três jogos gráficos em `/jogos/grafico` (tinycc +
libsaguigui, veja a seção "Jogos gráficos" abaixo):

| Arquivo | Jogo | Controles |
| --- | --- | --- |
| `/jogos/sudoku.c` | Sudoku 9x9 | `linha coluna valor` + Enter (ex.: `3 5 7`); `0` apaga, `q` sai |
| `/jogos/pong.c` | Pong 1 jogador contra o CPU | `W`/`S` movem a raquete, `Q` sai |
| `/jogos/tetris.c` | Tetris | `A`/`D` movem, `W` gira, `S` desce rápido, `Q` sai |

Compile e rode no Terminal:

```text
csagui build -o /tmp/sudoku /jogos/sudoku.c
/tmp/sudoku

csagui build -o /tmp/pong /jogos/pong.c
/tmp/pong

csagui build -o /tmp/tetris /jogos/tetris.c
/tmp/tetris
```

Os jogos de `/jogos` usam o **terminal** (não a interface gráfica). Sudoku roda
em modo linha (o SaguiOS faz eco e edição de linha); Pong e Tetris rodam em
modo bruto, redesenha a tela a cada quadro e leem o teclado sem bloquear.

## O que o `csagui` 0.3 ganhou para isso

- **Escalares e arrays globais**: `int pontos;`, `int tab[81];` e
  `int base[28] = { 1, 2, 3, ... };` com inicializador opcional (os elementos
  que faltam são zerados). Limites: 64 globais, 512 elementos por array,
  2048 inicializadores no total.
- **Arrays de `int`**: `int tab[81] = {...}` e leitura/escrita com índice
  `tab[i]`, em qualquer expressão.
- **`for`** com init/condição/incremento (`for (i = 0; i < 81; i = i + 1)`),
  **`break`** e **`continue`** (funcionam em `while` e `for` aninhados).
- **Literais de caractere**: `'q'`, `'\n'`, `'\\'`, `'\0'`, `'1'` etc.,
  compilados como números (como o C faz).
- **Builtins embutidos** (funcionam em `build` e `compile -c`):

  | Builtin | O que faz |
  | --- | --- |
  | `puts(literal)` | Escreve o texto e uma nova linha |
  | `texto(literal)` | Escreve o texto sem nova linha |
  | `numero(n)` | Imprime `n` em decimal, sem nova linha |
  | `caractere(n)` | Escreve o byte `n` na saída (ex.: `111` = `o`) |
  | `limpar_tela()` | Limpa o terminal (`SIS_LIMPAR_TELA`) |
  | `dormir_ms(ms)` | Dorme, liberando a CPU |
  | `entrada_bruta(m)` | Modo do terminal: `1` bruto (sem eco), `0` linha |
  | `tecla()` | Lê 1 byte do teclado, **bloqueante** |
  | `tecla_tentada()` | Modo bruto, **sem bloquear**: devolve o byte ou `-1` |
  | `aleatorio(n)` | Devolve de 0 a `n-1` (baseado no relógio e num contador) |

- **Múltiplos declaradores globais**: `int a, b, c;` declara vários de uma vez.

## A chamada de sistema `SIS_TECLA_TENTEU` (63)

`tecla_tentada()` usa a chamada 63 (`int 0x80`, `SIS_TECLA_TENTEU`), nova
desde esta versão. Ela devolve o próximo byte do teclado **sem bloquear**
(ou `-1` se não há tecla; fora do modo bruto devolve `-1`) e respeita o
protocolo de teclas especiais (ESC + letra, como o shell usa). O modo linha
continua entregando linhas inteiras com eco; a leitura normal por `tecla()`
em modo bruto continua bloqueante, como antes.

Internamente: `kernel/dispositivos/terminal_es.c` ganhou `terminal_tentar()`
(mesma máquina de estado do `ler_bruto`, sem espera); `kernel/fs/arquivos.c`
derivou `sis_tecla_tenteu`; registro em `sistema.c` e `abi.h`. A `libsagui` também expõe
`tecla_tentada()` (`userland/libsagui/chamadas.c`) para programas compilados
com a cadeia normal, como o shell e o editor; os jogos usam a syscall
direta, sem depender dela.

## Jogos gráficos (tinycc + libsaguigui)

Os jogos de `/jogos` usam o terminal. Já os de `/jogos/grafico` são programas
gráficos completos: abrem uma janela de verdade, desenham nos pixels e leem
cliques e teclas pelo `gui.h` (`jan_abrir`, `g_retangulo`, `g_texto`,
`proximo_evento`). Eles são compilados **pelo tinycc dentro do SaguiOS**, com
a biblioteca `libsaguigui`:

| Arquivo | Jogo | Controles |
| --- | --- | --- |
| `/jogos/grafico/sudoku.c` | Sudoku 9x9 | setas movem, `1`-`9` colocam, `0`/Backspace apaga, `R` recomeça, `Q`/Esc sai; clique também seleciona |
| `/jogos/grafico/tetris.c` | Tetris ("tretis") | setas esquerda/direita movem (segure para repetir), seta para baixo acelera, `W`/seta para cima gira, `R` recomeça, `Q`/Esc sai |
| `/jogos/grafico/velha.c` | Jogo da Velha contra a CPU | clique ou `1`-`9`, setas + Enter, `R` recomeça, `Q`/Esc sai (você é X, a CPU é O) |

Cada jogo tem um `.mk` com tudo o que o compilador precisa. Para compilar
todos de uma vez:

```text
make -all /jogos/grafico
```

Isso gera `sudoku`, `tetris` e `velha` na pasta atual. A partir da raiz (o
normal ao abrir o Terminal), os programas ficam em `/sudoku`, `/tetris` e
`/velha`. Depois:

```text
/sudoku
/tetris
/velha
```

Em outra pasta, rode `./sudoku`, `./tetris` e `./velha`.

Ou compile um só com `make /jogos/grafico/sudoku.mk`. O `make` está descrito
em [tcc.md](tcc.md#make-e-arquivos-mk); o formato do `.mk` também aparece em
`/jogos/grafico/leia-me.txt`.

## Limites honestos

- Os jogos de `/jogos` são **de console** (ASCII), não gráficos: o terminal
  não sabe posicionar o cursor, então cada quadro é desenhado do zero após
  `limpar_tela()`. Funciona bem, mas deve a estética ao fato de o modo bruto
  entregar um byte por vez. Os jogos de `/jogos/grafico` são a versão com
  janela de verdade, compilada pelo tinycc.
- O Pong/Tetris leem o teclado sem bloquear (tecla_tentada), então a bola
  anda mesmo sem apertar nada. O Sudoku lê linhas inteiras com eco (modo
  linha do SaguiOS).
- Nos jogos de `/jogos` (subconjunto do `csagui`) não há divisão fracionária,
  ponteiro, `struct` nem variáveis locais de array: o estado vive em globais.
  Os jogos gráficos usam C completo pelo tinycc.
- `aleatorio` é simples (relógio + contador), suficiente para os jogos não
  repetidos; não é um gerador criptográfico.
- O teste automático `/testes/ferramentas_nativas` verifica que os três
  jogos **compilam** no convidado; como eles esperam teclado, quem joga é
  você: ligue a imagem e rode `/tmp/sudoku`, `/tmp/pong` ou `/tmp/tetris`.
