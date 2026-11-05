#!/usr/bin/env python3
"""H21 - gerenciador de janelas e chamadas de sistema gráficas.

Critério: dois programas de teste em ring 3 abrem uma janela cada; o teste
arrasta uma, fecha a outra, e o programa fechado termina sozinho.
Também confere que cada programa só recebe os eventos da sua janela.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, cor_do_pixel  # noqa: E402


def janela(serial, nome):
    m = re.search(rf'janela (\d+) criada por \d+: "{nome}" (\d+)x(\d+) em \((\d+),(\d+)\)', serial)
    return dict(id=int(m.group(1)), l=int(m.group(2)), a=int(m.group(3)), x=int(m.group(4)), y=int(m.group(5)))


def main():
    with Maquina("h21", "ide", imagem=imagem_de_teste("/testes/duas-janelas", ["--vesa"])) as m:
        m.esperar_serial("janela Janela B: aberta", 120)
        time.sleep(0.5)
        s = m.serial()
        a, b = janela(s, "Janela A"), janela(s, "Janela B")
        verificar(a["id"] != b["id"], f"duas janelas, de dois processos: A em ({a['x']},{a['y']}), B em ({b['x']},{b['y']})")
        png = m.captura("H21-duas-janelas")
        verificar(cor_do_pixel(png, a["x"] + 10, a["y"] + 30) == (0x33, 0x66, 0xCC), "conteúdo azul da janela A desenhado pelo programa")
        verificar(cor_do_pixel(png, b["x"] + 100, b["y"] + 100) == (0xCC, 0x33, 0x33), "conteúdo vermelho da janela B, por cima")

        # Arrasta A pela barra de título (a parte que B não cobre).
        m.arrastar(a["x"] + 15, a["y"] + 10, a["x"] + 15 + 300, a["y"] + 10 + 280)
        mov = re.findall(rf"janela {a['id']} movida para \((\d+),(\d+)\)", m.serial())
        verificar(mov and (int(mov[-1][0]), int(mov[-1][1])) == (a["x"] + 300, a["y"] + 280),
                  f"janela A arrastada para {mov[-1] if mov else None}")
        ax, ay = a["x"] + 300, a["y"] + 280
        png = m.captura("H21-janela-arrastada")
        verificar(cor_do_pixel(png, ax + 10, ay + 30) == (0x33, 0x66, 0xCC), "A aparece na posição nova")
        verificar(cor_do_pixel(png, a["x"] + 10, a["y"] + 30) != (0x33, 0x66, 0xCC), "e não ficou rastro na posição antiga")
        # O contorno escuro de 1 pixel fica por fora da moldura: confere a
        # coluna da esquerda de onde A esteve e um ponto do caminho.
        verificar(cor_do_pixel(png, a["x"] - 1, a["y"] + 150) != (0x10, 0x10, 0x10) and
                  cor_do_pixel(png, a["x"] + 150 - 1, a["y"] + 260) != (0x10, 0x10, 0x10),
                  "nenhum rastro do contorno escuro pelo caminho")

        # Clique e tecla vão só para quem tem o foco (A, depois do arraste).
        m.clique(ax + 2 + 50, ay + 24 + 40)
        m.tecla("x")
        time.sleep(0.5)
        s = m.serial()
        verificar("janela Janela A: clique em (50,40) botao 1" in s, "A recebeu o clique em coordenadas da sua área")
        verificar("janela Janela A: tecla 78" in s and "janela Janela B: tecla" not in s, "a tecla foi só para a janela com foco")
        verificar("janela Janela B: clique" not in s, "B não recebeu eventos de A")

        # Fecha B pelo X da moldura: o programa termina sozinho.
        m.clique(b["x"] + b["l"] + 4 - 2 - 20 + 8, b["y"] + 12)
        m.esperar_serial("duas-janelas: B terminou", 20)
        s = m.serial()
        verificar("janela Janela B: recebi fechar, terminando" in s, "B recebeu o evento fechar e terminou sozinho")
        png = m.captura("H21-janela-fechada")
        verificar(cor_do_pixel(png, b["x"] + 100, b["y"] + 100) != (0xCC, 0x33, 0x33), "a janela B sumiu da tela")
        verificar(cor_do_pixel(png, ax + 10, ay + 30) == (0x33, 0x66, 0xCC), "A continua lá")
    return 0


if __name__ == "__main__":
    sys.exit(main())
