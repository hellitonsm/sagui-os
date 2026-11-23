#!/usr/bin/env python3
"""H19 - mouse / touchpad PS/2.

Critério: movimentos enviados pelo monitor do QEMU movem o cursor; clique é
detectado e registrado na serial.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, cor_do_pixel, imagem_de_teste, imagem_console  # noqa: E402


def cliques(serial, botao):
    return [(int(x), int(y)) for x, y in re.findall(rf"\[MOUSE\] botao {botao} apertado em \((\d+),(\d+)\)", serial)]


def main():
    with Maquina("h19", "ide", imagem=imagem_console()) as m:
        m.esperar_serial("sagui:/$", 120)
        verificar("[MOUSE] mouse PS/2 ligado" in m.serial(), "mouse PS/2 respondeu e foi ligado (IRQ 12)")
        verificar(any("Mouse/touchpad PS/2" in l for l in m.tela_texto()), "etapa do mouse na tela de inicialização")

        # Clique no centro (posição inicial).
        m.monitor("mouse_button 1"); time.sleep(0.3); m.monitor("mouse_button 0"); time.sleep(0.3)
        c0 = cliques(m.serial(), "esquerdo")
        verificar(c0 == [(512, 384)], f"clique esquerdo detectado no centro da tela: {c0}")

        # Move para a direita e para baixo, em vários passos.
        for _ in range(5):
            m.monitor("mouse_move 40 30")
            time.sleep(0.1)
        time.sleep(0.5)
        m.monitor("mouse_button 1"); time.sleep(0.3); m.monitor("mouse_button 0"); time.sleep(0.3)
        c1 = cliques(m.serial(), "esquerdo")[-1]
        verificar(c1[0] > 512 + 100 and c1[1] > 384 + 50, f"o cursor andou para a direita e para baixo: {c1}")
        png = m.captura("H19-cursor-movido")
        # A ponta da seta é preta e o miolo branco, logo abaixo e à direita da ponta.
        verificar(cor_do_pixel(png, c1[0], c1[1]) == (0, 0, 0) and cor_do_pixel(png, c1[0] + 1, c1[1] + 3) == (255, 255, 255),
                  "a seta do cursor está desenhada na nova posição")

        # Move para a esquerda e para cima; botão direito.
        for _ in range(4):
            m.monitor("mouse_move -60 -40")
            time.sleep(0.1)
        time.sleep(0.5)
        m.monitor("mouse_button 2"); time.sleep(0.3); m.monitor("mouse_button 0"); time.sleep(0.3)
        c2 = cliques(m.serial(), "direito")
        verificar(len(c2) == 1 and c2[0][0] < c1[0] and c2[0][1] < c1[1], f"clique direito detectado mais acima e à esquerda: {c2}")
        verificar("botao direito solto" in m.serial(), "soltar o botão também é registrado")

        # Não sai da tela.
        for _ in range(10):
            m.monitor("mouse_move -200 -200")
        time.sleep(0.5)
        m.monitor("mouse_button 1"); time.sleep(0.3); m.monitor("mouse_button 0"); time.sleep(0.3)
        verificar(cliques(m.serial(), "esquerdo")[-1] == (0, 0), "o cursor para no canto da tela (0,0)")

        # O teclado continua funcionando com o mouse ligado (mesmo controlador).
        out = m.comando("echo teclado ok")
        verificar("teclado ok" in out, "o teclado continua funcionando junto com o mouse")
        verificar("[I8042] config lida" in m.serial(), "diagnóstico do controlador registrado na serial")

    # H19-fix: no G470 a fila do controlador travou o teclado. Aqui a IRQ 12
    # fica bloqueada, os bytes do mouse ficam parados na fila, e o teclado
    # tem que continuar funcionando.
    with Maquina("h19-sem-irq12", "ide", imagem=imagem_de_teste("sem_irq12", ["--vesa", "--console"])) as m:
        m.esperar_serial("sagui:/$", 120)
        for _ in range(20):
            m.monitor("mouse_move 10 5")
        time.sleep(1)
        out = m.comando("echo teclado vivo", tempo=30)
        verificar("teclado vivo" in out, "com a IRQ 12 bloqueada e bytes do mouse na fila, o teclado funciona")
        m.monitor("mouse_button 1"); time.sleep(0.5); m.monitor("mouse_button 0"); time.sleep(0.5)
        c = cliques(m.serial(), "esquerdo")
        verificar(c and c[-1][0] > 512, f"o mouse continua andando, recolhido pelo relógio: {c[-1] if c else None}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
