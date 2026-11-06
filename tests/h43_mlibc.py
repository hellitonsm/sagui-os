#!/usr/bin/env python3
"""mlibc: sysdeps (malloc/printf/open + obrigatorios) no convidado."""
from qemu import Maquina, verificar, imagem_de_teste


def main():
    with Maquina('h43-mlibc', 'ide', imagem=imagem_de_teste('/testes/mlibc_nativo')) as m:
        m.esperar_serial('[MLIBC] sysdeps validados', 120)
        m.esperar_serial('(mlibc_nativo) terminou com codigo 0', 60)
        verificar(True, 'mlibc sysdeps validados no convidado')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
