#!/usr/bin/env python3
"""B3 - bip pelo alto-falante do PC.

Critério: o comando bip toca (o kernel programa o canal 2 do PIT e a saída
oscila, conferida pelo bit 5 da porta 0x61 no alto-falante emulado do QEMU);
o som termina sozinho; no Sagui Pega Banana há um bip ao pegar banana e outro
ao perder vida, sem travar o jogo.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, abrir_terminal  # noqa: E402

SOM = ["-audiodev", "none,id=som", "-machine", "pcspk-audiodev=som"]


def main():
    with Maquina("b3", "ide", extras=SOM) as m:
        m.esperar_serial("area de trabalho no ar", 120)
        abrir_terminal(m)   # o Terminal agora abre pelo ícone (D039)
        time.sleep(1)
        out = m.comando("bip 440 300")
        verificar("bip: 440 Hz por 300 ms" in out, "comando bip 440 300")
        s = m.serial()
        verificar("[BIP] 440 Hz 300 ms" in s, "o kernel tocou a nota pedida")
        verificar("[BIP] canal 2 do PIT oscilando: sim" in s, "o canal 2 do PIT está oscilando (alto-falante emulado)")
        m.esperar_serial("[BIP] silencio", 5)
        verificar(True, "o som para sozinho no fim da nota")
        antes = m.serial().count("[BIP] ")
        m.comando("bip escala")
        time.sleep(2)
        notas = re.findall(r"\[BIP\] (\d+) Hz", m.serial()[len(m.serial()) - 2000:])
        verificar(m.serial().count("[BIP] ") - antes >= 9 and "1047" in notas, "bip escala toca as 8 notas")
        out = m.comando("bip 5")
        verificar("uso: bip" in out, "frequência inválida mostra o uso")
        m.captura("B3-comando-bip")

        # No jogo: pega a primeira banana e deixa a próxima cair.
        m.comando("banana")
        m.esperar_serial("banana: tela de inicio", 20)
        time.sleep(0.5)
        m.tecla("ret")
        m.esperar_serial("banana: nova banana em x=", 10)
        x = int(re.findall(r"banana: nova banana em x=(\d+)", m.serial())[0])
        sagui = (480 - 48) // 2
        passos = round(((x + 8) - (sagui + 24)) / 16)
        for _ in range(abs(passos)):
            m.tecla("right" if passos > 0 else "left", 0.05)
        m.esperar_serial("banana: pegou! pontos 1", 30)
        time.sleep(0.3)
        s = m.serial()
        depois = s[s.index("banana: pegou! pontos 1"):]
        verificar("[BIP] 1319 Hz 60 ms" in depois, "bip agudo ao pegar a banana")
        m.esperar_serial("banana: caiu no chao", 40)
        time.sleep(0.5)
        s = m.serial()
        depois = s[s.index("banana: caiu no chao"):]
        verificar("[BIP] 330 Hz 120 ms" in depois and "[BIP] 220 Hz 200 ms" in depois, "som grave descendo ao perder uma vida")
        n_antes = s.count("banana: nova banana")
        time.sleep(2)
        verificar(m.serial().count("banana: nova banana") > n_antes, "o jogo continua enquanto o bip toca")
    return 0


if __name__ == "__main__":
    sys.exit(main())
