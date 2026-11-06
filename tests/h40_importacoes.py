#!/usr/bin/env python3
"""Importacoes Go e Lua executadas dentro do convidado."""
from qemu import Maquina, verificar, imagem_de_teste


def main():
    with Maquina('h40-importacoes', 'ide', imagem=imagem_de_teste('/testes/importacoes')) as m:
        m.esperar_serial('[IMPORT] bibliotecas embutidas validadas', 120)
        m.esperar_serial('(importacoes) terminou com codigo 0', 60)
        verificar(True, 'importacoes embutidas e erros validados no convidado')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
