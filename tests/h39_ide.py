#!/usr/bin/env python3
"""IDE: executar exemplos Go e Lua na máquina convidada."""
import re
import time
from qemu import Maquina, verificar, abrir_terminal


def main():
    with Maquina('h39-ide', 'ide') as m:
        m.esperar_serial('area de trabalho no ar', 120)
        abrir_terminal(m)   # o Terminal agora abre pelo ícone (D039)
        m.comando('ide /go/ola.go')
        m.esperar_serial('ide: pronto', 30)
        match = re.findall(r'criada por \d+: "IDE - sem nome" (\d+)x(\d+) em \((\d+),(\d+)\)', m.serial())[-1]
        x, y = int(match[2]), int(match[3])
        # Conteúdo da janela começa depois da borda e da barra de título.
        m.clique(x + 2 + 290, y + 24 + 52)
        m.esperar_serial('Go no SaguiOS!', 60)
        m.esperar_serial('Terminado (codigo 0)', 60)
        verificar('Compilado. Executando...' in m.serial(), 'Go compilado e executado')
        m.clique(x + int(match[0]) + 4 - 2 - 20 + 8, y + 12)
        m.esperar_serial('ide: fechado', 20)
        time.sleep(1)
        m.comando('ide /lua/ola.lua')
        m.esperar_serial('ide: aberto /lua/ola.lua', 30)
        match = re.findall(r'criada por \d+: "IDE - sem nome" (\d+)x(\d+) em \((\d+),(\d+)\)', m.serial())[-1]
        x, y = int(match[2]), int(match[3])
        m.clique(x + 2 + 290, y + 24 + 52)
        m.esperar_serial('soma', 60)
        verificar('Interpretando Lua...' in m.serial(), 'Lua interpretado')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
