# Kit gráfico no Lua do SaguiOS: `gui`

Além de compilar e montar programas dentro do sistema (`csagui`, `asagui`,
`gosagui`), o SaguiOS dá ao **Lua nativo** acesso direto ao mesmo kit gráfico
que os programas em C usam: janela, rótulo, botão, caixa de texto, área de
texto com rolagem, lista com seleção e diálogos. Tudo roda no anel 3, nos
pixels da própria janela do processo — o kernel só entrega a área de desenho
e os eventos.

Por que no Lua e não no C do `csagui`? O `csagui` é um subconjunto mínimo
(sem ponteiros, structs ou arrays), então não consegue chamar `jan_abrir` nem
percorrer estruturas. O interpretador Lua já é completo o bastante — tabelas,
laços, funções, `break` — e é o lugar natural para escrever programas gráficos
sem sair do sistema:

```text
lua /lua/gui.lua
lua /lua/relogio.lua
```

## Exemplos que vêm no rootfs

| Arquivo | O que mostra |
| --- | --- |
| `/lua/gui.lua` | Janela com rótulo, botão contador, caixa de texto e lista; diálogo ao ativar um item |
| `/lua/relogio.lua` | Relógio de uptime atualizando numa janela, com laço de eventos sem bloqueio |
| `/lua/ola.lua`, `/lua/recursos.lua`, ... | Os exemplos de console de sempre |

E para os outros compiladores nativos (sem gráficos, por enquanto):

| Arquivo | Compilador | O que mostra |
| --- | --- | --- |
| `/c/ola.c`, `/c/tabuada.c` | `csagui` | Laços, desvios e chamadas dentro do sistema |
| `/asm/ola.asm` | `asagui` | Syscalls `int 0x80` direto de assembly |
| `/go/ola.go`, `/go/biblioteca.go`, `/go/sobre.go` | `gosagui` | `fmt`, `sagui`, `init` e bootstrap próprio |

## A biblioteca `gui`

A tabela global `gui` também pode ser obtida com `require("gui")`. Os
identificadores de janela e de componentes são números (índices + 1; 0 ou
`nil` = inválido). Limites: 8 janelas, 24 botões, 12 caixas, 8 áreas de
texto e 8 listas de até 32 itens por processo.

### Janela e desenho

| Função | O que faz |
| --- | --- |
| `gui.janela(titulo, largura, altura)` | Abre a janela; devolve o id ou `nil` se faltou memória/janela |
| `gui.fechar(id)` | Fecha a janela e invalida os componentes dela |
| `gui.mostrar(id)` | Redesenha a janela inteira |
| `gui.mostrar_area(id, x, y, l, a)` | Redesenha só um retângulo |
| `gui.titulo(id, titulo)` | Muda o título da janela |
| `gui.retangulo(id, x, y, l, a, cor)` | Retângulo cheio |
| `gui.moldura(id, x, y, l, a, cor)` | Contorno de 1 pixel |
| `gui.linha(id, x0, y0, x1, y1, cor)` | Linha reta |
| `gui.texto(id, x, y, texto, cor)` | Texto com a fonte 8x16 do sistema |
| `gui.rotulo` | Apelido de `gui.texto` |
| `gui.relevo(id, x, y, l, a, afundado)` | Moldura em relevo (botões e campos) |
| `gui.largura_texto(texto)` | Largura em pixels do texto, na fonte 8x16 |

Cores são números (`0xRRGGBB`); o tema está em `gui.COR_FUNDO`,
`gui.COR_TEXTO`, `gui.COR_TEXTO_FRACO`, `gui.COR_BORDA`, `gui.COR_BOTAO`,
`gui.COR_BOTAO_APERTADO`, `gui.COR_DESTAQUE` (verde do SaguiOS),
`gui.COR_SELECAO` e `gui.COR_BRANCO`.

### Componentes

| Função | O que faz |
| --- | --- |
| `gui.botao(jid, x, y, l, a, texto)` | Cria e desenha um botão; devolve o id |
| `gui.botao_clicado(bid, evento)` | `true` quando o evento é um clique completo dentro do botão |
| `gui.caixa(jid, x, y, l, texto_inicial)` | Caixa de texto de uma linha; devolve o id |
| `gui.caixa_tratar(cid, evento)` | Trata o evento (foco, digitação); `true` no Enter |
| `gui.caixa_texto(cid)` | Texto atual da caixa |
| `gui.caixa_definir(cid, texto)` | Substitui o texto e redesenha |
| `gui.area(jid, x, y, l, a)` | Área de texto com rolagem; devolve o id |
| `gui.area_definir(aid, texto)` | Substitui o conteúdo e redesenha |
| `gui.area_texto(aid)` | Conteúdo atual |
| `gui.area_tratar(aid, evento)` | Trata o evento; `true` se o texto mudou |
| `gui.desenhar_area(aid)` | Redesenha a área |
| `gui.area_modificada(aid)` | `true` se o usuário editou o texto |
| `gui.lista(jid, x, y, l, a, itens)` | Lista com `itens` (tabela de textos); devolve o id |
| `gui.lista_tratar(lid, evento)` | Devolve `gui.LISTA_NADA`, `gui.LISTA_SELECIONOU` ou `gui.LISTA_ATIVOU` |
| `gui.lista_selecao(lid)` | Índice do item selecionado, **1-based** (0 = nenhum) |
| `gui.desenhar_lista(lid)` | Redesenha a lista |

### Diálogos (modais, esperam a resposta)

| Função | O que devolve |
| --- | --- |
| `gui.confirmar(titulo, pergunta)` | `true` para Sim, `false` para Não |
| `gui.mensagem(titulo, texto)` | Nada; fecha com OK, Enter ou Esc |
| `gui.pedir_texto(titulo, pergunta, inicial)` | O texto digitado, ou `nil` se cancelou |

### Eventos

`gui.evento(esperar)` devolve uma tabela com o próximo evento de **qualquer**
janela do processo, ou `nil` se não veio nenhum (`gui.evento(false)` não
bloqueia; o padrão é `true`):

```lua
local e = gui.evento(true)
-- e.tipo         1..5 (gui.EV_CLIQUE, EV_SOLTOU, EV_MOVER, EV_TECLA, EV_FECHAR)
-- e.nome         "clique", "soltou", "mover", "tecla" ou "fechar"
-- e.janela       id da janela no Lua (a mesma devolvida por gui.janela)
-- e.x, e.y       posição relativa à área de conteúdo
-- e.botoes       1 esquerdo, 2 direito
-- e.cliques      2 no duplo clique
-- e.tecla        caractere Latin-1, ou gui.TECLA_CIMA/.../TECLA_ESC (>= 0x100)
-- e.modificadores  1 Shift, 2 Ctrl
```

A mesma tabela é aceita pelos manipuladores (`gui.botao_clicado`,
`gui.caixa_tratar`, `gui.area_tratar`, `gui.lista_tratar`), então o laço
principal fica assim:

```lua
while true do
    local e = gui.evento(true)
    if e.tipo == gui.EV_FECHAR then break end
    if gui.botao_clicado(botao, e) then
        cliques = cliques + 1
    end
    if gui.caixa_tratar(caixa, e) then
        sagui.registrar("enter na caixa: " .. gui.caixa_texto(caixa))
    end
end
gui.fechar(j)
```

## Exemplo mínimo completo

```lua
local j = gui.janela("Ola", 240, 120)
local b = gui.botao(j, 20, 20, 200, 28, "Clique aqui")
gui.mostrar(j)
while true do
    local e = gui.evento(true)
    if e.tipo == gui.EV_FECHAR then break end
    if gui.botao_clicado(b, e) then
        gui.mensagem("Ola", "Botao clicado!")
    end
end
gui.fechar(j)
```

Salve como `/tmp/ola.lua` no sistema e rode com `lua /tmp/ola.lua`.

## Teste automático

`tests/h45_gui_lua.py` monta a imagem com `/testes/gui_lua` como autoteste do
kernel (criação de janela, botão e texto sem interação, `require("gui")` e as
constantes) e confere a saída pela serial. Rode, num ambiente com QEMU:

```sh
python3 tests/h45_gui_lua.py
```

Os programas interativos (`/lua/gui.lua`, `/lua/relogio.lua`) são feitos para
o olho humano; o `h22_componentes.py` já cobre o mesmo kit em C, com cliques
de mouse no QEMU.

## Limites

- O desenho é por software, direto nos pixels da janela — não há aceleração.
- Sem coleta de lixo: janelas e componentes vivem até `gui.fechar(id)` ou até
  o processo terminar. Fechar a janela invalida os componentes dela.
- O texto é Latin-1, como no resto do sistema; prefira ASCII ou acentos
  Latin-1 nos literais.
- `gui.evento` entrega eventos de todas as janelas do processo; use `e.janela`
  para distinguir.
- Não há campos de senha, barras de rolagem horizontais nem menus — o kit é o
  mesmo de `userland/inc/gui.h`, mais nada.
