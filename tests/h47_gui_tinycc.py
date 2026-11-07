#!/usr/bin/env python3
"""H47 - libsaguigui: a biblioteca grafica do tinycc no SaguiOS.

O sysroot instala /usr/lib/libsaguigui.a (kit grafico + fonte 8x16) e o
header /usr/include/gui.h. O exemplo /tcc/exemplo-gui.c e compilado dentro
do convidado com `tinycc -lsaguigui` e exercitado com eventos sinteticos
(sem interacao).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    with Maquina("h47-gui-tinycc", "ide", imagem=imagem_de_teste("/testes/gui_tinycc", ["--vesa"])) as m:
        m.esperar_serial("[GUI-TINYCC] exemplo compilado e linkado com -lsaguigui", 120)
        m.esperar_serial("[GUI-TINYCC] janela, caixa e botao exercitados no convidado", 60)
        m.esperar_serial("[GUI-TINYCC] biblioteca grafica do tinycc validada", 60)
        m.esperar_serial("(gui_tinycc) terminou com codigo 0", 60)
        s = m.serial()
        verificar("[GUI-TINYCC] biblioteca grafica do tinycc validada" in s,
                  "exemplo compilado com -lsaguigui e componentes exercitados")
        m.captura("H47-gui-tinycc")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
