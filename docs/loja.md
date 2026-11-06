# Loja de apps do SaguiOS

## O que e

`/bin/loja` e uma loja pequena. Por enquanto **nao ha loja on-line**:
o link fica configuravel para quando houver um servidor.

```text
loja                                    # janela
loja listar                             # fontes locais no terminal
loja instalar ola                       # instala /go/ola.go ou /lua/ola.lua
loja configurar http://loja.exemplo/   # grava o link em /config/loja.conf
loja link                               # mostra o link atual
loja atualizar                          # busca o link com HTTP e lista
```

## Fonte local

A loja lista os fontes Go de `/go` e Lua de `/lua` como apps:

| Origem | Instala para | Como roda |
| --- | --- | --- |
| `/go/nome.go` | `/store/nome` (ELF via `gosagui build -o`) | direto: `/store/nome` |
| `/lua/nome.lua` | `/store/nome.lua` (copia do script) | `lua /store/nome.lua` |

Pasta `/store`: e so para apps instalados pela loja, separada de `/bin`
(os programas do sistema). `loja listar` mostra quem esta instalado.

Pelo terminal: `loja instalar ola` (Go tem prioridade se houver os dois).
Pela janela: selecione e clique em **Instalar**; **Abrir** executa o
instalado.

## Link da loja on-line

O link fica em `/config/loja.conf`, linha `link=<url>`:

- Pela janela: botao **Configuracoes**.
- Pelo terminal: `loja configurar <url>` (use `loja configurar ""` para apagar).
- **Atualizar** faz `http_obter()` no link e lista as linhas `nome url`
  devolvidas, sem instalar nada ainda. Sem link, termina com erro.

## Testes

`tests/h42_loja.py` roda `/testes/loja_nativo` no QEMU: listar fontes
locais, instalar um app Go (compila e executa), instalar um app Lua,
gravar e reler o link.
