#!/usr/bin/env python3
"""H10 - paginação.

Critério: teste cria dois espaços de endereço, escreve no mesmo endereço
virtual em cada um e confirma valores diferentes; acesso inválido gera a tela
de page fault.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    with Maquina("h10", "ide", imagem=imagem_de_teste("paginacao")) as m:
        m.esperar_serial("[TESTE] paginacao ", 60)
        s = m.serial()
        falhas = [l for l in s.splitlines() if "[TESTE] falha" in l]
        verificar(not falhas, f"nenhuma falha interna {falhas}")
        verificar("espaco 1 = 11111111" in s and "espaco 2 = 22222222" in s,
                  "mesmo endereço virtual, valores diferentes em cada espaço")
        verificar("[TESTE] paginacao ok" in s, "autoteste de paginação passou (inclui devolução das páginas)")
        verificar("um espa" in "\n".join(m.tela_texto()), "etapa de paginação aparece na inicialização")
        m.captura("H10-dois-espacos")
    with Maquina("h10-pf", "ide", imagem=imagem_de_teste("falta_pagina")) as m:
        m.esperar_serial("[SAGUI] EXCECAO", 60)
        verificar("EXCECAO 14" in m.serial(), "serial registra a exceção 14 (falta de página)")
        m.esperar_tela("Falta de p", 10)
        texto = "\n".join(m.tela_texto())
        verificar("0x90000000" in texto and "leitura" in texto and "mapeada" in texto and "kernel" in texto,
                  "tela explica: leitura, endereço 0x90000000, página não mapeada, modo kernel")
        m.captura("H10-tela-falta-de-pagina")
    return 0


if __name__ == "__main__":
    sys.exit(main())
