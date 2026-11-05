# Go no SaguiOS: bootstrap e importacao de bibliotecas

## Estado real

O sistema inclui `/bin/gosagui`, um compilador **nativo de um subconjunto minimo de Go**, escrito em C freestanding. Ele roda no anel 3 do SaguiOS, le um arquivo fonte e grava um executavel ELF32 i386 que o carregador existente pode executar. Nao depende de GCC, assembler, linker ou servico externo durante a compilacao dentro do sistema.

Esta etapa acrescenta duas pecas:

1. **Bootstrap proprio**: todo programa gerado comeca por um ponto de entrada que executa as funcoes `init` e depois `main`, encerrando o processo com codigo 0. Uma escrita curta/erro continua encerrando com codigo 1.
2. **Importacao de bibliotecas embutidas**: a clausula `import` e aceita para os pacotes `fmt` e `sagui`, e as chamadas qualificadas sao resolvidas pelo compilador para a ABI do SaguiOS.

**Isso nao e um porte do compilador oficial do Go.** Nao implementa a linguagem completa, `go build`, modulos, biblioteca padrao do Go, runtime Go, GC, goroutines ou o bootstrap oficial da toolchain. O bootstrap aqui e apenas a sequencia de inicializacao do executavel gerado. O pedido de executar a toolchain oficial dentro do SaguiOS permanece um trabalho futuro.

## Usar

No hospedeiro Linux, com as dependencias habituais do projeto:

```sh
make
make rodar
```

No Terminal do SaguiOS:

```text
gosagui version
gosagui build -o /go/ola /go/ola.go
/go/ola
gosagui build -o /go/biblioteca /go/biblioteca.go
/go/biblioteca
```

Saida de `/go/biblioteca`:

```text
[init] Ola, bibliotecas!
com importacao
```

O exemplo tambem grava `[GO] biblioteca importada` no log do sistema (serial), via `sagui.Registrar`. O destino deve ter pasta existente. Alteracoes no SGFS ficam na camada da RAM e se perdem ao reiniciar; use um volume FAT32 montado para persistir arquivos.

## Linguagem aceita

```go
package main

import "fmt"
import s "sagui"

func init() {
    print("[init] ")
}

func main() {
    fmt.Print("Ola, ")
    fmt.Println("bibliotecas!")
    fmt.Println("com", "importacao")
    s.Registrar("[GO] biblioteca importada")
}
```

- Exatamente um `package main`.
- Zero ou mais `import` antes das funcoes, incluindo a forma agrupada:
  ```go
  import (
      "fmt"
      s "sagui"
  )
  ```
- Uma ou mais `func init()` e exatamente uma `func main()`, ambas sem argumentos ou resultados.
- Corpo com chamadas a `print`/`println` e a funcoes qualificadas de pacotes importados.
- Cada argumento deve ser um literal string entre aspas duplas. Chamadas como `fmt.Println("a", "b")` sao aceitas; `fmt.Println(42)` nao.
- Espacos, comentarios `//` e `/* ... */`, separacao por nova linha ou ponto e virgula.
- Escapes `\n`, `\r`, `\t`, `\\` e `\"`.
- A chave de abertura de cada funcao deve estar na mesma linha da assinatura; declaracao de package/import deve terminar com nova linha ou ponto e virgula.
- Sem variaveis, numeros, expressoes, funcoes adicionais, controle de fluxo, goroutines, GC ou bibliotecas externas.
- Fonte de ate 16 KiB, codigo de ate 16 KiB, dados de ate 16 KiB; byte NUL na fonte e rejeitado.

O parser e propositalmente restritivo: construcoes fora do subconjunto geram erro e retorno 1, nao sao silenciosamente ignoradas. Trata-se de um subconjunto de sintaxe, nao de uma implementacao conforme da especificacao Go. A saida de `print`/`println`/`fmt` vai para o descritor 1; `sagui.Erro` vai para o descritor 2; `sagui.Registrar` vai para o log do kernel. A ferramenta preserva bytes de texto e nao faz conversao UTF-8 para o terminal Latin-1. Prefira ASCII nos exemplos desta etapa.

## Bibliotecas embutidas

O compilador resolve estas chamadas diretamente para syscalls. Nao ha arquivos `.a`, ligador ou carregamento dinamico.

| Pacote | Chamada | Efeito |
| --- | --- | --- |
| `fmt` | `fmt.Print(textos...)` | escreve os textos no descritor 1, sem separador nem quebra de linha |
| `fmt` | `fmt.Println(textos...)` | escreve os textos separados por espaco e termina com `\n` |
| `sagui` | `sagui.Erro(textos...)` | escreve os textos no descritor 2 |
| `sagui` | `sagui.Registrar(textos...)` | envia cada texto ao log do sistema (`SIS_REGISTRAR`) |
| `sagui` | `sagui.LimparTela()` | limpa a tela (`SIS_LIMPAR_TELA`); nao recebe argumentos |

Regras de importacao:

- O apelido padrao e o ultimo componente do caminho (`"fmt"` cria `fmt`).
- Um apelido explicito pode ser dado com `import f "fmt"`.
- Apenas `fmt` e `sagui` sao aceitos. Qualquer outro caminho e rejeitado.
- Apelidos repetidos sao rejeitados.
- Importacao nao usada e rejeitada, como no Go.
- Chamar uma funcao sem importar o pacote, usar apelido errado ou funcao inexistente gera erro.

## Bootstrap e layout do ELF

O ELF tem entrada em `0x40000000`, um segmento PT_LOAD de leitura/execucao, codigo seguido por dados somente de leitura, alinhamento de 4096 bytes e usa a ABI (`int 0x80`) do SaguiOS.

O codigo gerado tem tres partes:

1. No inicio, um salto relativo para o bloco de bootstrap.
2. As funcoes na ordem do fonte, terminando cada uma com `ret`.
3. O bootstrap, que chama cada `init` na ordem em que apareceu no fonte, chama `main` e executa `SIS_SAIR` com codigo 0.

Os pacotes embutidos nao tem inicializacao propria. O bootstrap nao cria goroutines, nao inicializa GC, nao configura pilha de sinais e nao implementa o runtime oficial do Go. Ele apenas garante a ordem `init` -> `main` -> saida.

O programa termina com 1 em caso de escrita curta/erro, e 0 em caso de sucesso. A imagem reserva 16 KiB para codigo antes dos dados para dispensar relocacoes.

A analise completa precede a abertura do destino. Erros de sintaxe preservam um destino existente. Erros de gravacao removem o arquivo parcial, mas **nao** restauram uma versao anterior; a gravacao nao e transacional. Nao use caminhos diferentes que apontem para o mesmo arquivo como origem/destino.

## Implementacao

`userland/programas/gosagui.c` contem lexer, parser, emissor x86, tabela de importacoes, bootstrap e gravador ELF. O Makefile ja descobre programas C nesse diretorio, portanto inclui o compilador automaticamente em `/bin`. Os exemplos de `rootfs/go` entram no ramdisk pela regra existente.

## Validacao realizada

```sh
make -j4
python3 tests/h37_go_nativo.py
python3 tests/rodar_todos.py h13 h14 h15 h16
```

O teste nativo executa `/testes/go_nativo` no QEMU, que:

1. Compila `/go/ola.go` e `/go/biblioteca.go` dentro do convidado.
2. Executa os dois ELFs por pipe, conferindo codigo de saida e a ordem `init` -> `main`.
3. Confere a saida de `fmt.Print`/`fmt.Println` com multiplos argumentos.
4. Confere o registro de `sagui.Registrar` na serial.
5. Rejeita chamada desconhecida, importacao inexistente, apelido errado, pacote nao importado, importacao nao usada, argumento invalido em `LimparTela` e mais fontes invalidas.
6. Executa novamente o ELF anterior depois das rejeicoes, confirmando que foi preservado.

A suite completa e o hardware fisico nao foram validados nesta alteracao.

## Caminho para Go completo

Antes de prometer compatibilidade com a toolchain oficial, e necessario definir e implementar um porte GOOS/GOARCH (SaguiOS/386):

1. Auditar syscalls, memoria virtual, limites de processos/arquivos e persistencia para as necessidades da toolchain; definir ABI do runtime e tratamento de TLS/pilha.
2. Portar os componentes especificos de SO do runtime: threads/escalonamento, sincronizacao, sinais ou mecanismo equivalente, temporizadores, memoria, GC e encerramento. O bootstrap atual nao cobre nada disso.
3. Portar `os`, `syscall`/equivalente e dependencias da toolchain; oferecer operacoes de filesystem e subprocessos exigidas, com diagnosticos para recursos indisponiveis.
4. Adicionar o alvo aos compiladores/linker Go e gerar inicialmente a toolchain por cross-compilacao em um hospedeiro suportado.
5. Disponibilizar fontes e ferramentas em armazenamento com capacidade adequada (a toolchain completa nao cabe na meta atual de disquete).
6. Validar compilacao de programas reais e recompilacao da propria toolchain dentro do SaguiOS, antes de declarar suporte nativo completo.

O compilador minimo aqui nao e um bootstrap do compilador oficial: o subconjunto e pequeno demais para compilar suas fontes.
