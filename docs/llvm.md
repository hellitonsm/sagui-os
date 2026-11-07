# LLVM no SaguiOS — opção A (AOT / compilação estática)

Documentação do porte do LLVM (Low Level Virtual Machine) para o SaguiOS.
**Estratégia adotada: AOT (Ahead-of-Time)** — copiar `clang`, `llc` e `opt`
compilados cruzados para o sistema de arquivos do SaguiOS, permitindo compilar
programas dentro do sistema (gerar `.o` ou executáveis) sem executar código
gerado dinamicamente (não é JIT).

---

## Por que AOT e não JIT?

O LLVM é escrito em C++ moderno e consume muita memória. No estado atual do
SaguiOS (i386, hobby OS):

- Não há suporte confirmado a `mmap()` com `PROT_EXEC` (pré-requisito para JIT
  W^X — write XOR execute).
- Não há C++ runtime completo (exceções, stack unwinding, RTTI) na libc.
- O LLVM consome muita RAM para otimizar código; o gerenciador de memória do
  kernel ainda não foi testado com cargas desse porte.

A compilação AOT apenas lê um arquivo `.ll`/`.c`, processa na memória e grava o
binário no disco usando a libc existente — muito mais simples e viável hoje.

---

## Pré-requisitos

### No host (onde se compila o LLVM)

- Cross-compiler `i686-elf-{gcc,g++,ar,ld,strip,objcopy,as,nm}` (mesma toolchain
  já usada pelo mlibc do SaguiOS: `.local/windows-tools/toolchain/bin/`).
- CMake 3.20+ e Ninja.
- Fontes do LLVM (uma release estável, ex.: 17.x ou 18.x).
- Espaço em disco: vários GB (o LLVM é gigante).

### No SaguiOS (alvo)

- Arquitetura: **i386 (32 bits)**.
- libc: mlibc upstream estática já portada em `build/mlibc-upstream/`.
- Sistema de arquivos SGFS funcional (leitura/escrita de arquivos no ramdisk).
- Ainda **não confirmado**: `mmap`/`mprotect` com `PROT_EXEC` (não necessário
  para AOT, mas bloqueia JIT).

---

## Etapa 1 — Cross-compilação do LLVM no host

O LLVM não é compilado dentro do SaguiOS. Usa-se o CMake no host com o
toolchain `tools/llvm-sagui/toolchain.cmake`.

### Toolchain file

`tools/llvm-sagui/toolchain.cmake` já está criado no repositório. Ele aponta
para os compiladores cruzados `i686-elf-*` e para o sysroot
`build/mlibc-upstream` (headers + `libc.a` do mlibc).

### Comandos de build

```bash
# 1. Baixe/extraia as fontes do LLVM em ../llvm (relativo à raiz do SaguiOS)

# 2. Configure o build mínimo (apenas alvo X86, sem threads, sem testes)
cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=tools/llvm-sagui/toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_ENABLE_THREADS=OFF \
  -DLLVM_BUILD_TOOLS=ON \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_ENABLE_BACKTRACES=OFF \
  ../llvm

# 3. Compile primeiro a ferramenta de tabelas (host), depois as ferramentas AOT
ninja llvm-tblgen
ninja clang llc opt
```

Otimizações importantes para hobby OS:

| Opção | Motivo |
|---|---|
| `LLVM_TARGETS_TO_BUILD="X86"` | Compila apenas o gerador de código x86; economiza GBs de RAM e tempo. |
| `LLVM_ENABLE_THREADS=OFF` | Desativa multi-threading (kernel do SaguiOS ainda não tem pthreads robusto). |
| `LLVM_BUILD_TOOLS=ON` | Gera `clang`, `llc`, `opt` (ferramentas de linha de comando). |
| `LLVM_INCLUDE_TESTS/BENCHMARKS=OFF` | Não gera suítes de teste no alvo. |

### Resultado esperado

Binários cruzados para i386-pc-saguios (freestanding):
- `bin/clang` — compilador C/C++ front-end
- `bin/llc` — LLVM IR -> código de máquina (`.s`, `.o`, executável)
- `bin/opt` — otimizador de IR
- `bin/llvm-tblgen` — gerador de tabelas (roda no host)

---

## Etapa 2 — Integração no SaguiOS

### Copiar ferramentas para o sistema de arquivos

```bash
# No host, após o build:
mkdir -p build/rootfs/bin
cp <build-llvm>/bin/clang   build/rootfs/bin/
cp <build-llvm>/bin/llc     build/rootfs/bin/
cp <build-llvm>/bin/opt     build/rootfs/bin/

# Reconstruir o ramdisk e a imagem
python tools/mksgfs.py build/rootfs build/ramdisk.sgfs
python tools/mkimagem.py --mbr build/boot/mbr.bin --estagio2 build/boot/estagio2.bin \
    --kernel build/kernel.bin --kernel-elf build/kernel.elf --ramdisk build/ramdisk.sgfs \
    --saida build/saguios.img --vesa
```

### Como usar dentro do SaguiOS

No shell do convidado (`sagsh`):

```text
# Compilar C fonte para objeto
clang -c programa.c -o programa.o

# LLVM IR -> objeto
llc entrada.ll -filetype=obj -o saida.o

# LLVM IR -> executável (liga com libc do mlibc, se disponível)
llc entrada.ll -filetype=obj -o saida.o
# depois ligar com o ld do SaguiOS (programa-upstream.ld ou programa.ld)

# Otimizar IR
opt -O3 entrada.ll -o otimizado.ll
```

**Nota:** como o SaguiOS é freestanding e i386, os programas gerados precisam
ser linkados com o script de ligação adequado (`userland/programa-upstream.ld`
para mlibc upstream, `userland/programa.ld` para a libsagui tradicional) e com
os símbolos de entrada (`_start` ou `_inicio`) esperados pelo kernel.

---

## Limitações conhecidas (opção A)

1. **Sem C++ runtime completo**: o LLVM usa exceções, RTTI e stack unwinding
   internamente. Se a libc alvo não prover esses símbolos, o link de programas
   C++ complexos pode falhar. Programas C puros funcionam melhor.
2. **Sem `mmap`/`PROT_EXEC`**: não há suporte confirmado a alocação de memória
   executável no kernel. Isso não impede o AOT (apenas grava arquivos), mas
   trava qualquer plano de JIT futuro.
3. **Pouca memória**: o LLVM é pesado. Compilar arquivos grandes dentro do
   SaguiOS com 512 MB de RAM pode esgotar a memória do sistema.
4. **Símbolos ausentes**: se o linker reclamar de `abort`, `stderr` ou funções
   matemáticas (`sqrt`, etc.), a mlibc precisa ser expandida com essas funções.

---

## Próximos passos

1. Executar a cross-compilação do LLVM no host com o toolchain acima.
2. Copiar `clang`/`llc`/`opt` para `build/rootfs/bin/` e regenerar a imagem.
3. Testar dentro do SaguiOS: compilar um programa C simples (`oi.c`) e
   executá-lo.
4. Auditar a `mlibc` já portada (`build/mlibc-upstream/`) para símbolos que o
   LLVM exigir (`abort`, `stderr`, math) e adicioná-los conforme necessário.

---

## Arquivos relacionados

- `tools/llvm-sagui/toolchain.cmake` — toolchain CMake para o SaguiOS i386
- `build/mlibc-upstream/` — libc estática (mlibc) usada como sysroot
- `docs/mlibc.md` — documentação do porte do mlibc (base do LLVM)
