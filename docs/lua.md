# Lua no SaguiOS: interpretador nativo

## Estado real

A imagem inclui `/bin/lua`, um interpretador **nativo de um subconjunto de
Lua**, escrito em C freestanding e rodando no anel 3. Ele lê um arquivo fonte
e executa direto sobre a árvore sintática: não depende de `gcc`, de uma
biblioteca externa nem do Lua oficial dentro do sistema, e não gera ELF (ao
contrário do `gosagui`).

```text
lua /lua/ola.lua
lua /lua/recursos.lua
lua -e "print(2 + 3)"
lua -v
lua                 # modo interativo (REPL)
```

Os exemplos ficam em `/lua`: `ola.lua` (básico), `recursos.lua` (funções,
tabelas, bibliotecas), `arquivos.lua` (leitura/escrita e `dofile`), `erro.lua`
e `sintaxe.lua` (mensagens de erro).

**Isto não é o Lua oficial.** É um subconjunto pequeno, com números inteiros
de 32 bits, sem coleta de lixo, sem corrotinas, sem metatabelas e sem
carregamento de bibliotecas externas. A seção "Limites" lista o que fica de
fora.

## O que funciona

### Linguagem

- Valores `nil`, booleanos, números, textos, tabelas e funções.
- Variáveis globais e locais, `local` com várias atribuições e `local function`.
- Atribuição múltipla (`a, b = f()`), campos (`t.x`) e índices (`t[k]`).
- Funções com parâmetros, `...` (varargs), múltiplos retornos e recursão.
- Fechamentos: a função interna enxerga e altera as locais da externa.
- Chamadas de método (`obj:metodo(args)`) e funções anônimas.
- `if`/`elseif`/`else`, `while`, `repeat`/`until`, `for` numérico (com passo)
  e `for` genérico (`for k, v in pairs(t)`), com `break` e `return`.
- Operadores `+ - * / // % ^`, comparações, `and`, `or`, `not`, `#` e `..`.
- Comentários de linha (`--`) e de bloco (`--[[ ... ]]`), textos longos
  (`[[ ... ]]`) e escapes (`\n`, `\t`, `\xNN`, `\ddd`, `\z`).

Como o PRD proíbe a FPU, **não há ponto flutuante**: todos os números são
inteiros de 32 bits e `/` e `//` fazem divisão inteira. Um literal com `.` ou
expoente é recusado com mensagem clara.

### Biblioteca padrão (subconjunto)

- Base: `print`, `type`, `tostring`, `tonumber`, `assert`, `error`, `select`,
  `next`, `pairs`, `ipairs`, `rawequal`, `rawget`, `rawset`, `unpack`,
  `dofile`, `arg`.
- `string`: `len`, `sub`, `upper`, `lower`, `rep`, `find` (texto literal, sem
  padrões), `reverse`, `byte`, `char`, `format` (`%s %c %d %i %u %x %X %q` e
  largura/preenchimento). Métodos também funcionam: `("abc"):upper()`.
- `table`: `insert`, `remove`, `concat`, `sort`, `unpack`.
- `math`: `abs`, `max`, `min`, `random`, `randomseed`, `tointeger`.
- `os`: `time`, `clock`, `date`, `difftime`, `exit`. `time`/`clock` contam
  desde o boot (não há relógio de época); `date` usa o RTC da placa.
- `io`: `open`, `read`, `write`, `close`, `lines`, com os modos `r`, `w`,
  `a`, `r+`, `w+`, `a+`, leitura de linha (`"l"`/`"*l"`), de bytes (número) e
  do arquivo inteiro (`"a"`/`"*a"`). Arquivos são objetos com `:read`,
  `:write`, `:close` e `:lines`.

### Extensões do SaguiOS

A tabela global `sagui` liga o script ao sistema:

| Função | O que faz |
| --- | --- |
| `sagui.registrar(texto)` | Escreve no log do sistema (serial), sem sujar a tela |
| `sagui.limpar_tela()` | Limpa o terminal |
| `sagui.dormir(ms)` | Dorme, liberando a CPU |
| `sagui.tempo()` | Milissegundos desde o boot |
| `sagui.executar(caminho, ...)` | Executa um programa e espera; devolve o código |
| `sagui.bip(freq, ms)` | Toca o alto-falante do PC |

`arg[0]` é o nome do script e `arg[1..]` são os argumentos da linha de
comando.

## Limites

- Sem números fracionários, `math.floor`, `math.sqrt` e afins.
- Sem metatabelas (`setmetatable`, `__index`, `__add`), sem `pcall`/`xpcall`
  e sem corrotinas.
- Sem `require`, `load`, `loadstring`, `package` nem bibliotecas C externas.
- Sem coleta de lixo: o processo termina e o kernel libera a memória. Scripts
  muito longos devem evitar criar valores sem parar.
- `string.find` e `string.format` não implementam padrões/expressões regulares.
- Um erro de sintaxe ou de execução imprime `lua: linha N: ...` e encerra o
  processo com código 1 (inclusive no modo interativo).
- A pilha de chamadas é limitada a 40 níveis de função Lua, para caber nos
  64 KB de pilha de usuário.
- O fonte é lido inteiro para a memória (limite de 512 KB) antes de executar.

## Como o interpretador é feito

`userland/programas/lua.c` tem quatro partes:

1. **Valores e tabelas**: valores marcados por tipo, textos imutáveis e
   tabelas com hash aberto (encadeamento) que preserva a ordem de inserção,
   para `pairs` ser determinístico nos testes.
2. **Analisador léxico e parser**: descendente recursivo, produz uma árvore de
   nós (`N_IF`, `N_WHILE`, `N_CALL`, ...). A precedência segue a do Lua
   (`or` < `and` < comparação < `..` < soma < multiplicação < unário < `^`).
3. **Execução**: percorre a árvore com ambientes encadeados; funções guardam o
   ambiente onde nasceram (fechamentos). `return` usa um resultado local e só
   depois copia para o retorno da chamada.
4. **Bibliotecas**: funções internas identificadas por um número e ligadas às
   tabelas `string`, `table`, `math`, `os`, `io` e `sagui`.

O shell ganhou o comando `lua` (aparece no `ajuda`), mas qualquer programa
pode ser chamado por caminho: `/bin/lua script.lua`.

## Testes

`tests/h38_lua_nativo.py` roda `/testes/lua_nativo` no QEMU. Esse programa:

- executa `/lua/ola.lua` e confere a saída exata;
- executa `/lua/recursos.lua` e confere funções, recursão, fechamentos,
  tabelas, `pairs`/`ipairs`, `string`, `table`, `math` e os laços;
- executa `/lua/arquivos.lua`, que grava e relê `/lua/saida.txt` e chama
  `dofile("/lua/dofile.lua")`;
- confere que erro de execução e erro de sintaxe saem com código 1 e mensagem
  na saída de erro;
- confere `lua -e "print(2 + 3)"` e `lua -v`.

Tudo roda dentro do SaguiOS, no anel 3, sem ferramentas do hospedeiro.
