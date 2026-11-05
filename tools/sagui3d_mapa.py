#!/usr/bin/env python3
"""Confere um arquivo de fase do Sagui 3D e desenha a planta vista de cima.

Uso: tools/sagui3d_mapa.py fase.txt [planta.png]

Faz as mesmas verificações que o jogo (salas convexas, pontos definidos,
coisas dentro das salas) e mais algumas que ajudam a desenhar fases:
lados que encostam em outra sala sem formar passagem (os pontos não batem)
e degraus altos demais para subir. É uma ferramenta de quem faz as fases;
não vai para dentro da imagem."""
import sys

DEGRAU = 24


def ler(caminho):
    pontos, salas, coisas, ajustes = {}, [], [], []
    info = {"nome": "", "minimo": 0, "sagui": None}
    for num, linha in enumerate(open(caminho, encoding="utf-8"), 1):
        linha = linha.split("#")[0].split()
        if not linha:
            continue
        cmd, args = linha[0], linha[1:]
        if cmd == "nome":
            info["nome"] = " ".join(args)
        elif cmd == "minimo":
            info["minimo"] = int(args[0])
        elif cmd == "ponto":
            pontos[int(args[0])] = (int(args[1]), int(args[2]))
        elif cmd == "sala":
            i = args.index(":")
            props = dict(zip(args[0:i:2], args[1:i:2]))
            salas.append({"props": props, "cantos": [int(a) for a in args[i + 1:]], "linha": num})
        elif cmd in ("lado", "secreta"):
            ajustes.append((cmd, int(args[0]), int(args[1])))
        elif cmd == "sagui":
            info["sagui"] = tuple(int(a) for a in args)
        else:
            coisas.append((cmd, int(args[0]), int(args[1])))
    return pontos, salas, coisas, ajustes, info


def area(p):
    return sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p)))


def dentro(poli, x, y):
    for i in range(len(poli)):
        (ax, ay), (bx, by) = poli[i], poli[(i + 1) % len(poli)]
        if (bx - ax) * (y - ay) - (by - ay) * (x - ax) < 0:
            return False
    return True


def conferir(caminho):
    pontos, salas, coisas, ajustes, info = ler(caminho)
    erros = []
    for s in salas:
        c = s["cantos"]
        for k in c:
            if k not in pontos:
                erros.append(f"linha {s['linha']}: ponto {k} não existe")
        if erros:
            continue
        if area([pontos[k] for k in c]) < 0:
            c.reverse()
        poli = [pontos[k] for k in c]
        for i in range(len(c)):
            (ax, ay), (bx, by), (cx, cy) = poli[i], poli[(i + 1) % len(c)], poli[(i + 2) % len(c)]
            if (bx - ax) * (cy - by) - (by - ay) * (cx - bx) < 0:
                erros.append(f"linha {s['linha']}: sala não convexa no ponto {c[(i + 1) % len(c)]}")
        s["poli"] = poli
        s["chao"] = int(s["props"].get("chao", 0))
        s["teto"] = int(s["props"].get("teto", 128))
    if erros:
        return erros, None
    lados = {}
    for i, s in enumerate(salas):
        c = s["cantos"]
        for k in range(len(c)):
            lados[(c[k], c[(k + 1) % len(c)])] = i
    passagens = 0
    for (a, b), i in lados.items():
        j = lados.get((b, a))
        if j is not None:
            passagens += 1
            d = salas[j]["chao"] - salas[i]["chao"]
            vao = min(salas[i]["teto"], salas[j]["teto"]) - max(salas[i]["chao"], salas[j]["chao"])
            if vao < 56:
                erros.append(f"aviso: passagem {a}-{b} baixa demais para o sagui ({vao})")
    for cmd, a, b in ajustes:
        if (a, b) not in lados and (b, a) not in lados:
            erros.append(f"{cmd} {a} {b}: nenhuma sala tem esse lado")
        if cmd == "secreta" and not ((a, b) in lados and (b, a) in lados):
            erros.append(f"secreta {a} {b}: não é passagem")
    for nome, x, y in coisas + ([("sagui", info["sagui"][0], info["sagui"][1])] if info["sagui"] else []):
        if not any(dentro(s["poli"], x, y) for s in salas):
            erros.append(f"{nome} em ({x}, {y}) está fora das salas")
    bananas = sum(1 for c in coisas if c[0] == "banana")
    resumo = (f"{info['nome']}: {len(salas)} salas, {passagens // 2} passagens, {bananas} bananas "
              f"(mínimo {info['minimo']}), {sum(1 for c in coisas if c[0] == 'macaco')} macacos")
    return erros, (pontos, salas, coisas, ajustes, info, lados, resumo)


def desenhar(dados, saida):
    from PIL import Image, ImageDraw
    pontos, salas, coisas, ajustes, info, lados, _ = dados
    xs = [p[0] for p in pontos.values()]
    ys = [p[1] for p in pontos.values()]
    x0, y0, x1, y1 = min(xs) - 40, min(ys) - 40, max(xs) + 40, max(ys) + 40
    esc = min(900 / (x1 - x0), 700 / (y1 - y0))
    img = Image.new("RGB", (int((x1 - x0) * esc), int((y1 - y0) * esc)), (20, 30, 24))
    d = ImageDraw.Draw(img)
    t = lambda x, y: ((x - x0) * esc, (y1 - y) * esc)
    secretas = {(a, b) for c, a, b in ajustes if c == "secreta"} | {(b, a) for c, a, b in ajustes if c == "secreta"}
    for s in salas:
        tom = max(0, min(255, 60 + s["chao"] * 2))
        d.polygon([t(*p) for p in s["poli"]], fill=(tom // 2, tom, tom // 2))
    for (a, b) in lados:
        cor = (240, 200, 60) if (a, b) in secretas else ((90, 160, 90) if (b, a) in lados else (230, 230, 220))
        d.line([t(*pontos[a]), t(*pontos[b])], fill=cor, width=3 if (b, a) not in lados else 1)
    cores = {"banana": (250, 220, 60), "cacho": (255, 170, 0), "macaco": (160, 100, 50), "porteira": (240, 80, 80), "pote": (120, 60, 140)}
    for nome, x, y in coisas:
        px, py = t(x, y)
        d.ellipse([px - 5, py - 5, px + 5, py + 5], fill=cores.get(nome, (255, 255, 255)))
    if info["sagui"]:
        px, py = t(info["sagui"][0], info["sagui"][1])
        d.ellipse([px - 7, py - 7, px + 7, py + 7], fill=(255, 255, 255))
    img.save(saida)


if __name__ == "__main__":
    erros, dados = conferir(sys.argv[1])
    for e in erros:
        print(e)
    if dados:
        print(dados[-1])
        if len(sys.argv) > 2:
            desenhar(dados, sys.argv[2])
    sys.exit(1 if any(not e.startswith("aviso") for e in erros) else 0)
