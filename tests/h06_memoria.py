#!/usr/bin/env python3
"""H06 - memória física.

Critério: teste aloca e libera blocos de vários tamanhos sem corromper;
comando de diagnóstico mostra memória total e livre (o comando `mem` do
terminal é conferido também no teste da H09).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    img = imagem_de_teste("memoria")
    for mem in ("128M", "1G"):
        with Maquina("h06-" + mem, "ide", imagem=img, memoria=mem) as m:
            m.esperar_serial("[TESTE] memoria", 60)
            m.esperar_serial("[TESTE] memoria ", 120)
            serial = m.serial()
            falhas = [l for l in serial.splitlines() if "[TESTE] falha" in l]
            verificar(not falhas, f"[{mem}] nenhuma falha no autoteste de memória {falhas}")
            verificar("[TESTE] memoria ok" in serial, f"[{mem}] autoteste de memória passou")
            verificar("liberado duas vezes" in serial, f"[{mem}] liberação dupla detectada e registrada")
            if mem == "1G":
                m.captura("H06-autoteste-memoria")
    return 0


if __name__ == "__main__":
    sys.exit(main())
