#!/usr/bin/env python3
"""Bootstrap e importacao de bibliotecas no compilador Go nativo do SaguiOS."""
from qemu import Maquina, verificar, imagem_de_teste


def main():
    with Maquina('h37-go', 'ide', imagem=imagem_de_teste('/testes/go_nativo')) as m:
        m.esperar_serial('[GO] biblioteca importada', 60)
        m.esperar_serial('[GO] bootstrap, importacao e ELF validados', 60)
        m.esperar_serial('(go_nativo) terminou com codigo 0', 30)
        verificar('[GO] bootstrap, importacao e ELF validados' in m.serial(),
                  'bootstrap, importacao de biblioteca e ELF gerado no convidado')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
