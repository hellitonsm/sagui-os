#!/usr/bin/env python3
"""Roda todos os testes tests/hNN_*.py em ordem (ou os passados como argumento)."""
import glob
import os
import subprocess
import sys
import time

AQUI = os.path.dirname(os.path.abspath(__file__))


def main():
    filtros = sys.argv[1:]
    testes = sorted(glob.glob(os.path.join(AQUI, "h[0-9][0-9]*.py"))) + sorted(glob.glob(os.path.join(AQUI, "b[0-9]_*.py")))
    if filtros:
        testes = [t for t in testes if any(os.path.basename(t).startswith(f.lower()) for f in filtros)]
    # Compila antes: testar um binário velho depois de um erro de compilação
    # daria um resultado enganoso.
    raiz = os.path.dirname(AQUI)
    if subprocess.run(["make", "-s", "-C", raiz]).returncode != 0:
        print("ERRO: o build falhou; nenhum teste rodou")
        return 1
    falhas = []
    for t in testes:
        nome = os.path.basename(t)
        print(f"== {nome}", flush=True)
        ini = time.time()
        try:
            r = subprocess.run([sys.executable, t], timeout=600)
            ok = r.returncode == 0
        except subprocess.TimeoutExpired:
            ok = False
            print("  TEMPO ESGOTADO")
        print(f"   {'PASSOU' if ok else 'FALHOU'} ({time.time() - ini:.1f}s)", flush=True)
        if not ok:
            falhas.append(nome)
    print(f"\n{len(testes) - len(falhas)}/{len(testes)} testes passaram")
    if falhas:
        print("Falharam:", ", ".join(falhas))
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
