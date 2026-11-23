#!/usr/bin/env python3
"""H15 - pipes.

Critério: teste liga dois processos por um pipe e transfere um texto maior
que o buffer sem perder nem duplicar dados.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    with Maquina("h15", "ide", imagem=imagem_de_teste("/testes/pipe")) as m:
        m.esperar_serial("[TESTE] programa terminou", 120)
        s = m.serial().replace("\r", "")
        for l in s.splitlines():
            if l.strip().startswith(("[ok]", "[FALHA]", "recebidos", "levou", "consumidor:")):
                print("   ", l.strip())
        verificar("recebidos 20000 bytes" in s, "20000 bytes atravessaram um pipe de 512 bytes")
        verificar("consumidor: recebi 20000 bytes (esperava 20000), 0 diferentes" in s,
                  "no outro sentido também (escritor bloqueando com o pipe cheio)")
        verificar("RESULTADO pipe: SUCESSO" in s, "teste de pipes passou")
        m.captura("H15-pipes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
