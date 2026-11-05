# Go no SaguiOS: etapa inicial de compilacao nativa

## Estado real

O sistema inclui `/bin/gosagui`, um compilador **nativo de um subconjunto minimo de Go**, escrito em C freestanding. Ele roda no anel 3 do SaguiOS, le um arquivo fonte e grava um executavel ELF32 i386 que o carregador existente pode executar. Nao depende de GCC, assembler, linker ou servico externo durante a compilacao dentro do sistema.

**Nao e um porte do compilador oficial do Go.** Nao implementa a linguagem completa, `go build`, modulos, biblioteca padrao, runtime Go ou bootstrap. O pedido de executar a toolchain oficial dentro do SaguiOS permanece um trabalho futuro; este incremento fornece apenas uma primeira cadeia nativa verificavel.

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
```

Resultado:

```text
Ola, Go no SaguiOS!
```

O exemplo acompanha a imagem. O destino deve ter pasta existente. Alteracoes no SGFS ficam na camada da RAM e se perdem ao reiniciar; use um volume FAT32 montado para persistir arquivos.

## Linguagem aceita

```go
package main

func main() {
    print("Ola, ")
    println("Go no SaguiOS!")
}
```

- Exatamente um `package main` e uma `func main()` sem argumentos ou resultados.
- Zero ou mais chamadas a `print` e `println`, cada uma com exatamente um literal string entre aspas duplas.
- Espacos, comentarios `//` e `/* ... */`, separacao por nova linha ou ponto e virgula.
- Escapes `\n`, `\r`, `\t`, `\\` e `\"`.
- A chave de abertura de `main` deve estar na mesma linha da assinatura; declaracao de package deve terminar com nova linha ou ponto e virgula.
- Sem imports, fmt, variaveis, numeros, expressoes, funcoes adicionais, controle de fluxo, goroutines, GC ou bibliotecas.
- Fonte de ate 16 KiB, codigo de ate 16 KiB, dados de ate 16 KiB; byte NUL na fonte e rejeitado.

O parser e propositalmente restritivo: construcoes fora do subconjunto geram erro e retorno 1, nao sao silenciosamente ignoradas. Trata-se de um subconjunto de sintaxe, nao de uma implementacao conforme da especificacao Go. Em particular, a saida de print/println vai para o descritor 1 do SaguiOS; a ferramenta preserva bytes de texto e nao faz conversao UTF-8 para o terminal Latin-1. Prefira ASCII nos exemplos desta etapa.

## Implementacao

`userland/programas/gosagui.c` contem lexer, parser, emissor x86 e gravador ELF. O Makefile ja descobre programas C nesse diretorio, portanto inclui o compilador automaticamente em `/bin`. Os exemplos de `rootfs/go` entram no ramdisk pela regra existente.

O ELF tem entrada em `0x40000000`, um segmento PT_LOAD de leitura/execucao, codigo seguido por dados somente de leitura, alinhamento de 4096 bytes e usa `SIS_ESCREVER`/`SIS_SAIR` da ABI do SaguiOS. O programa termina com 1 em caso de escrita curta/erro, e 0 em caso de sucesso. A imagem reserva 16 KiB para codigo antes dos dados para dispensar relocacoes nesta primeira versao.

A analise completa precede a abertura do destino. Erros de sintaxe preservam um destino existente. Erros de gravacao removem o arquivo parcial, mas **nao** restauram uma versao anterior; a gravacao nao e transacional. Nao use caminhos diferentes que apontem para o mesmo arquivo como origem/destino.

## Validacao realizada

```sh
make -j4
python3 tests/h37_go_nativo.py
python3 tests/rodar_todos.py h13 h14 h15 h16
```

Passaram o teste nativo novo e os quatro testes de regressao selecionados. A suite completa e o hardware fisico nao foram validados nesta alteracao.

O teste nativo executa `/testes/go_nativo` no QEMU, que:

1. Executa `/bin/gosagui` para compilar o exemplo dentro do convidado.
2. Rejeita chamada desconhecida e mais sete fontes invalidas (tipos nao suportados, lixo final, escape/comentario incompleto, separacao invalida, package incorreto e escape desconhecido).
3. Executa o ELF anterior depois das rejeicoes, confirmando que ele foi preservado.
4. Captura e compara a saida por pipe e confere o codigo de saida.

## Caminho para Go completo

Antes de prometer compatibilidade com a toolchain oficial, e necessario definir e implementar um porte GOOS/GOARCH (SaguiOS/386):

1. Auditar syscalls, memoria virtual, limites de processos/arquivos e persistencia para as necessidades da toolchain; definir ABI do runtime e tratamento de TLS/pilha.
2. Portar os componentes especificos de SO do runtime: threads/escalonamento, sincronizacao, sinais ou mecanismo equivalente, temporizadores, memoria, GC e encerramento.
3. Portar `os`, `syscall`/equivalente e dependencias da toolchain; oferecer operacoes de filesystem e subprocessos exigidas, com diagnosticos para recursos indisponiveis.
4. Adicionar o alvo aos compiladores/linker Go e gerar inicialmente a toolchain por cross-compilacao em um hospedeiro suportado.
5. Disponibilizar fontes e ferramentas em armazenamento com capacidade adequada (a toolchain completa nao cabe na meta atual de disquete).
6. Validar compilacao de programas reais e recompilacao da propria toolchain dentro do SaguiOS, antes de declarar suporte nativo completo.

O compilador minimo aqui nao e um bootstrap do compilador oficial: o subconjunto e pequeno demais para compilar suas fontes.
