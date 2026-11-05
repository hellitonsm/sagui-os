#!/usr/bin/env python3
"""H33 - FAT32: gravação.

Critério: teste grava arquivos e pastas no QEMU; depois do desmontar, a VPS
confere com mtools e com fsck.vfat que o sistema de arquivos está íntegro e
com o conteúdo certo.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_console, criar_pendrive, pendrive_qemu, RAIZ, SAIDA  # noqa: E402

GRANDE = "".join(f"linha {i:05d} do arquivo grande do pendrive\n" for i in range(3000))


def latin1(t):
    return t.encode("latin-1")


def main():
    pen = criar_pendrive("h33-pen", rotulo="GRAVAR", pastas=("fotos",),
                         arquivos={"LEIAME.TXT": "original\n", "grande.txt": GRANDE,
                                   "um nome de arquivo bem comprido para testar os nomes longos.txt": "apagar\n"})
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h33", "ide", imagem=imagem_console(), extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        m.comando("montar")
        m.comando("echo Olá pendrive > /usb/novo.txt")                                  # criar
        m.comando("echo mais uma linha >> /usb/novo.txt")                              # aumentar
        m.comando("echo substituído > /usb/LEIAME.TXT")                                # sobrescrever
        m.comando("cp /leia-me.txt /usb")                                               # da RAM para o pendrive
        m.comando("cp /usb/grande.txt /usb/grande-copia.txt")                           # vários clusters
        m.comando("mkdir \"/usb/Música e fotos\"")                                      # pasta com acento e espaço
        m.comando("echo dentro > \"/usb/Música e fotos/arquivo com nome longo dentro.txt\"")
        m.comando("mkdir /usb/vazia")
        m.comando("rmdir /usb/vazia")                                                   # remover pasta
        m.digitar("rm \"/usb/um nome de arquivo bem comprido para testar os nomes longos.txt\"\n", 0.03)
        m.esperar_serial("do pendrive? (s/n)", 15)
        out = m.comando("s")
        verificar("Não apagado" not in out, "rm no pendrive pede confirmação; com 's' apaga")
        m.comando("mv /usb/novo.txt /usb/fotos")                                        # mover para dentro da pasta
        m.comando("echo da ram > /tmp/ram.txt")
        m.comando("mv /tmp/ram.txt /usb/veio-da-ram.txt")                               # entre RAM e pendrive
        out = m.comando("ls /tmp")
        verificar("ram.txt" not in out, "mv entre a RAM e o pendrive copia e apaga a origem")
        out = m.comando("cat /usb/fotos/novo.txt")
        verificar("Olá pendrive" in out and "mais uma linha" in out, "o SaguiOS relê o que gravou")
        m.comando("limpar")
        m.comando("ls /usb")
        m.captura("H33-pendrive-gravado")
        out = m.comando("desmontar")
        verificar("Pode remover o pendrive." in out, "desmontar grava tudo e libera")
        out = m.comando("ls /usb")
        verificar("não existe" in out, "depois de desmontar, /usb some")
        time.sleep(0.5)

    # Conferência na VPS, com ferramentas do Linux.
    def tipo(caminho):
        r = subprocess.run(["mtype", "-i", pen + "@@1M", f"::{caminho}"], capture_output=True)
        return r.stdout

    verificar(tipo("/fotos/novo.txt") == latin1("Olá pendrive\nmais uma linha\n"), "mtools: /fotos/novo.txt criado, aumentado e movido")
    verificar(tipo("/LEIAME.TXT") == latin1("substituído\n"), "mtools: LEIAME.TXT sobrescrito")
    original = open(os.path.join(RAIZ, "rootfs", "leia-me.txt"), encoding="utf-8").read()
    verificar(tipo("/leia-me.txt") == latin1(original), "mtools: leia-me.txt copiado da RAM, idêntico (em Latin-1)")
    verificar(tipo("/grande-copia.txt") == GRANDE.encode(), f"mtools: cópia de {len(GRANDE)} bytes idêntica")
    verificar(tipo("/Música e fotos/arquivo com nome longo dentro.txt") == b"dentro\n", "mtools: pasta e arquivo com nome longo e acento")
    verificar(tipo("/veio-da-ram.txt") == b"da ram\n", "mtools: arquivo movido da RAM")
    lista = subprocess.run(["mdir", "-i", pen + "@@1M", "-b", "::/"], capture_output=True, text=True).stdout
    verificar("vazia" not in lista and "comprido" not in lista, "mtools: pasta removida e arquivo apagado não existem mais")

    particao = os.path.join(SAIDA, "h33-particao.img")
    subprocess.run(["dd", f"if={pen}", f"of={particao}", "bs=512", "skip=2048", "status=none"], check=True)
    r = subprocess.run(["fsck.vfat", "-n", "-v", particao], capture_output=True, text=True)
    print("   " + "\n   ".join(l for l in r.stdout.splitlines()[-3:]))
    verificar(r.returncode == 0, f"fsck.vfat: sistema de arquivos íntegro (código {r.returncode})")
    verificar("differ" not in r.stdout and "Free cluster summary wrong" not in r.stdout and "Orphan" not in r.stdout,
              "fsck.vfat: FATs iguais, contagem de livres certa, nada órfão")
    return 0


if __name__ == "__main__":
    sys.exit(main())
