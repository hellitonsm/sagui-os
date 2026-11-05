#!/usr/bin/env python3
"""H07 - teclado PS/2 com ABNT2.

Critério: no QEMU, teclas enviadas pelo monitor aparecem corretas na tela,
incluindo ç. O teste cobre todas as teclas da tabela do PRD 4.4, as teclas
mortas (combinações válidas e inválidas), Shift, Caps Lock, AltGr e Ctrl.

O QEMU nomeia as teclas pela posição física. Num teclado ABNT2 a tecla na
posição do ';' americano é o ç, a do '[' é o acento agudo etc.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402

# (teclas do QEMU, o que o SaguiOS deve produzir em Latin-1, descrição)
ROTEIRO = [
    (["o", "l", "a"], "ola", "letras"),
    (["shift-a"], "A", "Shift"),
    (["semicolon"], "ç", "ç (0x27)"),
    (["shift-semicolon"], "Ç", "Ç"),
    (["bracket_left", "a"], "á", "´ + a"),
    (["bracket_left", "e"], "é", "´ + e"),
    (["bracket_left", "i", "bracket_left", "o", "bracket_left", "u"], "íóú", "´ + i o u"),
    (["shift-bracket_left", "a"], "à", "` + a"),
    (["shift-apostrophe", "a", "shift-apostrophe", "e", "shift-apostrophe", "o"], "âêô", "^ + a e o"),
    (["apostrophe", "a", "apostrophe", "o"], "ãõ", "~ + a o"),
    (["shift-6", "u"], "ü", "¨ + u"),
    (["bracket_left", "x"], "´x", "combinação inválida mostra acento e letra"),
    (["apostrophe", "spc"], "~", "morta + espaço = o próprio acento"),
    (["shift-bracket_left", "shift-a"], "À", "` + A"),
    (["grave_accent", "shift-grave_accent"], "'\"", "' e \" (0x29)"),
    (["bracket_right", "shift-bracket_right"], "[{", "[ { (0x1B)"),
    (["backslash", "shift-backslash"], "]}", "] } (0x2B)"),
    (["less", "shift-less"], "\\|", "\\ | (0x56)"),
    (["comma", "shift-comma", "dot", "shift-dot"], ",<.>", ", < . > (0x33, 0x34)"),
    (["slash", "shift-slash"], ";:", "; : (0x35)"),
    (["ro", "shift-ro"], "/?", "/ ? (0x73)"),
    (["shift-1", "shift-2", "shift-7", "minus", "shift-equal"], "!@&-+", "símbolos com Shift"),
    (["caps_lock", "b", "semicolon", "shift-c", "caps_lock", "d"], "BÇcd", "Caps Lock (letras e ç; Shift inverte)"),
    (["alt_r-q", "alt_r-e"], "/°", "AltGr (/ e °) = Alt da direita"),
    (["ctrl-c"], "\x03", "Ctrl+C vira o código 3"),
]


def main():
    img = imagem_de_teste("teclado")
    with Maquina("h07", "ide", imagem=img) as m:
        m.esperar_serial("[TESTE] teclado pronto", 60)
        esperado = ""
        for teclas, saida, _ in ROTEIRO:
            for t in teclas:
                m.tecla(t, 0.05)
            esperado += saida
        m.captura("H07-teclado-abnt2")
        m.tecla("ret")
        m.esperar_serial("[TESTE] teclado linha=", 30)
        linha = m.serial().split("[TESTE] teclado linha=")[1].split()[0]
        obtido = bytes.fromhex(linha).decode("latin-1")
        # Confere item a item para a mensagem de erro apontar a tecla certa.
        pos = 0
        for teclas, saida, desc in ROTEIRO:
            pedaco = obtido[pos:pos + len(saida)]
            verificar(pedaco == saida, f"{desc}: {saida!r} (obtido {pedaco!r})")
            pos += len(saida)
        verificar(obtido == esperado, "linha completa idêntica à esperada")
    return 0


if __name__ == "__main__":
    sys.exit(main())
