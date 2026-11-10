# velha.mk - receita do Jogo da Velha grafico.
# Compile no Terminal com:  make /jogos/grafico/velha.mk
# ou todos de uma vez com:  make -all /jogos/grafico
NOME        = Jogo da Velha grafico
FONTE       = velha.c
SAIDA       = velha
BIBLIOTECAS = -lsaguigui
OPCOES      = -static
INCLUI      =
COMPILADOR  = /bin/tinycc
EXECUTAR    = nao
