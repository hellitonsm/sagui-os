#!/usr/bin/env python3
"""Compilacao de subconjunto Go dentro do SaguiOS e execucao do ELF produzido."""
from qemu import Maquina, verificar, imagem_de_teste


def main():
    with Maquina('h37-go', 'ide', imagem=imagem_de_teste('/testes/go_nativo')) as m:
        m.esperar_serial('[GO] compilacao nativa, ELF e saida validados', 60)
        m.esperar_serial('(go_nativo) terminou com codigo 0', 30)
        verificar('[GO] compilacao nativa, ELF e saida validados' in m.serial(),
                  'compilador nativo, rejeicao sem truncar e executavel gerado')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
