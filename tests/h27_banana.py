#!/usr/bin/env python3
"""H27 - Sagui Pega Banana.

Critério: teste automático joga alguns segundos enviando setas pelo monitor
do QEMU; capturas da tela de jogo e da tela de fim de jogo.
O teste joga de verdade: lê no log onde a primeira banana nasceu e leva o
sagui até embaixo dela; depois deixa as outras caírem até o fim do jogo.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402


def main():
    with Maquina("h27", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("banana")
        m.esperar_serial("banana: tela de inicio", 20)
        time.sleep(0.5)
        m.captura("H27-banana-inicio")
        m.tecla("ret")
        m.esperar_serial("banana: nova banana em x=", 10)
        x = int(re.findall(r"banana: nova banana em x=(\d+)", m.serial())[0])
        sagui = (480 - 48) // 2
        passos = round(((x + 8) - (sagui + 24)) / 16)
        tecla = "right" if passos > 0 else "left"
        for _ in range(abs(passos)):
            m.tecla(tecla, 0.05)
        time.sleep(0.3)
        pos = re.findall(r"banana: sagui x=(\d+)", m.serial())
        verificar(abs(passos) == 0 or (pos and abs(int(pos[-1]) + 24 - (x + 8)) <= 16),
                  f"as setas movem o sagui até embaixo da banana (banana em x={x}, sagui em x={pos[-1] if pos else sagui})")
        time.sleep(1.5)
        m.captura("H27-banana-jogando")
        m.esperar_serial("banana: pegou! pontos 1", 30)
        verificar(True, "pegou a banana: 1 ponto")
        m.esperar_serial("banana: caiu no chao, vidas 2", 40)
        verificar(True, "banana que cai no chão tira uma vida")
        m.esperar_serial("banana: fim de jogo", 90)
        fim = re.findall(r"banana: fim de jogo, pontos (\d+), recorde (\d+)", m.serial())[-1]
        verificar(int(fim[0]) >= 1 and fim[0] == fim[1], f"fim de jogo com {fim[0]} ponto(s); recorde da sessão {fim[1]}")
        time.sleep(0.5)
        m.captura("H27-banana-fim-de-jogo")
        m.tecla("ret")
        time.sleep(1)
        verificar(m.serial().count("banana: inicio") == 2, "Enter na tela de fim começa outra partida")
    return 0


if __name__ == "__main__":
    sys.exit(main())
