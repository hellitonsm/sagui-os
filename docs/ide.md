# IDE do SaguiOS

A IDE é o editor de código do sistema e o **programa padrão** para abrir
arquivos `.go`, `.lua`, `.c`, `.h` e `.mk`: no Terminal, digitar `ola.go`
(ou `/jogos/grafico/sudoku.mk`) já abre a IDE; no **Arquivos**, o duplo clique
nesses arquivos faz o mesmo. Arquivos de texto comuns continuam abrindo no
`editor`.

No Terminal também dá para abrir direto: `ide` ou `ide /go/ola.go`. A janela
tem barra de ações, área de código com **números de linha** e **realce de
sintaxe**, barra de status e painel de saída.

## Editar

- **Novo**, **Abrir**, **Salvar**, **Salvar como**.
- **Modelos** carrega um exemplo editável de Go, Lua, C (tinycc) ou Makefile
  (`.mk`). Salve com a extensão certa (por exemplo, `/tmp/meu.c`).
- **Localizar** (Ctrl+F) procura o texto a partir do cursor e destaca o
  trecho; **Ir p/ linha** (Ctrl+G) salta para uma linha.
- Ctrl+S salva. O título mostra o nome do arquivo e um `*` quando há
  alteração não salva; fechar com alterações pergunta se quer salvar.
- A área de código tem auto-indentação (Enter copia os espaços da linha e
  abre mais quatro após `{`, `[` ou `(`), números de linha e realce de
  palavras-chave, strings, números e comentários de C, Go, Lua e `.mk`.
- A barra de status mostra a linguagem, a linha e a coluna do cursor, o
  tamanho e o estado de modificação.

O texto usa Latin-1, como em todo o sistema; prefira ASCII em código Go.

## Executar

**Executar** salva as alterações antes de compilar e executa o resultado;
**Parar** interrompe o processo em andamento. A saída e os erros aparecem no
painel inferior e o código de saída é exibido ao final.

| Arquivo | Como roda |
| --- | --- |
| `.go` | `gosagui` compila para um ELF temporário em `/tmp`, executado só se a compilação der certo e removido ao terminar. |
| `.lua` | `/bin/lua` interpreta o script; não produz executável. |
| `.c` / `.h` | `tinycc` compila para `/tmp` e o executável é rodado em seguida. |
| `.mk` | `/bin/make` compila a receita (a partir da pasta atual); com `EXECUTAR = sim` o próprio `make` roda o programa, senão a IDE executa a `SAIDA` gerada. |

Go e Lua são implementações de subconjuntos, não as ferramentas oficiais:
veja [Go](go.md) e [Lua](lua.md). O C usa o TinyCC completo descrito em
[tcc.md](tcc.md); as receitas `.mk` estão em
[tcc.md](tcc.md#make-e-arquivos-mk).

O painel mostra apenas as últimas linhas, não aceita entrada interativa e é
indicado para programas não interativos. Arquivos no SGFS e em `/tmp` não
persistem após reiniciar; use um volume FAT32 para persistência.

Exemplos de importação embutida: abra `/go/importacoes.go` ou
`/lua/importacoes.lua` e clique em **Executar**.
