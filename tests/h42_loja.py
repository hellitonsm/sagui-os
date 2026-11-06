#!/usr/bin/env python3
"""Loja: fonte local, instalacao Go/Lua e link no convidado."""
from qemu import Maquina, verificar, imagem_de_teste


def main():
    with Maquina('h42-loja', 'ide', imagem=imagem_de_teste('/testes/loja_nativo')) as m:
        m.esperar_serial('[LOJA] fonte local, instalacao Go e link validados', 120)
        m.esperar_serial('(loja_nativo) terminou com codigo 0', 60)
        verificar(True, 'loja de apps validada no convidado')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
