#!/usr/bin/env python3
"""H17 - framebuffer VESA.

Critério: captura mostra fundo colorido e formas desenhadas na resolução
detectada. Se o VESA falhar, o sistema volta para o modo texto e avisa.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, cor_do_pixel, tamanho_png  # noqa: E402


def perto(c, alvo, tol=30):
    return all(abs(a - b) <= tol for a, b in zip(c, alvo))


def main():
    for modo in ("ide", "usb"):
        with Maquina("h17", modo, imagem=imagem_de_teste("grafico", ["--vesa", "--console"])) as m:
            m.esperar_serial("[TESTE] grafico ", 60)
            verificar("[TESTE] grafico ok" in m.serial(), f"[{modo}] autoteste gráfico passou")
            verificar("[VIDEO] 1024x768, 32 bits" in m.serial(), f"[{modo}] modo 1024x768x32 escolhido e ligado")
            png = m.captura(f"H17-formas-{modo}", guardar=(modo == "ide"))
            verificar(tamanho_png(png) == (1024, 768), f"[{modo}] a tela tem 1024x768")
            verificar(perto(cor_do_pixel(png, 100, 100), (255, 210, 0)), f"[{modo}] retângulo amarelo onde foi desenhado")
            verificar(perto(cor_do_pixel(png, 380, 100), (255, 255, 255)) and perto(cor_do_pixel(png, 280, 100), (200, 0, 0)),
                      f"[{modo}] caixa branca com borda vermelha")
            topo, base = cor_do_pixel(png, 900, 5), cor_do_pixel(png, 900, 760)
            verificar(topo[1] > topo[2] and base[2] > base[1], f"[{modo}] fundo em degradê de verde (topo) para azul (base)")
            verificar(perto(cor_do_pixel(png, 40, 728), (255, 255, 255)), f"[{modo}] linhas desenhadas")
    with Maquina("h17-texto", "ide", imagem=imagem_de_teste("grafico")) as m:   # sem --vesa: estágio 2 não liga o gráfico
        m.esperar_serial("[TESTE] grafico ", 60)
        verificar("modo gráfico ligado pelo estágio 2" in m.serial() and "[TESTE] grafico FALHOU" in m.serial(),
                  "sem VESA, o kernel percebe e não tenta desenhar (fica em modo texto)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
