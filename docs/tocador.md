# Tocador do SaguiOS

## O que e

`/bin/tocador` toca musica e video. Como o SaguiOS so tem o alto-falante
do PC (B3), sem codec, sem DAC e sem FPU, **nao ha MP3/MP4/AVI**: os
formatos sao `.mus` (audio) e `.vid` (video), ambos de texto.

```text
tocador                        # janela com /musicas e /videos
tocador /musicas/demo.mus      # toca no terminal (bip)
tocador --info /musicas/demo.mus
tocador /videos/demo.vid       # mostra caminho/quadros no terminal
```

No Arquivos, duplo clique num `.mus`/`.vid` abre o Tocador.

## Formato .mus

```text
# Titulo da musica
440 200 La
523 200 Do
0 100 (pausa)
```

- `freq` em Hz, `0` = pausa. Faixa: 20 a 20000 Hz.
- `duracao` em ms. Faixa: 10 a 10000 ms.
- No maximo 256 notas. O kernel toca em segundo plano; `Parar` silencia.

## Formato .vid

Quadros separados por `---`. Comandos por linha:

```text
# Titulo do video
R 10 10 200 60 2
T 20 30 Demo SaguiOS
---
R 10 10 200 60 3
T 20 30 Quadro 2
```

- `R x y larg alt c` desenha um retangulo. `c` = 0 a 7 (paleta 0-7).
- `T x y texto` desenha texto em branco.

## Testes

`tests/h41_tocador.py` roda `/testes/tocador_nativo` no QEMU: info e
toque de `.mus`, info de `.vid`, recusa de `.mp3` e arquivo faltando.
