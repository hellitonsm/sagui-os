#!/usr/bin/env python3
"""H38 - interpretador Lua nativo.

O /bin/lua roda no anel 3, sem libc e sem o Lua oficial. Este teste executa
scripts embutidos em /lua e confere o resultado do teste lua_nativo, que roda
tudo dentro do proprio sistema (inclusive a captura da saida).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    with Maquina('h38-lua', 'ide', imagem=imagem_de_teste('/testes/lua_nativo')) as m:
        m.esperar_serial('[LUA] script basico e laco numerico', 120)
        m.esperar_serial('[LUA] funcoes, tabelas e bibliotecas', 120)
        m.esperar_serial('[LUA] extensao sagui chamada do script', 120)
        m.esperar_serial('[LUA] arquivos e dofile', 120)
        m.esperar_serial('[LUA] erro de execucao', 120)
        m.esperar_serial('[LUA] erro de sintaxe', 120)
        m.esperar_serial('[LUA] -e e -v', 120)
        m.esperar_serial('[LUA] interpretador nativo validado', 120)
        m.esperar_serial('(lua_nativo) terminou com codigo 0', 60)
        s = m.serial()
        verificar('[LUA] interpretador nativo validado' in s,
                  'scripts, bibliotecas, arquivos e erros validados no convidado')
        m.captura('H38-lua-nativo')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
