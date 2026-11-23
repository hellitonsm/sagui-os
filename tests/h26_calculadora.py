#!/usr/bin/env python3
"""H26 - Calculadora.

Critério: teste calcula `12,5 × 4 = 50`, `7 ÷ 2 = 3,5`, `1 ÷ 0` mostra aviso;
captura em docs/capturas/. Também: número grande demais, apagar o último
dígito, negativos, e uso pelo teclado e pelos botões.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402

BOTOES = {"C": (0, 0), "<-": (1, 0), "÷": (2, 0), "×": (3, 0), "7": (0, 1), "8": (1, 1), "9": (2, 1), "-": (3, 1),
          "4": (0, 2), "5": (1, 2), "6": (2, 2), "+": (3, 2), "1": (0, 3), "2": (1, 3), "3": (2, 3), "=": (3, 3),
          "0": (0, 4), ",": (2, 4)}


def ultimo_visor(serial):
    return re.findall(r"calculadora: visor ([^\r\n]*)", serial)[-1]


def main():
    with Maquina("h26", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("calculadora")
        m.esperar_serial("calculadora: pronta", 20)
        time.sleep(0.5)
        g = re.findall(r'criada por \d+: "Calculadora" \d+x\d+ em \((\d+),(\d+)\)', m.serial())[-1]
        ox, oy = int(g[0]) + 2, int(g[1]) + 24

        def clicar(*teclas):
            for t in teclas:
                c, l = BOTOES[t]
                m.clique(ox + 10 + c * 70 + 31, oy + 92 + l * 58 + 25)

        def esperar_visor(n_antes, tempo=10):
            fim = time.time() + tempo
            while m.serial().count("calculadora: visor") <= n_antes and time.time() < fim:
                time.sleep(0.2)
            return ultimo_visor(m.serial())

        n = m.serial().count("calculadora: visor")
        clicar("1", "2", ",", "5", "×", "4", "=")
        v = esperar_visor(n)
        verificar(v == "50", f"12,5 × 4 = {v} (pelos botões)")
        m.captura("H26-calculadora-50")

        n = m.serial().count("calculadora: visor")
        m.digitar("7/2\n", 0.08)
        v = esperar_visor(n)
        verificar(v == "3,5", f"7 ÷ 2 = {v} (pelo teclado)")
        m.captura("H26-calculadora-3-5")

        n = m.serial().count("calculadora: visor")
        clicar("C", "1", "÷", "0", "=")
        v = esperar_visor(n)
        verificar("dividir por zero" in v, f"1 ÷ 0 mostra aviso: {v}")
        m.captura("H26-calculadora-divisao-por-zero")

        n = m.serial().count("calculadora: visor")
        m.digitar("c999999999999*999999999999\n", 0.04)
        v = esperar_visor(n)
        verificar("grande demais" in v, f"número grande demais avisa: {v}")

        n = m.serial().count("calculadora: visor")
        m.digitar("c128", 0.08)
        m.tecla("backspace")
        m.digitar("3-200\n", 0.08)
        v = esperar_visor(n)
        verificar(v == "-77", f"apagar o último dígito e negativos: 123 - 200 = {v}")

        n = m.serial().count("calculadora: visor")
        m.digitar("c0,1+0,2\n", 0.08)
        v = esperar_visor(n)
        verificar(v == "0,3", f"ponto fixo sem erro de arredondamento: 0,1 + 0,2 = {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
