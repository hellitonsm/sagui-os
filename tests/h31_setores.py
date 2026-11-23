#!/usr/bin/env python3
"""H31 - armazenamento em massa.

Critério: teste lê e grava setores numa imagem de teste no QEMU e confere
byte a byte com a VPS.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, criar_pendrive, pendrive_qemu  # noqa: E402


def soma(dados):
    s = 0
    for b in dados:
        s = (s * 31 + b) & 0xFFFFFFFF
    return s


def main():
    pen = criar_pendrive("h31-pen")
    original = open(pen, "rb").read()
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h31", "ide", imagem=imagem_de_teste("usb_setores"), extras=extras) as m:
        m.esperar_serial("[TESTE] usb_setores ", 60)
        s = m.serial()
        falhas = [l for l in s.splitlines() if "[TESTE] falha" in l]
        verificar(not falhas, f"nenhuma falha no kernel {falhas}")
        lida = int(re.search(r"soma setores 0-63: ([0-9a-f]+)", s).group(1), 16)
    verificar(lida == soma(original[:64 * 512]), "a leitura dos setores 0-63 bate com o arquivo da VPS")
    depois = open(pen, "rb").read()
    padrao = bytes((i * 7 + i // 512) & 0xFF for i in range(40 * 512))
    verificar(depois[100000 * 512:100040 * 512] == padrao, "os 40 setores gravados estão no arquivo, byte a byte")
    verificar(depois[-512:] == b"\xA5" * 512, "o último setor do pendrive foi gravado")
    resto_igual = depois[:100000 * 512] == original[:100000 * 512] and depois[100040 * 512:-512] == original[100040 * 512:-512]
    verificar(resto_igual, "nenhum outro setor foi alterado")
    return 0


if __name__ == "__main__":
    sys.exit(main())
