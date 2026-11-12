#!/usr/bin/env python3
"""H50 - grep com os recursos do grep do Linux.

Critério: o programa /bin/grep casa padrões BRE, ERE e literais, entende as
opções de seleção, prefixo, contexto, recursão, cores e códigos de saída, e
o teste no convidado termina com SUCESSO e sem nenhuma falha.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    with Maquina("h50", "ide", imagem=imagem_de_teste("/testes/h50_grep")) as m:
        m.esperar_serial("RESULTADO h50_grep", 120)
        m.esperar_serial("[TESTE] programa terminou", 30)
        s = m.serial().replace("\r", "")
        linhas = [l.strip() for l in s.splitlines()
                  if l.strip().startswith("[ok]") or l.strip().startswith("[FALHA]")]
        for l in linhas:
            print("   ", l)
        verificar(len(linhas) >= 50, f"{len(linhas)} verificações rodaram no convidado")
        verificar(not [l for l in linhas if l.startswith("[FALHA]")],
                  "nenhuma verificação falhou")
        verificar("RESULTADO h50_grep: SUCESSO (0 falhas)" in s,
                  "teste do grep passou")
        verificar("(h50_grep) terminou com codigo 0" in s,
                  "programa de teste saiu com código 0")
        m.captura("H50-grep")
    return 0


if __name__ == "__main__":
    sys.exit(main())
