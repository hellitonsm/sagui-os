#!/usr/bin/env python3
"""H08 - sistema de arquivos SGFS.

Critério: teste interno lista as entradas do rootfs/ e lê o leia-me.txt inteiro.
A lista e o conteúdo vindos do kernel são comparados com a pasta rootfs/ da VPS.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, RAIZ  # noqa: E402

ROOTFS = os.path.join(RAIZ, "build", "rootfs")   # rootfs/ + programas compilados


def esperado_rootfs():
    itens = set()
    for dirpath, _dirs, arquivos in os.walk(ROOTFS):
        rel = os.path.relpath(dirpath, ROOTFS)
        if rel != ".":
            itens.add("/" + rel.replace(os.sep, "/"))
        for a in arquivos:
            if a != ".gitkeep":
                itens.add("/" + os.path.relpath(os.path.join(dirpath, a), ROOTFS).replace(os.sep, "/"))
    return itens


def main():
    img = imagem_de_teste("sgfs")
    with Maquina("h08", "ide", imagem=img) as m:
        m.esperar_serial("[TESTE] sgfs ", 60)
        serial = m.serial().replace("\r", "")
        falhas = [l for l in serial.splitlines() if "[TESTE] falha" in l or "[TESTE] caminho" in l]
        verificar(not falhas, f"nenhuma falha interna {falhas}")
        verificar("[TESTE] sgfs ok" in serial, "autoteste do SGFS passou")
        listados = {l.split()[-1] for l in serial.splitlines() if l.startswith("[SGFS] ") and "/" in l}
        esperado = esperado_rootfs()
        verificar(listados == esperado, f"lista do kernel = conteúdo do rootfs montado ({len(esperado)} entradas)")
        conteudo = serial.split("[ARQ-INICIO]\n")[1].split("[ARQ-FIM]")[0]
        original = open(os.path.join(ROOTFS, "leia-me.txt"), encoding="utf-8").read()
        verificar(conteudo == original, f"leia-me.txt lido inteiro e idêntico ({len(original)} caracteres, acentos em Latin-1)")
        m.captura("H08-sgfs-lista")
    return 0


if __name__ == "__main__":
    sys.exit(main())
