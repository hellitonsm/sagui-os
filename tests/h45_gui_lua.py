#!/usr/bin/env python3
"""H45 - kit grafico embutido no Lua nativo.

O /bin/lua ganhou a biblioteca "gui" (janela, botao, caixa, area, lista,
dialogos e eventos), ligada a libsagui no anel 3. Este teste executa
/testes/gui_lua no QEMU, que exercita a biblioteca sem interacao e confere
a saida.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    with Maquina("h45-gui-lua", "ide", imagem=imagem_de_teste("/testes/gui_lua", ["--vesa"])) as m:
        m.esperar_serial("[GUI] biblioteca gui embutida no Lua", 120)
        m.esperar_serial("[GUI] constantes de evento e de lista", 60)
        m.esperar_serial("[GUI] janela, botao e texto criados e fechados", 60)
        m.esperar_serial("[GUI] require('gui') funciona", 60)
        m.esperar_serial("[GUI] kit grafico do Lua validado", 60)
        m.esperar_serial("(gui_lua) terminou com codigo 0", 60)
        s = m.serial()
        verificar("[GUI] kit grafico do Lua validado" in s,
                  "janela, botao, caixa, lista, eventos e require('gui') validados no convidado")
        m.captura("H45-gui-lua")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
