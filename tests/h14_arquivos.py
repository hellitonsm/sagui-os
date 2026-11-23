#!/usr/bin/env python3
"""H14 - arquivos e pastas graváveis.

Critério: teste em ring 3 cria tmp/teste.txt, escreve, fecha, reabre, lê o
mesmo conteúdo, apaga e confirma que sumiu; o mesmo com uma pasta.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    with Maquina("h14", "ide", imagem=imagem_de_teste("/testes/arquivos")) as m:
        m.esperar_serial("[TESTE] programa terminou", 60)
        s = m.serial().replace("\r", "")
        linhas = [l.strip() for l in s.splitlines() if l.strip().startswith("[ok]") or l.strip().startswith("[FALHA]")]
        for l in linhas:
            print("   ", l)
        verificar(len(linhas) >= 25, f"{len(linhas)} verificações rodaram no anel 3")
        verificar("RESULTADO arquivos: SUCESSO" in s, "teste de arquivos e pastas passou")
        verificar("(arquivos) terminou com codigo 0" in s, "programa saiu com código 0")
        m.captura("H14-arquivos-gravaveis")
    return 0


if __name__ == "__main__":
    sys.exit(main())
