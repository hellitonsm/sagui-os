# tetris.mk - receita do Tetris (tretis) grafico.
# Compile no Terminal com:  make /jogos/grafico/tetris.mk
# ou todos de uma vez com:  make -all /jogos/grafico
NOME        = Tretis grafico
FONTE       = tetris.c
SAIDA       = tetris
BIBLIOTECAS = -lsaguigui
OPCOES      = -static
INCLUI      =
COMPILADOR  = /bin/tinycc
EXECUTAR    = nao
