#!/usr/bin/env python3
"""H02 - estágio 1 (MBR).

Critério: no QEMU (IDE e USB) aparece `SaguiOS: carregando...`.
Também confere a estrutura do setor: 512 bytes, 0x55AA, partição ativa e
área de BPB sem código. Para isolar o MBR, monta uma imagem de teste em que
o estágio 2 só para a máquina (assim nada apaga a mensagem da tela).
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, RAIZ, SAIDA  # noqa: E402

B = os.path.join(RAIZ, "build")


def main():
    mbr = open(os.path.join(B, "saguios.img"), "rb").read(512)
    verificar(len(mbr) == 512 and mbr[510:512] == b"\x55\xAA", "assinatura 0x55AA no fim do setor 0")
    verificar(mbr[0] == 0xEB and mbr[2] == 0x90, "salto curto + NOP no início (formato com BPB)")
    verificar(mbr[3:0x5A] == bytes(0x57), "área do BPB falso (0x03-0x59) reservada e zerada")
    part = mbr[0x1BE:0x1CE]
    status, tipo = part[0], part[4]
    inicio, qtd = struct.unpack("<II", part[8:16])
    verificar(status == 0x80, "partição 1 marcada como ativa (0x80)")
    verificar(tipo != 0 and inicio == 1 and qtd > 0, f"partição válida: tipo 0x{tipo:02X}, LBA {inicio}, {qtd} setores")
    verificar(mbr[0x1CE:0x1FE] == bytes(48), "demais entradas da tabela vazias")

    os.makedirs(SAIDA, exist_ok=True)
    stub = os.path.join(SAIDA, "estagio2_parado.bin")
    subprocess.run(["nasm", "-f", "bin", "-o", stub, os.path.join(RAIZ, "tests", "recursos", "estagio2_parado.asm")], check=True)
    img = os.path.join(SAIDA, "h02.img")
    subprocess.run([sys.executable, os.path.join(RAIZ, "tools", "mkimagem.py"), "--mbr", os.path.join(B, "boot", "mbr.bin"),
                    "--estagio2", stub, "--kernel", os.path.join(B, "kernel.bin"), "--kernel-elf", os.path.join(B, "kernel.elf"),
                    "--ramdisk", os.path.join(B, "ramdisk.sgfs"), "--saida", img], check=True, stdout=subprocess.DEVNULL)
    # mkimagem grava tamanho.txt ao lado da imagem; o da imagem real não é tocado.
    for modo in ("ide", "usb"):
        with Maquina("h02", modo, imagem=img) as m:
            m.esperar_tela("SaguiOS: carregando...", 60)
            verificar(True, f"[{modo}] mensagem 'SaguiOS: carregando...' na tela")
            m.captura(f"H02-mbr-{modo}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
