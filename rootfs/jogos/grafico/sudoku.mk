# sudoku.mk - receita do Sudoku grafico.
# Compile no Terminal com:  make /jogos/grafico/sudoku.mk
# ou todos de uma vez com:  make -all /jogos/grafico
NOME        = Sudoku grafico
FONTE       = sudoku.c
SAIDA       = sudoku
BIBLIOTECAS = -lsaguigui
OPCOES      = -static
INCLUI      =
COMPILADOR  = /bin/tinycc
EXECUTAR    = nao
