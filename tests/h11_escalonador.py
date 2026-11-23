#!/usr/bin/env python3
"""H11 - processos e escalonador.

Critério: duas tarefas de teste imprimem A e B alternados na serial sem
cooperar entre si (cada uma só escreve e gasta CPU; quem alterna é o
escalonador, no tique do relógio).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    with Maquina("h11", "ide", imagem=imagem_de_teste("tarefas")) as m:
        m.esperar_serial("[TESTE] tarefas ", 120)
        s = m.serial()
        falhas = [l for l in s.splitlines() if "[TESTE] falha" in l]
        verificar(not falhas, f"nenhuma falha interna {falhas}")
        # As letras vão do "saida=" até o fim do teste; no meio pode cair a
        # mensagem de término da tarefa que acabou primeiro, que é descartada.
        trecho = s.split("[TESTE] saida=")[1].split("[TESTE]")[0]
        trecho = re.sub(r"\[SAGUI\][^\n]*\n", "", trecho)
        saida = "".join(c for c in trecho if c in "AB")
        verificar(saida.count("A") == 60 and saida.count("B") == 60, "cada tarefa escreveu suas 60 letras")
        trechos = re.findall(r"A+|B+", saida)
        print("   sequência:", saida[:70], "...")
        verificar(len(trechos) >= 20, f"A e B se alternaram {len(trechos)} vezes (preempção pelo relógio)")
        verificar(max(len(t) for t in trechos) <= 4, "nenhuma tarefa ficou com a CPU mais que uma fatia (2 tiques)")
        verificar("[TESTE] tarefas ok" in s, "tarefas terminaram e foram recolhidas")
        m.captura("H11-tarefas-A-B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
