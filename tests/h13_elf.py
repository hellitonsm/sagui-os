#!/usr/bin/env python3
"""H13 - carregador ELF e libsagui.

Critério: o kernel carrega bin/ola do SGFS e o programa imprime os argumentos
recebidos. Também confere que um arquivo que não é ELF é recusado.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    with Maquina("h13", "ide", imagem=imagem_de_teste("/bin/ola um dois três")) as m:
        m.esperar_serial("[TESTE] programa terminou", 60)
        s = m.serial()
        verificar("executar /bin/ola -> processo" in s, "kernel carregou /bin/ola do SGFS")
        verificar("rodando no anel 3" in s, "o programa rodou (libsagui + imprimir)")
        verificar("Recebi 4 argumento(s)" in s, "argc = 4")
        for i, a in enumerate(["/bin/ola", "um", "dois", "três"]):
            verificar(f'argv[{i}] = "{a}"' in s, f"argv[{i}] = {a!r}")
        verificar("(ola) terminou com codigo 0" in s, "saiu com código 0 (valor de principal)")
        m.captura("H13-ola-argumentos")
    with Maquina("h13-nao-elf", "ide", imagem=imagem_de_teste("/leia-me.txt")) as m:
        m.esperar_serial("[TESTE] programa /leia-me.txt iniciado", 60)
        verificar("iniciado: -13" in m.serial(), "um texto não é aceito como programa (ERRO_FORMATO)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
