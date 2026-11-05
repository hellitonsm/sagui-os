#!/usr/bin/env python3
"""H22 - kit gráfico da libsagui.

Critério: programa de demonstração mostra todos os componentes funcionando
com mouse e teclado; captura em docs/capturas/.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def origem(serial, titulo):
    m = re.findall(rf'criada por \d+: "{titulo}" (\d+)x(\d+) em \((\d+),(\d+)\)', serial)[-1]
    return int(m[2]) + 2, int(m[3]) + 24, int(m[0]), int(m[1])   # canto da área de conteúdo


def main():
    with Maquina("h22", "ide", imagem=imagem_de_teste("/bin/componentes", ["--vesa"])) as m:
        m.esperar_serial("componentes: pronto", 120)
        ox, oy, _, _ = origem(m.serial(), "Componentes da libsagui")
        m.captura("H22-componentes")

        m.clique(ox + 86, oy + 54)
        m.clique(ox + 86, oy + 54)
        m.esperar_serial("componentes: botao clicado 2", 10)
        verificar(True, "botão: dois cliques contados")

        m.clique(ox + 16 + 5 + 11 * 8 + 6, oy + 92)            # fim do texto da caixa
        for _ in range(11):
            m.tecla("backspace", 0.03)
        m.digitar("Olá Sagui\n", 0.05)
        m.esperar_serial("componentes: caixa enter: Olá Sagui", 10)
        verificar(True, "caixa de texto: apagou, digitou com acento e Enter")

        m.clique(ox + 100, oy + 140)                          # área de texto
        m.tecla("pgdn")
        m.esperar_serial("componentes: area topo", 10)
        topo = int(re.findall(r"componentes: area topo (\d+)", m.serial())[-1])
        verificar(topo > 0, f"área de texto rola com PgDn (primeira linha visível: {topo})")
        m.digitar("x", 0.05)
        m.esperar_serial("componentes: area mudou", 10)
        verificar(True, "área de texto aceita digitação")

        m.clique(ox + 400, oy + 120 + 2 + 2 * 20 + 8)        # terceiro item da lista
        m.esperar_serial("componentes: lista selecionou Editor", 10)
        m.clique(ox + 400, oy + 120 + 2 + 3 * 20 + 8, duplo=True)
        m.esperar_serial("componentes: lista ativou Calculadora", 10)
        verificar(True, "lista: seleção com um clique e ativação com duplo clique")
        m.captura("H22-componentes-em-uso")

        m.clique(ox + 16 + 80, oy + 352 + 14)                 # "Apagar texto..."
        m.esperar_serial('"Confirmar"', 10)
        time.sleep(0.5)
        dx, dy, dl, da = origem(m.serial(), "Confirmar")
        m.captura("H22-dialogo-confirmar")
        m.clique(dx + dl - 16 - 45, dy + da - 40 + 13)        # botão "Sim"
        m.esperar_serial("componentes: confirmar -> sim", 10)
        verificar(True, "diálogo 'Tem certeza?' respondido com Sim")

        m.clique(ox + 190 + 60, oy + 352 + 14)                # "Mensagem"
        m.esperar_serial('"Mensagem"', 10)
        time.sleep(0.3)
        m.tecla("ret")
        m.esperar_serial("componentes: mensagem fechada", 10)
        verificar(True, "caixa de mensagem fecha com Enter")
    return 0


if __name__ == "__main__":
    sys.exit(main())
