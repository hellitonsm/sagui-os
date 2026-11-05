#!/usr/bin/env python3
"""H01 - ambiente.

Critério: `make` roda sem erro com um alvo vazio; o painel abre no navegador.
O painel é conferido executando o JavaScript dele no Node, com um DOM
simulado, sobre o status.js real (se o Node existir na máquina).
"""
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import verificar, RAIZ, SAIDA  # noqa: E402

DOM_FALSO = """
const els = {};
const mk = () => ({textContent: "", innerHTML: "", hidden: false, style: {}, classList: {toggle() {}}, querySelector() { return mk(); }});
globalThis.document = {getElementById: id => els[id] || (els[id] = mk()), addEventListener() {}, createElement() { return {}; }, head: {appendChild() {}}};
globalThis.window = globalThis; globalThis.setInterval = () => {}; globalThis.fetch = () => new Promise(() => {});
"""


def main():
    r = subprocess.run(["make", "-s", "-C", RAIZ, "vazio"], capture_output=True, text=True)
    verificar(r.returncode == 0 and "alvo vazio ok" in r.stdout, "make vazio roda sem erro")
    for f in ("index.html", "status.json", "status.js"):
        verificar(os.path.exists(os.path.join(RAIZ, "dashboard", f)), f"dashboard/{f} existe")
    html = open(os.path.join(RAIZ, "dashboard", "index.html"), encoding="utf-8").read()
    verificar(not re.search(r'(src|href)="https?://', html), "painel sem dependências externas")
    if shutil.which("node"):
        js = re.findall(r"<script>(.*?)</script>", html, re.S)[0]
        os.makedirs(SAIDA, exist_ok=True)
        arq = os.path.join(SAIDA, "painel_teste.js")
        with open(arq, "w", encoding="utf-8") as f:
            f.write(DOM_FALSO + js + f'\nrequire({os.path.join(RAIZ, "dashboard", "status.js")!r}); '
                    'desenhar(window.SAGUI_STATUS); console.log("PAINEL_OK " + els.historias.innerHTML.length);')
        r = subprocess.run(["node", arq], capture_output=True, text=True)
        verificar("PAINEL_OK" in r.stdout, "JavaScript do painel desenha todas as seções sem erro")
    return 0


if __name__ == "__main__":
    sys.exit(main())
