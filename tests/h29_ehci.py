#!/usr/bin/env python3
"""H29 - PCI e controlador EHCI.

Critério: comando de diagnóstico lista os controladores EHCI encontrados e
as portas com dispositivo conectado, no QEMU.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_console, criar_pendrive, pendrive_qemu  # noqa: E402


def main():
    pen = criar_pendrive("h29-pen")
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h29", "ide", imagem=imagem_console(), extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        s = m.serial()
        verificar("[EHCI] controlador" in s and "rodando" in s, "controlador EHCI achado no PCI, reiniciado e rodando")
        out = m.comando("usb")
        verificar("Controladores USB 2.0 (EHCI): 1" in out, "o comando usb lista o controlador")
        verificar("porta 0: dispositivo conectado" in out, "e a porta com o pendrive")
        m.captura("H29-diagnostico-usb")
    # Dois controladores e boot pelo próprio pendrive (como no G470: a BIOS usava o EHCI).
    pen2 = criar_pendrive("h29-pen2")
    extras = ["-device", "usb-ehci,id=ehci2"] + ["-drive", f"if=none,id=p2,format=raw,file={pen2}",
                                                 "-device", "usb-storage,bus=ehci2.0,drive=p2"]
    with Maquina("h29-usb", "usb", imagem=imagem_console(), extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        out = m.comando("usb")
        verificar("Controladores USB 2.0 (EHCI): 2" in out, "com boot pelo pendrive e um segundo controlador: os dois aparecem")
        verificar(out.count("dispositivo conectado") >= 2, "o pendrive de boot e o de dados aparecem nas portas")
    return 0


if __name__ == "__main__":
    sys.exit(main())
