#!/usr/bin/env python3
"""H32 - partições e FAT32: leitura.

Critério: pendrive de teste criado na VPS com pastas, acentos e nomes longos é
listado e lido corretamente pelo ls e pelo cat. Também: pendrive FAT32 sem
tabela de partições ("superfloppy"), arquivo grande (vários clusters) e um
pendrive em outro formato, que não monta.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_console, criar_pendrive, pendrive_qemu  # noqa: E402

GRANDE = "".join(f"linha {i:05d} do arquivo grande do pendrive\n" for i in range(3000))   # ~130 KB


def main():
    arquivos = {
        "LEIAME.TXT": "nome curto em maiúsculas\n",
        "Relatório de viagem.txt": "Fomos à praia e vimos um sagui.\nAção, coração, pão.\n",
        "um nome de arquivo bem comprido para testar os nomes longos.txt": "nome longo ok\n",
        "fotos/praia do sagui.txt": "na pasta fotos\n",
        "documentos/2026/notas de março.txt": "pasta dentro de pasta\n",
        "grande.txt": GRANDE,
    }
    pen = criar_pendrive("h32-pen", rotulo="MEUPEN", pastas=("fotos", "documentos", "documentos/2026"), arquivos=arquivos)
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h32", "ide", imagem=imagem_console(), extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        out = m.comando("discos")
        verificar('usb0' in out and 'FAT32  "MEUPEN"' in out and "não montado" in out, "discos: usb0, FAT32, rótulo MEUPEN, não montado")
        out = m.comando("montar usb0")
        verificar("Pendrive montado em /usb" in out, "montar usb0")
        out = m.comando("ls /usb")
        for nome in ("LEIAME.TXT", "Relatório de viagem.txt", "um nome de arquivo bem comprido para testar os nomes longos.txt",
                     "fotos/", "documentos/", "grande.txt"):
            verificar(nome in out, f"ls /usb mostra {nome!r}")
        out = m.comando("cat \"/usb/Relatório de viagem.txt\"")
        verificar("Fomos à praia e vimos um sagui." in out and "Ação, coração, pão." in out, "cat de nome longo com acentos, conteúdo com acentos")
        out = m.comando("cd /usb/documentos/2026")
        out = m.comando("ls")
        verificar("notas de março.txt" in out, "cd em pastas do pendrive e ls relativo")
        out = m.comando("cat \"notas de março.txt\"")
        verificar("pasta dentro de pasta" in out, "cat com caminho relativo dentro do pendrive")
        m.comando("cd /")
        out = m.comando("cat /usb/leiame.txt")
        verificar("nome curto em maiúsculas" in out, "nomes sem diferença de maiúsculas (leiame.txt = LEIAME.TXT)")
        out = m.comando("wc /usb/grande.txt")
        verificar(f"{GRANDE.count(chr(10))} linhas" in out and f"{len(GRANDE)} letras" in out,
                  f"arquivo grande ({len(GRANDE)} bytes, vários clusters) lido inteiro")
        out = m.comando("ls /usb/fotos | conta")
        verificar("1 linhas" in out, "pipe com arquivos do pendrive")
        m.comando("limpar")
        m.comando("discos")
        m.comando("ls /usb")
        m.captura("H32-ls-pendrive")

    sem = criar_pendrive("h32-superfloppy", rotulo="SEMTABELA", particionado=False, arquivos={"oi.txt": "sem tabela\n"})
    ntfs = criar_pendrive("h32-ntfs", formato="ntfs")
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(sem, "a") + pendrive_qemu(ntfs, "b")
    with Maquina("h32-formatos", "ide", imagem=imagem_console(), extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        out = m.comando("discos")
        verificar('FAT32  "SEMTABELA"' in out, "FAT32 sem tabela de partições é reconhecido")
        verificar("formato não suportado" in out, "pendrive com outro formato aparece como não suportado")
        nome_ntfs = [l.split()[0] for l in out.splitlines() if "não suportado" in l][0]
        out = m.comando(f"montar {nome_ntfs}")
        verificar("formato não suportado" in out, "e não monta")
    return 0


if __name__ == "__main__":
    sys.exit(main())
