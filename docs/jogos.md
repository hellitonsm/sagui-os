# Jogos graficos do SaguiOS

Os quatro jogos classicos do SaguiOS rodam como programas graficos nativos
(janela propria, desenhados com a `libsagui`/`gui.h`):

| Programa | Jogo | Controles |
| --- | --- | --- |
| `/bin/sudoku` | Sudoku 9x9 | Setas movem, 1-9 coloca, 0/Backspace apaga, R recomeca, Q/Esc sai; clique numa casa |
| `/bin/pong` | Pong (jogador x CPU) | W/S ou setas movem a raquete, R recomeca, Q/Esc sai |
| `/bin/tetris` | Tetris | Setas movem/descem, W ou seta cima gira, R recomeca, Q/Esc sai |
| `/bin/tabuada` | Tabuada (quiz de multiplicacao) | Digite a resposta e Enter, R nova conta, Q/Esc sai |

Abra pelo icone na area de trabalho ou pelo menu Iniciar, ou rode pelo
Terminal (ex.: `/bin/sudoku`). Cada jogo abre a propria janela.

## Implementacao

Os jogos sao programas C normais em `userland/programas/` (`sudoku.c`,
`pong.c`, `tetris.c`, `tabuada.c`), compilados no build do host com a
`libsagui` (como os outros programas graficos). Eles usam `jan_abrir`,
`g_retangulo`, `g_texto`, `proximo_evento` e `teclas_seguradas` da `gui.h`.

Os fontes de terminal originais (compilados pelo `csagui` dentro do sistema)
continuam em `rootfs/jogos/` e `rootfs/c/tabuada.c` como referencia.

## Jogos graficos no rootfs

Ha tambem versoes graficas em `/jogos/grafico` (sudoku, tetris, velha),
compiladas dentro do proprio sistema com o `tinycc` + `libsaguigui`:

```text
make /jogos/grafico/sudoku.mk
./sudoku
```

Veja `rootfs/jogos/grafico/leia-me.txt` para detalhes.
