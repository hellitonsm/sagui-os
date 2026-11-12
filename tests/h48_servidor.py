#!/usr/bin/env python3
"""H48 - servidor de arquivos do SaguiOS pela rede (SAGUI/1, porta 2500).

O kernel escuta TCP na porta 2500 (kernel/rede/servidor.c); o comando
"servidor" liga as fatias de atendimento e o cliente do Windows
(tools/windows/sagui-fs.ps1) chega pelo encaminhamento de porta da NAT do
VirtualBox. Aqui quem conecta é o próprio teste, por um hostfwd do QEMU que
faz o papel do redirecionamento — e fala o mesmo protocolo do sagui-fs.ps1.

Critério: com o servidor ligado no convidado,
  - o cliente conecta, recebe o banner SAGUI/1 e a conexão veio do host
    (10.0.2.2, o lado da NAT do QEMU);
  - LIST, INFO, GET, PUT, RM, MKDIR, RMDIR e REN funcionam; um pedido
    errado devolve "ERRO" em vez de derrubar a conexão;
  - o GET devolve byte a byte o que está no rootfs/, em arquivos maiores
    que um segmento (1400 bytes: o TCP divide e confirma cada pedaço);
  - o PUT grava e o GET lê de volta o mesmo conteúdo;
  - uma segunda conexão também é atendida (o servidor serve uma por fatia
    de chamada de sistema);
  - depois do q o shell volta ao prompt e o wget volta a funcionar: o
    cadeado da pilha de rede é liberado quando o servidor encerra.
"""
import os
import re
import socket
import sys
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA, RAIZ, FalhaTeste  # noqa: E402

PORTA = 2500          # onde o SaguiOS escuta (SERVIDOR_PORTA)
PAGINA_HTTP = "ola do lado de fora do SaguiOS\n"

# Maior que a fila de recepção do servidor (4096 bytes): o PUT atravessa
# mais de uma rodada da janela que o servidor anuncia.
DADOS_PUT = b"".join(f"linha {i:03d} enviada pelo teste h48\n".encode("latin-1")
                     for i in range(300))


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


def conteudo_imagem(caminho):
    """O que o mksgfs põe na imagem: .txt/.md/.cfg vêm em UTF-8 no rootfs/ e
    são convertidos para Latin-1 (o idioma do SaguiOS); o resto é igual."""
    with open(os.path.join(RAIZ, "rootfs", caminho), "rb") as f:
        dados = f.read()
    if os.path.splitext(caminho)[1].lower() in {".txt", ".md", ".cfg"}:
        return dados.decode("utf-8").encode("latin-1")
    return dados


class Cliente:
    """O protocolo SAGUI/1, do mesmo jeito que o sagui-fs.ps1 (a norma é o
    kernel/rede/servidor.c). Conecta com tentativas: o SYN pode chegar numa
    fresta entre duas fatias do servidor e o teste não deve desistir."""

    def __init__(self, porta):
        fim = time.time() + 20
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", porta), timeout=10)
                break
            except OSError:
                if time.time() > fim:
                    raise FalhaTeste(f"não conectou no servidor de arquivos (porta {porta})")
                time.sleep(0.5)
        self.sock.settimeout(60)
        self.arq = self.sock.makefile("rb")
        banner = self.linha()
        if banner != "SAGUI/1":
            raise FalhaTeste(f"banner inesperado: {banner!r}")

    def __enter__(self):
        return self

    def __exit__(self, *a):
        try:
            self.arq.close()
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass

    def linha(self):
        dados = self.arq.readline()
        if not dados:
            raise FalhaTeste("a conexão caiu antes da resposta")
        return dados.decode("latin-1").rstrip("\r\n")

    def resposta(self, padrao):
        linha = self.linha()
        if linha.startswith("ERRO "):
            raise FalhaTeste(f"o servidor devolveu {linha!r}")
        m = re.fullmatch(padrao, linha)
        if not m:
            raise FalhaTeste(f"resposta inesperada (esperava {padrao}): {linha!r}")
        return m

    def mandar(self, texto):
        self.sock.sendall(texto.encode("latin-1") + b"\n")

    def listar(self, pasta="/"):
        self.mandar(f"LIST {pasta}")
        n = int(self.resposta(r"ENTRADAS (\d+)").group(1))
        entradas = {}
        for _ in range(n):
            m = self.resposta(r"([APX]) (\d+) (.*)")
            entradas[m.group(3)] = (m.group(1), int(m.group(2)))
        return entradas

    def info(self, caminho):
        self.mandar(f"INFO {caminho}")
        m = self.resposta(r"TIPO ([APX]) (\d+)")
        return m.group(1), int(m.group(2))

    def baixar(self, caminho):
        self.mandar(f"GET {caminho}")
        n = int(self.resposta(r"DADOS (\d+)").group(1))
        dados = self.arq.read(n)
        if len(dados) != n:
            raise FalhaTeste("a conexão caiu no meio dos dados")
        return dados

    def enviar(self, caminho, dados):
        self.mandar(f"PUT {caminho} {len(dados)}")
        self.resposta(r"PRONTO")
        self.sock.sendall(dados)
        self.resposta(r"OK")

    def comando_ok(self, texto):
        self.mandar(texto)
        self.resposta(r"OK")

    def erro(self, texto, motivo):
        self.mandar(texto)
        linha = self.linha()
        verificar(linha.startswith("ERRO ") and motivo in linha,
                  f"{texto.split()[0]} de algo errado devolve ERRO ({linha!r})")

    def sair(self):
        self.mandar("SAIR")
        verificar(self.linha() == "TCHAU", "o SAIR devolve TCHAU e fecha a conexão")


def esperar_prompt(m, n_antes, tempo=60):
    """Espera um prompt novo (o serial já tem os prompts antigos)."""
    fim = time.time() + tempo
    while m.serial().count("sagui:") <= n_antes:
        if time.time() > fim:
            raise FalhaTeste("o sagsh não voltou ao prompt")
        time.sleep(0.2)


def main():
    leia_me = conteudo_imagem("leia-me.txt")     # 2675 bytes: 2 segmentos
    tetris = conteudo_imagem("jogos/tetris.c")   # 4588 bytes: 4 segmentos

    pasta = os.path.join(SAIDA, "h48-http")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "pagina.txt"), "w") as f:
        f.write(PAGINA_HTTP)
    porta_http = porta_livre()
    srv = servidor_http(porta_http, pasta)

    # No host, uma porta livre encaminhada para a 2500 do convidado (como a
    # regra "sagui-arquivos" da NAT do VirtualBox, mas sem bater com ela se
    # uma VM estiver ligada).
    porta_fs = porta_livre()

    try:
        with Maquina("h48", "ide", imagem=imagem_de_teste("", ["--vesa", "--console"]),
                     extras=["-netdev", f"user,id=net0,hostfwd=tcp:127.0.0.1:{porta_fs}-:{PORTA}",
                             "-device", "e1000,netdev=net0"]) as m:
            m.esperar_serial("sagui:/$", 120)
            verificar("10.0.2.15" in m.serial() and "DHCP" in m.serial(),
                      "o endereço veio do DHCP antes do prompt (10.0.2.15)")

            # O servidor roda na frente: o shell fica bloqueado esperando o
            # filho, e as teclas chegam ao servidor pelo modo bruto.
            prompts = m.serial().count("sagui:")
            m.digitar("servidor")
            m.tecla("ret")
            m.esperar_serial("servidor: arquivos do SaguiOS na porta 2500", 30)

            with Cliente(porta_fs) as c:
                verificar("[SERVIDOR] conexao de 10.0.2.2" in m.serial(),
                          "a conexão veio do lado da NAT (o host é 10.0.2.2)")

                entradas = c.listar("/")
                verificar("leia-me.txt" in entradas and "bin" in entradas,
                          "o LIST mostra a raiz (leia-me.txt e bin)")
                verificar(entradas["leia-me.txt"] == ("A", len(leia_me)),
                          "o LIST devolve o tipo e o tamanho do leia-me.txt")
                verificar(entradas["bin"][0] == "P", "o LIST marca as pastas com P")

                verificar(c.info("leia-me.txt") == ("A", len(leia_me)),
                          "o INFO devolve o tipo e o tamanho do leia-me.txt")
                verificar(c.info("jogos")[0] == "P", "o INFO diz que jogos é pasta")

                c.erro("GET nao-existe.txt", "nao existe")
                c.erro("BLABLABLA", "comando desconhecido")

                verificar(c.baixar("leia-me.txt") == leia_me,
                          "o GET devolve o leia-me.txt byte a byte (2 segmentos)")
                verificar(c.baixar("jogos/tetris.c") == tetris,
                          "o GET devolve o tetris.c byte a byte (4 segmentos)")

                c.enviar("h48-enviado.txt", DADOS_PUT)
                verificar(c.baixar("h48-enviado.txt") == DADOS_PUT,
                          "o PUT gravou e o GET lê de volta o mesmo conteúdo")

                c.comando_ok("MKDIR h48-pasta")
                c.comando_ok("REN h48-pasta h48-mudou")
                entradas = c.listar("/")
                verificar("h48-mudou" in entradas and "h48-pasta" not in entradas,
                          "o REN renomeou a pasta")

                interno = b"conteudo gravado na pasta renomeada\n"
                c.enviar("h48-mudou/dentro.txt", interno)
                verificar(c.baixar("h48-mudou/dentro.txt") == interno,
                          "o PUT seguiu a pasta renomeada")

                c.comando_ok("RM h48-mudou/dentro.txt")
                c.comando_ok("RMDIR h48-mudou")
                verificar("h48-mudou" not in c.listar("/"),
                          "o RM e o RMDIR apagaram o que criaram")

                c.sair()

            # O servidor atende uma conexão por fatia: uma segunda conexão
            # logo depois da primeira tem que dar certo também.
            with Cliente(porta_fs) as c:
                entradas = c.listar("bin")
                verificar(entradas.get("servidor", ("?", 0))[0] == "X",
                          "a segunda conexão é atendida (e o servidor é X em /bin)")
                c.sair()

            m.tecla("q")
            m.esperar_serial("servidor: encerrado", 30)
            verificar("conexao 2 atendida" in m.serial(),
                      "o servidor atendeu as duas conexões, uma por fatia")
            esperar_prompt(m, prompts)

            out = m.comando("cat h48-enviado.txt")
            verificar("linha 000 enviada pelo teste h48" in out and
                      "linha 299 enviada pelo teste h48" in out,
                      "o cat confirma o arquivo que o PUT gravou")

            out = m.comando(f"wget http://10.0.2.2:{porta_http}/pagina.txt pagina.txt", tempo=90)
            verificar("Salvo" in out, "o wget funciona depois do servidor encerrar")
            out = m.comando("cat pagina.txt")
            verificar(PAGINA_HTTP.strip() in out, "o arquivo baixado pelo wget está correto")
    finally:
        srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
