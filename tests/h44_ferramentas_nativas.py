#!/usr/bin/env python3
"""Assembler x86 reduzido e compilador C nativos do SaguiOS."""
from qemu import Maquina, verificar, imagem_de_teste


def main():
    with Maquina('h44-ferramentas', 'ide', imagem=imagem_de_teste('/testes/ferramentas_nativas')) as m:
        m.esperar_serial('[TOOLS] assembler, compilador C e ligador ELF relocavel validados', 90)
        m.esperar_serial('(ferramentas_nativas) terminou com codigo 0', 30)
        serial = m.serial()
        verificar('[TOOLS] assembler, compilador C e ligador ELF relocavel validados' in serial,
                  'assembler, compilador C e ligador relocavel executados dentro do convidado')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
