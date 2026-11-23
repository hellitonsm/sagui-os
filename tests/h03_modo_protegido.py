#!/usr/bin/env python3
"""H03 - estágio 2 e entrada no modo protegido.

Critério: o kernel em C escreve na tela, em modo texto VGA,
`Ola! Eu sou o SaguiOS.` e a quantidade de memória detectada.
Roda nos dois modos de boot e com duas quantidades de RAM, para provar que a
memória vem mesmo do mapa E820 e não de um número fixo.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, RAIZ, SAIDA, imagem_console  # noqa: E402


def imagem_estagio2_visivel():
    """Imagem com o estágio 2 real e um kernel que só para (não limpa a tela)."""
    B = os.path.join(RAIZ, "build")
    os.makedirs(SAIDA, exist_ok=True)
    obj = os.path.join(SAIDA, "kernel_parado.o")
    elf = os.path.join(SAIDA, "kernel_parado.elf")
    binario = os.path.join(SAIDA, "kernel_parado.bin")
    img = os.path.join(SAIDA, "h03-estagio2.img")
    subprocess.run(["nasm", "-f", "elf32", "-o", obj, os.path.join(RAIZ, "tests", "recursos", "kernel_parado.asm")], check=True)
    subprocess.run(["ld", "-m", "elf_i386", "-T", os.path.join(RAIZ, "kernel", "ligacao.ld"), "-o", elf, obj], check=True)
    subprocess.run(["objcopy", "-O", "binary", elf, binario], check=True)
    subprocess.run([sys.executable, os.path.join(RAIZ, "tools", "mkimagem.py"), "--mbr", os.path.join(B, "boot", "mbr.bin"),
                    "--estagio2", os.path.join(B, "boot", "estagio2.bin"), "--kernel", binario, "--kernel-elf", elf,
                    "--ramdisk", os.path.join(B, "ramdisk.sgfs"), "--saida", img], check=True, stdout=subprocess.DEVNULL)
    return img


def main():
    img = imagem_estagio2_visivel()
    for modo in ("ide", "usb"):
        with Maquina("h03-e2", modo, imagem=img) as m:
            m.esperar_tela("[6/6] entrando no modo protegido", 60)
            texto = "\n".join(m.tela_texto())
            for etapa in ("[1/6] A20 ok", "[2/6] memoria ok", "[3/6] kernel", "[4/6] ramdisk", "[5/6] video"):
                verificar(etapa in texto, f"[{modo}] estágio 2 mostrou '{etapa}'")
            verificar(texto.count(" ok") >= 3, f"[{modo}] kernel e ramdisk conferidos pela soma de verificação")
            m.captura(f"H03-estagio2-{modo}")

    for modo in ("ide", "usb"):
        for mem_mb in (512, 128):
            with Maquina("h03", modo, imagem=imagem_console(), memoria=f"{mem_mb}M") as m:
                m.esperar_serial("[SAGUI] boot ok", 120)
                tela = m.tela_texto()
                texto = "\n".join(tela)
                verificar("Ola! Eu sou o SaguiOS." in tela[1], f"[{modo} {mem_mb}M] saudação na tela VGA, no topo (erro 1)")
                achou = re.search(r"Memoria detectada: (\d+) MB", texto)
                verificar(achou is not None, f"[{modo} {mem_mb}M] memória detectada na tela")
                mb = int(achou.group(1))
                # O QEMU reserva um pouco (BIOS, VGA, ACPI): aceitamos até 2 MB a menos.
                verificar(mem_mb - 2 <= mb <= mem_mb, f"[{modo} {mem_mb}M] {mb} MB livres, coerente com {mem_mb} MB")
                # Desde a fase 2 o detalhe do ramdisk vai só para a serial.
                verificar("Ramdisk SGFS" in m.serial(), f"[{modo} {mem_mb}M] ramdisk encontrado depois do kernel")
                verificar("[SAGUI] boot ok" in m.serial(), f"[{modo} {mem_mb}M] kernel chegou ao fim da inicialização")
                if mem_mb == 512:
                    m.captura(f"H03-kernel-{modo}")
    # H04-fix: com memória acima de 4 GB, a tela separa o total do que um
    # kernel de 32 bits consegue usar, e a saudação continua no topo.
    with Maquina("h03-6g", "ide", imagem=imagem_console(), memoria="6G") as m:
        m.esperar_serial("[SAGUI] boot ok", 120)
        tela = m.tela_texto()
        texto = "\n".join(tela)
        verificar("Ola! Eu sou o SaguiOS." in tela[1], "[6G] saudação continua no topo")
        total = int(re.search(r"Memoria detectada: (\d+) MB", texto).group(1))
        usavel = int(re.search(r"abaixo de 4 GB\): (\d+) MB", texto).group(1))
        verificar(total > 6000 and 2900 < usavel < 4096, f"[6G] total {total} MB, utilizável abaixo de 4 GB {usavel} MB")
        verificar("  livre" in m.serial(), "[6G] mapa E820 completo está no log serial")
        m.captura("H04-fix-memoria-6g", guardar=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
