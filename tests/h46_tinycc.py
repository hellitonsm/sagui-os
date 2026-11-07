#!/usr/bin/env python3
"""TinyCC rodando dentro do SaguiOS como compilador C nativo."""
from qemu import Maquina, verificar, imagem_de_teste


def main():
    with Maquina('h46-tinycc', 'ide', imagem=imagem_de_teste('/testes/tinycc')) as m:
        m.esperar_serial('[TINYCC] tinycc executa no convidado', 90)
        m.esperar_serial('[TINYCC] tinycc compila e executa C no convidado', 90)
        serial = m.serial()
        verificar('[TINYCC] tinycc executa no convidado' in serial,
                  'o tinycc executa no convidado')
        verificar('[TINYCC] tinycc compila e executa C no convidado' in serial,
                  'o tinycc compila e executa um programa C no convidado')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
