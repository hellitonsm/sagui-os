# Jogos em C compilados dentro do SaguiOS

Desde a versão 0.3 do `csagui`, o SaguiOS compila programas em C pequenos o
bastante para rodar **dentro do próprio sistema**, sem nenhuma ferramenta do
hospedeiro — e com entrada de teclado no terminal. São três jogos em
`/jogos`:

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

Os jogos usam o **terminal** (não a interface gráfica). Sudoku roda em modo
linha (o SaguiOS faz eco e edição de linha); Pong e Tetris rodam em modo
bruto, redesenha a tela a cada quadro e leem o teclado sem bloquear.

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

## Limites honestos

- Os jogos são **de console** (ASCII), não gráficos: o terminal não sabe
  posicionar o cursor, então cada quadro é desenhado do zero após
  `limpar_tela()`. Funciona bem, mas deve a estética ao fato de o modo bruto
  entregar um byte por vez.
- O Pong/Tetris leem o teclado sem bloquear (tecla_tentada), então a bola
  anda mesmo sem apertar nada. O Sudoku lê linhas inteiras com eco (modo
  linha do SaguiOS).
- Sem divisão fracionária, ponteiro, `struct` ou variáveis locais de array:
  o estado dos jogos vive em globais.
- `aleatorio` é simples (relógio + contador), suficiente para os jogos não
  repetidos; não é um gerador criptográfico.
- O teste automático `/testes/ferramentas_nativas` verifica que os três
  jogos **compilam** no convidado; como eles esperam teclado, quem joga é
  você: ligue a imagem e rode `/tmp/sudoku`, `/tmp/pong` ou `/tmp/tetris`.
