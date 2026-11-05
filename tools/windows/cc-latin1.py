#!/usr/bin/env python3
"""
cc-latin1.py - compilador de apoio para montar o SaguiOS no Windows.

O GCC cruzado pré-compilado para Windows (i686-elf) foi construído sem
libiconv, então `-fexec-charset=ISO-8859-1` falha com "no iconv implementation".
No Linux isso funciona e os textos do sistema saem em Latin-1 (PRD 4.4).

Este script faz o mesmo sem iconv: lê o .c em UTF-8, troca cada caractere não
ASCII que esteja DENTRO de um literal de texto/caractere por um escape octal
(\343 para 0xE3, por exemplo) e compila o resultado como ASCII. Assim os bytes
que vão para o binário são exatamente os de Latin-1, como no build do Linux.
Comentários continuam em UTF-8 (o GCC lê UTF-8 por padrão e não gera código
para eles).

É usado como CC pelo Makefile:
    CC="python tools/windows/cc-latin1.py"
"""
import os
import shutil
import subprocess
import sys


def transcrever(texto, nome):
    saida = []
    i = 0
    n = len(texto)
    estado = "codigo"  # codigo | linha | bloco | texto | caractere

    while i < n:
        c = texto[i]

        if estado == "codigo":
            if c == "/" and i + 1 < n and texto[i + 1] == "/":
                estado = "linha"
                saida.append("//")
                i += 2
                continue
            if c == "/" and i + 1 < n and texto[i + 1] == "*":
                estado = "bloco"
                saida.append("/*")
                i += 2
                continue
            if c == '"':
                estado = "texto"
                saida.append('"')
                i += 1
                continue
            if c == "'":
                estado = "caractere"
                saida.append("'")
                i += 1
                continue
            saida.append(c)
            i += 1
            continue

        if estado == "linha":
            if c == "\n":
                estado = "codigo"
            saida.append(c)
            i += 1
            continue

        if estado == "bloco":
            if c == "*" and i + 1 < n and texto[i + 1] == "/":
                estado = "codigo"
                saida.append("*/")
                i += 2
                continue
            saida.append(c)
            i += 1
            continue

        # Dentro de "..." ou '...'
        if c == "\\":
            if i + 1 < n:
                saida.append(texto[i:i + 2])
                i += 2
                continue
            saida.append(c)
            i += 1
            continue
        if (estado == "texto" and c == '"') or (estado == "caractere" and c == "'"):
            estado = "codigo"
            saida.append(c)
            i += 1
            continue
        if c == "\n":
            # literal não terminado: deixa o compilador dar o erro
            estado = "codigo"
            saida.append(c)
            i += 1
            continue

        cp = ord(c)
        if cp > 0xFF:
            sys.exit(f"cc-latin1: {nome}: caractere {c!r} (U+{cp:04X}) não cabe em Latin-1")
        if cp > 0x7F:
            # octal de 3 dígitos: não "engole" o caractere seguinte como \x faria
            saida.append("\\%03o" % cp)
            i += 1
            continue
        saida.append(c)
        i += 1

    return "".join(saida)


def main():
    args = list(sys.argv[1:])

    # tira o -fexec-charset (o wrapper já entrega o texto pronto)
    limpos = []
    pular = False
    for a in args:
        if pular:
            pular = False
            continue
        if a == "-fexec-charset":
            pular = True
            continue
        if a.startswith("-fexec-charset="):
            continue
        limpos.append(a)
    args = limpos

    if not args or not args[-1].endswith(".c"):
        # não é uma compilação de .c: repassa como está
        gcc = shutil.which("i686-elf-gcc") or "i686-elf-gcc"
        sys.exit(subprocess.call([gcc] + args))

    fonte = args[-1]
    with open(fonte, "r", encoding="utf-8") as f:
        texto = f.read()
    convertido = transcrever(texto, fonte)

    gcc = shutil.which("i686-elf-gcc")
    if not gcc:
        sys.exit("cc-latin1: i686-elf-gcc não encontrado no PATH")

    # Compila da entrada padrão para não sujar a árvore com arquivos temporários.
    # Como não há "arquivo atual", o diretório do fonte original entra em
    # -iquote (é onde o GCC procuraria os #include "..." dele).
    origem_dir = os.path.dirname(os.path.abspath(fonte)).replace("\\", "/")
    novos = args[:-1] + ["-iquote", origem_dir, "-x", "c", "-"]
    # Comentários continuam em UTF-8 (o GCC lê UTF-8 por padrão); os literais
    # já viraram ASCII com escapes octais.
    resultado = subprocess.run([gcc] + novos, input=convertido.encode("utf-8"))
    sys.exit(resultado.returncode)


if __name__ == "__main__":
    main()
