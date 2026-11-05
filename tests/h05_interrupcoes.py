#!/usr/bin/env python3
"""H05 - interrupções e relógio.

Critério: um teste que provoca divisão por zero mostra a tela de erro do
SaguiOS; o contador de segundos avança.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    img = imagem_de_teste("divisao")
    with Maquina("h05-div", "ide", imagem=img) as m:
        m.esperar_serial("[SAGUI] EXCECAO", 60)
        verificar("EXCECAO 0 (Divis" in m.serial(), "serial registra a exceção 0, divisão por zero")
        m.esperar_tela("O SaguiOS encontrou um erro", 10)
        texto = "\n".join(m.tela_texto())
        verificar("Divis" in texto and "por zero" in texto, "tela de erro mostra o nome da exceção")
        verificar(re.search(r"EIP=[0-9a-f]{8}", texto) and "EAX=" in texto and "CR2=" in texto,
                  "tela de erro mostra os registradores")
        verificar("[TESTE] divisao FALHOU" not in m.serial(), "a CPU gerou a exceção (não passou direto)")
        m.captura("H05-tela-de-erro-divisao")

    img = imagem_de_teste("relogio")
    with Maquina("h05-rel", "ide", imagem=img) as m:
        m.esperar_serial("[TESTE] relogio", 60)
        m.esperar_serial("[TESTE] relogio ok", 30)
        segs = [int(x) for x in re.findall(r"segundos desde o boot: (\d+)", m.serial())]
        verificar(len(segs) == 3 and segs[0] < segs[1] < segs[2], f"contador de segundos avança: {segs}")
        verificar(True, "100 tiques por segundo, conferido contra o relógio CMOS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
