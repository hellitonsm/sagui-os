#!/usr/bin/env python3
"""H36 - link que demora a subir (o caso do VirtualBox).

Critério: o VirtualBox mantém o link dos seus modelos de placa (Intel PRO/1000
e AMD PCnet-FAST III) *fora do ar* por ~5 s depois do reset - o `LinkUpDelay`
do CFGM, 5000 ms por padrão. Enquanto o link não sobe, o controlador emulado
descarta silenciosamente todo quadro recebido e não transmite nada. Quem pede
DHCP logo depois de inicializar a placa fica sem endereço em silêncio.

Aqui o QEMU imita esse atraso com `set_link net0 off` + `set_link net0 on`
alguns segundos depois (o link já está no ar bem depois do reset, mas antes do
fim da espera do driver). O sistema tem que
  - esperar o link em vez de desistir na primeira leitura;
  - avisar no serial quanto tempo esperou;
  - ainda assim pegar DHCP, mostrar `Link: ligado` no comando `rede` e baixar
    uma página por HTTP.

O caso do link que só aparece *depois* do limite também é conferido: o boot
avisa, cai no endereço fixo da NAT e não trava; o comando `rede` mostra o link
de agora, não o do boot.
"""
import os
import socket
import sys
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402

CONTEUDO = "ola de dentro do VirtualBox com link atrasado\n"
ATRASO = 3.0          # segundos com o link no chão, durante o boot
ATRASO_LONGO = 8.0    # além da espera do driver (6 s): só o aviso


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


def com_link_atrasado(nome, placa, driver, atraso):
    m = Maquina(nome, "ide", imagem=imagem_de_teste("", ["--vesa", "--console"]),
                extras=["-netdev", "user,id=net0", "-device", placa + ",netdev=net0"])
    m.ligar()
    # derruba o link antes do driver ler o estado e levanta só depois
    m.monitor("set_link net0 off")
    time.sleep(atraso)
    m.monitor("set_link net0 on")
    return m


def conferir_placa(placa, driver, porta, pasta):
    print(f"== placa {driver}, link sobe depois de {ATRASO:.0f} s")
    m = com_link_atrasado("h36-" + driver, placa, driver, ATRASO)
    try:
        m.esperar_serial("sagui:/$", 180)
        s = m.serial()
        verificar("link ligado depois de" in s,
                  f"[{driver}] o driver esperou o link em vez de desistir")
        verificar("10.0.2.15" in s and "DHCP" in s,
                  f"[{driver}] mesmo com o link atrasado o DHCP deu endereço")
        verificar("[aviso] Rede: link sem sinal" not in s,
                  f"[{driver}] o boot não avisou de cabo desconectado")

        out = m.comando("rede")
        verificar("Link.....: ligado" in out, f"[{driver}] rede mostra o link ligado")
        verificar("Quadros..: " in out and "0 enviados, 0 recebidos" not in out,
                  f"[{driver}] os quadros do DHCP foram enviados e recebidos")

        baixou = m.comando(f"wget http://10.0.2.2:{porta}/arquivo pagina.txt", tempo=120)
        verificar("Salvo" in baixou,
                  f"[{driver}] o wget falou com o servidor depois do atraso")
        lido = m.comando("cat pagina.txt")
        verificar(CONTEUDO.strip() in lido, f"[{driver}] o arquivo baixado está correto")
    finally:
        m.desligar()


def conferir_link_tardio(placa, driver):
    print(f"== placa {driver}, link só sobe depois da espera ({ATRASO_LONGO:.0f} s)")
    m = com_link_atrasado("h36-tarde-" + driver, placa, driver, ATRASO_LONGO)
    try:
        m.esperar_serial("sagui:/$", 180)
        s = m.serial()
        verificar("link sem sinal depois de" in s,
                  f"[{driver}] o boot avisou que o link não subiu")
        verificar("sagui:/$" in s and "endereco fixo" in s,
                  f"[{driver}] o sistema não travou: seguiu com o endereço fixo da NAT")
        time.sleep(1.0)
        out = m.comando("rede")
        verificar("Link.....: ligado" in out,
                  f"[{driver}] o comando rede relê o link em vez de mostrar o valor do boot")
    finally:
        m.desligar()


def principal():
    pasta = os.path.join(SAIDA, "h36-servidor")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "arquivo"), "w") as f:
        f.write(CONTEUDO)
    porta = porta_livre()
    srv = servidor_http(porta, pasta)
    try:
        conferir_placa("e1000", "e1000", porta, pasta)
        conferir_placa("pcnet", "pcnet", porta, pasta)
        conferir_link_tardio("pcnet", "pcnet")
    finally:
        srv.shutdown()


if __name__ == "__main__":
    principal()
