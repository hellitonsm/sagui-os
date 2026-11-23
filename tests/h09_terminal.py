#!/usr/bin/env python3
"""H09 - terminal (comandos básicos).

Critério original: roteiro digita cada comando no QEMU e confere a saída pela
captura de tela e pela serial: ajuda, ls, cd, cat, limpar, mem, data,
reiniciar.

Na H16 o terminal provisório do kernel foi removido (como pede o PRD) e os
comandos passaram a ser programas do sagsh. Este teste continua conferindo
os mesmos comandos, agora pelo shell, para garantir que nada se perdeu.
Também confere a tela inicial (saudação no topo, etapas com [ ok ]) nos dois
modos de boot.
"""
import datetime
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, RAIZ, imagem_console  # noqa: E402


def main():
    for modo in ("ide", "usb"):
        with Maquina("h09", modo, imagem=imagem_console()) as m:
            m.esperar_serial("sagui:/$", 120)
            tela = m.tela_texto()
            verificar("sagui:/$" in "\n".join(tela), f"[{modo}] prompt sagui:/$ na tela")
            verificar("Ola! Eu sou o SaguiOS." in tela[1], f"[{modo}] saudação continua no topo com o shell aberto")
            verificar(sum(1 for l in tela if "[ ok ]" in l) >= 4, f"[{modo}] as etapas de inicialização aparecem com [ ok ]")
            m.captura(f"H09-tela-inicial-{modo}")
            if modo == "usb":
                out = m.comando("ls")
                verificar("leia-me.txt" in out, "[usb] comandos funcionam com boot pelo pendrive")
                continue

            out = m.comando("ajuda")
            for c in ("ajuda", "ls", "cd", "cat", "limpar", "mem", "data", "reiniciar"):
                verificar(re.search(r"\b" + c + r"\b", out), f"ajuda lista o comando {c}")
            m.captura("H09-ajuda")

            m.comando("limpar")
            out = m.comando("ls")
            for nome in ("arte/", "bin/", "historia/", "leia-me.txt", "sobre.txt", "tmp/"):
                verificar(nome in out, f"ls mostra {nome}")
            m.comando("cd historia")
            out = m.comando("ls")
            verificar("linux.txt" in out and "saguios.txt" in out, "cd historia; ls")
            verificar("sagui:/historia$" in "\n".join(m.tela_texto()), "cd muda o prompt")
            m.captura("H09-ls-cd")

            out = m.comando("cat linux.txt")
            original = open(os.path.join(RAIZ, "rootfs", "historia", "linux.txt"), encoding="utf-8").read()
            faltando = [l for l in original.splitlines() if l.strip() and l not in out]
            verificar(not faltando, "cat mostra todas as linhas do linux.txt")
            m.comando("cd ../arte")
            out = m.comando("cat sagui.txt")
            verificar("S A G U I O S" in out, "cd ../arte e cat sagui.txt (caminho relativo com ..)")
            m.captura("H09-cat-sagui")
            out = m.comando("cat /bin")
            verificar("é uma pasta" in out, "cat numa pasta avisa")
            m.comando("cd /")

            m.comando("limpar")
            tela = [l for l in m.tela_texto() if l.strip()]
            verificar(len(tela) == 1 and tela[0].startswith("sagui:/$"), "limpar deixa só o prompt na tela")

            out = m.comando("mem")
            total = int(re.search(r"total \(E820\):\s+(\d+) MB", out).group(1))
            livre = int(re.search(r"Livre:\s+(\d+) MB", out).group(1))
            verificar(500 <= total <= 512 and 400 < livre <= total, f"mem: total {total} MB, livre {livre} MB")
            out = m.comando("data")
            hoje = datetime.datetime.now(datetime.timezone.utc)
            verificar(hoje.strftime("%d/%m/%Y") in out, f"data mostra a data de hoje ({hoje:%d/%m/%Y})")
            dia = ["segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado", "domingo"][hoje.weekday()]
            verificar(dia in out, f"data mostra o dia da semana ({dia})")
            m.captura("H09-mem-data")

            out = m.comando("cat ação")
            verificar("ação" in out, "acentos digitados chegam ao shell (cat ação)")

            m.digitar("reiniciar\n", 0.05)
            m.esperar_serial("[SAGUI] reiniciando", 30)
            try:
                m.proc.wait(timeout=30)
                reiniciou = True
            except Exception:
                reiniciou = False
            verificar(reiniciou, "reiniciar reinicia a máquina (QEMU encerrou no reset)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
