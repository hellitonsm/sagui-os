#!/usr/bin/env python3
"""H35 - rede no VirtualBox: Intel PRO/1000 MT Desktop e AMD PCnet-FAST III.

Critério: nas duas placas que o VirtualBox oferece (e que o QEMU imita com
`-device e1000` e `-device pcnet`), o SaguiOS
  - reconhece o modelo pelo PCI e mostra o MAC;
  - pega endereço por DHCP (o mesmo esquema da NAT do VirtualBox: 10.0.2.15);
  - resolve ARP do próximo salto (sem MAC fixo do slirp do QEMU);
  - baixa uma página por HTTP com o `wget` e grava num arquivo.
Também confere o caso sem placa nenhuma: o sistema avisa e segue normal
(sem travar no DHCP).
"""
import os
import socket
import sys
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402

CONTEUDO = "ola de dentro do VirtualBox\n"


def porta_livre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def servidor_http(porta, pasta):
    class SemLog(SimpleHTTPRequestHandler):
        def __init__(self, *a, **k):
            super().__init__(*a, directory=pasta, **k)

        def log_message(self, *a):
            pass

    srv = ThreadingHTTPServer(("127.0.0.1", porta), SemLog)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def conferir_placa(nome, placa_extra, driver, porta):
    print(f"== placa {driver}")
    with Maquina("h35-" + driver, "ide", imagem=imagem_de_teste("", ["--vesa", "--console"]),
                 extras=["-netdev", "user,id=net0"] + placa_extra) as m:
        m.esperar_serial("sagui:/$", 120)
        s = m.serial()
        verificar(f"Rede: placa {driver}" in s, f"[{driver}] o driver foi escolhido pelo PCI")
        verificar("Rede: Intel" in s or "Rede: AMD PCnet" in s, f"[{driver}] a placa foi inicializada")
        verificar("10.0.2.15" in s and "DHCP" in s, f"[{driver}] o endereço veio do DHCP (10.0.2.15)")

        out = m.comando("rede")
        verificar("Placa....: " + driver in out, f"[{driver}] o comando rede mostra a placa")
        verificar("Endereco.: 10.0.2.15/24 (DHCP)" in out, f"[{driver}] rede mostra o endereço /24 do DHCP")
        verificar("Gateway..: 10.0.2.2" in out, f"[{driver}] rede mostra o gateway")
        verificar("Link.....: ligado" in out, f"[{driver}] rede mostra o link ligado")

        out = m.comando(f"wget http://10.0.2.2:{porta}/pagina.txt pagina.txt", tempo=90)
        verificar("Salvo" in out, f"[{driver}] wget baixou a página (ARP + TCP + HTTP)")
        out = m.comando("cat pagina.txt")
        verificar(CONTEUDO.strip() in out, f"[{driver}] o arquivo baixado está correto")


def main():
    pasta = os.path.join(SAIDA, "h35-servidor")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "pagina.txt"), "w") as f:
        f.write(CONTEUDO)
    porta = porta_livre()
    srv = servidor_http(porta, pasta)
    try:
        conferir_placa("Intel", ["-device", "e1000,netdev=net0"], "e1000", porta)
        conferir_placa("AMD", ["-device", "pcnet,netdev=net0"], "pcnet", porta)

        print("== sem placa")
        with Maquina("h35-sem", "ide", imagem=imagem_de_teste("", ["--vesa", "--console"]),
                     extras=["-nic", "none"]) as m:
            m.esperar_serial("sagui:/$", 120)
            verificar("Nenhuma placa de rede conhecida" in m.serial(),
                      "sem placa, o boot avisa e continua")
            out = m.comando("rede")
            verificar("nenhuma placa conhecida" in out, "o comando rede explica o que falta")
    finally:
        srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
