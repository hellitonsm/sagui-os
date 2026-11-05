#!/usr/bin/env python3
"""H12 - anel 3 e chamadas de sistema.

Critério: um programa de teste em ring 3 escreve `Ola do ring 3!` pela
chamada escrever e sai com sair; uma instrução privilegiada (cli) no ring 3
encerra só aquele processo. Também: ler memória do kernel encerra o
processo, e um ponteiro do kernel passado ao escrever é recusado.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    with Maquina("h12", "ide", imagem=imagem_de_teste("anel3")) as m:
        m.esperar_serial("[TESTE] anel3 ", 120)
        s = m.serial()
        falhas = [l for l in s.splitlines() if "[TESTE] falha" in l]
        verificar(not falhas, f"nenhuma falha interna {falhas}")
        verificar("Ola do ring 3!" in s, "programa no anel 3 escreveu 'Ola do ring 3!' pela chamada escrever")
        verificar("ola-anel3 terminou: codigo 42" in s, "saiu pela chamada sair; ponteiro do kernel recusado")
        verificar("cli-anel3 terminou: codigo -1000, motivo 'Prote" in s, "cli no anel 3: proteção geral, só aquele processo encerrado")
        verificar("espiao-anel3 terminou: codigo -1000, motivo 'Falta de p" in s, "ler memória do kernel: falta de página, processo encerrado")
        verificar("[TESTE] anel3 ok" in s and "EXCECAO" not in s, "o kernel continuou funcionando (sem tela de erro)")
        tela = "\n".join(m.tela_texto())
        verificar("Ola do ring 3!" in tela and "encerrado" in tela, "mensagens aparecem na tela")
        m.captura("H12-anel3-protecao")
    return 0


if __name__ == "__main__":
    sys.exit(main())
