# Assembly e C nativos no SaguiOS

O projeto inclui três ferramentas que rodam **dentro do anel 3**:

- `/bin/asagui`: assembler i386 independente, com um subconjunto da sintaxe NASM; gera ELF32 executável ou objeto relocável.
- `/bin/csagui`: compilador nativo de um subconjunto de C; gera objetos `.o` e mantém um modo legado de programa sem libc.
- `/bin/ldagui`: ligador estático de objetos ELF32 i386, com relocations comuns de C sem PIC.

`asagui`, `csagui` e `ldagui` não são portas de NASM, GCC, Clang ou GNU ld. O fluxo convidado não depende dessas ferramentas. O alvo continua sendo i386/ELF32.

## Uso

```text
asagui version
asagui build -o /tmp/ola-asm /asm/ola.asm
/tmp/ola-asm

csagui version
csagui compile -c -o /tmp/ola.o /c/ola.c
# Com o sysroot upstream instalado no rootfs:
ldagui link -o /tmp/ola /usr/lib/crt0.o /tmp/ola.o /usr/lib/mlibc.o
/tmp/ola

# Modo legado de exemplo, sem libc:
csagui build -o /tmp/ola-direto /c/ola.c
/tmp/ola-direto
```

Os exemplos `/asm/ola.asm` e `/c/ola.c` vêm no rootfs. O Makefile já descobre automaticamente `userland/programas/*.c` e `userland/testes/*.c`, então os binários e o teste são incluídos em `/bin` e `/testes` sem uma lista manual.

`csagui` grava assembly intermediário em `/tmp/csagui-<pid>.asm`, aguarda `/bin/asagui` e remove o temporário. O destino só é aberto pelo assembler depois de a análise e a montagem terminarem sem erro; erros de sintaxe preservam um destino que já existia. A gravação não restaura uma versão anterior se houver falha física durante a escrita.

## Assembler `asagui`

### Sintaxe e diretivas

- Registradores de 32 bits: `eax`, `ecx`, `edx`, `ebx`, `esp`, `ebp`, `esi`, `edi`.
- Rótulos e mnemonics não diferenciam maiúsculas de minúsculas. Comentários começam com `;` ou `#` fora de strings.
- Diretivas reconhecidas: `bits 32`, `global nome`, `extern nome`, `section .text`, `section .rodata`, `section .data`, `db` e `dd`.
- `section` é apenas uma indicação sintática no assembler atual: um objeto gerado por `asagui assemble -c` contém uma seção `.text` com código e dados concatenados. O executável de `asagui build` tem um único segmento carregável RWX.
- Imediatos e dados numéricos aceitam decimal (zeros à esquerda continuam decimais) e hexadecimal com prefixo `0x`. `db` aceita bytes numéricos de -128 a 255 e strings entre aspas; reconhece escapes para barra invertida, aspas simples/duplas, LF, CR e tabulação. `dd` aceita valores de 32 bits ou rótulos, inclusive referências para a frente.
- O fonte precisa definir `_start:` ou `start:`. O ELF entra nesse rótulo. Rótulos duplicados, referências não resolvidas e instruções desconhecidas são erros.

### Instruções implementadas

`mov`, `lea`, `add`, `sub`, `cmp`, `and`, `or`, `xor`, `test`, `imul`, `idiv`, `neg`, `inc`, `dec`, `push`, `pop`, `cdq`, `call`, `ret`, `leave`, `jmp`, `je`/`jz`, `jne`/`jnz`, `jl`, `jle`, `jg`, `jge`, `int`, `nop` e `hlt`.

O conjunto de operandos também é deliberadamente pequeno:

- `mov reg, reg`, `mov reg, imediato/rótulo`, `mov reg, [rótulo]` e `mov [rótulo], reg`;
- cargas/armazenamentos `mov reg, [reg+desloc]` e `mov [reg+desloc], reg`, com deslocamento assinado de 8 bits; `lea` aceita rótulo absoluto ou registrador-base com deslocamento;
- `add`, `sub`, `cmp`, `and`, `or` e `xor` aceitam registrador-destino com registrador ou imediato de 32 bits; `test` aceita dois registradores; `imul` aceita dois registradores;
- `push` aceita registrador ou imediato de 32 bits; as demais instruções de um operando usam o registrador indicado acima;
- saltos e `call` recebem um rótulo. Saltos condicionais são relativos e assinados de 32 bits.

Não há macros, `%define`, includes, script de link, `align`, `resb`, tamanhos de operando, registradores de 8/16 bits, endereçamento completo do x86 ou sintaxe NASM geral.

`asagui build -o programa.elf fonte.asm` exige `_start:`/`start:` e produz ELF32 executável. `asagui assemble -c -o modulo.o fonte.asm` produz ELF32 `ET_REL`, sem exigir ponto de entrada. O objeto contém tabela de símbolos e `.rel.text`; referências absolutas geram `R_386_32`, chamadas/saltos relativos geram `R_386_PC32`. `global` exporta símbolos e `extern` declara referências externas. A saída de `asagui build` continua sem tabela de seções.

Limites: fonte de 16 KiB, no máximo 4096 linhas (cada uma com até 255 bytes), 512 símbolos, 2048 relocations e 32 KiB de bytes de código/dados. `section` ainda não separa código, rodata e data: tudo é concatenado em `.text` no objeto, e o linker usa um segmento RWX.

## Compilador `csagui`

### Subconjunto aceito

Exemplo:

```c
int soma(int a, int b) {
    return a + b;
}

int main(void) {
    int i = 0;
    int total = 0;
    while (i < 4) {
        total = total + i;
        i = i + 1;
    }
    if (soma(total, 1) == 7) {
        puts("Compilado dentro do SaguiOS");
        return 0;
    }
    return 1;
}
```

Suporte atual do modo `compile -c`:

- funções definidas com até 8 parâmetros; qualquer nome de tipo é tratado como uma palavra de 32 bits, com qualificadores e `*` aceitos na assinatura/declarador; `main` é exportado para o startup externo;
- variáveis escalares locais, inclusive declaradas como ponteiro, com ou sem inicializador; parâmetros e variáveis usam slots de 32 bits;
- literais inteiros decimais e hexadecimais, variáveis, strings C, chamadas, atribuição e operações aritméticas/comparações/logical listadas abaixo;
- blocos, declarações locais, expressões, `return`, `if`/`else` e `while`;
- chamadas a funções definidas no mesmo arquivo e a símbolos externos (até 8 argumentos), usando cdecl i386. Por isso uma chamada a `printf`, `puts`, `malloc` ou outra função C pode gerar relocation externa, mas o símbolo precisa existir nos objetos passados ao `ldagui`;
- strings são gravadas com terminador NUL e o objeto usa relocations para passar o endereço da string a funções como `printf`/`puts`.

`csagui compile -c` reconhece e ignora linhas de diretiva iniciadas por `#`; **não** abre headers, não expande `#define`, não importa declarações nem verifica protótipos. Tipos e argumentos são tratados como palavras de 32 bits; não há verificação de tipos, promoção de argumentos variádicos ou modelo completo de overflow. `int`, `char *`, `size_t` e outros valores de 32 bits podem ser passados pela ABI cdecl, mas acesso por ponteiro, arrays e structs ainda não são implementados.

O modo antigo `csagui build -o programa.elf` gera um executável sem libc: `_start` chama `main`, termina com `SIS_SAIR` e `puts` é uma embutida que escreve direto com `SIS_ESCREVER`. Esse modo existe para os exemplos e não aceita chamadas externas.

### Limites e rejeições explícitas

Fonte de até 8 KiB, até 64 funções, 8 parâmetros por função, 32 parâmetros/variáveis por função, 8 KiB de strings e 16 KiB de assembly intermediário. As funções locais aceitam somente variáveis escalares e expressões implementadas; não há arrays, acesso a membros, operadores `++`/`--`, `for`, `do`, `switch`, `break`, `continue`, `goto`, casts, floats, literais char, inicializadores compostos ou inline assembly. O escopo é único por função: declarar novamente o mesmo nome, inclusive em blocos internos, é erro.

Cada função usa um frame fixo de 256 bytes; argumentos são empilhados da direita para a esquerda. `compile -c` gera um objeto ELF32 `ET_REL`; o modo `build` gera ELF32 `ET_EXEC`. Uma falha de execução (por exemplo, divisão por zero) não é convertida em exceção C.

## Ligador `ldagui`

`ldagui link -o programa.elf objeto1.o objeto2.o ...` une objetos ELF32 relocáveis i386 e produz ELF32 estático com entry `_start` (ou o símbolo passado com `--entry`). O link é separado da compilação; objetos aceitam funções externas e símbolos do outro objeto.

O subconjunto suportado é `SHT_REL` com `R_386_32`, `R_386_PC32` e `R_386_PLT32`. O linker inclui seções `SHF_ALLOC`, trata `.bss` como zeros e aplica relocs em símbolos locais/globais/weak. Ainda não aceita arquivos `.a`, executáveis pre-linkados, ELF64, dynamic linking, `SHT_RELA`, GOT/TLS, `SHN_COMMON` ou outras classes de relocation. Compile objetos para i386 sem PIC/PIE, sem stack protector e com `-fno-common`. A saída concatena código, rodata e dados em um único segmento RWX; o linker não cria permissões separadas por seção.

## Testes e estado

`userland/testes/ferramentas_nativas.c` executa os exemplos sem libc e testa também o fluxo `.o` → link separado, com um stub `printf` para validar argumento variádico e resolução de símbolo. Rode com:

```sh
make
python3 tests/h44_ferramentas_nativas.py
```

Os fontes dos três utilitários e o teste nativo foram compilados com as flags freestanding. Em harnesses de host, `asagui` gerou objetos ELF32; `ldagui` linkou objetos próprios e objetos i386 de GCC (`-m32`, `-fno-pic/-fno-pie`) e os executáveis resultantes rodaram com status esperado no host. Isso valida a geração e as relocations cobertas, mas **não substitui** o H44 dentro do SaguiOS. NASM e QEMU não estão instalados aqui, então imagem e H44 ainda precisam ser executados com as dependências do projeto.

O fluxo do compilador/linker já está no convidado, mas **o sysroot do upstream mlibc ainda não foi construído nem instalado no rootfs**; os caminhos `/usr/lib/crt0.o` e `/usr/lib/mlibc.o` no exemplo são a interface pretendida para a próxima etapa, não arquivos já presentes.
